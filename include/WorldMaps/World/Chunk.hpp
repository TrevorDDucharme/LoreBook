#pragma once
#include <cstdint>
#include <cmath>
#include <array>
#include <functional>
#include <unordered_map>
#include <string>
#include <chrono>
#include <CL/cl.h>
#include <WorldMaps/World/LayerDelta.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/// Base resolution of a single quadtree leaf chunk (samples per side).
static constexpr int CHUNK_BASE_RES = 32;

/// Maximum quadtree depth. depth 20 → 32 × 2^20 ≈ 33M samples/axis.
static constexpr int CHUNK_MAX_DEPTH = 20;

// ─────────────────────────────────────────────────────────────────────
// ChunkCoord – identifies a node in the world quadtree
// ─────────────────────────────────────────────────────────────────────

struct ChunkCoord {
    int x     = 0; // horizontal index at this depth
    int y     = 0; // vertical index at this depth
    int depth = 0; // quadtree depth (0 = root = entire world)

    bool operator==(const ChunkCoord& o) const {
        return x == o.x && y == o.y && depth == o.depth;
    }
    bool operator!=(const ChunkCoord& o) const { return !(*this == o); }

    /// Number of cells per axis at this depth
    int cellsPerAxis() const { return 1 << depth; }

    /// World-space bounds in radians.
    /// lon ∈ [-π, π], lat ∈ [-π/2, π/2]
    void getBoundsRadians(float& lonMin, float& lonMax,
                          float& latMin, float& latMax) const {
        int divisor = cellsPerAxis();
        float lonRange = static_cast<float>(2.0 * M_PI);
        float latRange = static_cast<float>(M_PI);
        float cellW = lonRange / divisor;
        float cellH = latRange / divisor;
        lonMin = static_cast<float>(-M_PI) + x * cellW;
        lonMax = lonMin + cellW;
        latMin = static_cast<float>(-M_PI / 2.0) + y * cellH;
        latMax = latMin + cellH;
    }

    /// World-space bounds in degrees.
    void getBoundsDegrees(float& lonMin, float& lonMax,
                          float& latMin, float& latMax) const {
        float r2d = static_cast<float>(180.0 / M_PI);
        float lo, hi, la, lb;
        getBoundsRadians(lo, hi, la, lb);
        lonMin = lo * r2d; lonMax = hi * r2d;
        latMin = la * r2d; latMax = lb * r2d;
    }

    /// Parent coordinate (depth − 1). Root returns itself.
    ChunkCoord parent() const {
        if (depth <= 0) return *this;
        return { x / 2, y / 2, depth - 1 };
    }

    /// Four children at depth + 1.
    std::array<ChunkCoord, 4> children() const {
        int cx = x * 2, cy = y * 2, cd = depth + 1;
        return {{
            { cx,     cy,     cd },
            { cx + 1, cy,     cd },
            { cx,     cy + 1, cd },
            { cx + 1, cy + 1, cd }
        }};
    }

    /// Check if this coord contains another (i.e. other is a descendant).
    bool contains(const ChunkCoord& other) const {
        if (other.depth < depth) return false;
        int shift = other.depth - depth;
        return (other.x >> shift) == x && (other.y >> shift) == y;
    }
};

/// Hash for use in std::unordered_map / unordered_set
struct ChunkCoordHash {
    size_t operator()(const ChunkCoord& c) const {
        size_t h = std::hash<int>{}(c.x);
        h ^= std::hash<int>{}(c.y)     + 0x9e3779b9 + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(c.depth) + 0x9e3779b9 + (h << 6) + (h >> 2);
        return h;
    }
};

// ─────────────────────────────────────────────────────────────────────
// ChunkLayerCache – per-layer GPU cache entry for one chunk
// ─────────────────────────────────────────────────────────────────────

struct ChunkLayerCache {
    cl_mem sampleBuffer = nullptr; // scalar data (float per texel)
    cl_mem colorBuffer  = nullptr; // RGBA data  (float4 per texel)
    int    generatedResX = 0;      // resolution this was last generated at
    int    generatedResY = 0;
    bool   dirty         = true;   // needs (re-)generation

    std::chrono::steady_clock::time_point lastAccess;

    void touch() { lastAccess = std::chrono::steady_clock::now(); }
};

// ─────────────────────────────────────────────────────────────────────
// ChunkData – everything stored at a single quadtree node
// ─────────────────────────────────────────────────────────────────────

struct ChunkData {
    ChunkCoord coord;
    bool isLeaf = true;

    /// Per-layer cached GPU buffers
    std::unordered_map<std::string, ChunkLayerCache> layerCaches;

    /// Per-layer edit deltas
    std::unordered_map<std::string, LayerDelta> layerDeltas;

    /// Mark a specific layer dirty (will trigger re-generation next access)
    void markDirty(const std::string& layerName) {
        auto it = layerCaches.find(layerName);
        if (it != layerCaches.end()) it->second.dirty = true;
    }

    /// Mark all layers dirty
    void markAllDirty() {
        for (auto& [name, cache] : layerCaches)
            cache.dirty = true;
    }
};