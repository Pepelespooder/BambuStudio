#include "GLGizmoVoronoi.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PresetBundle.hpp"
#include "libslic3r/VoronoiMesh.hpp"
#include "libslic3r/Geometry.hpp"

#include <GL/glew.h>
#include <thread>
#include <ctime>
#include <random>

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

    , tr_paint_exclusions(_u8L("Paint exclusions"))
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
        
        // Hollow cells option
        ImGui::Checkbox("Hollow cells", &m_configuration.hollow_cells);
        
        ImGui::Separator();
        
        // Phase 4: Random seed control
        ImGui::Text("%s:", tr_random_seed.c_str());
        ImGui::InputInt("##random_seed", &m_configuration.random_seed);
        ImGui::SameLine();
        if (ImGui::Button("Randomize##seed")) {
            randomize_seed();
        }
        
        // Phase 4: Seed preview
        if (ImGui::Checkbox(tr_seed_preview.c_str(), &m_configuration.show_seed_preview)) {
            if (m_configuration.show_seed_preview) {
                update_seed_preview();
            } else {
                m_seed_preview_model.reset();
            }
        }
        
        if (m_configuration.show_seed_preview && ImGui::Button("Update Preview")) {
            update_seed_preview();
        }
        
        ImGui::Separator();
        
        // Phase 5: Triangle painting exclusion
        if (ImGui::Checkbox(tr_paint_exclusions.c_str(), &m_configuration.enable_triangle_painting)) {
            if (m_configuration.enable_triangle_painting) {
                // Initialize painting system
                update_from_model_object(true);
            }
            request_rerender();
        }
        
        if (m_configuration.enable_triangle_painting) {
            ImGui::Text("Brush radius:");
            ImGui::SliderFloat("##cursor_radius", &m_cursor_radius, 
                             get_cursor_radius_min(), get_cursor_radius_max(), "%.1f");
            
            ImGui::TextColored(ImVec4(0.8f, 0.2f, 0.2f, 1.0f),
                             "Paint surfaces red to exclude");
            ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1.0f),
                             "Click & drag to paint");
        }
        
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
            voronoi_config.random_seed = m_state.config.random_seed;
            
            // Phase 4 Part 2: Layer exclusion
            voronoi_config.enable_layer_exclusion = m_state.config.enable_layer_exclusion;
            voronoi_config.exclusion_height_min = m_state.config.exclusion_height_min;
            voronoi_config.exclusion_height_max = m_state.config.exclusion_height_max;
            
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
        
        Model& model = *plater->model();
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
    config.random_seed = m_configuration.random_seed;
    
    // Call the seed generation from VoronoiMesh
    // For now, we'll generate simple preview based on bounding box
    BoundingBoxf3 bbox = bounding_box(mesh);
    
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
    
    GLShaderProgram* shader = wxGetApp().get_shader("gouraud_light");
    if (shader) {
        shader->start_using();
        shader->set_uniform("view_model_matrix", view_model_matrix);
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
        shader->set_uniform("emission_factor", 0.5f);
        
        m_seed_preview_model.set_color(-1, green_color);
        m_seed_preview_model.render();
        
        shader->stop_using();
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
                int mesh_id = &mv - &mo->volumes.front();
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
            int mesh_id = &mv - &mo->volumes.front();
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

} // namespace Slic3r::GUI
