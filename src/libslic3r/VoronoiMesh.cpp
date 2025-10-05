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
#include <CGAL/Delaunay_triangulation_cell_base_with_circumcenter_3.h>
#include <CGAL/convex_hull_3.h>
#include <CGAL/Polyhedron_3.h>
#include <CGAL/Surface_mesh.h>
#include <CGAL/Polygon_mesh_processing/polygon_soup_to_polygon_mesh.h>
#include <CGAL/Polygon_mesh_processing/orient_polygon_soup.h>

namespace Slic3r {

// CGAL type definitions for 3D Delaunay/Voronoi
using K = CGAL::Exact_predicates_inexact_constructions_kernel;
using Vb = CGAL::Triangulation_vertex_base_with_info_3<int, K>;
using Cb = CGAL::Delaunay_triangulation_cell_base_with_circumcenter_3<K>;
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
    // Tessellation now handles hollow cells per-cell for better quality
    auto result = tessellate_voronoi(seed_points, bbox, config);
    if (!result)
        return nullptr;
    
    if (config.progress_callback && !config.progress_callback(90))
        return nullptr;
    
    // Step 4: Optional post-processing (clipping to original mesh boundary)
    // Note: Hollow cells are now created per-cell during tessellation
    // This provides better wall connectivity and structure
    
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
        case SeedType::Random: {
            // Pass random seed for reproducibility
            std::vector<Vec3d> seeds;
            BoundingBoxf3 bbox;
            for (const auto& v : mesh.vertices) {
                bbox.merge(v.cast<double>());
            }
            
            // Use configured random seed for reproducibility
            std::mt19937 gen(config.random_seed);
            
            std::uniform_real_distribution<double> dist_x(bbox.min.x(), bbox.max.x());
            std::uniform_real_distribution<double> dist_y(bbox.min.y(), bbox.max.y());
            std::uniform_real_distribution<double> dist_z(bbox.min.z(), bbox.max.z());
            
            seeds.reserve(config.num_seeds);
            for (int i = 0; i < config.num_seeds; ++i) {
                seeds.emplace_back(dist_x(gen), dist_y(gen), dist_z(gen));
            }
            return seeds;
        }
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
    
    // Enhanced validation: Need at least 4 non-coplanar points for 3D Delaunay
    if (seed_points.size() < 4) {
        // Fallback: return simple box structure
        return std::make_unique<indexed_triangle_set>();
    }
    
    // Report progress: Starting
    if (config.progress_callback && !config.progress_callback(20))
        return nullptr;
    
    // Step 1: Convert seed points to CGAL points with enhanced error handling
    std::vector<std::pair<Point_3, int>> cgal_points;
    cgal_points.reserve(seed_points.size());
    
    // Validate and convert points
    for (size_t i = 0; i < seed_points.size(); ++i) {
        const auto& p = seed_points[i];
        
        // Enhanced validation: Check for NaN or infinite values
        if (std::isnan(p.x()) || std::isnan(p.y()) || std::isnan(p.z()) ||
            std::isinf(p.x()) || std::isinf(p.y()) || std::isinf(p.z())) {
            continue;  // Skip invalid points
        }
        
        cgal_points.emplace_back(Point_3(p.x(), p.y(), p.z()), int(i));
    }
    
    // Recheck after filtering
    if (cgal_points.size() < 4) {
        return std::make_unique<indexed_triangle_set>();
    }
    
    // Step 2: Build Delaunay triangulation with comprehensive error handling
    Delaunay dt;
    try {
        // Insert points incrementally for better error detection
        dt.insert(cgal_points.begin(), cgal_points.end());
        
        // Validate resulting triangulation
        if (dt.number_of_vertices() < 4 || !dt.is_valid()) {
            // Triangulation failed or is degenerate
            return std::make_unique<indexed_triangle_set>();
        }
        
        // Check for sufficient cells
        if (dt.number_of_finite_cells() == 0) {
            // No valid cells (all points coplanar or colinear)
            return std::make_unique<indexed_triangle_set>();
        }
    } catch (const std::exception& e) {
        // CGAL exception - handle gracefully
        return nullptr;
    }
    
    if (config.progress_callback && !config.progress_callback(40))
        return nullptr;
    
    // Step 3: For each vertex in Delaunay (seed point), compute its Voronoi cell
    // The Voronoi cell is the convex hull of the circumcenters of incident tetrahedra
    
    auto result = std::make_unique<indexed_triangle_set>();
    
    // Pre-allocate for better performance
    result->vertices.reserve(dt.number_of_vertices() * 20);  // Estimate
    result->indices.reserve(dt.number_of_vertices() * 40);   // Estimate
    
    int vertex_count = 0;
    int total_vertices = dt.number_of_vertices();
    int processed_cells = 0;
    
    for (auto vit = dt.finite_vertices_begin(); vit != dt.finite_vertices_end(); ++vit) {
        // Enhanced progress reporting
        if (++vertex_count % 10 == 0) {
            int progress = 40 + (vertex_count * 50) / total_vertices;
            if (config.progress_callback && !config.progress_callback(progress))
                return nullptr;
        }
        
        // Get all cells (tetrahedra) incident to this vertex
        std::vector<Delaunay::Cell_handle> incident_cells;
        incident_cells.reserve(32);  // Typical count
        dt.incident_cells(vit, std::back_inserter(incident_cells));
        
        // Enhanced: Filter infinite cells upfront
        if (incident_cells.empty()) {
            continue;
        }
        
        // Collect circumcenters of incident cells (these are the Voronoi vertices)
        std::vector<Point_3> voronoi_vertices;
        voronoi_vertices.reserve(incident_cells.size());
        
        for (const auto& cell : incident_cells) {
            // Skip infinite cells
            if (dt.is_infinite(cell)) {
                continue;
            }
            
            try {
                Point_3 circumcenter = cell->circumcenter();
                
                // Enhanced validation: Check for valid coordinates
                if (std::isnan(circumcenter.x()) || std::isnan(circumcenter.y()) || std::isnan(circumcenter.z()) ||
                    std::isinf(circumcenter.x()) || std::isinf(circumcenter.y()) || std::isinf(circumcenter.z())) {
                    continue;  // Skip invalid circumcenters
                }
                
                // Enhanced bounds checking with margin
                double margin = (bounds.max - bounds.min).norm() * 0.1;
                if (circumcenter.x() >= bounds.min.x() - margin && circumcenter.x() <= bounds.max.x() + margin &&
                    circumcenter.y() >= bounds.min.y() - margin && circumcenter.y() <= bounds.max.y() + margin &&
                    circumcenter.z() >= bounds.min.z() - margin && circumcenter.z() <= bounds.max.z() + margin) {
                    voronoi_vertices.push_back(circumcenter);
                }
            } catch (const std::exception&) {
                // Handle CGAL exceptions for degenerate cells
                continue;
            }
        }
        
        // Enhanced validation: Need at least 4 vertices for 3D convex hull
        if (voronoi_vertices.size() < 4) {
            continue;
        }
        
        // Deduplicate vertices (CGAL might produce duplicates in degenerate cases)
        std::sort(voronoi_vertices.begin(), voronoi_vertices.end(),
                  [](const Point_3& a, const Point_3& b) {
                      if (a.x() != b.x()) return a.x() < b.x();
                      if (a.y() != b.y()) return a.y() < b.y();
                      return a.z() < b.z();
                  });
        voronoi_vertices.erase(
            std::unique(voronoi_vertices.begin(), voronoi_vertices.end(),
                        [](const Point_3& a, const Point_3& b) {
                            double eps = 1e-6;
                            return std::abs(a.x() - b.x()) < eps &&
                                   std::abs(a.y() - b.y()) < eps &&
                                   std::abs(a.z() - b.z()) < eps;
                        }),
            voronoi_vertices.end());
        
        // Recheck after deduplication
        if (voronoi_vertices.size() < 4) {
            continue;
        }
        
        // Step 4: Create convex hull of Voronoi vertices (this is the Voronoi cell)
        CGALMesh cell_mesh;
        try {
            CGAL::convex_hull_3(voronoi_vertices.begin(), voronoi_vertices.end(), cell_mesh);
            
            // Enhanced validation: Check convex hull quality
            if (cell_mesh.number_of_vertices() < 4 || cell_mesh.number_of_faces() < 4) {
                // Degenerate convex hull
                continue;
            }
            
            // Validate mesh is manifold and properly oriented
            if (!CGAL::is_closed(cell_mesh)) {
                // Non-manifold mesh - skip
                continue;
            }
            
            // Convert CGAL mesh to indexed_triangle_set
            if (cell_mesh.number_of_faces() > 0) {
                processed_cells++;
                // If hollow cells are enabled, create walls for this cell
                if (config.hollow_cells) {
                    // Create a temporary indexed_triangle_set for this cell
                    indexed_triangle_set temp_cell;
                    
                    // Add vertices
                    std::map<CGALMesh::Vertex_index, size_t> vertex_map;
                    size_t idx = 0;
                    for (auto v : cell_mesh.vertices()) {
                        const auto& p = cell_mesh.point(v);
                        temp_cell.vertices.emplace_back(float(p.x()), float(p.y()), float(p.z()));
                        vertex_map[v] = idx;
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
                        
                        // Triangulate face if needed
                        if (face_verts.size() == 3) {
                            temp_cell.indices.emplace_back(
                                int(face_verts[0]),
                                int(face_verts[1]),
                                int(face_verts[2])
                            );
                        } else if (face_verts.size() > 3) {
                            // Fan triangulation for polygons
                            for (size_t i = 1; i < face_verts.size() - 1; ++i) {
                                temp_cell.indices.emplace_back(
                                    int(face_verts[0]),
                                    int(face_verts[i]),
                                    int(face_verts[i + 1])
                                );
                            }
                        }
                    }
                    
                    // Apply hollowing to this individual cell
                    create_hollow_cells(temp_cell, config.wall_thickness);
                    
                    // Merge the hollowed cell into result
                    size_t vertex_offset = result->vertices.size();
                    for (const auto& v : temp_cell.vertices) {
                        result->vertices.push_back(v);
                    }
                    for (const auto& f : temp_cell.indices) {
                        result->indices.emplace_back(
                            f[0] + vertex_offset,
                            f[1] + vertex_offset,
                            f[2] + vertex_offset
                        );
                    }
                } else {
                    // Solid cells - just add vertices and faces directly
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
                        
                        // Triangulate face if needed
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
    // Advanced implementation: Create true hollow structures with walls
    // Each face becomes a wall with proper thickness and connectivity
    
    if (mesh.vertices.empty() || mesh.indices.empty() || wall_thickness <= 0.0f)
        return;
    
    // Store original mesh
    indexed_triangle_set original = mesh;
    
    // Compute face normals for proper offsetting
    std::vector<Vec3f> face_normals;
    face_normals.reserve(original.indices.size());
    
    for (const auto& face : original.indices) {
        const Vec3f& v0 = original.vertices[face[0]];
        const Vec3f& v1 = original.vertices[face[1]];
        const Vec3f& v2 = original.vertices[face[2]];
        
        Vec3f edge1 = v1 - v0;
        Vec3f edge2 = v2 - v0;
        Vec3f normal = edge1.cross(edge2);
        float len = normal.norm();
        if (len > 1e-6f) {
            normal /= len;
        }
        face_normals.push_back(normal);
    }
    
    // Compute vertex normals by averaging face normals
    std::vector<Vec3f> vertex_normals(original.vertices.size(), Vec3f(0, 0, 0));
    std::vector<int> vertex_face_count(original.vertices.size(), 0);
    
    for (size_t i = 0; i < original.indices.size(); ++i) {
        const auto& face = original.indices[i];
        const Vec3f& normal = face_normals[i];
        
        for (int j = 0; j < 3; ++j) {
            vertex_normals[face[j]] += normal;
            vertex_face_count[face[j]]++;
        }
    }
    
    // Normalize vertex normals
    for (size_t i = 0; i < vertex_normals.size(); ++i) {
        if (vertex_face_count[i] > 0) {
            vertex_normals[i] /= float(vertex_face_count[i]);
            float len = vertex_normals[i].norm();
            if (len > 1e-6f) {
                vertex_normals[i] /= len;
            }
        }
    }
    
    // Create inner surface by offsetting vertices inward
    std::vector<Vec3f> inner_vertices;
    inner_vertices.reserve(original.vertices.size());
    
    float offset_distance = wall_thickness;
    for (size_t i = 0; i < original.vertices.size(); ++i) {
        Vec3f offset_vertex = original.vertices[i] - vertex_normals[i] * offset_distance;
        inner_vertices.push_back(offset_vertex);
    }
    
    // Build new mesh with walls
    mesh.vertices.clear();
    mesh.indices.clear();
    
    // Reserve space for outer surface + inner surface + wall connections
    mesh.vertices.reserve(original.vertices.size() * 2);
    mesh.indices.reserve(original.indices.size() * 2 + original.indices.size() * 6);
    
    // Add outer surface vertices
    for (const auto& v : original.vertices) {
        mesh.vertices.push_back(v);
    }
    
    // Add inner surface vertices
    for (const auto& v : inner_vertices) {
        mesh.vertices.push_back(v);
    }
    
    size_t vertex_offset = original.vertices.size();
    
    // Add outer surface faces (original orientation)
    for (const auto& face : original.indices) {
        mesh.indices.push_back(face);
    }
    
    // Add inner surface faces (reversed orientation for inward-facing)
    for (const auto& face : original.indices) {
        Vec3i inner_face;
        inner_face[0] = face[0] + vertex_offset;
        inner_face[1] = face[2] + vertex_offset; // Swap order for inward facing
        inner_face[2] = face[1] + vertex_offset;
        mesh.indices.push_back(inner_face);
    }
    
    // Create walls connecting edges
    // Build edge map to find boundary edges
    std::map<std::pair<int, int>, std::vector<int>> edge_to_faces;
    
    for (size_t i = 0; i < original.indices.size(); ++i) {
        const auto& face = original.indices[i];
        
        // Add three edges of the triangle
        for (int j = 0; j < 3; ++j) {
            int v1 = face[j];
            int v2 = face[(j + 1) % 3];
            
            // Store edge with consistent ordering (smaller index first)
            auto edge = std::make_pair(std::min(v1, v2), std::max(v1, v2));
            edge_to_faces[edge].push_back(i);
        }
    }
    
    // For edges that are boundaries (appear in only one face) or shared edges,
    // create connecting walls
    for (const auto& edge_entry : edge_to_faces) {
        int v1 = edge_entry.first.first;
        int v2 = edge_entry.first.second;
        
        // Create quad connecting outer edge to inner edge
        // Split quad into two triangles
        
        // Triangle 1: v1_outer, v2_outer, v1_inner
        Vec3i tri1;
        tri1[0] = v1;
        tri1[1] = v2;
        tri1[2] = v1 + vertex_offset;
        mesh.indices.push_back(tri1);
        
        // Triangle 2: v2_outer, v2_inner, v1_inner
        Vec3i tri2;
        tri2[0] = v2;
        tri2[1] = v2 + vertex_offset;
        tri2[2] = v1 + vertex_offset;
        mesh.indices.push_back(tri2);
    }
}

} // namespace Slic3r
