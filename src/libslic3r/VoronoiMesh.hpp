#ifndef slic3r_VoronoiMesh_hpp_
#define slic3r_VoronoiMesh_hpp_

#include "TriangleMesh.hpp"
#include "Point.hpp"
#include <vector>
#include <memory>

namespace Slic3r {

// 3D Voronoi tessellation for mesh generation
class VoronoiMesh
{
public:
    enum class SeedType {
        Vertices,    // Use mesh vertices as seed points
        Grid,        // Use regular grid of points
        Random       // Use random points within bounding box
    };
    
    struct Config {
        SeedType seed_type = SeedType::Vertices;
        int num_seeds = 50;
        float wall_thickness = 1.0f;
        bool hollow_cells = true;
        int random_seed = 42;  // For reproducible random generation
        
        // Phase 4 Part 2: Layer-based exclusion
        bool enable_layer_exclusion = false;
        float exclusion_height_min = 0.0f;
        float exclusion_height_max = 5.0f;
        
        // Progress callback
        std::function<bool(int)> progress_callback = nullptr;
    };
    
    // Generate a Voronoi mesh from an input mesh
    // Returns nullptr if generation fails or is cancelled
    static std::unique_ptr<indexed_triangle_set> generate(
        const indexed_triangle_set& input_mesh,
        const Config& config
    );

private:
    // Generate seed points based on configuration
    static std::vector<Vec3d> generate_seed_points(
        const indexed_triangle_set& mesh,
        const Config& config
    );
    
    // Generate seed points from mesh vertices
    static std::vector<Vec3d> generate_vertex_seeds(
        const indexed_triangle_set& mesh,
        int max_seeds
    );
    
    // Generate seed points on a regular grid
    static std::vector<Vec3d> generate_grid_seeds(
        const indexed_triangle_set& mesh,
        int num_seeds
    );
    
    // Generate random seed points
    static std::vector<Vec3d> generate_random_seeds(
        const indexed_triangle_set& mesh,
        int num_seeds
    );
    
    // Perform 3D Voronoi tessellation using CGAL
    // This is a placeholder for the actual CGAL implementation
    static std::unique_ptr<indexed_triangle_set> tessellate_voronoi(
        const std::vector<Vec3d>& seed_points,
        const BoundingBoxf3& bounds,
        const Config& config
    );
    
    // Clip Voronoi cells to original mesh boundary
    static void clip_to_mesh_boundary(
        indexed_triangle_set& voronoi_mesh,
        const indexed_triangle_set& original_mesh
    );
    
    // Create hollow cells by offsetting cell walls inward
    static void create_hollow_cells(
        indexed_triangle_set& mesh,
        float wall_thickness
    );
};

} // namespace Slic3r

#endif // slic3r_VoronoiMesh_hpp_
