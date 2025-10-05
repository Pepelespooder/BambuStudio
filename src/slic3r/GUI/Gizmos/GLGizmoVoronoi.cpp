#include "GLGizmoVoronoi.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/format.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/VoronoiMesh.hpp"

#include <GL/glew.h>
#include <thread>

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
    : GLGizmoBase(parent, sprite_id)
    , m_volume(nullptr)
    , m_show_wireframe(false)
    , m_move_to_center(false)
    , tr_mesh_name(_u8L("Mesh name"))
    , tr_seed_type(_u8L("Seed type"))
    , tr_num_seeds(_u8L("Number of seeds"))
    , tr_wall_thickness(_u8L("Wall thickness"))
{
}

GLGizmoVoronoi::~GLGizmoVoronoi()
{
    stop_worker_thread_request();
    if (m_worker.joinable())
        m_worker.join();
    m_glmodel.reset();
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
        
        // Number of seeds
        ImGui::Text("%s:", tr_num_seeds.c_str());
        ImGui::SliderInt("##num_seeds", &m_configuration.num_seeds, 10, 500);
        
        // Wall thickness
        ImGui::Text("%s:", tr_wall_thickness.c_str());
        ImGui::SliderFloat("##wall_thickness", &m_configuration.wall_thickness, 0.1f, 5.0f);
        
        // Hollow cells option
        ImGui::Checkbox("Hollow cells", &m_configuration.hollow_cells);
        
        ImGui::Separator();
        
        // Apply button
        {
            std::lock_guard<std::mutex> lock(m_state_mutex);
            if (m_state.status == State::idle) {
                if (ImGui::Button("Generate Voronoi")) {
                    apply_voronoi();
                }
            } else if (m_state.status == State::running) {
                ImGui::Text("Processing... %d%%", m_state.progress);
                if (ImGui::Button("Cancel")) {
                    stop_worker_thread_request();
                }
            }
        }
        
        if (ImGui::Button("Close")) {
            close();
        }
        
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
    if (m_state == On) {
        const Selection& selection = m_parent.get_selection();
        Model& model = *wxGetApp().plater()->model();
        m_volume = get_model_volume(selection, model);
        m_move_to_center = true;
    } else {
        m_volume = nullptr;
        m_glmodel.reset();
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
    std::lock_guard<std::mutex> lock(m_state_mutex);
    
    if (m_state.result && m_state.status == State::running) {
        // Apply the result to the model
        // TODO: Implement mesh replacement logic
        
        m_state.status = State::idle;
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

} // namespace Slic3r::GUI
