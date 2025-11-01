# Voronoi Wireframe Edge Shapes & Curvature Feature

## ✅ Feature Complete: Custom Edge Cross-Sections + Curved Struts

Users can now control the **cross-sectional shape** AND **curvature** of the Voronoi wireframe struts!

## New Controls Added

### 1. **Edge Shape** (Dropdown)
Choose from 5 different cross-section profiles:
- **Cylinder** - Smooth round struts (default)
- **Square** - Rectangular cross-section struts
- **Hexagon** - 6-sided polygonal struts
- **Octagon** - 8-sided polygonal struts
- **Star** - 5-pointed star-shaped struts

### 2. **Edge Detail** (Slider: 3-32)
Controls the **smoothness/resolution** of the edge cross-section:
- **3** = Very low detail, angular shapes
- **8** = Balanced detail (default)
- **16** = High detail, smooth curves
- **32** = Maximum detail, very smooth

### 3. **Edge Curvature** (Slider: 0.0-1.0)
Controls how much the struts **bend and curve** between vertices:
- **0.0** = Perfectly straight struts (default)
- **0.3** = Gentle curves, organic feel
- **0.6** = Medium curves, flowing appearance
- **1.0** = Maximum curves, very organic/twisted

### 4. **Edge Subdivisions** (Slider: 0-10)
Controls how many **curve segments** are used per edge:
- **0** = Straight lines only (ignores curvature)
- **2-3** = Smooth gentle curves
- **5-7** = Very smooth flowing curves
- **10** = Maximum smoothness, intricate curves

## How It Works

### Curvature Generation
**Algorithm:** Quadratic Bezier curves

Each straight Voronoi edge is replaced with a smooth curve:
1. **Control Point** is placed perpendicular to the edge at the midpoint
2. **Offset Distance** = edge_length × curvature × 0.5
3. **Bezier Formula:** B(t) = (1-t)²P₀ + 2(1-t)t·P₁ + t²P₂
   - P₀ = start vertex
   - P₁ = control point (offset perpendicular)
   - P₂ = end vertex
4. The curve is subdivided into `edge_subdivisions + 1` segments

**Result:** Struts gracefully arc through 3D space instead of being straight!

### Shape Generation
Each edge shape uses a different algorithm to generate its cross-section:

**Cylinder:**
- Standard circular profile using sin/cos
- Smooth at all detail levels

**Square:**
- Interpolated rectangular profile
- Creates clean 90-degree corners when detail is low
- Smooths to rounded square at high detail

**Hexagon/Octagon:**
- Snaps vertices to regular polygon angles
- More sides visible with higher detail setting
- Creates sharp geometric edges

**Star:**
- 5-pointed star with alternating inner/outer radii
- Outer radius = edge_thickness
- Inner radius = 40% of edge_thickness
- Smooth transitions between points at high detail

### Implementation Details

**File:** `VoronoiMesh.cpp:740-785`
```cpp
auto get_profile_point = [&](int i, float radius) -> Vec3d {
    // Generates 2D profile point based on shape type
    // Returns Vec3d(x, y, 0) in profile space
    // x, y are then transformed by perpendicular vectors
}
```

## Use Cases

### Artistic/Decorative
- **Star** shapes create interesting shadow patterns
- **Square** provides industrial/mechanical aesthetic
- **Hexagon** gives organic/honeycomb appearance

### Functional
- **Square** struts may be stronger for structural loads
- **Cylinder** minimizes material while maintaining strength
- **Octagon** balances between round and square benefits

### Print Optimization
- Lower **Edge Detail** = faster slicing, fewer vertices
- Higher **Edge Detail** = smoother curves, better quality
- Adjust based on printer resolution and model complexity

## Parameter Interactions

| Parameter | Affects | Best Practice |
|-----------|---------|---------------|
| Random Seed | Pattern layout | Change to get different wireframe arrangements |
| Num Seeds | Wireframe density | 10-50 = coarse, 100-500 = fine |
| Edge Thickness | Strut diameter | 0.5-2mm typical, adjust for strength |
| Edge Shape | Cross-section | Choose based on aesthetics/function |
| Edge Detail | Smoothness | 8-16 for most cases, 32 for renders |

## File Changes

### Configuration
- `GLGizmoVoronoi.hpp:98-104` - Added EdgeShape enum
- `GLGizmoVoronoi.hpp:110-111` - Added edge_shape and edge_segments params
- `VoronoiMesh.hpp:88-95` - Added EdgeShape enum to VoronoiMesh
- `VoronoiMesh.hpp:27-28` - Added to Config struct

### UI
- `GLGizmoVoronoi.cpp:179-192` - Edge shape dropdown and detail slider

### Generation
- `VoronoiMesh.cpp:676-851` - Complete shape generation implementation
- Procedural cross-section generation for each shape type
- Proper vertex/face creation with caps

## Example Workflows

### Organic Flowing Lattice
1. Seed Type: Random
2. Num Seeds: 100
3. Random Seed: 42
4. Edge Thickness: 1.5mm
5. Edge Shape: Cylinder
6. Edge Detail: 12
7. **Edge Curvature: 0.4**
8. **Edge Subdivisions: 5**

### Mechanical Support Structure (Straight)
1. Seed Type: Grid
2. Num Seeds: 50
3. Edge Thickness: 2.0mm
4. Edge Shape: Square
5. Edge Detail: 8
6. **Edge Curvature: 0.0**
7. **Edge Subdivisions: 0**

### Decorative Twisted Star Pattern
1. Seed Type: Random
2. Num Seeds: 150
3. Edge Thickness: 1.0mm
4. Edge Shape: Star
5. Edge Detail: 16
6. **Edge Curvature: 0.6**
7. **Edge Subdivisions: 7**

### Extreme Organic Sculpture
1. Seed Type: Random
2. Num Seeds: 80
3. Edge Thickness: 2.5mm
4. Edge Shape: Octagon
5. Edge Detail: 20
6. **Edge Curvature: 1.0**
7. **Edge Subdivisions: 10**

---

**Status:** Fully implemented and ready for testing! 🎉
