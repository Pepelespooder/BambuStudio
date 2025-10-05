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
                }
                else if (face_vertices.size() > 3) {
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
                if (mesh.is_empty() || !CGAL::is_closed(mesh))
                    return false;
                PMP::orient_to_bound_a_volume(mesh);
            }
            catch (...) {
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

        // Step 3: Perform Voronoi tessellation
        const indexed_triangle_set* clip_mesh = config.clip_to_input ? &input_mesh : nullptr;
        auto result = tessellate_voronoi(seed_points, bbox, config, clip_mesh);
        if (!result)
            return nullptr;

        if (config.progress_callback && !config.progress_callback(90))
            return nullptr;

        if (config.clip_to_input) {
            clip_to_mesh_boundary(*result, input_mesh);
        }

        // Step 4: Finalize progress
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
            return generate_random_seeds(mesh, config.num_seeds, config.random_seed);
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
        int num_seeds,
        int random_seed)
    {
        std::vector<Vec3d> seeds;

        // Compute bounding box
        BoundingBoxf3 bbox;
        for (const auto& v : mesh.vertices) {
            bbox.merge(v.cast<double>());
        }

        // Use provided seed for reproducibility
        std::mt19937 gen(random_seed);

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

        // Need at least 4 non-coplanar points for 3D Delaunay
        if (seed_points.size() < 4) {
            return std::make_unique<indexed_triangle_set>();
        }

        if (config.progress_callback && !config.progress_callback(20))
            return nullptr;

        // Convert seed points to CGAL points
        std::vector<std::pair<Point_3, int>> cgal_points;
        cgal_points.reserve(seed_points.size());

        for (size_t i = 0; i < seed_points.size(); ++i) {
            const auto& p = seed_points[i];

            // Check for NaN or infinite values
            if (std::isnan(p.x()) || std::isnan(p.y()) || std::isnan(p.z()) ||
                std::isinf(p.x()) || std::isinf(p.y()) || std::isinf(p.z())) {
                continue;
            }

            cgal_points.emplace_back(Point_3(p.x(), p.y(), p.z()), int(i));
        }

        if (cgal_points.size() < 4) {
            return std::make_unique<indexed_triangle_set>();
        }

        // Build Delaunay triangulation
        Delaunay dt;
        try {
            dt.insert(cgal_points.begin(), cgal_points.end());

            if (dt.number_of_vertices() < 4 || !dt.is_valid()) {
                return std::make_unique<indexed_triangle_set>();
            }

            if (dt.number_of_finite_cells() == 0) {
                return std::make_unique<indexed_triangle_set>();
            }
        }
        catch (const std::exception&) {
            return nullptr;
        }

        if (config.progress_callback && !config.progress_callback(40))
            return nullptr;

        const bool clip_cells = clip_mesh != nullptr && !clip_mesh->indices.empty();

        // For each vertex in Delaunay (seed point), compute its Voronoi cell
        auto result = std::make_unique<indexed_triangle_set>();

        result->vertices.reserve(dt.number_of_vertices() * 20);
        result->indices.reserve(dt.number_of_vertices() * 40);
        std::vector<int> face_cell_ids;
        face_cell_ids.reserve(dt.number_of_vertices() * 40);

        int processed_vertices = 0;
        const int total_vertices = std::max(1, static_cast<int>(dt.number_of_vertices()));

        for (auto vit = dt.finite_vertices_begin(); vit != dt.finite_vertices_end(); ++vit) {
            ++processed_vertices;
            if ((processed_vertices % 10 == 0) || (processed_vertices == total_vertices)) {
                int progress = 40 + (processed_vertices * 50) / total_vertices;
                if (config.progress_callback && !config.progress_callback(progress))
                    return nullptr;
            }

            // Get all cells (tetrahedra) incident to this vertex
            std::vector<Delaunay::Cell_handle> incident_cells;
            incident_cells.reserve(32);
            dt.incident_cells(vit, std::back_inserter(incident_cells));

            if (incident_cells.empty()) {
                continue;
            }

            // Collect circumcenters of incident cells
            std::vector<Point_3> voronoi_vertices;
            voronoi_vertices.reserve(incident_cells.size());

            for (const auto& cell : incident_cells) {
                if (dt.is_infinite(cell)) {
                    continue;
                }

                try {
                    const Point_3 circumcenter = cell->circumcenter();

                    if (std::isnan(circumcenter.x()) || std::isnan(circumcenter.y()) || std::isnan(circumcenter.z()) ||
                        std::isinf(circumcenter.x()) || std::isinf(circumcenter.y()) || std::isinf(circumcenter.z())) {
                        continue;
                    }

                    const double margin = (bounds.max - bounds.min).norm() * 0.1;
                    if (circumcenter.x() >= bounds.min.x() - margin && circumcenter.x() <= bounds.max.x() + margin &&
                        circumcenter.y() >= bounds.min.y() - margin && circumcenter.y() <= bounds.max.y() + margin &&
                        circumcenter.z() >= bounds.min.z() - margin && circumcenter.z() <= bounds.max.z() + margin) {
                        voronoi_vertices.push_back(circumcenter);
                    }
                }
                catch (const std::exception&) {
                    continue;
                }
            }

            if (voronoi_vertices.size() < 4) {
                continue;
            }

            // Deduplicate vertices
            std::sort(voronoi_vertices.begin(), voronoi_vertices.end(),
                [](const Point_3& a, const Point_3& b) {
                    if (a.x() != b.x()) return a.x() < b.x();
                    if (a.y() != b.y()) return a.y() < b.y();
                    return a.z() < b.z();
                });

            voronoi_vertices.erase(
                std::unique(voronoi_vertices.begin(), voronoi_vertices.end(),
                    [](const Point_3& a, const Point_3& b) {
                        const double eps = 1e-9;
                        return std::abs(CGAL::to_double(a.x() - b.x())) < eps &&
                            std::abs(CGAL::to_double(a.y() - b.y())) < eps &&
                            std::abs(CGAL::to_double(a.z() - b.z())) < eps;
                    }),
                voronoi_vertices.end());

            if (voronoi_vertices.size() < 4) {
                continue;
            }

            // Create convex hull of Voronoi vertices
            CGALMesh cell_mesh;
            try {
                CGAL::convex_hull_3(voronoi_vertices.begin(), voronoi_vertices.end(), cell_mesh);

                if (cell_mesh.number_of_vertices() < 4 || cell_mesh.number_of_faces() < 4) {
                    continue;
                }

                if (!CGAL::is_closed(cell_mesh)) {
                    continue;
                }

                indexed_triangle_set cell_geometry = surface_mesh_to_indexed(cell_mesh);

                if (config.hollow_cells && cell_geometry.vertices.size() >= 4) {
                    create_hollow_cells(cell_geometry, config.wall_thickness);
                }

                if (cell_geometry.indices.empty()) {
                    continue;
                }

                if (clip_cells) {
                    try {
                        MeshBoolean::cgal::intersect(cell_geometry, *clip_mesh);
                    }
                    catch (...) {
                        continue;
                    }
                    if (cell_geometry.indices.empty()) {
                        continue;
                    }
                }

                const size_t vertex_offset = result->vertices.size();
                result->vertices.insert(result->vertices.end(),
                    cell_geometry.vertices.begin(),
                    cell_geometry.vertices.end());

                const int base = static_cast<int>(vertex_offset);
                int cell_id = vit->info();

                for (const auto& face : cell_geometry.indices) {
                    result->indices.emplace_back(
                        face(0) + base,
                        face(1) + base,
                        face(2) + base);
                    face_cell_ids.push_back(cell_id);
                }
            }
            catch (...) {
                continue;
            }
        }

        if (config.progress_callback && !config.progress_callback(90))
            return nullptr;

        // Store cell IDs in properties
        result->properties.resize(result->indices.size());
        for (size_t i = 0; i < result->properties.size() && i < face_cell_ids.size(); ++i) {
            result->properties[i].cell_id = face_cell_ids[i];
        }

        return result;
    }

    void VoronoiMesh::clip_to_mesh_boundary(
        indexed_triangle_set& voronoi_mesh,
        const indexed_triangle_set& original_mesh)
    {
        if (voronoi_mesh.vertices.empty() || original_mesh.vertices.empty())
            return;

        try {
            TriangleMesh voronoi_tm(voronoi_mesh);
            TriangleMesh original_tm(original_mesh);

            MeshBoolean::cgal::intersect(voronoi_tm, original_tm);

            voronoi_mesh = voronoi_tm.its;
        }
        catch (...) {
            // Keep original Voronoi mesh if boolean operation fails
        }
    }

    void VoronoiMesh::create_hollow_cells(
        indexed_triangle_set& mesh,
        float wall_thickness)
    {
        if (mesh.vertices.empty() || mesh.indices.empty() || wall_thickness <= 0.0f)
            return;

        indexed_triangle_set original = mesh;

        const size_t vertex_count = original.vertices.size();
        const size_t face_count = original.indices.size();

        // Compute vertex normals
        std::vector<Vec3f> vertex_normals(vertex_count, Vec3f::Zero());
        std::vector<float> vertex_weights(vertex_count, 0.0f);

        for (size_t fi = 0; fi < face_count; ++fi) {
            const auto& face = original.indices[fi];
            const Vec3f& v0 = original.vertices[face[0]];
            const Vec3f& v1 = original.vertices[face[1]];
            const Vec3f& v2 = original.vertices[face[2]];

            Vec3f normal = (v1 - v0).cross(v2 - v0);
            float area = 0.5f * normal.norm();

            if (area > 1e-6f) {
                normal.normalize();
                for (int j = 0; j < 3; ++j) {
                    vertex_normals[face[j]] += normal * area;
                    vertex_weights[face[j]] += area;
                }
            }
        }

        // Normalize vertex normals
        for (size_t i = 0; i < vertex_count; ++i) {
            if (vertex_weights[i] > 1e-6f) {
                vertex_normals[i] /= vertex_weights[i];
                vertex_normals[i].normalize();
            }
            else {
                vertex_normals[i] = Vec3f(0, 0, 1);
            }
        }

        // Create inner vertices
        std::vector<Vec3f> inner_vertices(vertex_count);
        for (size_t i = 0; i < vertex_count; ++i) {
            inner_vertices[i] = original.vertices[i] - vertex_normals[i] * wall_thickness;
        }

        // Build the hollow mesh
        mesh.vertices.clear();
        mesh.indices.clear();
        mesh.vertices.reserve(vertex_count * 2);
        mesh.indices.reserve(face_count * 4);

        // Add outer vertices
        mesh.vertices.insert(mesh.vertices.end(), original.vertices.begin(), original.vertices.end());
        // Add inner vertices
        mesh.vertices.insert(mesh.vertices.end(), inner_vertices.begin(), inner_vertices.end());

        const size_t offset = vertex_count;

        // Add outer faces
        for (const auto& face : original.indices) {
            mesh.indices.push_back(face);
        }

        // Add inner faces (reversed winding)
        for (const auto& face : original.indices) {
            Vec3i inner_face(face[0] + offset, face[2] + offset, face[1] + offset);
            mesh.indices.push_back(inner_face);
        }

        // Connect outer and inner shells with side faces
        std::map<std::pair<int, int>, bool> edge_processed;

        for (const auto& face : original.indices) {
            for (int j = 0; j < 3; ++j) {
                int a = face[j];
                int b = face[(j + 1) % 3];
                auto edge = std::make_pair(std::min(a, b), std::max(a, b));

                if (!edge_processed[edge]) {
                    edge_processed[edge] = true;

                    // Create two triangles for each edge
                    mesh.indices.push_back(Vec3i(a, b, a + offset));
                    mesh.indices.push_back(Vec3i(b, b + offset, a + offset));
                }
            }
        }

        mesh.properties.clear();
        mesh.properties.resize(mesh.indices.size());
    }

} // namespace Slic3r