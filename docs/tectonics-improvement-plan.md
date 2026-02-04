# Tectonics Layer Improvement Plan

## Current State (Working Baseline)
- Voronoi plates on sphere (configurable plate count)
- Each plate has constant 2D velocity
- Pressure accumulates from velocity divergence
- Height = base plate height + pressure contribution
- ~500 simulation steps

## Phase 1: Better Boundary Detection
**Goal:** Clear, visible plate boundaries

- [ ] Detect boundary cells (where neighbor plateId differs)
- [ ] Store boundary flag in voxel struct
- [ ] Classify boundary type:
  - **Convergent:** plates moving toward each other → mountains
  - **Divergent:** plates moving apart → rifts/valleys
  - **Transform:** plates sliding past each other → fault lines
- [ ] Use dot product of relative velocity with boundary normal to classify

## Phase 2: Improved Pressure Physics
**Goal:** Realistic mountain range shapes

- [ ] Pressure propagates inward from boundaries (not just local divergence)
- [ ] Pressure decay with distance from boundary
- [ ] Higher pressure accumulation rate at convergent boundaries
- [ ] Negative pressure (extension) at divergent boundaries
- [ ] Anisotropic pressure spread (perpendicular to boundary)

## Phase 3: Continental vs Oceanic Plates
**Goal:** Realistic land/ocean distribution

- [ ] Assign plate types: continental (thick, buoyant) vs oceanic (thin, dense)
- [ ] Continental-continental collision → highest mountains (Himalayas)
- [ ] Oceanic-continental collision → subduction, coastal mountains (Andes)
- [ ] Oceanic-oceanic collision → island arcs (Japan)
- [ ] Divergent oceanic → mid-ocean ridges
- [ ] Store plate density/thickness as plate property

## Phase 4: Time Evolution
**Goal:** Dynamic plate motion over geological time

- [ ] Plates can rotate (angular velocity, not just linear)
- [ ] Plate velocity changes over time (random drift)
- [ ] Plate merging when small plate gets consumed
- [ ] Plate splitting at high-stress divergent zones
- [ ] Track cumulative deformation history

## Phase 5: Terrain Features
**Goal:** Recognizable geological features

- [ ] Mountain chains aligned with convergent boundaries
- [ ] Rift valleys at divergent boundaries
- [ ] Volcanic hotspots (random positions, independent of plates)
- [ ] Erosion smoothing over time
- [ ] Sediment accumulation in low areas
- [ ] Continental shelves (shallow water around continents)

## Phase 6: Advanced Physics (Optional)
**Goal:** Research-quality simulation

- [ ] Mantle convection cells driving plate motion
- [ ] Isostatic adjustment (crust floats on mantle)
- [ ] Thermal evolution (cooling crust, heat flow)
- [ ] Lithospheric flexure near loads
- [ ] Slab pull / ridge push forces

---

## Implementation Notes

### Struct Evolution
Current:
```c
typedef struct {
    float velX, velY;
    float pressure;
    int plateId;
} TecVoxel;
```

Phase 1-2 expansion:
```c
typedef struct {
    float velX, velY;
    float pressure;
    int plateId;
    int isBoundary;      // 0 = interior, 1 = boundary
    int boundaryType;    // 0 = none, 1 = convergent, 2 = divergent, 3 = transform
} TecVoxel;
```

Phase 3+ expansion:
```c
typedef struct {
    float velX, velY;
    float pressure;
    float thickness;     // crust thickness
    int plateId;
    int isBoundary;
    int boundaryType;
    int plateType;       // 0 = oceanic, 1 = continental
} TecVoxel;
```

### Kernel Additions
- `tec_detect_boundaries`: mark boundary cells and classify type
- `tec_propagate_pressure`: diffuse pressure from boundaries inward
- `tec_erode`: smooth terrain over time
- `tec_update_velocities`: evolve plate motion

### Testing Approach
After each phase:
1. Verify visually in app
2. Check for plate boundaries visible as height changes
3. Confirm no noise/artifacts at cubemap edges
4. Profile performance (stay under 1 second for full simulation)

---

## Priority Order
1. **Phase 1** - Most bang for buck, makes plates actually visible
2. **Phase 3** - Land/ocean distinction is critical for world-building
3. **Phase 2** - Better mountains
4. **Phase 5** - User-visible features
5. **Phase 4** - Nice to have
6. **Phase 6** - Only if needed for realism
