# Voronoi Mesh Gizmo - Implementation Guide

## Overview

This document describes the implementation of a Voronoi mesh gizmo for BambuStudio, which enables users to convert 3D models into Voronoi mesh structures directly within the slicer.

## Architecture

### Components

1. **GLGizmoVoronoi** (`src/slic3r/GUI/Gizmos/GLGizmoVoronoi.{hpp,cpp}`)
   - User interface for the Voronoi gizmo
   - Parameter controls (seed type, count, wall thickness)
   - Async worker thread management
   - Preview and mesh application

2. **VoronoiMesh** (`src/libslic3r/VoronoiMesh.{hpp,cpp}`)
   - Core 3D Voronoi tessellation algorithm
   - Seed point generation strategies
   - Mesh conversion utilities

### Integration Points

The gizmo needs to be registered in the GLGizmosManager:

```cpp
// In GLGizmosManager.hpp - Add to EType enum:
enum EType : unsigned char
{
    // ... existing entries ...
    Voronoi,     // Add before Undefined
    Undefined,
};

// In GLGizmosManager.cpp - init() method:
#include "GLGizmoVoronoi.hpp"  // Add include

m_gizmos.emplace_back(new GLGizmoVoronoi(m_parent, EType::Voronoi));
```

## Features

### Current Implementation (Proof of Concept)

✅ **Completed:**
- Basic gizmo structure with UI
- Worker thread for async processing
- Configuration management
- Seed point generation strategies:
  - Vertices: Use mesh vertices as seeds
  - Grid: Regular 3D grid of points
  - Random: Random points in bounding box
- Parameter controls:
  - Seed type selection
  - Number of seeds (10-500)
  - Wall thickness (0.1-5.0mm)
  - Hollow cells option

⚠️ **Placeholder (Requires Full Implementation):**
- 3D Voronoi tessellation (CGAL integration needed)
- Voronoi cell to mesh conversion
- Clipping to original mesh boundary
- Hollow cell wall generation
- Mesh application to ModelVolume

## Technical Requirements

### Dependencies

- **CGAL** (Computational Geometry Algorithms Library)
  - `CGAL::Delaunay_triangulation_3` for 3D Delaunay triangulation
  - Voronoi diagram computation via dual of Delaunay
  - Boolean operations for mesh clipping

### Algorithms

#### 1. Seed Point Generation

Three strategies are implemented:

**Vertices Mode:**
- Uses existing mesh vertices as seed points
- Fast and preserves original structure
- Good for maintaining model detail

**Grid Mode:**
- Creates regular 3D grid of seed points
- Uniform cell distribution
- Predictable, aesthetic result

**Random Mode:**
- Randomly distributes points in bounding box
- Organic, natural appearance
- Each run produces unique result

#### 2. Voronoi Tessellation (To Be Implemented)

The full implementation requires:

```cpp
// Pseudo-code for CGAL implementation:
// 1. Create Delaunay triangulation
CGAL::Delaunay_triangulation_3<K> dt;
dt.insert(seed_points.begin(), seed_points.end());

// 2. For each Delaunay vertex (seed point):
for (auto vit = dt.finite_vertices_begin(); 
     vit != dt.finite_vertices_end(); ++vit) {
    
    // 3. Get incident cells (tetrahedra)
    std::vector<Cell_handle> cells;
    dt.incident_cells(vit, std::back_inserter(cells));
    
    // 4. Compute Voronoi cell (dual of Delaunay cell)
    VoronoiCell cell = compute_dual_cell(cells);
    
    // 5. Clip to bounding box
    cell = clip_to_bounds(cell, bbox);
    
    // 6. Convert to triangle mesh
    indexed_triangle_set cell_mesh = cell_to_mesh(cell);
    
    // 7. Add wall thickness if hollow
    if (config.hollow_cells) {
        cell_mesh = add_wall_thickness(cell_mesh, config.wall_thickness);
    }
    
    // 8. Merge into result
    result = merge_meshes(result, cell_mesh);
}

// 9. Clip to original mesh boundary
result = boolean_intersection(result, original_mesh);
```

#### 3. Hollow Cell Generation

To create hollow cells with wall thickness:

1. For each Voronoi cell:
   - Compute inward offset surface (inner wall)
   - Keep original surface (outer wall)
   - Connect walls at cell boundaries
   - Remove duplicate vertices/faces

2. Wall thickness is controlled via offset distance

## Usage Workflow

### User Interaction

1. **Select Model**: User selects a single model volume
2. **Open Gizmo**: Click Voronoi tool in toolbar
3. **Configure Parameters**:
   - Choose seed type (Vertices/Grid/Random)
   - Adjust number of seeds
   - Set wall thickness
   - Enable/disable hollow cells
4. **Generate**: Click "Generate Voronoi" button
5. **Process**: Background worker generates Voronoi mesh
   - Progress bar shows completion percentage
   - Can cancel during processing
6. **Apply**: Once complete, mesh replaces original
7. **Close**: Exit gizmo to return to normal editing

### Performance Considerations

- **Seed Count**: More seeds = more detail but slower processing
  - Recommended: 50-200 for most models
  - High detail: 200-500 (may take minutes)
  - Fast preview: 10-50

- **Mesh Complexity**: Original mesh complexity affects:
  - Boundary clipping time
  - Memory usage
  - Final mesh quality

- **Wall Thickness**: Affects:
  - Printability
  - Structural strength
  - Processing time (hollow mode)

## Build Integration

### CMakeLists.txt Updates Required

Add to `src/slic3r/CMakeLists.txt`:
```cmake
add_library(libslic3r_gui STATIC
    # ... existing files ...
    Gizmos/GLGizmoVoronoi.cpp
    Gizmos/GLGizmoVoronoi.hpp
)
```

Add to `src/libslic3r/CMakeLists.txt`:
```cmake
add_library(libslic3r STATIC
    # ... existing files ...
    VoronoiMesh.cpp
    VoronoiMesh.hpp
)
```

### Icon Resources

Create icon files:
- `resources/icons/voronoi.svg` (light mode)
- `resources/icons/voronoi_dark.svg` (dark mode)

## Testing Strategy

### Unit Tests

Create tests for:
1. Seed point generation
   - Verify correct number of seeds
   - Check bounds for random/grid modes
   - Validate vertex subsampling

2. Mesh operations
   - Test with various mesh complexities
   - Verify thread safety
   - Test cancellation

### Integration Tests

1. Load simple mesh (cube, sphere)
2. Generate Voronoi with default settings
3. Verify output mesh is valid
4. Check topology (manifold, no self-intersections)

### Performance Tests

1. Benchmark with various seed counts
2. Test memory usage
3. Verify cancellation response time

## Known Limitations & Future Enhancements

### Current Limitations

1. **No CGAL Implementation**: Core tessellation is placeholder
2. **No Mesh Application**: Result not applied to ModelVolume
3. **No Preview Rendering**: Cannot preview before applying
4. **No Undo Support**: Need undo/redo integration

### Future Enhancements

1. **Advanced Seed Strategies**:
   - Surface-based (points on mesh surface)
   - Curvature-based (more seeds in high-curvature areas)
   - User-painted seeds

2. **Cell Styling**:
   - Variable wall thickness per cell
   - Cell-specific materials
   - Fracture patterns

3. **Performance Optimizations**:
   - GPU acceleration for tessellation
   - Octree spatial subdivision
   - Progressive refinement

4. **Export Options**:
   - Export individual cells
   - Export as multi-material object
   - Save seed configuration

## References

### CGAL Documentation

- [3D Triangulations](https://doc.cgal.org/latest/Triangulation_3/)
- [Voronoi Diagrams](https://doc.cgal.org/latest/Voronoi_diagram_2/)
- [Boolean Operations](https://doc.cgal.org/latest/Polygon_mesh_processing/)

### Related Projects

- OpenVDB Voronoi fracture
- Blender Cell Fracture addon
- Houdini Voronoi Fracture

## Conclusion

This implementation provides a solid foundation for a Voronoi mesh gizmo in BambuStudio. The architecture is in place, and the main remaining work is implementing the CGAL-based 3D Voronoi tessellation algorithm and mesh conversion logic.

The modular design allows for incremental development:
1. Start with simple cube/sphere tests
2. Implement basic tessellation
3. Add hollow cell support
4. Optimize performance
5. Add advanced features

**Estimated Development Time**:
- Basic CGAL integration: 2-3 days
- Full tessellation + hollow cells: 5-7 days
- Polish + testing: 2-3 days
- **Total: ~2 weeks** for complete implementation

The feasibility is **HIGH** - all required infrastructure exists, and the implementation path is clear.
