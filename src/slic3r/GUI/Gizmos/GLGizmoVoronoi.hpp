#ifndef slic3r_GLGizmoVoronoi_hpp_
#define slic3r_GLGizmoVoronoi_hpp_

#include "GLGizmoPainterBase.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include <imgui/imgui.h>

#include <mutex>
#include <thread>
#include <wx/string.h>

namespace Slic3r {
class ModelVolume;
class Model;

namespace GUI {

class GLGizmoVoronoi : public GLGizmoPainterBase
{
public:
    GLGizmoVoronoi(GLCanvas3D& parent, unsigned int sprite_id);
    virtual ~GLGizmoVoronoi();
    
    bool on_esc_key_down();
    
    std::string get_icon_filename(bool is_dark_mode) const override;

protected:
    virtual std::string on_get_name() const override;
    virtual std::string on_get_name_str() override { return "Voronoi"; }
    virtual void on_render_input_window(float x, float y, float bottom_limit) override;
    virtual bool on_is_activable() const override;
    virtual bool on_is_selectable() const override { return GLGizmoPainterBase::on_is_selectable(); }
    virtual void on_set_state() override;
    
    virtual bool on_init() override;
    virtual void on_render() override;
    virtual void on_render_for_picking() override {}
    
    // Phase 5: Painting integration
    void render_painter_gizmo() const override;
    void render_triangles(const Selection& selection) const override;
    void update_model_object() override;
    void update_from_model_object(bool first_update) override;
    PainterGizmoType get_painter_type() const override { return PainterGizmoType::FDM_SUPPORTS; }
    void on_opening() override;
    void on_shutdown() override;
    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    CommonGizmosDataID on_get_requirements() const override;

private:
    void apply_voronoi();
    void close();
    
    void process();
    void stop_worker_thread_request();
    void worker_finished();
    
    void create_gui_cfg();
    void request_rerender();
    
    void set_center_position();
    
    // Phase 4: Seed preview and randomization
    void update_seed_preview();
    void randomize_seed();
    void render_seed_preview();
    
    // 2D Voronoi preview
    void render_2d_voronoi_preview();
    void update_2d_voronoi_preview();
    struct VoronoiCell2D {
        std::vector<Vec2f> vertices;
        Vec2f seed_point;
        ImU32 color;
    };
    std::vector<VoronoiCell2D> m_2d_voronoi_cells;
    struct DelaunayEdge2D {
        Vec2f a;
        Vec2f b;
    };
    std::vector<DelaunayEdge2D> m_2d_delaunay_edges;
    struct Configuration
    {
        enum SeedType {
            SEED_VERTICES,
            SEED_GRID,
            SEED_RANDOM
        };
        
        SeedType seed_type = SEED_VERTICES;
        int num_seeds = 50;
        float wall_thickness = 1.0f;
        bool hollow_cells = true;
        bool clip_to_input = false;
        int random_seed = 42;  // For reproducible random generation
        bool show_seed_preview = false;
        
        // Phase 5: Triangle painting exclusion
        bool enable_triangle_painting = false;
        
        bool operator==(const Configuration& rhs) const {
            return seed_type == rhs.seed_type && 
                   num_seeds == rhs.num_seeds && 
                   wall_thickness == rhs.wall_thickness &&
                   hollow_cells == rhs.hollow_cells &&
                   clip_to_input == rhs.clip_to_input &&
                   random_seed == rhs.random_seed;
        }
        bool operator!=(const Configuration& rhs) const {
            return !(*this == rhs);
        }
    };
    
    Configuration m_configuration;
    
    // Seed preview
    std::vector<Vec3f> m_seed_preview_points;
    GLModel m_seed_preview_model;
    
    bool m_move_to_center;
    
    const ModelVolume* m_volume;
    
    bool m_show_wireframe;
    GLModel m_glmodel;
    
    struct State {
        enum Status {
            idle,
            running,
            cancelling
        };
        
        Status status = idle;
        int progress = 0;
        Configuration config;
        const ModelVolume* mv = nullptr;
        std::unique_ptr<indexed_triangle_set> result;
    };
    
    std::thread m_worker;
    std::mutex m_state_mutex;
    State m_state;
    
    struct GuiCfg
    {
        int top_left_width = 100;
        int bottom_left_width = 100;
        int input_width = 100;
        int window_offset_x = 100;
        int window_offset_y = 100;
        int window_padding = 0;
        size_t max_char_in_name = 30;
    };
    std::optional<GuiCfg> m_gui_cfg;
    
    const std::string tr_mesh_name;
    const std::string tr_seed_type;
    const std::string tr_num_seeds;
    const std::string tr_wall_thickness;
    const std::string tr_random_seed;
    const std::string tr_seed_preview;
    
    class VoronoiCanceledException : public std::exception
    {
    public:
        const char* what() const throw() {
            return L("Voronoi generation has been canceled");
        }
    };
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoVoronoi_hpp_
