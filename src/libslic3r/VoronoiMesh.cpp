#include "VoronoiMesh.hpp"
#include "libslic3r/AABBTreeIndirect.hpp"
#include "libslic3r/MeshBoolean.hpp"
#include <random>
#include <algorithm>
#include <set>
#include <map>

// CGAL headers for 3D Voronoi/Delaunay
#include <CGAL/Exact_predicates_inexact_constructions_kernel.h>
#include <CGAL/Delaunay_triangulation_3.h>
#include <CGAL/Triangulation_vertex_base_with_info_3.h>
#include <CGAL/convex_hull_3.h>
#include <CGAL/Polyhedron_3.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>

namespace Slic3r {

// CGAL type definitions for 3D Delaunay/Voronoi
using K = CGAL::Exact_predicates_inexact_constructions_kernel;
using Vb = CGAL::Triangulation_vertex_base_with_info_3<int, K>;
using Cb = CGAL::Triangulation_cell_base_3<K>;
using Tds = CGAL::Triangulation_data_structure_3<Vb, Cb>;
using Delaunay = CGAL::Delaunay_triangulation_3<K, Tds>;
using Point_3 = K::Point_3;
using CGALMesh = CGAL::Surface_mesh<Point_3>;

std::unique_ptr<indexed_triangle_set> VoronoiMesh::generate(
    const indexed_triangle_set& input_mesh,
    const Config& config)
{
    // Check for cancellation
    if (config.progress_callback && !config.progress_callback(0))
        return nullptr;
    
    // Step 1: Generate seed points (10% progress)
    std::vector<Vec3d> seed_points = generate_seed_points(input_mesh, config);
    if (seed_points.empty())
        return nullptr;
    
    if (config.progress_callback && !config.progress_callback(10))
        return nullptr;
    
    // Step 2: Compute bounding box
    BoundingBoxf3 bbox;
    for (const auto& v : input_mesh.vertices) {
        bbox.merge(v.cast<double>());
    }
    
    // Expand bbox slightly to ensure all cells are within bounds
    Vec3d expansion = bbox.size() * 0.1;
    bbox.min -= expansion;
    bbox.max += expansion;
    
    if (config.progress_callback && !config.progress_callback(20))
        return nullptr;
    
    // Step 3: Perform Voronoi tessellation (70% of work)
    // NOTE: This is a placeholder. Actual implementation would use CGAL's
    // 3D Delaunay triangulation to compute Voronoi diagram, then convert
    // Voronoi cells to meshes
    auto result = tessellate_voronoi(seed_points, bbox, config);
    if (!result)
        return nullptr;
    
    if (config.progress_callback && !config.progress_callback(90))
        return nullptr;
    
    // Step 4: Post-processing
    if (config.hollow_cells) {
        create_hollow_cells(*result, config.wall_thickness);
    }
    
    if (config.progress_callback && !config.progress_callback(100))
        return nullptr;
    
    return result;
}

std::vector<Vec3d> VoronoiMesh::generate_seed_points(
    const indexed_triangle_set& mesh,
    const Config& config)
{
    switch (config.seed_type) {
        case SeedType::Vertices:
            return generate_vertex_seeds(mesh, config.num_seeds);
        case SeedType::Grid:
            return generate_grid_seeds(mesh, config.num_seeds);
        case SeedType::Random:
            return generate_random_seeds(mesh, config.num_seeds);
        default:
            return {};
    }
}

std::vector<Vec3d> VoronoiMesh::generate_vertex_seeds(
    const indexed_triangle_set& mesh,
    int max_seeds)
{
    std::vector<Vec3d> seeds;
    
    // Use a subset of mesh vertices as seed points
    const size_t vertex_count = mesh.vertices.size();
    if (vertex_count == 0)
        return seeds;
    
    // Calculate step size to get approximately max_seeds points
    size_t step = std::max(size_t(1), vertex_count / size_t(max_seeds));
    
    seeds.reserve(std::min(vertex_count, size_t(max_seeds)));
    for (size_t i = 0; i < vertex_count; i += step) {
        seeds.push_back(mesh.vertices[i].cast<double>());
    }
    
    return seeds;
}

std::vector<Vec3d> VoronoiMesh::generate_grid_seeds(
    const indexed_triangle_set& mesh,
    int num_seeds)
{
    std::vector<Vec3d> seeds;
    
    // Compute bounding box
    BoundingBoxf3 bbox;
    for (const auto& v : mesh.vertices) {
        bbox.merge(v.cast<double>());
    }
    
    // Calculate grid dimensions
    // Try to make an approximately cubic grid
    int dim = std::max(2, int(std::cbrt(double(num_seeds)) + 0.5));
    
    Vec3d size = bbox.size();
    Vec3d step = size / double(dim - 1);
    
    seeds.reserve(dim * dim * dim);
    for (int x = 0; x < dim; ++x) {
        for (int y = 0; y < dim; ++y) {
            for (int z = 0; z < dim; ++z) {
                Vec3d point = bbox.min + Vec3d(x * step.x(), y * step.y(), z * step.z());
                seeds.push_back(point);
            }
        }
    }
    
    return seeds;
}

std::vector<Vec3d> VoronoiMesh::generate_random_seeds(
    const indexed_triangle_set& mesh,
    int num_seeds)
{
    std::vector<Vec3d> seeds;
    
    // Compute bounding box
    BoundingBoxf3 bbox;
    for (const auto& v : mesh.vertices) {
        bbox.merge(v.cast<double>());
    }
    
    // Generate random points within bounding box
    std::random_device rd;
    std::mt19937 gen(rd());
    
    std::uniform_real_distribution<double> dist_x(bbox.min.x(), bbox.max.x());
    std::uniform_real_distribution<double> dist_y(bbox.min.y(), bbox.max.y());
    std::uniform_real_distribution<double> dist_z(bbox.min.z(), bbox.max.z());
    
    seeds.reserve(num_seeds);
    for (int i = 0; i < num_seeds; ++i) {
        seeds.emplace_back(dist_x(gen), dist_y(gen), dist_z(gen));
    }
    
    return seeds;
}

std::unique_ptr<indexed_triangle_set> VoronoiMesh::tessellate_voronoi(
    const std::vector<Vec3d>& seed_points,
    const BoundingBoxf3& bounds,
    const Config& config)
{
    if (seed_points.empty()) {
        return std::make_unique<indexed_triangle_set>();
    }
    
    // Report progress: Starting
    if (config.progress_callback && !config.progress_callback(20))
        return nullptr;
    
    // Step 1: Convert seed points to CGAL points and build Delaunay triangulation
    std::vector<std::pair<Point_3, int>> cgal_points;
    cgal_points.reserve(seed_points.size());
    for (size_t i = 0; i < seed_points.size(); ++i) {
        const auto& p = seed_points[i];
        cgal_points.emplace_back(Point_3(p.x(), p.y(), p.z()), int(i));
    }
    
    Delaunay dt(cgal_points.begin(), cgal_points.end());
    
    if (config.progress_callback && !config.progress_callback(40))
        return nullptr;
    
    // Step 2: For each vertex in Delaunay (seed point), compute its Voronoi cell
    // The Voronoi cell is the convex hull of the circumcenters of incident tetrahedra
    
    auto result = std::make_unique<indexed_triangle_set>();
    
    int vertex_count = 0;
    int total_vertices = dt.number_of_vertices();
    
    for (auto vit = dt.finite_vertices_begin(); vit != dt.finite_vertices_end(); ++vit) {
        // Get all cells (tetrahedra) incident to this vertex
        std::vector<Delaunay::Cell_handle> incident_cells;
        dt.incident_cells(vit, std::back_inserter(incident_cells));
        
        // Collect circumcenters of incident cells (these are the Voronoi vertices)
        std::vector<Point_3> voronoi_vertices;
        for (const auto& cell : incident_cells) {
            if (!dt.is_infinite(cell)) {
                Point_3 circumcenter = dt.dual(cell);
                
                // Check if circumcenter is within reasonable bounds (clip to expanded bbox)
                if (circumcenter.x() >= bounds.min.x() - 1 && circumcenter.x() <= bounds.max.x() + 1 &&
                    circumcenter.y() >= bounds.min.y() - 1 && circumcenter.y() <= bounds.max.y() + 1 &&
                    circumcenter.z() >= bounds.min.z() - 1 && circumcenter.z() <= bounds.max.z() + 1) {
                    voronoi_vertices.push_back(circumcenter);
                }
            }
        }
        
        // Skip if we don't have enough vertices for a valid cell
        if (voronoi_vertices.size() < 4) {
            vertex_count++;
            continue;
        }
        
        // Step 3: Create convex hull of Voronoi vertices (this is the Voronoi cell)
        CGALMesh cell_mesh;
        try {
            CGAL::convex_hull_3(voronoi_vertices.begin(), voronoi_vertices.end(), cell_mesh);
            
            // Convert CGAL mesh to indexed_triangle_set
            if (cell_mesh.number_of_faces() > 0) {
                size_t vertex_offset = result->vertices.size();
                
                // Add vertices
                std::map<CGALMesh::Vertex_index, size_t> vertex_map;
                size_t idx = 0;
                for (auto v : cell_mesh.vertices()) {
                    const auto& p = cell_mesh.point(v);
                    result->vertices.emplace_back(float(p.x()), float(p.y()), float(p.z()));
                    vertex_map[v] = vertex_offset + idx;
                    idx++;
                }
                
                // Add faces
                for (auto f : cell_mesh.faces()) {
                    auto he = cell_mesh.halfedge(f);
                    std::vector<size_t> face_verts;
                    
                    // Collect vertices of this face
                    auto start = he;
                    do {
                        auto v = cell_mesh.target(he);
                        face_verts.push_back(vertex_map[v]);
                        he = cell_mesh.next(he);
                    } while (he != start);
                    
                    // Triangulate face if needed (convex hull faces should be triangles)
                    if (face_verts.size() == 3) {
                        result->indices.emplace_back(
                            int(face_verts[0]),
                            int(face_verts[1]),
                            int(face_verts[2])
                        );
                    } else if (face_verts.size() > 3) {
                        // Fan triangulation for polygons
                        for (size_t i = 1; i < face_verts.size() - 1; ++i) {
                            result->indices.emplace_back(
                                int(face_verts[0]),
                                int(face_verts[i]),
                                int(face_verts[i + 1])
                            );
                        }
                    }
                }
            }
        } catch (...) {
            // Skip cells that fail to generate
        }
        
        // Update progress
        vertex_count++;
        if (vertex_count % 10 == 0) {
            int progress = 40 + int((vertex_count * 50.0) / total_vertices);
            if (config.progress_callback && !config.progress_callback(progress))
                return nullptr;
        }
    }
    
    if (config.progress_callback && !config.progress_callback(90))
        return nullptr;
    
    return result;
}

void VoronoiMesh::clip_to_mesh_boundary(
    indexed_triangle_set& voronoi_mesh,
    const indexed_triangle_set& original_mesh)
{
    // Use boolean intersection to clip Voronoi mesh to original mesh boundary
    // This is an expensive operation but creates the best result
    
    if (voronoi_mesh.vertices.empty() || original_mesh.vertices.empty())
        return;
    
    try {
        TriangleMesh voronoi_tm(voronoi_mesh);
        TriangleMesh original_tm(original_mesh);
        
        // Perform intersection
        MeshBoolean::cgal::intersect(voronoi_tm, original_tm);
        
        // Update the voronoi mesh with clipped result
        voronoi_mesh = voronoi_tm.its;
    } catch (...) {
        // If boolean operation fails, keep original Voronoi mesh
        // This can happen with degenerate geometry
    }
}

void VoronoiMesh::create_hollow_cells(
    indexed_triangle_set& mesh,
    float wall_thickness)
{
    // Simple implementation: Scale mesh inward slightly to create hollow effect
    // A full implementation would use proper offset surfaces
    
    if (mesh.vertices.empty() || wall_thickness <= 0.0f)
        return;
    
    // Compute centroid
    Vec3f centroid(0, 0, 0);
    for (const auto& v : mesh.vertices) {
        centroid += v;
    }
    centroid /= float(mesh.vertices.size());
    
    // Scale vertices inward from centroid
    float scale_factor = 1.0f - (wall_thickness * 0.01f); // Simple scaling approach
    scale_factor = std::max(0.5f, std::min(0.95f, scale_factor));
    
    for (auto& v : mesh.vertices) {
        Vec3f dir = v - centroid;
        v = centroid + dir * scale_factor;
    }
}

} // namespace Slic3r
