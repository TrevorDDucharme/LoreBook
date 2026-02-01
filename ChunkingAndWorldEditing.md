# Chunking and World Editing Plan

## Overview

This document captures the design and implementation plan for multichannel per-layer chunk deltas and interactive editing tools in LoreBook's world map system. The goal is to support per-sample edits that modify arbitrary numeric channels on a per-layer basis (for example, 20 biome blending weights), persist those edits in the Vault DB, and provide efficient rendering and interactive editing (brush, per-sample editor, Undo/Redo hooks) with good runtime performance.

---

## Goals

- Provide a concrete, implementable design to store and apply multichannel per-layer deltas at chunk granularity.
- Enable interactive editing: brush painting, per-sample editing, Add/Set semantics, and optional renormalize for weighted channels.
- Persist changes into the Vault DB (`LayerDeltas`) with a migration that keeps existing vaults compatible.
- Keep rendering responsive by applying chunk-level caching and partial texture rebuilds.
- Reuse and extend existing chunk/LOD types where possible (minimize rewrite).

---

## High-level Design Summary

- Use an existing chunk representation (WorldChunk / ChunkLOD) and make LOD0 explicitly editable.
- Each editable LOD contains per-layer floating-point grids (dense by default) where each cell holds a variable-length channel vector (vector<float>). The channel count is layer-specific (e.g., 20 for Biome).
- Sampling path (MapLayer sample API) will accept an optional chunk-delta object and must combine procedural base values with deltas using a configurable semantics (Add or Set). For weighted layers, an optional renormalize step is available.
- Persistence will be implemented in the Vault DB via a new table (`LayerDeltas`) storing compact binary blobs per chunk+layer and meta fields (channel count, resolution, lod). Deltas will be loaded lazily and written asynchronously.
- Projection implementations (Mercator/Spherical) will map each output pixel back to a chunk and sample index, fetch the chunk's delta (cached), and pass it to the layer sample function. Dirty flags permit partial texture rebuilds per-chunk.

---

## Chunk & Delta Data Model (Conceptual)

- WorldChunk
  - ChunkCoord (x,y,z / quadtree or fixed grid unit)
  - vector<ChunkLOD> lods (LOD0 is editable)

- ChunkLOD
  - resolution (grid size, e.g., default 32×32)
  - metadata (world-space bounds, channel counts per layer)
  - LayerDelta container: map<layerName, LayerDelta>

- LayerDelta
  - channelCount (int)
  - resolution (matches LOD resolution)
  - dense float array: length = resolution*resolution*channelCount (row-major) — chosen for fast sampling
  - sparse representation optional for storage optimization (future)

Notes:
- Default resolution: 32×32 for LOD0 (configurable per-layer if needed).
- Channel counts are per-layer; BiomeLayer will expose 20 channels by default.

---

## Sampling API & Layer Changes (Conceptual)

- `cl_mem` becomes multichannel-aware and can hold per-layer channel vectors.
- `MapLayer::sample(World&, lon, lat, const ChunkDelta* delta)` (conceptual) — the sampling path takes an optional chunk delta pointer for the current sample and must apply deltas according to the chosen semantics.
- Semantics supported:
  - Add: sample_value += delta_value
  - Set: sample_value = delta_value (overrides base)
  - Optional automatic renormalize for weight vectors (normalize so sum == 1.0)

Compatibility:
- Existing layers that return scalars (e.g., elevation) will map to channelCount = 1.
- Weighted layers (e.g., Biome) will handle channel vectors explicitly.

---

## Projection, Caching & Partial Rebuilds

- During texture projection (Mercator/Globe), compute the chunk key and sample index for each output texel.
- Fetch chunk deltas from an in-memory chunk cache (lazy load from DB if missing) and hand them to the layer sample function.
- Track per-chunk dirty flags; changes mark the chunk dirty and only those regions are re-projected into GPU textures.
- Use an LRU for chunk in-memory retention to cap memory usage; unload least-recently used chunks if necessary.
- For painting operations affecting multiple chunks, mark each affected chunk dirty and schedule background DB writes.

---

## Persistence in Vault DB (High-Level)

- New table: `LayerDeltas` (conceptual columns):
  - chunk_x, chunk_y, lod
  - layer_name (text)
  - channel_count (int)
  - resolution (int)
  - blob (binary) — dense float array in row-major order
  - updated_at (timestamp)

- Migration: add the `LayerDeltas` table in a new schema migration. Existing vaults remain usable (missing table = no edits).
- CRUD behavior:
  - Read: lazy-load chunk-layer blobs on first access, cache in memory.
  - Write: queue chunk-layer writes to a background thread / write queue; flush on explicit save and periodically.
  - Delete: clearing a chunk or layer removes the DB row and invalidates cache.

---

## UI & Editing Tools

- Per-layer parameter panel:
  - Shows layer metadata (channel count, channel names where available)
  - Expose `Clear Deltas` and `Reseed Layer` buttons
  - Editing mode selector (Browse, Paint, Sample Edit)

- Per-sample editor (click on map/globe):
  - Shows the vector<float> for the sample (channels) in a compact list (sliders/boxes), with ability to set values and apply renormalize.
  - Apply/Cancel, with Undo support (per-chunk change snapshot)

- Brush / Painting tool:
  - Controls: brush size, falloff, mode (Add / Set), selected channel(s), strength, renormalize toggle
  - Painting modifies the dense grid for the affected chunk cells (and partial cells at boundaries), marks chunks dirty and queues DB writes
  - Option to paint multiple channels at once or use automatic weight distribution

- Additional UX:
  - Visual overlay to show chunk bounds and brush footprint
  - Indicator for unsaved changes and progress for background saves
  - Keyboard/modifier keys to switch between Add vs Set quickly

---

## Concurrency & Performance

- Use `std::shared_mutex` for read-mostly access patterns to the in-memory chunk map; shared for sampling, exclusive for writes.
- Background worker for DB writes and LOD rebuilds (std::async or a thread pool).
- Limit concurrent background tasks to avoid thrashing.
- Use binary blobs for compact on-disk storage; compress in future if necessary.
- Avoid blocking UI thread: sampling & projection happen on the UI thread but should only use cached data. Heavy rebuilds or disk I/O must be async.

---

## Manual Verification & Run-First Checklist

- Build and run the GUI. Verify map and globe still render.
- Open a vault and confirm no data loss for vaults without `LayerDeltas`.
- Enter Paint mode, select Biome layer, paint on the globe; verify immediate visual changes, chunk dirty flags, and that changes persist after application and on re-opening the vault.
- Test small and large brush sizes across chunk boundaries (ensure neighboring chunks are updated correctly).
- Test Add vs Set behavior and the renormalize toggle on weighted layers.
- Verify `Reseed Layer` does not clobber saved deltas; `Clear Deltas` removes rows from DB and visually reverts.

---

## Migration & Backwards Compatibility

- Implement a DB migration that creates `LayerDeltas` and indexes on chunk coordinates + layer name.
- When the table is absent, runtime behaves as before (no edits). After migration, lazy-load will allow reconstructing chunk state from stored blobs.
- Provide a small export tool (optional) for deltas if external editing is desired.

---

## Prioritization & Roadmap

1. Extend `ChunkLOD` to contain generic per-layer dense deltas and confirm LOD0 is editable.
2. Implement the chunk cache, dirty flags, and partial rebuild hooks in projections.
3. Update `cl_mem` and `MapLayer` interfaces to support multichannel sampling and delta application.
4. Add DB schema & Vault helpers for `LayerDeltas` and background write queue.
5. Implement UI: Layer Parameters panel, per-sample editor, and painting tools.
6. Performance tuning: caching, LRU, background tasks, and test across zooms/resolutions.

---

## Open Questions / Decisions (Summary)

- Storage format chosen: dense float grid per chunk (fast sampling). We can add an optional sparse representation later for low-edit density scenarios.
- Default editable resolution: 32×32 for LOD0 (configurable per-layer later).
- Default edit semantics: support both Add and Set with an optional `renormalize` for weighted layers (Biomes). Recommend Add default for brush, Set available as a mode.
- Priority layers: Biome, Water, Elevation (implement in that order).

---

## Next steps

- Implement the ChunkLOD extension and LayerDelta storage in memory, wire simple sampling that reads deltas but does not persist yet.
- Add projection cache/dirty flags so per-chunk updates only repaint affected textures.
- Add Vault DB migration and simple persistence helpers; ensure background write queue is in place.
- Add UI controls and brush painting tools, iterate on UX with the app running.

---

This plan is intentionally pragmatic: it reuses existing chunk types where possible, keeps the sampling path explicit (chunk deltas passed into sample calls), persists deltas in the Vault DB for durability, and focuses on a smooth interactive experience with partial rebuilds and async persistence.
