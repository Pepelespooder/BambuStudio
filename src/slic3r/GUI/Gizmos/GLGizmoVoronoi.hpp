#ifndef slic3r_GLGizmoVoronoi_hpp_
#define slic3r_GLGizmoVoronoi_hpp_

#include "GLGizmoBase.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <mutex>
#include <thread>

namespace Slic3r {
class ModelVolume;
class Model;

namespace GUI {

class GLGizmoVoronoi : public GLGizmoBase
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
    virtual bool on_is_selectable() const override { return false; }
    virtual void on_set_state() override;
    
    virtual bool on_init() override { return true; }
    virtual void on_render() override;
    virtual void on_render_for_picking() override {}
    
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
        
        bool operator==(const Configuration& rhs) const {
            return seed_type == rhs.seed_type && 
                   num_seeds == rhs.num_seeds && 
                   wall_thickness == rhs.wall_thickness &&
                   hollow_cells == rhs.hollow_cells;
        }
        bool operator!=(const Configuration& rhs) const {
            return !(*this == rhs);
        }
    };
    
    Configuration m_configuration;
    
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
