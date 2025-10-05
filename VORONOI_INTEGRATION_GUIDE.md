# Voronoi Gizmo Integration Guide

## Quick Start Integration

This guide shows how to integrate the Voronoi gizmo into BambuStudio with minimal changes.

## Step 1: Register the Gizmo in GLGizmosManager

### 1.1 Update GLGizmosManager.hpp

Add the Voronoi entry to the EType enum (around line 86):

```cpp
enum EType : unsigned char
{
    // Order must match index in m_gizmos!
    Move,
    Rotate,
    Scale,
    Flatten,
    Cut,
    MeshBoolean,
    Assembly,
    MmuSegmentation,
    Text,
    Svg,
    FdmSupports,
    Seam,
    BrimEars,
    FuzzySkin,
    Measure,
    Simplify,
    Voronoi,        // <- ADD THIS LINE
    SlaSupports,
    // BBS
    //FaceRecognition,
    Hollow,
    Undefined,
};
```

### 1.2 Update GLGizmosManager.cpp

Add the include at the top (around line 45):

```cpp
#include "GLGizmoSimplify.hpp"
#include "GLGizmoVoronoi.hpp"  // <- ADD THIS LINE
#include "GLGizmoSlaSupports.hpp"
```

Add the gizmo instantiation in the `init()` method (around line 104):

```cpp
m_gizmos.emplace_back(new GLGizmoSimplify(m_parent, EType::Simplify));
m_gizmos.emplace_back(new GLGizmoVoronoi(m_parent, EType::Voronoi));  // <- ADD THIS LINE

//m_gizmos.emplace_back(new GLGizmoSlaSupports(m_parent, sprite_id++));
```

## Step 2: Update CMake Build Files

### 2.1 Update src/slic3r/CMakeLists.txt

Add the gizmo source files to the build:

```cmake
add_library(libslic3r_gui STATIC
    # ... existing files ...
    GUI/Gizmos/GLGizmoSimplify.cpp
    GUI/Gizmos/GLGizmoSimplify.hpp
    GUI/Gizmos/GLGizmoVoronoi.cpp     # <- ADD THIS LINE
    GUI/Gizmos/GLGizmoVoronoi.hpp     # <- ADD THIS LINE
    GUI/Gizmos/GLGizmoSlaSupports.cpp
    # ... rest of files ...
)
```

### 2.2 Update src/libslic3r/CMakeLists.txt

Add the VoronoiMesh utility files:

```cmake
add_library(libslic3r STATIC
    # ... existing files ...
    Geometry/Voronoi.cpp
    Geometry/Voronoi.hpp
    VoronoiMesh.cpp          # <- ADD THIS LINE
    VoronoiMesh.hpp          # <- ADD THIS LINE
    TriangleMesh.cpp
    # ... rest of files ...
)
```

## Step 3: ✅ Icon Resources (Already Included!)

**Good news!** The toolbar icons are already included in this PR:

- ✅ `resources/images/toolbar_voronoi.svg` (light mode)
- ✅ `resources/images/toolbar_voronoi_dark.svg` (dark mode)

The icons follow BambuStudio's design language:
- Cube outline representing the model boundary
- Cellular Voronoi structure inside (green lines)
- Seed points shown as small green dots
- Matches existing toolbar icon style

**No action needed** - icons are ready to use!

## Step 4: Build the Project

### Linux:
```bash
cd /path/to/BambuStudio
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
```

### Windows:
```cmd
cd C:\path\to\BambuStudio
mkdir build
cd build
cmake .. -G "Visual Studio 16 2019" -A x64
cmake --build . --config Release
```

### macOS:
```bash
cd /path/to/BambuStudio
mkdir -p build
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(sysctl -n hw.ncpu)
```

## Step 5: Test the Integration

1. Launch BambuStudio
2. Load a 3D model
3. Look for the Voronoi icon in the toolbar (should appear after Simplify)
4. Click the icon to activate the gizmo
5. The Voronoi configuration panel should appear

## Troubleshooting

### Build Errors

**Error: `GLGizmoVoronoi.hpp: No such file or directory`**
- Solution: Ensure files are in correct location: `src/slic3r/GUI/Gizmos/`
- Check CMakeLists.txt has correct paths

**Error: `VoronoiMesh.hpp: No such file or directory`**
- Solution: Ensure files are in `src/libslic3r/`
- Check CMakeLists.txt includes the files

**Error: `undefined reference to VoronoiMesh::generate`**
- Solution: Ensure VoronoiMesh.cpp is added to libslic3r CMakeLists.txt
- Clean build directory and rebuild

### Runtime Issues

**Gizmo doesn't appear in toolbar**
- Check: EType enum order matches gizmo initialization order
- Check: Icon files exist in resources/icons/
- Check: Gizmo is marked as selectable (on_is_selectable returns true)

**Clicking gizmo does nothing**
- Check: on_is_activable() returns true
- Check: A single model volume is selected
- Enable debug logging to see errors

**Progress bar stuck at 0%**
- This is expected - the core CGAL algorithm needs implementation
- See VORONOI_GIZMO_IMPLEMENTATION.md for next steps

## Verification Checklist

- [ ] Code compiles without errors
- [ ] BambuStudio launches successfully
- [ ] Voronoi icon appears in toolbar
- [ ] Clicking icon activates gizmo
- [ ] UI panel shows with controls:
  - [ ] Seed type dropdown (Vertices/Grid/Random)
  - [ ] Number of seeds slider (10-500)
  - [ ] Wall thickness slider (0.1-5.0)
  - [ ] Hollow cells checkbox
  - [ ] Generate button
  - [ ] Close button
- [ ] Selecting different seed types updates UI
- [ ] Clicking "Generate" starts processing
- [ ] Progress bar appears (currently 0% - placeholder)
- [ ] Can cancel processing
- [ ] Can close gizmo

## Next Steps

After successful integration, the next development phase is implementing the CGAL-based 3D Voronoi tessellation. See `VORONOI_GIZMO_IMPLEMENTATION.md` for:

1. CGAL integration details
2. Voronoi cell extraction algorithm
3. Mesh conversion logic
4. Performance optimization strategies

## Support

For issues or questions:
1. Check the main implementation guide: `VORONOI_GIZMO_IMPLEMENTATION.md`
2. Review existing gizmo implementations for reference:
   - `GLGizmoSimplify` - Similar async processing pattern
   - `GLGizmoMeshBoolean` - Mesh modification example
3. Check BambuStudio wiki for general build instructions

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                     User Interface Layer                      │
├─────────────────────────────────────────────────────────────┤
│  GLGizmoVoronoi                                               │
│  ├─ UI Panel (ImGui)                                          │
│  │  ├─ Seed Type Selection                                    │
│  │  ├─ Parameter Sliders                                      │
│  │  └─ Action Buttons                                         │
│  ├─ Worker Thread                                             │
│  └─ State Management                                          │
└────────────┬────────────────────────────────────────────────┘
             │
             │ Configuration
             │
             ▼
┌─────────────────────────────────────────────────────────────┐
│                  Algorithm Layer                              │
├─────────────────────────────────────────────────────────────┤
│  VoronoiMesh                                                  │
│  ├─ Seed Generation                                           │
│  │  ├─ Vertices Mode                                          │
│  │  ├─ Grid Mode                                              │
│  │  └─ Random Mode                                            │
│  ├─ 3D Tessellation (CGAL) [TO BE IMPLEMENTED]               │
│  │  ├─ Delaunay Triangulation                                 │
│  │  ├─ Voronoi Dual Computation                               │
│  │  └─ Cell Extraction                                        │
│  └─ Mesh Operations                                           │
│     ├─ Cell to Mesh Conversion                                │
│     ├─ Boundary Clipping                                      │
│     └─ Hollow Cell Generation                                 │
└────────────┬────────────────────────────────────────────────┘
             │
             │ indexed_triangle_set
             │
             ▼
┌─────────────────────────────────────────────────────────────┐
│                  Data Layer                                   │
├─────────────────────────────────────────────────────────────┤
│  TriangleMesh                                                 │
│  ModelVolume                                                  │
│  ModelObject                                                  │
└─────────────────────────────────────────────────────────────┘
```

## Code Flow

```
1. User selects model
   ↓
2. User opens Voronoi gizmo
   ↓
3. GLGizmoVoronoi::on_set_state() called
   ↓
4. UI displays configuration panel
   ↓
5. User adjusts parameters
   ↓
6. User clicks "Generate"
   ↓
7. apply_voronoi() spawns worker thread
   ↓
8. Worker thread calls VoronoiMesh::generate()
   ├─ Generate seed points
   ├─ Perform tessellation
   ├─ Convert cells to meshes
   └─ Return result
   ↓
9. worker_finished() called on main thread
   ↓
10. Result applied to ModelVolume
    ↓
11. Scene updated and rendered
```

## File Summary

| File | Purpose | Status |
|------|---------|--------|
| GLGizmoVoronoi.hpp | Gizmo interface declaration | ✅ Complete |
| GLGizmoVoronoi.cpp | Gizmo implementation | ✅ Complete |
| VoronoiMesh.hpp | Algorithm interface | ✅ Complete |
| VoronoiMesh.cpp | Algorithm implementation | ⚠️ Placeholder |
| VORONOI_GIZMO_IMPLEMENTATION.md | Technical documentation | ✅ Complete |
| VORONOI_INTEGRATION_GUIDE.md | Integration instructions | ✅ Complete |

Legend:
- ✅ Complete and ready
- ⚠️ Placeholder/needs CGAL implementation
- ❌ Not yet created
