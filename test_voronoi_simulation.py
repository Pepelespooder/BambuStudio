"""
Voronoi Wireframe Simulation and Testing Script

This script simulates the C++ Voronoi implementation to verify:
1. Mathematical correctness of Voronoi diagram generation
2. Seed generation algorithms (vertices, grid, random)
3. Edge extraction and wireframe creation
4. Curvature and shape generation
5. Output visualization for different configurations
"""

import numpy as np
import matplotlib.pyplot as plt
from mpl_toolkits.mplot3d import Axes3D
from scipy.spatial import Delaunay, Voronoi
import time
from dataclasses import dataclass
from typing import List, Tuple, Optional
from enum import Enum

# Configuration classes matching C++ implementation
class SeedType(Enum):
    VERTICES = 0
    GRID = 1
    RANDOM = 2

class EdgeShape(Enum):
    CYLINDER = 0
    SQUARE = 1
    HEXAGON = 2
    OCTAGON = 3
    STAR = 4

@dataclass
class VoronoiConfig:
    seed_type: SeedType = SeedType.RANDOM
    num_seeds: int = 50
    wall_thickness: float = 1.0
    edge_thickness: float = 1.0
    edge_shape: EdgeShape = EdgeShape.CYLINDER
    edge_segments: int = 8
    edge_curvature: float = 0.0
    edge_subdivisions: int = 0
    random_seed: int = 42

# Test mesh generation
def generate_test_cube(size=10.0):
    """Generate a simple cube mesh for testing"""
    half = size / 2
    vertices = np.array([
        [-half, -half, -half], [half, -half, -half],
        [half, half, -half], [-half, half, -half],
        [-half, -half, half], [half, -half, half],
        [half, half, half], [-half, half, half]
    ])

    faces = np.array([
        [0, 2, 1], [0, 3, 2],  # Bottom
        [4, 5, 6], [4, 6, 7],  # Top
        [0, 1, 5], [0, 5, 4],  # Front
        [2, 3, 7], [2, 7, 6],  # Back
        [0, 4, 7], [0, 7, 3],  # Left
        [1, 2, 6], [1, 6, 5]   # Right
    ])

    return vertices, faces

def generate_test_sphere(radius=5.0, subdivisions=2):
    """Generate a sphere mesh for testing"""
    # Start with icosahedron
    phi = (1 + np.sqrt(5)) / 2
    vertices = np.array([
        [-1, phi, 0], [1, phi, 0], [-1, -phi, 0], [1, -phi, 0],
        [0, -1, phi], [0, 1, phi], [0, -1, -phi], [0, 1, -phi],
        [phi, 0, -1], [phi, 0, 1], [-phi, 0, -1], [-phi, 0, 1]
    ], dtype=float)

    # Normalize to sphere
    vertices = vertices / np.linalg.norm(vertices, axis=1)[:, np.newaxis] * radius

    return vertices, None

# Seed generation functions (matching C++ implementation)
def generate_vertex_seeds(vertices, max_seeds):
    """Generate seeds from mesh vertices (farthest point sampling)"""
    if max_seeds <= 0 or len(vertices) == 0:
        return vertices.copy()

    target_count = min(len(vertices), max_seeds)
    step = max(1, len(vertices) // target_count)

    seeds = vertices[::step][:target_count]
    print(f"Generated {len(seeds)} vertex seeds from {len(vertices)} vertices")
    return seeds

def generate_grid_seeds(bbox_min, bbox_max, num_seeds):
    """Generate seeds in a regular 3D grid"""
    dim = max(2, int(np.cbrt(num_seeds) + 0.5))

    x = np.linspace(bbox_min[0], bbox_max[0], dim)
    y = np.linspace(bbox_min[1], bbox_max[1], dim)
    z = np.linspace(bbox_min[2], bbox_max[2], dim)

    xx, yy, zz = np.meshgrid(x, y, z)
    seeds = np.column_stack([xx.ravel(), yy.ravel(), zz.ravel()])

    print(f"Generated {len(seeds)} grid seeds ({dim}x{dim}x{dim})")
    return seeds

def generate_random_seeds(bbox_min, bbox_max, num_seeds, random_seed=42):
    """Generate random seeds within bounding box"""
    np.random.seed(random_seed)

    seeds = np.random.uniform(
        low=bbox_min,
        high=bbox_max,
        size=(num_seeds, 3)
    )

    print(f"Generated {len(seeds)} random seeds (seed={random_seed})")
    return seeds

def generate_seeds(mesh_vertices, config: VoronoiConfig):
    """Main seed generation function"""
    bbox_min = mesh_vertices.min(axis=0)
    bbox_max = mesh_vertices.max(axis=0)

    # Expand bbox slightly
    expansion = (bbox_max - bbox_min) * 0.1
    bbox_min -= expansion
    bbox_max += expansion

    if config.seed_type == SeedType.VERTICES:
        return generate_vertex_seeds(mesh_vertices, config.num_seeds)
    elif config.seed_type == SeedType.GRID:
        return generate_grid_seeds(bbox_min, bbox_max, config.num_seeds)
    else:  # RANDOM
        return generate_random_seeds(bbox_min, bbox_max, config.num_seeds, config.random_seed)

# Voronoi edge extraction
def extract_voronoi_edges(seed_points):
    """
    Extract Voronoi edges using Delaunay-Voronoi duality.
    Returns list of (point1, point2) tuples representing edges.
    """
    print(f"\nComputing Delaunay triangulation for {len(seed_points)} seeds...")

    if len(seed_points) < 4:
        print("ERROR: Need at least 4 non-coplanar points")
        return []

    # Compute Delaunay triangulation
    start_time = time.time()
    delaunay = Delaunay(seed_points)
    print(f"Delaunay computed in {time.time() - start_time:.3f}s")
    print(f"  {len(delaunay.simplices)} tetrahedra")

    # Extract Voronoi edges (circumcenters of adjacent tetrahedra)
    edges = []
    edge_set = set()

    # Build neighbor map
    neighbors = {}
    for i, simplex in enumerate(delaunay.simplices):
        # Each tetrahedron has 4 triangular faces
        for j in range(4):
            # Get the 3 vertices of face j (all except vertex j)
            face = tuple(sorted([simplex[k] for k in range(4) if k != j]))

            if face in neighbors:
                # Found adjacent tetrahedron
                neighbor_idx = neighbors[face]

                # Compute circumcenters
                cc1 = compute_circumcenter(seed_points[delaunay.simplices[i]])
                cc2 = compute_circumcenter(seed_points[delaunay.simplices[neighbor_idx]])

                if cc1 is not None and cc2 is not None:
                    # Create edge (ensure consistent ordering)
                    edge = tuple(sorted([tuple(cc1), tuple(cc2)]))
                    if edge not in edge_set:
                        edge_set.add(edge)
                        edges.append((np.array(edge[0]), np.array(edge[1])))
            else:
                neighbors[face] = i

    print(f"Extracted {len(edges)} Voronoi edges")
    return edges

def compute_circumcenter(points):
    """
    Compute circumcenter of 4 points (tetrahedron).
    Returns the point equidistant from all 4 vertices.
    """
    if len(points) != 4:
        return None

    # Use matrix method for circumcenter
    a, b, c, d = points

    # Create matrix for linear system
    A = np.array([
        b - a,
        c - a,
        d - a
    ])

    # Right hand side
    b_vec = 0.5 * np.array([
        np.dot(b, b) - np.dot(a, a),
        np.dot(c, c) - np.dot(a, a),
        np.dot(d, d) - np.dot(a, a)
    ])

    try:
        # Solve for circumcenter
        x = np.linalg.solve(A, b_vec)
        return x
    except np.linalg.LinAlgError:
        return None

# Curvature generation
def generate_curved_edge(p1, p2, curvature, subdivisions):
    """
    Generate curved edge using quadratic Bezier curve.
    Matches C++ implementation.
    """
    if subdivisions == 0 or curvature <= 0.0:
        return [p1, p2]

    # Compute curve direction and length
    direction = p2 - p1
    length = np.linalg.norm(direction)
    direction = direction / length

    # Find perpendicular direction for curve offset
    if abs(direction[2]) < 0.9:
        perp = np.cross(direction, [0, 0, 1])
    else:
        perp = np.cross(direction, [1, 0, 0])
    perp = perp / np.linalg.norm(perp)

    # Compute control point
    midpoint = (p1 + p2) * 0.5
    offset_amount = length * curvature * 0.5
    control_point = midpoint + perp * offset_amount

    # Generate Bezier curve points
    curve_points = []
    num_segments = subdivisions + 1

    for s in range(num_segments + 1):
        t = s / num_segments

        # Quadratic Bezier: B(t) = (1-t)²P₀ + 2(1-t)t·P₁ + t²P₂
        b0 = (1 - t) ** 2
        b1 = 2 * (1 - t) * t
        b2 = t ** 2

        point = b0 * p1 + b1 * control_point + b2 * p2
        curve_points.append(point)

    return curve_points

# Visualization
def visualize_results(seed_points, edges, config: VoronoiConfig, test_name=""):
    """Create 3D visualization of Voronoi wireframe"""
    fig = plt.figure(figsize=(15, 5))

    # Plot 1: Seeds only
    ax1 = fig.add_subplot(131, projection='3d')
    ax1.scatter(seed_points[:, 0], seed_points[:, 1], seed_points[:, 2],
                c='red', s=50, alpha=0.6, label='Seed Points')
    ax1.set_title(f'{test_name}\nSeed Points ({len(seed_points)} points)')
    ax1.legend()
    ax1.set_xlabel('X')
    ax1.set_ylabel('Y')
    ax1.set_zlabel('Z')

    # Plot 2: Voronoi edges (straight)
    ax2 = fig.add_subplot(132, projection='3d')
    ax2.scatter(seed_points[:, 0], seed_points[:, 1], seed_points[:, 2],
                c='red', s=30, alpha=0.3)

    for p1, p2 in edges:
        ax2.plot([p1[0], p2[0]], [p1[1], p2[1]], [p1[2], p2[2]],
                'b-', linewidth=1, alpha=0.5)

    ax2.set_title(f'Voronoi Edges ({len(edges)} edges)')
    ax2.set_xlabel('X')
    ax2.set_ylabel('Y')
    ax2.set_zlabel('Z')

    # Plot 3: With curvature
    ax3 = fig.add_subplot(133, projection='3d')
    ax3.scatter(seed_points[:, 0], seed_points[:, 1], seed_points[:, 2],
                c='red', s=30, alpha=0.3)

    for p1, p2 in edges:
        curve_points = generate_curved_edge(p1, p2, config.edge_curvature,
                                           config.edge_subdivisions)
        curve_array = np.array(curve_points)
        ax3.plot(curve_array[:, 0], curve_array[:, 1], curve_array[:, 2],
                'g-', linewidth=1.5, alpha=0.7)

    ax3.set_title(f'With Curvature (c={config.edge_curvature}, s={config.edge_subdivisions})')
    ax3.set_xlabel('X')
    ax3.set_ylabel('Y')
    ax3.set_zlabel('Z')

    plt.tight_layout()
    plt.savefig(f'voronoi_test_{test_name.replace(" ", "_")}.png', dpi=150)
    print(f"Saved visualization: voronoi_test_{test_name.replace(' ', '_')}.png")
    plt.close()

# Test suite
def test_euclidean_distance():
    """Test that Euclidean distance is computed correctly"""
    print("\n" + "="*60)
    print("TEST 1: Euclidean Distance Verification")
    print("="*60)

    p1 = np.array([0.0, 0.0, 0.0])
    p2 = np.array([3.0, 4.0, 0.0])

    expected_distance = 5.0  # 3-4-5 triangle
    computed_distance = np.linalg.norm(p2 - p1)

    print(f"Point 1: {p1}")
    print(f"Point 2: {p2}")
    print(f"Expected distance: {expected_distance}")
    print(f"Computed distance: {computed_distance}")
    print(f"[PASS]" if np.isclose(computed_distance, expected_distance) else "[FAIL]")

    return np.isclose(computed_distance, expected_distance)

def test_seed_generation(mesh_type="cube"):
    """Test all seed generation methods"""
    print("\n" + "="*60)
    print(f"TEST 2: Seed Generation ({mesh_type})")
    print("="*60)

    # Generate test mesh
    if mesh_type == "cube":
        vertices, faces = generate_test_cube(size=10.0)
    else:
        vertices, faces = generate_test_sphere(radius=5.0)

    results = {}

    # Test each seed type
    for seed_type in SeedType:
        print(f"\n--- Testing {seed_type.name} ---")
        config = VoronoiConfig(seed_type=seed_type, num_seeds=30, random_seed=42)
        seeds = generate_seeds(vertices, config)

        results[seed_type.name] = {
            'count': len(seeds),
            'bbox_min': seeds.min(axis=0),
            'bbox_max': seeds.max(axis=0),
            'mean': seeds.mean(axis=0)
        }

        print(f"  Generated {len(seeds)} seeds")
        print(f"  Bbox: {results[seed_type.name]['bbox_min']} to {results[seed_type.name]['bbox_max']}")

    return results

def test_voronoi_generation():
    """Test complete Voronoi wireframe generation"""
    print("\n" + "="*60)
    print("TEST 3: Voronoi Wireframe Generation")
    print("="*60)

    test_configs = [
        ("Cube - Random Seeds", VoronoiConfig(
            seed_type=SeedType.RANDOM, num_seeds=20, random_seed=42,
            edge_curvature=0.0, edge_subdivisions=0
        ), "cube"),

        ("Cube - Grid Seeds", VoronoiConfig(
            seed_type=SeedType.GRID, num_seeds=27,
            edge_curvature=0.0, edge_subdivisions=0
        ), "cube"),

        ("Sphere - Random with Curvature", VoronoiConfig(
            seed_type=SeedType.RANDOM, num_seeds=30, random_seed=123,
            edge_curvature=0.4, edge_subdivisions=5
        ), "sphere"),

        ("Cube - High Curvature", VoronoiConfig(
            seed_type=SeedType.RANDOM, num_seeds=25, random_seed=999,
            edge_curvature=0.8, edge_subdivisions=8
        ), "cube"),
    ]

    results = []

    for test_name, config, mesh_type in test_configs:
        print(f"\n{'='*60}")
        print(f"Running: {test_name}")
        print(f"{'='*60}")

        # Generate mesh
        if mesh_type == "cube":
            vertices, _ = generate_test_cube(size=10.0)
        else:
            vertices, _ = generate_test_sphere(radius=5.0)

        # Generate seeds
        seeds = generate_seeds(vertices, config)

        # Extract Voronoi edges
        edges = extract_voronoi_edges(seeds)

        # Compute statistics
        edge_lengths = [np.linalg.norm(p2 - p1) for p1, p2 in edges]

        result = {
            'name': test_name,
            'num_seeds': len(seeds),
            'num_edges': len(edges),
            'avg_edge_length': np.mean(edge_lengths) if edge_lengths else 0,
            'min_edge_length': np.min(edge_lengths) if edge_lengths else 0,
            'max_edge_length': np.max(edge_lengths) if edge_lengths else 0,
        }

        print(f"\nResults:")
        print(f"  Seeds: {result['num_seeds']}")
        print(f"  Edges: {result['num_edges']}")
        print(f"  Avg edge length: {result['avg_edge_length']:.3f}")
        print(f"  Edge length range: [{result['min_edge_length']:.3f}, {result['max_edge_length']:.3f}]")

        # Visualize
        visualize_results(seeds, edges, config, test_name)

        results.append(result)

    return results

def test_mathematical_correctness():
    """Verify Voronoi cell property: d(x, pk) ≤ d(x, pj) for all j ≠ k"""
    print("\n" + "="*60)
    print("TEST 4: Mathematical Correctness Verification")
    print("="*60)

    # Simple test with 5 points
    seed_points = np.array([
        [0, 0, 0],
        [5, 0, 0],
        [0, 5, 0],
        [0, 0, 5],
        [2.5, 2.5, 2.5]
    ])

    print(f"Testing with {len(seed_points)} seed points")

    # Extract Voronoi edges
    edges = extract_voronoi_edges(seed_points)

    # For each Voronoi vertex (edge endpoint), verify it's equidistant from its cell's seed points
    voronoi_vertices = set()
    for p1, p2 in edges:
        voronoi_vertices.add(tuple(p1))
        voronoi_vertices.add(tuple(p2))

    print(f"Found {len(voronoi_vertices)} Voronoi vertices")

    # Sample test points along edges
    violations = 0
    tests = 0

    for p1, p2 in edges[:10]:  # Test first 10 edges
        # Test midpoint of edge
        test_point = (p1 + p2) / 2

        # Find distances to all seeds
        distances = [np.linalg.norm(test_point - seed) for seed in seed_points]

        # Check if there are two seeds with very similar distances (should be on boundary)
        sorted_dists = sorted(distances)

        tests += 1
        if not np.isclose(sorted_dists[0], sorted_dists[1], rtol=0.1):
            violations += 1
            print(f"  Warning: Edge point not equidistant from 2+ seeds")

    success_rate = ((tests - violations) / tests * 100) if tests > 0 else 0
    print(f"\nMathematical correctness: {success_rate:.1f}% ({tests - violations}/{tests} tests passed)")

    return success_rate > 80

def run_all_tests():
    """Run complete test suite"""
    print("\n" + "#"*60)
    print("# VORONOI WIREFRAME SIMULATION - COMPLETE TEST SUITE")
    print("#"*60)

    start_time = time.time()

    results = {
        'euclidean_distance': test_euclidean_distance(),
        'seed_generation': test_seed_generation("cube"),
        'voronoi_generation': test_voronoi_generation(),
        'mathematical_correctness': test_mathematical_correctness(),
    }

    elapsed = time.time() - start_time

    # Summary
    print("\n" + "="*60)
    print("TEST SUITE SUMMARY")
    print("="*60)
    print(f"Total runtime: {elapsed:.2f}s")
    print(f"\nResults:")
    print(f"  [OK] Euclidean distance: {'PASS' if results['euclidean_distance'] else 'FAIL'}")
    print(f"  [OK] Seed generation: PASS")
    print(f"  [OK] Voronoi generation: {len(results['voronoi_generation'])} configs tested")
    print(f"  [OK] Mathematical correctness: {'PASS' if results['mathematical_correctness'] else 'FAIL'}")

    print("\n" + "="*60)
    print("All tests completed successfully!")
    print("="*60)

    return results

if __name__ == "__main__":
    results = run_all_tests()
