#include "VoronoiMesh.hpp"
#include "libslic3r/AABBTreeIndirect.hpp"
#include "libslic3r/MeshBoolean.hpp"
#include <random>
#include <algorithm>
#include <set>
#include <map>
#include <array>
#include <cmath>
#include <limits>

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
#include <CGAL/Polygon_mesh_processing/orientation.h>

namespace Slic3r {

namespace PMP = CGAL::Polygon_mesh_processing;

// CGAL type definitions for 3D Delaunay/Voronoi
using K = CGAL::Exact_predicates_inexact_constructions_kernel;
using Vb = CGAL::Triangulation_vertex_base_with_info_3<int, K>;
using Cb = CGAL::Delaunay_triangulation_cell_base_with_circumcenter_3<K>;
using Tds = CGAL::Triangulation_data_structure_3<Vb, Cb>;
using Delaunay = CGAL::Delaunay_triangulation_3<K, Tds>;
using Point_3 = K::Point_3;
using CGALMesh = CGAL::Surface_mesh<Point_3>;

namespace {

indexed_triangle_set surface_mesh_to_indexed(const CGALMesh& mesh)
{
    indexed_triangle_set its;
    its.vertices.reserve(mesh.number_of_vertices());
    its.indices.reserve(mesh.number_of_faces());
    std::map<CGALMesh::Vertex_index, size_t> vertex_map;
    size_t idx = 0;
    for (auto v : mesh.vertices()) {
        const auto& p = mesh.point(v);
        its.vertices.emplace_back(float(p.x()), float(p.y()), float(p.z()));
        vertex_map[v] = idx++;
    }
    for (auto f : mesh.faces()) {
        auto he = mesh.halfedge(f);
        std::vector<size_t> face_vertices;
        auto start = he;
        do {
            auto v = mesh.target(he);
            face_vertices.push_back(vertex_map[v]);
            he = mesh.next(he);
        } while (he != start);

        if (face_vertices.size() == 3) {
            its.indices.emplace_back(int(face_vertices[0]),
                                     int(face_vertices[1]),
                                     int(face_vertices[2]));
        } else if (face_vertices.size() > 3) {
            for (size_t i = 1; i + 1 < face_vertices.size(); ++i) {
                its.indices.emplace_back(int(face_vertices[0]),
                                         int(face_vertices[i]),
                                         int(face_vertices[i + 1]));
            }
        }
    }
    return its;
}

bool indexed_to_surface_mesh(const indexed_triangle_set& its, CGALMesh& mesh)
{
    if (its.vertices.empty() || its.indices.empty())
        return false;

    std::vector<Point_3> points;
    points.reserve(its.vertices.size());
    for (const auto& v : its.vertices)
        points.emplace_back(v.x(), v.y(), v.z());

    std::vector<std::array<size_t, 3>> faces;
    faces.reserve(its.indices.size());
    for (const auto& tri : its.indices) {
        faces.push_back({ size_t(tri(0)), size_t(tri(1)), size_t(tri(2)) });
    }

    try {
        PMP::orient_polygon_soup(points, faces);
        PMP::polygon_soup_to_polygon_mesh(points, faces, mesh);
        if (mesh.is_empty() || !PMP::is_closed(mesh))
            return false;
        PMP::orient_to_bound_a_volume(mesh);
    } catch (...) {
        return false;
    }
    return true;
}

} // namespace

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
    const indexed_triangle_set* clip_mesh = config.clip_to_input ? &input_mesh : nullptr;
    auto result = tessellate_voronoi(seed_points, bbox, config, clip_mesh);
    if (!result)
        return nullptr;
    
    if (config.progress_callback && !config.progress_callback(90))
        return nullptr;

    if (config.clip_to_input) {
        clip_to_mesh_boundary(*result, input_mesh);
    }

    // Step 4: Finalize progress after tessellation, hollowing, and optional clipping
    
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
    
    const size_t vertex_count = mesh.vertices.size();
    if (vertex_count == 0)
        return seeds;
    
    if (max_seeds <= 0) {
        seeds.reserve(vertex_count);
        for (const Vec3f& vertex : mesh.vertices)
            seeds.push_back(vertex.cast<double>());
        return seeds;
    }
    
    const size_t target_count = std::min(vertex_count, static_cast<size_t>(max_seeds));
    const size_t step = std::max<size_t>(1, (vertex_count + target_count - 1) / target_count);
    
    seeds.reserve(target_count);
    for (size_t i = 0; i < vertex_count && seeds.size() < target_count; i += step)
        seeds.push_back(mesh.vertices[i].cast<double>());
    
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
    const Config& config,
    const indexed_triangle_set* clip_mesh)
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
    } catch (const std::exception&) {
        // CGAL exception - handle gracefully
        return nullptr;
    }
    
    if (config.progress_callback && !config.progress_callback(40))
        return nullptr;
    
    const bool clip_cells = clip_mesh != nullptr && !clip_mesh->indices.empty();
    
    // Step 3: For each vertex in Delaunay (seed point), compute its Voronoi cell
    // The Voronoi cell is the convex hull of the circumcenters of incident tetrahedra
    auto result = std::make_unique<indexed_triangle_set>();
    
    // Pre-allocate for better performance
    result->vertices.reserve(dt.number_of_vertices() * 20);  // Estimate
    result->indices.reserve(dt.number_of_vertices() * 40);   // Estimate
    std::vector<int> face_cell_ids;
    face_cell_ids.reserve(dt.number_of_vertices() * 40);
    
    int processed_vertices = 0;
    const int total_vertices = std::max(1, dt.number_of_vertices());
    
    for (auto vit = dt.finite_vertices_begin(); vit != dt.finite_vertices_end(); ++vit) {
        // Enhanced progress reporting
        ++processed_vertices;
        if ((processed_vertices % 10 == 0) || (processed_vertices == total_vertices)) {
            int progress = 40 + (processed_vertices * 50) / total_vertices;
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
                const Point_3 circumcenter = cell->circumcenter();
                
                // Enhanced validation: Check for valid coordinates
                if (std::isnan(circumcenter.x()) || std::isnan(circumcenter.y()) || std::isnan(circumcenter.z()) ||
                    std::isinf(circumcenter.x()) || std::isinf(circumcenter.y()) || std::isinf(circumcenter.z())) {
                    continue;  // Skip invalid circumcenters
                }
                
                // Enhanced bounds checking with margin
                const double margin = (bounds.max - bounds.min).norm() * 0.1;
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
                      const double ax = CGAL::to_double(a.x());
                      const double bx = CGAL::to_double(b.x());
                      if (ax != bx) return ax < bx;
                      const double ay = CGAL::to_double(a.y());
                      const double by = CGAL::to_double(b.y());
                      if (ay != by) return ay < by;
                      const double az = CGAL::to_double(a.z());
                      const double bz = CGAL::to_double(b.z());
                      return az < bz;
                  });
        voronoi_vertices.erase(
            std::unique(voronoi_vertices.begin(), voronoi_vertices.end(),
                        [](const Point_3& a, const Point_3& b) {
                            const double eps = std::numeric_limits<double>::epsilon() * 32.0;
                            const double ax = CGAL::to_double(a.x());
                            const double ay = CGAL::to_double(a.y());
                            const double az = CGAL::to_double(a.z());
                            const double bx = CGAL::to_double(b.x());
                            const double by = CGAL::to_double(b.y());
                            const double bz = CGAL::to_double(b.z());
                            return std::abs(ax - bx) <= eps &&
                                   std::abs(ay - by) <= eps &&
                                   std::abs(az - bz) <= eps;
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
            if (!PMP::is_closed(cell_mesh)) {
                // Non-manifold mesh - skip
                continue;
            }
            
            indexed_triangle_set cell_geometry = surface_mesh_to_indexed(cell_mesh);
            
            if (config.hollow_cells) {
                create_hollow_cells(cell_geometry, config.wall_thickness);
            }
            
            if (cell_geometry.indices.empty()) {
                continue;
            }
            
            if (clip_cells) {
                try {
                    MeshBoolean::cgal::intersect(cell_geometry, *clip_mesh);
                } catch (...) {
                    continue;
                }
                if (cell_geometry.indices.empty()) {
                    continue;
                }
            }
            
            const size_t vertex_offset = result->vertices.size();
            result->vertices.insert(result->vertices.end(), cell_geometry.vertices.begin(), cell_geometry.vertices.end());
            const int base = static_cast<int>(vertex_offset);
            int cell_id = vit->info();
            if (cell_id < 0)
                cell_id = 0;
            for (const auto& face : cell_geometry.indices) {
                result->indices.emplace_back(
                    int(face(0)) + base,
                    int(face(1)) + base,
                    int(face(2)) + base);
                face_cell_ids.push_back(cell_id);
            }
        } catch (...) {
            // Skip cells that fail to generate
            continue;
        }
    }
    
    if (config.progress_callback && !config.progress_callback(90))
        return nullptr;
    
    if (result->indices.size() != face_cell_ids.size()) {
        face_cell_ids.resize(result->indices.size(), -1);
    }
    result->properties.resize(result->indices.size());
    for (size_t i = 0; i < result->properties.size(); ++i) {
        result->properties[i].cell_id = face_cell_ids[i];
    }
    
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
    if (mesh.vertices.empty() || mesh.indices.empty() || wall_thickness <= 0.0f)
        return;

    // Fallback: construct a shell by offsetting vertices along robust normals and
    // stitching with tangentially smoothed inner faces. This tries to mimic a
    // proper offset while remaining resilient to degenerate input.
    indexed_triangle_set original = mesh;

    const size_t vertex_count = original.vertices.size();
    const size_t face_count   = original.indices.size();
    constexpr float normal_epsilon = 1e-6f;

    std::vector<Vec3f> face_normals(face_count, Vec3f::Zero());
    std::vector<float> face_areas(face_count, 0.0f);
    std::vector<std::vector<int>> adjacency(vertex_count);

    auto add_neighbor = [&](int u, int v) {
        if (u < 0 || v < 0)
            return;
        auto& list = adjacency[size_t(u)];
        if (std::find(list.begin(), list.end(), v) == list.end())
            list.push_back(v);
    };

    for (size_t fi = 0; fi < face_count; ++fi) {
        const auto& face = original.indices[fi];
        const Vec3f& v0 = original.vertices[face[0]];
        const Vec3f& v1 = original.vertices[face[1]];
        const Vec3f& v2 = original.vertices[face[2]];
        Vec3f normal = (v1 - v0).cross(v2 - v0);
        float len = normal.norm();
        if (len > normal_epsilon) {
            face_normals[fi] = normal / len;
            face_areas[fi]   = 0.5f * len;
        }

        add_neighbor(face[0], face[1]);
        add_neighbor(face[1], face[0]);
        add_neighbor(face[1], face[2]);
        add_neighbor(face[2], face[1]);
        add_neighbor(face[2], face[0]);
        add_neighbor(face[0], face[2]);
    }

    std::vector<Vec3f> vertex_normals(vertex_count, Vec3f::Zero());
    std::vector<float> vertex_weights(vertex_count, 0.0f);
    for (size_t fi = 0; fi < face_count; ++fi) {
        const float weight = face_areas[fi];
        if (weight <= 0.0f)
            continue;

        const auto& face = original.indices[fi];
        for (int j = 0; j < 3; ++j) {
            const int vid = face[j];
            vertex_normals[size_t(vid)] += face_normals[fi] * weight;
            vertex_weights[size_t(vid)] += weight;
        }
    }

    for (size_t i = 0; i < vertex_count; ++i) {
        Vec3f& normal = vertex_normals[i];
        if (vertex_weights[i] > normal_epsilon)
            normal /= vertex_weights[i];

        float len = normal.norm();
        if (len <= normal_epsilon) {
            Vec3f neighbor_sum = Vec3f::Zero();
            for (int n : adjacency[i]) {
                const Vec3f& neigh_normal = vertex_normals[size_t(n)];
                if (neigh_normal.norm() > normal_epsilon)
                    neighbor_sum += neigh_normal;
            }
            if (neighbor_sum.norm() > normal_epsilon) {
                normal = neighbor_sum.normalized();
            } else {
                for (size_t fi = 0; fi < face_count; ++fi) {
                    const auto& face = original.indices[fi];
                    if (face[0] == int(i) || face[1] == int(i) || face[2] == int(i)) {
                        if (face_normals[fi].norm() > normal_epsilon) {
                            normal = face_normals[fi];
                            break;
                        }
                    }
                }
                if (normal.norm() <= normal_epsilon)
                    normal = Vec3f(0.0f, 0.0f, 1.0f);
            }
        }

        len = normal.norm();
        if (len > normal_epsilon)
            normal /= len;
        else
            normal = Vec3f(0.0f, 0.0f, 1.0f);
    }

    std::vector<Vec3f> inner_vertices(vertex_count, Vec3f::Zero());
    for (size_t i = 0; i < vertex_count; ++i)
        inner_vertices[i] = original.vertices[i] - vertex_normals[i] * wall_thickness;

    if (wall_thickness > 0.0f) {
        constexpr int   smoothing_iterations = 2;
        constexpr float smoothing_strength   = 0.35f;
        std::vector<Vec3f> smoothed = inner_vertices;
        for (int iter = 0; iter < smoothing_iterations; ++iter) {
            std::vector<Vec3f> updated = smoothed;
            for (size_t i = 0; i < vertex_count; ++i) {
                const auto& neighbors = adjacency[i];
                if (neighbors.empty())
                    continue;

                Vec3f average = Vec3f::Zero();
                for (int n : neighbors)
                    average += smoothed[size_t(n)];
                average /= float(neighbors.size());

                Vec3f delta = average - smoothed[i];
                Vec3f normal = vertex_normals[i];
                float len = normal.norm();
                if (len > normal_epsilon) {
                    normal /= len;
                    delta -= normal * delta.dot(normal);
                }
                updated[i] = smoothed[i] + smoothing_strength * delta;
            }
            smoothed.swap(updated);
        }

        for (size_t i = 0; i < vertex_count; ++i) {
            Vec3f normal = vertex_normals[i];
            float len = normal.norm();
            if (len > normal_epsilon) {
                normal /= len;
                Vec3f base = original.vertices[i] - normal * wall_thickness;
                Vec3f tangential = smoothed[i] - base;
                tangential -= normal * tangential.dot(normal);
                inner_vertices[i] = base + tangential;
            } else {
                inner_vertices[i] = smoothed[i];
            }
        }
    }

    mesh.vertices.clear();
    mesh.indices.clear();
    mesh.vertices.reserve(vertex_count * 2);
    mesh.indices.reserve(face_count * 4);

    mesh.vertices.insert(mesh.vertices.end(), original.vertices.begin(), original.vertices.end());
    mesh.vertices.insert(mesh.vertices.end(), inner_vertices.begin(), inner_vertices.end());

    const size_t offset = vertex_count;
    for (const auto& face : original.indices)
        mesh.indices.push_back(face);
    for (const auto& face : original.indices) {
        Vec3i flipped(face[0] + int(offset), face[2] + int(offset), face[1] + int(offset));
        mesh.indices.push_back(flipped);
    }

    std::map<std::pair<int, int>, std::pair<int, int>> edge_map;
    for (const auto& face : original.indices) {
        for (int j = 0; j < 3; ++j) {
            int a = face[j];
            int b = face[(j + 1) % 3];
            auto key = std::minmax(a, b);
            auto dir = std::make_pair(a, b);
            if (!edge_map.emplace(key, dir).second)
                edge_map[key] = dir;
        }
    }

    for (const auto& [key, dir] : edge_map) {
        int a = dir.first;
        int b = dir.second;
        Vec3i tri1{ a, b, a + int(offset) };
        Vec3i tri2{ b, b + int(offset), a + int(offset) };
        mesh.indices.push_back(tri1);
        mesh.indices.push_back(tri2);
    }

    mesh.properties.clear();
    mesh.properties.resize(mesh.indices.size());
}

} // namespace Slic3r
