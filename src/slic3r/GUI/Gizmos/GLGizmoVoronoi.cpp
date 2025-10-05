#include "GLGizmoVoronoi.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/VoronoiMesh.hpp"
#include "libslic3r/Geometry.hpp"

#include <GL/glew.h>
#include <thread>
#include <ctime>
#include <random>
#include <cmath>
#include <memory>
#include <algorithm>
#include <cfloat>
#include <cstring>

// 2D Voronoi library for preview
#define JC_VORONOI_IMPLEMENTATION
#include "jc_voronoi.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace Slic3r::GUI {

static void call_after_if_active(std::function<void()> fn, GUI_App* app = &wxGetApp())
{
    if (app == nullptr) return;
    app->CallAfter([fn, app]() {
        const Plater* plater = app->plater();
        if (plater == nullptr) return;
        const GLCanvas3D* canvas = plater->canvas3D();
        if (canvas == nullptr) return;
        const GLGizmosManager& mng = canvas->get_gizmos_manager();
        if (mng.get_current_type() != GLGizmosManager::Undefined) {
            // Check if it's the Voronoi gizmo - we'll add this check later
            fn();
        }
    });
}

static ModelVolume* get_model_volume(const Selection& selection, Model& model)
{
    const Selection::IndicesList& idxs = selection.get_volume_idxs();
    if (idxs.size() != 1)
        return nullptr;
    const GLVolume* selected_volume = selection.get_volume(*idxs.begin());
    if (selected_volume == nullptr)
        return nullptr;
    
    const GLVolume::CompositeID& cid = selected_volume->composite_id;
    const ModelObjectPtrs& objs = model.objects;
    if (cid.object_id < 0 || objs.size() <= static_cast<size_t>(cid.object_id))
        return nullptr;
    const ModelObject* obj = objs[cid.object_id];
    if (cid.volume_id < 0 || obj->volumes.size() <= static_cast<size_t>(cid.volume_id))
        return nullptr;
    return obj->volumes[cid.volume_id];
}

GLGizmoVoronoi::GLGizmoVoronoi(GLCanvas3D& parent, unsigned int sprite_id)
    : GLGizmoPainterBase(parent, sprite_id)
    , m_volume(nullptr)
    , m_show_wireframe(false)
    , m_move_to_center(false)
    , tr_mesh_name(_u8L("Mesh name"))
    , tr_seed_type(_u8L("Seed type"))
    , tr_num_seeds(_u8L("Number of seeds"))
    , tr_wall_thickness(_u8L("Wall thickness"))
    , tr_random_seed(_u8L("Random seed"))
    , tr_seed_preview(_u8L("Preview seeds"))
{
}

GLGizmoVoronoi::~GLGizmoVoronoi()
{
    stop_worker_thread_request();
    if (m_worker.joinable())
        m_worker.join();
    m_glmodel.reset();
    m_seed_preview_model.reset();
}

bool GLGizmoVoronoi::on_esc_key_down()
{
    return false;
}

std::string GLGizmoVoronoi::get_icon_filename(bool is_dark_mode) const
{
    // For now, return empty - we'll need to create an icon later
    return is_dark_mode ? "voronoi_dark.svg" : "voronoi.svg";
}

std::string GLGizmoVoronoi::on_get_name() const
{
    return _u8L("Voronoi");
}

void GLGizmoVoronoi::on_render_input_window(float x, float y, float bottom_limit)
{
    if (!m_gui_cfg.has_value())
        create_gui_cfg();
    
    const float approx_height = m_gui_cfg->window_offset_y + m_gui_cfg->window_padding;
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);
    
    const float scaling = m_parent.get_scale();
    const ImVec2 window_size(m_gui_cfg->bottom_left_width * scaling, approx_height * scaling);
    ImGui::SetNextWindowSize(window_size, ImGuiCond_Always);
    
    if (GizmoImguiBegin(on_get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse)) {
        
        // Display selected volume info
        if (m_volume) {
            ImGui::Text("%s: %s", tr_mesh_name.c_str(), m_volume->name.c_str());
            ImGui::Separator();
        }
        
        // Seed type selection
        ImGui::Text("%s:", tr_seed_type.c_str());
        const char* seed_types[] = { "Vertices", "Grid", "Random" };
        int current_seed = static_cast<int>(m_configuration.seed_type);
        if (ImGui::Combo("##seed_type", &current_seed, seed_types, IM_ARRAYSIZE(seed_types))) {
            m_configuration.seed_type = static_cast<Configuration::SeedType>(current_seed);
        }
        
        // Number of seeds with manual input
        ImGui::Text("%s:", tr_num_seeds.c_str());
        ImGui::SliderInt("##num_seeds", &m_configuration.num_seeds, 10, 500);
        
        // Manual input for precise seed count
        ImGui::SameLine();
        ImGui::SetNextItemWidth(60);
        ImGui::InputInt("##num_seeds_input", &m_configuration.num_seeds);
        m_configuration.num_seeds = std::max(10, std::min(500, m_configuration.num_seeds));
        
        // Wall thickness
        ImGui::Text("%s:", tr_wall_thickness.c_str());
        ImGui::SliderFloat("##wall_thickness", &m_configuration.wall_thickness, 0.1f, 5.0f);
        
        // Hollow / solid toggle
        ImGui::Text("Cells:");
        ImGui::SameLine();
        if (ImGui::RadioButton("Solid", !m_configuration.hollow_cells)) {
            m_configuration.hollow_cells = false;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Hollow", m_configuration.hollow_cells)) {
            m_configuration.hollow_cells = true;
        }
        ImGui::Checkbox("Clip to input", &m_configuration.clip_to_input);
        
        ImGui::Separator();
        
        // Phase 4: Random seed control
        ImGui::Text("%s:", tr_random_seed.c_str());
        ImGui::InputInt("##random_seed", &m_configuration.random_seed);
        ImGui::SameLine();
        // Randomize button with secondary styling
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, m_is_dark_mode ? ImVec4(60 / 255.0f, 60 / 255.0f, 60 / 255.0f, 1.0f) : ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_is_dark_mode ? ImVec4(70 / 255.0f, 70 / 255.0f, 70 / 255.0f, 1.0f) : ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_is_dark_mode ? ImVec4(50 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f) : ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
        
        if (ImGui::Button(into_u8(_u8L("Randomize")).c_str())) {
            randomize_seed();
        }
        
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(1);
        
        // Phase 4: Seed preview
        if (ImGui::Checkbox(tr_seed_preview.c_str(), &m_configuration.show_seed_preview)) {
            if (m_configuration.show_seed_preview) {
                update_seed_preview();
            } else {
                m_seed_preview_model.reset();
            }
        }
        
        if (m_configuration.show_seed_preview) {
            // Update Preview button with secondary styling
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, m_is_dark_mode ? ImVec4(60 / 255.0f, 60 / 255.0f, 60 / 255.0f, 1.0f) : ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_is_dark_mode ? ImVec4(70 / 255.0f, 70 / 255.0f, 70 / 255.0f, 1.0f) : ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_is_dark_mode ? ImVec4(50 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f) : ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
            
            if (ImGui::Button(into_u8(_u8L("Update Preview")).c_str())) {
                update_seed_preview();
                update_2d_voronoi_preview();
            }
            
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(1);
            
            // 2D Voronoi Preview
            ImGui::Separator();
            ImGui::Text("%s:", into_u8(_u8L("2D Preview")).c_str());
            render_2d_voronoi_preview();
        }
        
        ImGui::Separator();
        
        // Phase 5: Triangle painting exclusion
        if (!m_configuration.enable_triangle_painting) {
            // Show Painting button when not in painting mode with proper styling
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, m_is_dark_mode ? ImVec4(38 / 255.0f, 46 / 255.0f, 48 / 255.0f, 1.0f) : ImVec4(0.70f, 0.70f, 0.70f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_is_dark_mode ? ImVec4(50 / 255.0f, 58 / 255.0f, 61 / 255.0f, 1.0f) : ImVec4(0.80f, 0.80f, 0.80f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_is_dark_mode ? ImVec4(60 / 255.0f, 68 / 255.0f, 71 / 255.0f, 1.0f) : ImVec4(0.60f, 0.60f, 0.60f, 1.0f));
            
            if (ImGui::Button(into_u8(_u8L("Painting")).c_str())) {
                m_configuration.enable_triangle_painting = true;
                // Initialize painting system
                update_from_model_object(true);
                request_rerender();
            }
            
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(1);
        } else {
            // Show painting controls and Apply button when in painting mode
            ImGui::Text("%s:", into_u8(_u8L("Brush radius")).c_str());
            ImGui::SliderFloat("##cursor_radius", &m_cursor_radius, 
                             get_cursor_radius_min(), get_cursor_radius_max(), "%.1f");
            
            ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f),
                             "%s", into_u8(_u8L("Paint surfaces red to exclude")).c_str());
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                             "%s", into_u8(_u8L("Click & drag to paint")).c_str());
            
            // Apply button - only visible when painting is active with proper styling
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, m_is_dark_mode ? ImVec4(43 / 255.0f, 64 / 255.0f, 54 / 255.0f, 1.0f) : ImVec4(0.86f, 0.99f, 0.91f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_is_dark_mode ? ImVec4(50 / 255.0f, 74 / 255.0f, 64 / 255.0f, 1.0f) : ImVec4(0.76f, 0.94f, 0.86f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_is_dark_mode ? ImVec4(35 / 255.0f, 56 / 255.0f, 46 / 255.0f, 1.0f) : ImVec4(0.81f, 0.97f, 0.88f, 1.0f));
            
            if (ImGui::Button(into_u8(_u8L("Apply")).c_str())) {
                // Apply the painting changes
                update_model_object();
                m_configuration.enable_triangle_painting = false;
                request_rerender();
            }
            
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(1);
            
            ImGui::SameLine();
            
            // Cancel button with warning styling
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
            ImGui::PushStyleColor(ImGuiCol_Button, m_is_dark_mode ? ImVec4(64 / 255.0f, 43 / 255.0f, 43 / 255.0f, 1.0f) : ImVec4(0.99f, 0.86f, 0.86f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_is_dark_mode ? ImVec4(74 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f) : ImVec4(0.94f, 0.76f, 0.76f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_is_dark_mode ? ImVec4(56 / 255.0f, 35 / 255.0f, 35 / 255.0f, 1.0f) : ImVec4(0.97f, 0.81f, 0.81f, 1.0f));
            
            if (ImGui::Button(into_u8(_u8L("Cancel")).c_str())) {
                // Cancel painting mode without applying changes
                m_configuration.enable_triangle_painting = false;
                // Reset any pending changes if needed
                update_from_model_object(true);
                request_rerender();
            }
            
            ImGui::PopStyleColor(3);
            ImGui::PopStyleVar(1);
        }
        
        ImGui::Separator();
        
        // Apply button
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            if (m_state.status == State::idle) {
                // Generate button with primary styling
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, m_is_dark_mode ? ImVec4(0 / 255.0f, 174 / 255.0f, 66 / 255.0f, 1.0f) : ImVec4(0 / 255.0f, 174 / 255.0f, 66 / 255.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_is_dark_mode ? ImVec4(26 / 255.0f, 190 / 255.0f, 92 / 255.0f, 1.0f) : ImVec4(26 / 255.0f, 190 / 255.0f, 92 / 255.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_is_dark_mode ? ImVec4(0 / 255.0f, 158 / 255.0f, 54 / 255.0f, 1.0f) : ImVec4(0 / 255.0f, 158 / 255.0f, 54 / 255.0f, 1.0f));
                
                if (ImGui::Button(into_u8(_u8L("Generate Voronoi")).c_str())) {
                    apply_voronoi();
                }
                
                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar(1);
            } else if (m_state.status == State::running) {
                ImGui::Text("%s %d%%", into_u8(_u8L("Processing...")).c_str(), m_state.progress);
                
                // Cancel button with warning styling
                ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
                ImGui::PushStyleColor(ImGuiCol_Button, m_is_dark_mode ? ImVec4(64 / 255.0f, 43 / 255.0f, 43 / 255.0f, 1.0f) : ImVec4(0.99f, 0.86f, 0.86f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_is_dark_mode ? ImVec4(74 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f) : ImVec4(0.94f, 0.76f, 0.76f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_is_dark_mode ? ImVec4(56 / 255.0f, 35 / 255.0f, 35 / 255.0f, 1.0f) : ImVec4(0.97f, 0.81f, 0.81f, 1.0f));
                
                if (ImGui::Button(into_u8(_u8L("Cancel")).c_str())) {
                    stop_worker_thread_request();
                }
                
                ImGui::PopStyleColor(3);
                ImGui::PopStyleVar(1);
            }
        }
        
        // Close button with secondary styling
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 2.0f);
        ImGui::PushStyleColor(ImGuiCol_Button, m_is_dark_mode ? ImVec4(60 / 255.0f, 60 / 255.0f, 60 / 255.0f, 1.0f) : ImVec4(0.75f, 0.75f, 0.75f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, m_is_dark_mode ? ImVec4(70 / 255.0f, 70 / 255.0f, 70 / 255.0f, 1.0f) : ImVec4(0.85f, 0.85f, 0.85f, 1.0f));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, m_is_dark_mode ? ImVec4(50 / 255.0f, 50 / 255.0f, 50 / 255.0f, 1.0f) : ImVec4(0.65f, 0.65f, 0.65f, 1.0f));
        
        if (ImGui::Button(into_u8(_u8L("Close")).c_str())) {
            close();
        }
        
        ImGui::PopStyleColor(3);
        ImGui::PopStyleVar(1);
        
        GizmoImguiEnd();
    }
}

bool GLGizmoVoronoi::on_is_activable() const
{
    const Selection& selection = m_parent.get_selection();
    return selection.is_single_full_instance() && !selection.is_wipe_tower();
}

void GLGizmoVoronoi::on_set_state()
{
    if (get_state() == GLGizmoBase::EState::On) {
        const Selection& selection = m_parent.get_selection();
        Model& model = wxGetApp().plater()->model();
        m_volume = get_model_volume(selection, model);
        m_move_to_center = true;
        
        // Initialize seed preview if enabled
        if (m_configuration.show_seed_preview) {
            update_seed_preview();
        }
    } else {
        m_volume = nullptr;
        m_glmodel.reset();
        m_seed_preview_model.reset();
        m_seed_preview_points.clear();
    }
}

void GLGizmoVoronoi::on_render()
{
    // Render preview if available
    if (m_glmodel.is_initialized()) {
        glsafe(::glEnable(GL_BLEND));
        glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
        
        const Camera& camera = wxGetApp().plater()->get_camera();
        const Transform3d view_matrix = camera.get_view_matrix();
        const Transform3d projection_matrix = camera.get_projection_matrix();
        
        // Render the preview model
        glsafe(::glDisable(GL_BLEND));
    }
    
    // Phase 4: Render seed preview points
    if (m_configuration.show_seed_preview) {
        render_seed_preview();
    }
    
    // Phase 5: Render painting gizmo when painting is active
    if (m_configuration.enable_triangle_painting) {
        render_painter_gizmo();
    }
}

CommonGizmosDataID GLGizmoVoronoi::on_get_requirements() const
{
    return CommonGizmosDataID(
        int(CommonGizmosDataID::SelectionInfo)
    );
}

void GLGizmoVoronoi::apply_voronoi()
{
    if (!m_volume)
        return;
    
    // Start worker thread
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        if (m_state.status == State::running)
            return;
        
        m_state.status = State::running;
        m_state.progress = 0;
        m_state.config = m_configuration;
        m_state.mv = m_volume;
        m_state.result.reset();
    }
    
    if (m_worker.joinable())
        m_worker.join();
    
    m_worker = std::thread([this]() { process(); });
}

void GLGizmoVoronoi::close()
{
    stop_worker_thread_request();
    if (m_worker.joinable())
        m_worker.join();
    
    wxGetApp().plater()->canvas3D()->set_as_dirty();
    wxGetApp().plater()->get_view3D_canvas3D()->set_as_dirty();
}

void GLGizmoVoronoi::process()
{
    try {
        std::unique_ptr<indexed_triangle_set> result;
        
        // Get the input mesh
        const indexed_triangle_set* input_mesh = nullptr;
        VoronoiMesh::Config voronoi_config;
        
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            if (m_state.status != State::running || !m_state.mv)
                return;
            
            input_mesh = &m_state.mv->mesh().its;
            
            // Convert configuration
            voronoi_config.seed_type = static_cast<VoronoiMesh::SeedType>(m_state.config.seed_type);
            voronoi_config.num_seeds = m_state.config.num_seeds;
            voronoi_config.wall_thickness = m_state.config.wall_thickness;
            voronoi_config.hollow_cells = m_state.config.hollow_cells;
            voronoi_config.clip_to_input = m_state.config.clip_to_input;
            voronoi_config.random_seed = m_state.config.random_seed;
            
            // Set progress callback
            voronoi_config.progress_callback = [this](int progress) -> bool {
                std::lock_guard<std::mutex> lock(m_state_mutex);
                m_state.progress = progress;
                return m_state.status == State::running;
            };
        }
        
        // Generate Voronoi mesh
        result = VoronoiMesh::generate(*input_mesh, voronoi_config);
        
        if (result) {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            m_state.result = std::move(result);
            m_state.progress = 100;
        }
        
        call_after_if_active([this]() {
            worker_finished();
        });
        
    } catch (const VoronoiCanceledException&) {
        // Cancelled by user
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_state.status = State::idle;
    } catch (...) {
        // Error occurred
        std::lock_guard<std::mutex> lock(m_state_mutex);
        m_state.status = State::idle;
    }
}

void GLGizmoVoronoi::stop_worker_thread_request()
{
    std::lock_guard<std::mutex> lock(m_state_mutex);
    if (m_state.status == State::running)
        m_state.status = State::cancelling;
}

void GLGizmoVoronoi::worker_finished()
{
    std::unique_ptr<indexed_triangle_set> result_its;
    const ModelVolume* mv = nullptr;
    
    {
        std::lock_guard<std::mutex> lock(m_state_mutex);
        
        if (m_state.result && m_state.status == State::running) {
            result_its = std::move(m_state.result);
            mv = m_state.mv;
            m_state.status = State::idle;
        } else {
            m_state.status = State::idle;
            return;
        }
    }
    
    // Apply the result to the model (outside of lock)
    if (result_its && mv && !result_its->vertices.empty()) {
        // Get the model and update the volume's mesh
        Plater* plater = wxGetApp().plater();
        if (!plater)
            return;
        
        Model& model = plater->model();
        const Selection& selection = m_parent.get_selection();
        const Selection::IndicesList& idxs = selection.get_volume_idxs();
        
        if (idxs.size() != 1)
            return;
        
        const GLVolume* selected_volume = selection.get_volume(*idxs.begin());
        if (!selected_volume)
            return;
        
        const GLVolume::CompositeID& cid = selected_volume->composite_id;
        if (cid.object_id < 0 || cid.volume_id < 0)
            return;
        
        ModelObject* obj = model.objects[cid.object_id];
        if (!obj || cid.volume_id >= obj->volumes.size())
            return;
        
        ModelVolume* volume = obj->volumes[cid.volume_id];
        if (volume != mv)
            return;
        
        // Replace the mesh
        TriangleMesh new_mesh(*result_its);
        volume->set_mesh(std::move(new_mesh));
        volume->calculate_convex_hull();
        
        // Mark as modified and update
        obj->invalidate_bounding_box();
        plater->changed_object(cid.object_id);
        plater->update();
        
        request_rerender();
    }
}

void GLGizmoVoronoi::create_gui_cfg()
{
    if (m_gui_cfg.has_value())
        return;
    
    m_gui_cfg = GuiCfg();
    m_gui_cfg->top_left_width = 200;
    m_gui_cfg->bottom_left_width = 220;
    m_gui_cfg->input_width = 100;
    m_gui_cfg->window_offset_x = 0;
    m_gui_cfg->window_offset_y = 200;
    m_gui_cfg->window_padding = 10;
}

void GLGizmoVoronoi::request_rerender()
{
    wxGetApp().plater()->canvas3D()->set_as_dirty();
    wxGetApp().plater()->canvas3D()->request_extra_frame();
}

void GLGizmoVoronoi::set_center_position()
{
    if (m_move_to_center && m_volume) {
        m_move_to_center = false;
        // Position window near the selected object
    }
}

// Phase 4: Seed randomization
void GLGizmoVoronoi::randomize_seed()
{
    // Generate new random seed using current time
    m_configuration.random_seed = static_cast<int>(std::time(nullptr)) % 100000;
    
    // Update preview if it's enabled
    if (m_configuration.show_seed_preview) {
        update_seed_preview();
        update_2d_voronoi_preview();
    }
}

// Phase 4: Update seed preview points
void GLGizmoVoronoi::update_seed_preview()
{
    if (!m_volume)
        return;
    
    m_seed_preview_points.clear();
    m_seed_preview_model.reset();
    
    const indexed_triangle_set& mesh = m_volume->mesh().its;
    
    // Generate seed points based on current configuration
    std::vector<Vec3d> seeds;
    VoronoiMesh::Config config;
    config.seed_type = static_cast<VoronoiMesh::SeedType>(m_configuration.seed_type);
    config.num_seeds = m_configuration.num_seeds;
    config.clip_to_input = m_configuration.clip_to_input;
    config.random_seed = m_configuration.random_seed;
    
    // Compute bounding box for generated points
    BoundingBoxf3 bbox;
    for (const auto& v : mesh.vertices) {
        bbox.merge(v.cast<double>());
    }
    
    if (m_configuration.seed_type == Configuration::SEED_GRID) {
        // Grid seeds
        int seeds_per_axis = static_cast<int>(std::ceil(std::cbrt(m_configuration.num_seeds)));
        Vec3d step = bbox.size().cwiseQuotient(Vec3d(seeds_per_axis, seeds_per_axis, seeds_per_axis));
        
        for (int x = 0; x < seeds_per_axis; ++x) {
            for (int y = 0; y < seeds_per_axis; ++y) {
                for (int z = 0; z < seeds_per_axis; ++z) {
                    Vec3d pt = bbox.min + Vec3d(
                        (x + 0.5) * step.x(),
                        (y + 0.5) * step.y(),
                        (z + 0.5) * step.z()
                    );
                    m_seed_preview_points.push_back(pt.cast<float>());
                }
            }
        }
    } else if (m_configuration.seed_type == Configuration::SEED_RANDOM) {
        // Random seeds with configurable seed
        std::mt19937 rng(m_configuration.random_seed);
        std::uniform_real_distribution<double> dist_x(bbox.min.x(), bbox.max.x());
        std::uniform_real_distribution<double> dist_y(bbox.min.y(), bbox.max.y());
        std::uniform_real_distribution<double> dist_z(bbox.min.z(), bbox.max.z());
        
        for (int i = 0; i < m_configuration.num_seeds; ++i) {
            Vec3d pt(dist_x(rng), dist_y(rng), dist_z(rng));
            m_seed_preview_points.push_back(pt.cast<float>());
        }
    } else {
        // Vertex seeds - subsample vertices
        int step = std::max(1, static_cast<int>(mesh.vertices.size()) / m_configuration.num_seeds);
        for (size_t i = 0; i < mesh.vertices.size() && m_seed_preview_points.size() < static_cast<size_t>(m_configuration.num_seeds); i += step) {
            m_seed_preview_points.push_back(mesh.vertices[i]);
        }
    }
    
    // Create OpenGL model for rendering seed points
    GLModel::Geometry init_data;
    init_data.format = {GLModel::PrimitiveType::Points, GLModel::Geometry::EVertexLayout::P3};
    
    for (const Vec3f& pt : m_seed_preview_points) {
        init_data.add_vertex(pt);
    }
    
    m_seed_preview_model.init_from(std::move(init_data));
    
    request_rerender();
    
    // Also update 2D preview
    update_2d_voronoi_preview();
}

// Phase 4: Render seed preview
void GLGizmoVoronoi::render_seed_preview()
{
    if (!m_seed_preview_model.is_initialized() || m_seed_preview_points.empty())
        return;
    
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    
    // Set point size for seed visualization
    glsafe(::glPointSize(8.0f));
    
    const Camera& camera = wxGetApp().plater()->get_camera();
    Transform3d view_model_matrix = camera.get_view_matrix();
    
    // Get model transform
    const Selection& selection = m_parent.get_selection();
    if (!selection.is_empty()) {
        const GLVolume* vol = selection.get_volume(*selection.get_volume_idxs().begin());
        if (vol) {
            view_model_matrix = camera.get_view_matrix() * vol->world_matrix();
        }
    }
    
    // Render seed points in green
    std::array<float, 4> green_color = {0.0f, 0.7f, 0.0f, 0.9f};
    
    const auto& shader = wxGetApp().get_shader("gouraud_light");
    if (shader) {
        wxGetApp().bind_shader(shader);
        shader->set_uniform("view_model_matrix", view_model_matrix);
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        shader->set_uniform("emission_factor", 0.5f);
        
        m_seed_preview_model.set_color(-1, green_color);
        m_seed_preview_model.render_geometry();
        
        wxGetApp().unbind_shader();
    }
    
    glsafe(::glPointSize(1.0f));
    glsafe(::glDisable(GL_BLEND));
}

// Phase 4 Part 2: Render exclusion zone


// Phase 5: Painting integration methods
void GLGizmoVoronoi::render_painter_gizmo() const
{
    const Selection& selection = m_parent.get_selection();
    render_triangles(selection);
    render_cursor();
}

void GLGizmoVoronoi::render_triangles(const Selection& selection) const
{
    if (!m_configuration.enable_triangle_painting)
        return;
        
    const ModelObject* mo = m_c->selection_info()->model_object();
    if (mo && selection.is_from_single_instance()) {
        const GLVolume* gl_volume = selection.get_volume(*selection.get_volume_idxs().begin());
        
        for (const ModelVolume* mv : mo->volumes) {
            if (mv->is_model_part()) {
                auto it = std::find(mo->volumes.begin(), mo->volumes.end(), mv);
                int mesh_id = std::distance(mo->volumes.begin(), it);
                if (mesh_id < (int)m_triangle_selectors.size() && m_triangle_selectors[mesh_id]) {
                    const Transform3d trafo_matrix = mo->instances[selection.get_instance_idx()]->get_transformation().get_matrix() * mv->get_matrix();
                    m_triangle_selectors[mesh_id]->render(m_imgui, trafo_matrix);
                }
            }
        }
    }
}

void GLGizmoVoronoi::update_model_object()
{
    // Save painted triangle data to model volume
    // This is called when painting changes need to be saved
    
    const Selection& selection = m_parent.get_selection();
    const ModelObject* mo = m_c->selection_info()->model_object();
    
    if (!mo || !selection.is_from_single_instance())
        return;
        
    for (const ModelVolume* mv : mo->volumes) {
        if (mv->is_model_part()) {
            auto it = std::find(mo->volumes.begin(), mo->volumes.end(), mv);
            int mesh_id = std::distance(mo->volumes.begin(), it);
            if (mesh_id < (int)m_triangle_selectors.size() && m_triangle_selectors[mesh_id]) {
                // Triangle data is automatically managed by the base class
                // Just ensure changes trigger model update
                m_parent.request_extra_frame();
            }
        }
    }
}

void GLGizmoVoronoi::update_from_model_object(bool first_update)
{
    // Load painted triangle data from model volume
    // This is called when the gizmo is opened or model changes
    
    const ModelObject* mo = m_c->selection_info()->model_object();
    if (!mo)
        return;
        
    // Initialize triangle selectors if needed
    if (first_update || m_triangle_selectors.empty()) {
        m_triangle_selectors.clear();
        
        for (const ModelVolume* mv : mo->volumes) {
            if (mv->is_model_part()) {
                const TriangleMesh& mesh = mv->mesh();
                m_triangle_selectors.emplace_back(std::make_unique<TriangleSelectorGUI>(mesh));
            }
        }
    }
}

bool GLGizmoVoronoi::on_init()
{
    // Initialize painting system
    m_cursor_radius = 2.0f;
    return true;
}

// 2D Voronoi Preview Implementation
void GLGizmoVoronoi::update_2d_voronoi_preview()
{
    m_2d_voronoi_cells.clear();
    
    if (m_seed_preview_points.empty()) {
        return;
    }
    
    // Convert 3D seed points to 2D (project to XY plane)
    std::vector<jcv_point> points_2d;
    points_2d.reserve(m_seed_preview_points.size());
    
    // Find bounding box of projected points
    float min_x = FLT_MAX, max_x = -FLT_MAX;
    float min_y = FLT_MAX, max_y = -FLT_MAX;
    
    for (const auto& pt3d : m_seed_preview_points) {
        min_x = std::min(min_x, pt3d.x());
        max_x = std::max(max_x, pt3d.x());
        min_y = std::min(min_y, pt3d.y());
        max_y = std::max(max_y, pt3d.y());
    }
    
    // Add some padding to avoid edge cases
    float padding = 0.1f;
    min_x -= padding;
    max_x += padding;
    min_y -= padding;
    max_y += padding;
    
    // Normalize points to [0, 1] range for the 2D preview
    float scale_x = (max_x - min_x) > 0 ? 1.0f / (max_x - min_x) : 1.0f;
    float scale_y = (max_y - min_y) > 0 ? 1.0f / (max_y - min_y) : 1.0f;
    
    for (const auto& pt3d : m_seed_preview_points) {
        jcv_point pt2d;
        pt2d.x = (pt3d.x() - min_x) * scale_x;
        pt2d.y = (pt3d.y() - min_y) * scale_y;
        points_2d.push_back(pt2d);
    }
    
    // Set up bounding rectangle
    jcv_rect rect;
    rect.min.x = 0.0f;
    rect.min.y = 0.0f;
    rect.max.x = 1.0f;
    rect.max.y = 1.0f;
    
    // Generate Voronoi diagram
    jcv_diagram diagram;
    memset(&diagram, 0, sizeof(jcv_diagram));
    
    try {
        jcv_diagram_generate((int)points_2d.size(), points_2d.data(), &rect, nullptr, &diagram);
        
        // Extract cells from the diagram
        const jcv_site* sites = jcv_diagram_get_sites(&diagram);
        
        if (sites) {
            for (int i = 0; i < diagram.numsites; ++i) {
                const jcv_site* site = &sites[i];
                VoronoiCell2D cell;
                cell.seed_point = Vec2f(site->p.x, site->p.y);
                
                // Collect vertices from the edges
                jcv_graphedge* edge = site->edges;
                std::vector<Vec2f> vertices;
                
                while (edge) {
                    vertices.push_back(Vec2f(edge->pos[0].x, edge->pos[0].y));
                    edge = edge->next;
                }
                
                // Sort vertices in counter-clockwise order
                if (vertices.size() >= 3) {
                    // Simple sorting by angle from center
                    Vec2f center = cell.seed_point;
                    std::sort(vertices.begin(), vertices.end(), [&center](const Vec2f& a, const Vec2f& b) {
                        float angle_a = atan2f(a.y() - center.y(), a.x() - center.x());
                        float angle_b = atan2f(b.y() - center.y(), b.x() - center.x());
                        return angle_a < angle_b;
                    });
                    
                    cell.vertices = vertices;
                    
                    // Generate a color for this cell based on the seed index
                    float hue = (float(i) / float(diagram.numsites)) * 360.0f;
                    float r, g, b;
                    ImGui::ColorConvertHSVtoRGB(hue / 360.0f, 0.6f, 0.8f, r, g, b);
                    cell.color = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 0.7f));
                    
                    m_2d_voronoi_cells.push_back(cell);
                }
            }
        }
        
        jcv_diagram_free(&diagram);
    } catch (...) {
        // Fallback to simple hexagonal approximation if Voronoi generation fails
        for (size_t i = 0; i < points_2d.size(); ++i) {
            VoronoiCell2D cell;
            cell.seed_point = Vec2f(points_2d[i].x, points_2d[i].y);
            
            // Generate a simple hexagonal cell around each seed point
            float radius = 0.08f; // Approximate cell size
            
            for (int j = 0; j < 6; ++j) {
                float angle = (j * 2.0f * M_PI) / 6.0f;
                Vec2f vertex;
                vertex.x() = cell.seed_point.x() + radius * cosf(angle);
                vertex.y() = cell.seed_point.y() + radius * sinf(angle);
                cell.vertices.push_back(vertex);
            }
            
            // Generate a color for this cell based on the seed index
            float hue = (float(i) / float(points_2d.size())) * 360.0f;
            float r, g, b;
            ImGui::ColorConvertHSVtoRGB(hue / 360.0f, 0.6f, 0.8f, r, g, b);
            cell.color = ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 0.7f));
            
            m_2d_voronoi_cells.push_back(cell);
        }
    }
}

void GLGizmoVoronoi::render_2d_voronoi_preview()
{
    if (m_2d_voronoi_cells.empty()) {
        ImGui::Text("%s", into_u8(_u8L("No preview available")).c_str());
        return;
    }
    
    // Create a canvas for drawing
    ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
    ImVec2 canvas_size = ImVec2(200, 200); // Fixed size for the preview
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    
    // Draw background
    ImU32 bg_color = m_is_dark_mode ? IM_COL32(40, 40, 40, 255) : IM_COL32(240, 240, 240, 255);
    draw_list->AddRectFilled(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), bg_color);
    
    // Draw border
    ImU32 border_color = m_is_dark_mode ? IM_COL32(80, 80, 80, 255) : IM_COL32(160, 160, 160, 255);
    draw_list->AddRect(canvas_pos, ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y), border_color, 0.0f, 0, 2.0f);
    
    // Draw Voronoi cells
    for (const auto& cell : m_2d_voronoi_cells) {
        if (cell.vertices.size() >= 3) {
            // Convert vertices to screen coordinates
            std::vector<ImVec2> screen_vertices;
            screen_vertices.reserve(cell.vertices.size());
            
            for (const auto& vertex : cell.vertices) {
                ImVec2 screen_pos;
                screen_pos.x = canvas_pos.x + vertex.x() * canvas_size.x;
                screen_pos.y = canvas_pos.y + vertex.y() * canvas_size.y;
                screen_vertices.push_back(screen_pos);
            }
            
            // Draw filled polygon
            draw_list->AddConvexPolyFilled(screen_vertices.data(), (int)screen_vertices.size(), cell.color);
            
            // Draw cell outline
            ImU32 outline_color = m_is_dark_mode ? IM_COL32(200, 200, 200, 150) : IM_COL32(80, 80, 80, 150);
            for (size_t i = 0; i < screen_vertices.size(); ++i) {
                size_t next_i = (i + 1) % screen_vertices.size();
                draw_list->AddLine(screen_vertices[i], screen_vertices[next_i], outline_color, 1.0f);
            }
        }
        
        // Draw seed point
        ImVec2 seed_screen_pos;
        seed_screen_pos.x = canvas_pos.x + cell.seed_point.x() * canvas_size.x;
        seed_screen_pos.y = canvas_pos.y + cell.seed_point.y() * canvas_size.y;
        
        ImU32 seed_color = m_is_dark_mode ? IM_COL32(255, 255, 255, 255) : IM_COL32(0, 0, 0, 255);
        draw_list->AddCircleFilled(seed_screen_pos, 3.0f, seed_color);
    }
    
    // Reserve space for the canvas
    ImGui::Dummy(canvas_size);
    
    // Add some info text
    ImGui::Text("%s: %zu", into_u8(_u8L("Cells")).c_str(), m_2d_voronoi_cells.size());
}

} // namespace Slic3r::GUI
