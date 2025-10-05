# Voronoi Mesh Gizmo - Examples and Use Cases

## Visual Examples

### Example 1: Simple Cube with Vertex Seeds

**Input:**
- Model: 20mm cube
- Seed Type: Vertices
- Number of Seeds: 50
- Wall Thickness: 1mm
- Hollow Cells: Yes

**Expected Output:**
```
Original Cube (8 vertices)
    ┌─────────┐
    │         │
    │         │
    └─────────┘

After Voronoi (50 seed points, derived from vertices):
    ╔═╦═╦═╦═╦═╗
    ║ ║ ║ ║ ║ ║
    ╠═╬═╬═╬═╬═╣
    ║ ║ ║ ║ ║ ║
    ╠═╬═╬═╬═╬═╣
    ║ ║ ║ ║ ║ ║
    ╚═╩═╩═╩═╩═╝

Result: Organic cellular structure with ~50 cells
```

**Use Case:** Decorative box with organic pattern

---

### Example 2: Sphere with Grid Seeds

**Input:**
- Model: 50mm diameter sphere
- Seed Type: Grid
- Number of Seeds: 125 (5×5×5 grid)
- Wall Thickness: 0.5mm
- Hollow Cells: Yes

**Expected Output:**
```
Original Sphere:
        ╭───╮
      ╱       ╲
     │         │
      ╲       ╱
        ╰───╯

After Voronoi (125 grid seeds):
        ╭─┬─┬─╮
      ╱─┼─┼─┼─╲
     │┼─┼─┼─┼─┼│
      ╲─┼─┼─┼─╱
        ╰─┴─┴─╯

Result: Regular cellular pattern following sphere surface
```

**Use Case:** Lampshade with uniform light diffusion

---

### Example 3: Dragon Model with Random Seeds

**Input:**
- Model: Complex dragon mesh (50k triangles)
- Seed Type: Random
- Number of Seeds: 200
- Wall Thickness: 2mm
- Hollow Cells: No (solid cells)

**Expected Output:**
```
Original Dragon:
      /\___/\
     ( o   o )
     (  =^=  )
     /       \
    (         )
     \       /

After Voronoi (200 random seeds):
      ╱╲═══╱╲
     ╱ ╲╱╲╱ ╲
     ( ═╬╬═ )
     ╱   ║   ╲
    (  ══╬══  )
     ╲   ║   ╱

Result: Organic shattered/fractured appearance
```

**Use Case:** Artistic sculpture, low-poly aesthetic

---

## Parameter Effects

### Number of Seeds

| Seeds | Effect | Performance | Use Case |
|-------|--------|-------------|----------|
| 10-30 | Very large cells, chunky look | Fast (<1s) | Quick preview, large props |
| 50-100 | Medium cells, balanced detail | Medium (1-5s) | General purpose |
| 100-200 | Small cells, detailed pattern | Slow (5-30s) | High detail work |
| 200-500 | Very small cells, intricate | Very slow (30s-5m) | Fine art, jewelry |

### Seed Type Comparison

#### Vertices Mode
```
Advantages:
+ Preserves original model features
+ Fast computation
+ Predictable results
+ Good for models with good vertex distribution

Disadvantages:
- Limited by original mesh density
- May have uneven cell sizes

Best for:
- Organic models (characters, animals)
- Models with well-distributed geometry
```

#### Grid Mode
```
Advantages:
+ Uniform cell distribution
+ Predictable, regular pattern
+ Good for architectural models
+ Consistent cell sizes

Disadvantages:
- May not follow model contours well
- Can look mechanical/artificial

Best for:
- Architectural elements
- Technical/industrial designs
- When uniform pattern is desired
```

#### Random Mode
```
Advantages:
+ Natural, organic appearance
+ Each generation is unique
+ Good for artistic effects
+ Works well with any geometry

Disadvantages:
- Unpredictable results
- May need multiple attempts
- Harder to reproduce exact result

Best for:
- Artistic projects
- Fracture effects
- Natural/organic aesthetics
- Experimentation
```

### Wall Thickness Effects

| Thickness | Effect | Printability |
|-----------|--------|--------------|
| 0.1-0.5mm | Delicate, detailed | Challenging, may be fragile |
| 0.5-1.0mm | Light, visible detail | Good for most printers |
| 1.0-2.0mm | Sturdy, clear structure | Excellent printability |
| 2.0-5.0mm | Very thick, heavy | Very strong but uses more material |

### Hollow vs Solid Cells

**Hollow Cells (Recommended):**
```
Advantages:
+ Lighter weight (less material)
+ Faster printing
+ Interesting cross-sections
+ Better for large models

Structure:
╔═══╗     Each cell has walls but is hollow inside
║   ║     Reduces material usage by 50-80%
║   ║
╚═══╝
```

**Solid Cells:**
```
Advantages:
+ Stronger structure
+ No internal voids
+ Simpler geometry

Structure:
╔═══╗     Each cell is completely filled
║███║     More material, heavier
║███║
╚═══╝
```

---

## Real-World Use Cases

### 1. Architectural Models

**Application:** Building facade panels
```
Model: Flat panel 300×300×5mm
Settings:
- Seed Type: Grid
- Seeds: 100 (10×10 pattern)
- Wall Thickness: 1.5mm
- Hollow: Yes

Result: Lightweight panel with uniform cellular pattern
Benefits: 60% material savings, interesting shadows
```

### 2. Jewelry Design

**Application:** Pendant or bracelet link
```
Model: 30mm sphere or custom shape
Settings:
- Seed Type: Vertices
- Seeds: 50-100
- Wall Thickness: 0.5mm
- Hollow: Yes

Result: Delicate, organic structure
Benefits: Lightweight, catches light beautifully
```

### 3. Lampshades

**Application:** Desktop lamp cover
```
Model: Cylinder 150mm diameter, 200mm tall
Settings:
- Seed Type: Random
- Seeds: 200
- Wall Thickness: 1mm
- Hollow: Yes

Result: Organic light diffusion pattern
Benefits: Interesting light patterns, good ventilation
```

### 4. Prosthetics and Orthotics

**Application:** Lightweight arm brace
```
Model: Arm-shaped shell
Settings:
- Seed Type: Grid
- Seeds: 150
- Wall Thickness: 2mm
- Hollow: No

Result: Strong lattice structure with good ventilation
Benefits: Breathable, lightweight, customizable
```

### 5. Art Installations

**Application:** Large sculpture
```
Model: Abstract form or figure
Settings:
- Seed Type: Random
- Seeds: 300-500
- Wall Thickness: 3mm
- Hollow: Yes

Result: Complex, organic aesthetic
Benefits: Reduces weight, creates depth, visually interesting
```

### 6. Functional Parts

**Application:** Gripper handle
```
Model: Cylindrical grip 30mm diameter
Settings:
- Seed Type: Vertices
- Seeds: 50
- Wall Thickness: 2mm
- Hollow: No

Result: Textured, ergonomic surface
Benefits: Better grip, reduced material, unique look
```

---

## Workflow Examples

### Workflow 1: Quick Preview

```
1. Import model
2. Select Voronoi gizmo
3. Choose "Grid" seed type
4. Set seeds to 50
5. Click "Generate"
6. Review result in 1-2 seconds
7. Adjust parameters if needed
8. Apply final settings
```

### Workflow 2: Detailed Artistic Piece

```
1. Import high-poly model
2. Select Voronoi gizmo
3. Choose "Random" seed type
4. Set seeds to 200
5. Generate and preview
6. Try 3-4 different random generations
7. Select best result
8. Optionally adjust wall thickness
9. Apply and export
```

### Workflow 3: Engineering Part

```
1. Import functional part
2. Select Voronoi gizmo
3. Choose "Grid" for predictability
4. Set seeds to match structural needs
5. Enable hollow cells
6. Set wall thickness for strength (2-3mm)
7. Generate and verify strength
8. Test print small section first
9. Apply to full model
```

---

## Performance Expectations

### Small Models (<10k triangles, <50mm)

| Seeds | Time | Memory |
|-------|------|--------|
| 50 | 1-2s | 50MB |
| 100 | 2-5s | 100MB |
| 200 | 5-15s | 200MB |

### Medium Models (10k-50k triangles, 50-150mm)

| Seeds | Time | Memory |
|-------|------|--------|
| 50 | 2-5s | 100MB |
| 100 | 5-15s | 200MB |
| 200 | 15-60s | 400MB |

### Large Models (>50k triangles, >150mm)

| Seeds | Time | Memory |
|-------|------|--------|
| 50 | 5-10s | 200MB |
| 100 | 15-30s | 400MB |
| 200 | 30-120s | 800MB |

*Times are estimates for modern CPU (4+ cores). Actual performance depends on:
- CPU speed and core count
- Input mesh complexity
- Seed distribution
- Hollow cell setting

---

## Tips and Best Practices

### For Best Results:

1. **Start Small**: Begin with 50 seeds, increase if needed
2. **Preview First**: Use low seed count for quick preview
3. **Consider Scale**: Larger models can use more seeds
4. **Wall Thickness**: 1-2mm is good for most applications
5. **Hollow Mode**: Use for models >50mm to save material
6. **Seed Type**: Grid for uniform, Random for organic
7. **Test Print**: Print a small section before full model

### Common Mistakes to Avoid:

1. ❌ Too many seeds on small models (makes tiny fragile cells)
2. ❌ Too thin walls (<0.5mm) - may not print well
3. ❌ Solid cells on large models - wastes material
4. ❌ Not testing print settings first
5. ❌ Using Vertices mode on low-poly models

### Troubleshooting:

**Problem:** Cells too small/fragile
- Solution: Reduce seed count or increase wall thickness

**Problem:** Pattern looks too regular
- Solution: Switch from Grid to Random seed type

**Problem:** Processing takes too long
- Solution: Reduce seed count, use simpler input mesh

**Problem:** Result doesn't fit original shape
- Solution: Ensure boundary clipping is enabled (automatic)

---

## Material Savings Comparison

### Example: 100mm cube

| Mode | Seeds | Hollow | Volume | Savings |
|------|-------|--------|--------|---------|
| Original | - | - | 1000cm³ | 0% |
| Voronoi | 50 | No | 800cm³ | 20% |
| Voronoi | 50 | Yes | 300cm³ | 70% |
| Voronoi | 100 | Yes | 250cm³ | 75% |
| Voronoi | 200 | Yes | 200cm³ | 80% |

**Conclusion:** Hollow Voronoi structures can save 70-80% material while maintaining structural integrity for many applications.

---

## Further Exploration

Once comfortable with basic usage, explore:

1. **Multi-Material**: Apply different materials to different cells
2. **Variable Thickness**: Use thicker walls for structural areas
3. **Hybrid Designs**: Combine Voronoi with solid sections
4. **Colored Cells**: Paint individual cells for artistic effects
5. **Functional Integration**: Embed hardware in Voronoi structure

See `VORONOI_GIZMO_IMPLEMENTATION.md` for future enhancement ideas.
