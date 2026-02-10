#pragma once
#include <WorldMaps/World/Chunk.hpp>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <cmath>
#include <mutex>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/// Flat-map quadtree over the world surface.
///
/// Nodes are stored in a hash map keyed by ChunkCoord.  A node is considered
/// a leaf when none of its four children exist in the map.  The tree is
/// lazily expanded by ensureDepth() which creates nodes down to the requested
/// depth for a given viewport bounding box.
class QuadTree {
public:
    QuadTree() {
        // Create root node (depth 0, covers the entire world)
        ChunkCoord root{0, 0, 0};
        ChunkData rootData;
        rootData.coord = root;
        rootData.isLeaf = true;
        nodes_[root] = std::move(rootData);
    }

    // ── Depth / LOD helpers ──────────────────────────────────────────

    /// Compute the minimum quadtree depth needed so that visible chunks
    /// have at least `screenPixels` texels of coverage when assembled.
    /// screenPixels = max(viewportW, viewportH).
    /// zoomLevel: Mercator zoom (1 = full world visible).
    static int computeDepthForZoom(float zoomLevel, int screenPixels,
                                   int chunkRes = CHUNK_BASE_RES) {
        // At depth d the world has 2^d chunks per axis.
        // Visible span at zoom Z covers 1/Z of the world.
        // Visible chunks = 2^d / Z.  Each chunk = chunkRes texels.
        // We want 2^d / Z * chunkRes >= screenPixels.
        // → 2^d >= screenPixels * Z / chunkRes
        // → d >= log2(screenPixels * Z / chunkRes)
        if (zoomLevel < 1.0f) zoomLevel = 1.0f;
        float needed = static_cast<float>(screenPixels) * zoomLevel /
                       static_cast<float>(chunkRes);
        int d = static_cast<int>(std::ceil(std::log2(std::max(needed, 1.0f))));
        return std::clamp(d, 0, CHUNK_MAX_DEPTH);
    }

    /// Overload for the globe view where zoomLevel is sphere distance
    /// from the surface (smaller = closer, negative = inside sphere).
    /// Convert to an effective Mercator-like zoom based on the actual
    /// visible angular extent on the sphere surface.
    static int computeDepthForGlobeZoom(float sphereZoom, float fovDeg,
                                        int screenPixels,
                                        int chunkRes = CHUNK_BASE_RES) {
        // Camera distance from sphere center (radius = 1).
        // sphereZoom is surface distance, so center distance = 1 + sphereZoom.
        float dist = 1.0f + std::max(sphereZoom, -0.99f);
        dist = std::max(dist, 0.01f); // safety floor

        // Horizon half-angle: how much of the sphere the camera can see.
        // At dist=4 (far): acos(0.25) ≈ 75° — nearly a hemisphere.
        // At dist=1.1 (close): acos(0.91) ≈ 24° — small patch.
        // At dist=1.01 (very close): acos(0.99) ≈ 8° — tiny patch.
        float horizonAngle = std::acos(std::min(1.0f, 1.0f / dist));

        // FOV half-angle in radians.
        float fovHalfRad = std::max(fovDeg, 10.0f) * 0.5f
                           * static_cast<float>(M_PI) / 180.0f;

        // The viewport shows whichever is smaller: the horizon cap or the FOV.
        // When far, FOV limits the view; when close, the horizon shrinks below FOV.
        float visibleHalf = std::min(horizonAngle, fovHalfRad);
        visibleHalf = std::max(visibleHalf, 0.001f); // avoid division by zero

        // Effective Mercator zoom: full world (π radians half) ÷ visible half.
        float effectiveZoom = static_cast<float>(M_PI) / visibleHalf;

        return computeDepthForZoom(effectiveZoom, screenPixels, chunkRes);
    }

    // ── Tree operations ──────────────────────────────────────────────

    /// Ensure that all chunks overlapping the given world-space bounding box
    /// (radians) exist down to the requested depth.  Ancestor nodes along the
    /// path are created as needed and marked non-leaf.
    void ensureDepth(float lonMin, float lonMax,
                     float latMin, float latMax, int depth) {
        std::lock_guard<std::mutex> lk(mutex_);
        depth = std::clamp(depth, 0, CHUNK_MAX_DEPTH);

        int cells = 1 << depth;
        float lonRange = static_cast<float>(2.0 * M_PI);
        float latRange = static_cast<float>(M_PI);
        float cellW = lonRange / cells;
        float cellH = latRange / cells;

        // Determine index range that overlaps the bounding box
        int xMin = static_cast<int>(std::floor((lonMin + static_cast<float>(M_PI)) / cellW));
        int xMax = static_cast<int>(std::floor((lonMax + static_cast<float>(M_PI)) / cellW));
        int yMin = static_cast<int>(std::floor((latMin + static_cast<float>(M_PI / 2.0)) / cellH));
        int yMax = static_cast<int>(std::floor((latMax + static_cast<float>(M_PI / 2.0)) / cellH));

        xMin = std::clamp(xMin, 0, cells - 1);
        xMax = std::clamp(xMax, 0, cells - 1);
        yMin = std::clamp(yMin, 0, cells - 1);
        yMax = std::clamp(yMax, 0, cells - 1);

        for (int cy = yMin; cy <= yMax; ++cy) {
            for (int cx = xMin; cx <= xMax; ++cx) {
                ensureNode({cx, cy, depth});
            }
        }
    }

    /// Return coordinates of all leaf nodes that overlap the given bounds
    /// (radians).  If a maxDepth is provided, only returns leaves at that
    /// depth (or the deepest available ancestor).
    std::vector<ChunkCoord> getLeavesInBounds(float lonMin, float lonMax,
                                              float latMin, float latMax,
                                              int maxDepth = -1) {
        std::lock_guard<std::mutex> lk(mutex_);
        std::vector<ChunkCoord> result;
        collectLeaves({0, 0, 0}, lonMin, lonMax, latMin, latMax,
                      maxDepth, result);
        return result;
    }

    /// Get or create the ChunkData for a coordinate.  Returns nullptr only
    /// if depth is out of range.
    ChunkData* getOrCreate(const ChunkCoord& coord) {
        std::lock_guard<std::mutex> lk(mutex_);
        return ensureNode(coord);
    }

    /// Get ChunkData for an existing coordinate (returns nullptr if absent).
    ChunkData* get(const ChunkCoord& coord) {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = nodes_.find(coord);
        return (it != nodes_.end()) ? &it->second : nullptr;
    }

    /// Evict GPU buffers of least-recently-used chunks to stay within a
    /// maximum cache count.  Deltas are NOT evicted — only cl_mem caches.
    void evictLRU(size_t maxCachedNodes) {
        std::lock_guard<std::mutex> lk(mutex_);
        // Collect all nodes that have cached GPU data
        struct Entry {
            ChunkCoord coord;
            std::chrono::steady_clock::time_point oldest;
        };
        std::vector<Entry> cached;
        for (auto& [coord, data] : nodes_) {
            for (auto& [layer, cache] : data.layerCaches) {
                if (cache.sampleBuffer || cache.colorBuffer) {
                    auto tp = cache.lastAccess;
                    cached.push_back({coord, tp});
                    break; // one entry per node
                }
            }
        }
        if (cached.size() <= maxCachedNodes) return;

        // Sort by oldest access first
        std::sort(cached.begin(), cached.end(),
                  [](const Entry& a, const Entry& b) {
                      return a.oldest < b.oldest;
                  });

        size_t toEvict = cached.size() - maxCachedNodes;
        for (size_t i = 0; i < toEvict; ++i) {
            auto it = nodes_.find(cached[i].coord);
            if (it == nodes_.end()) continue;
            for (auto& [layer, cache] : it->second.layerCaches) {
                // Release buffers through OpenCLContext tracked allocator
                if (cache.sampleBuffer) {
                    clReleaseMemObject(cache.sampleBuffer);
                    cache.sampleBuffer = nullptr;
                }
                if (cache.colorBuffer) {
                    clReleaseMemObject(cache.colorBuffer);
                    cache.colorBuffer = nullptr;
                }
                cache.dirty = true;
            }
        }
    }

    /// Total number of nodes currently in the tree.
    size_t nodeCount() const {
        std::lock_guard<std::mutex> lk(mutex_);
        return nodes_.size();
    }

    /// Clear all GPU caches (used before shutdown).
    void releaseAllBuffers() {
        std::lock_guard<std::mutex> lk(mutex_);
        for (auto& [coord, data] : nodes_) {
            for (auto& [layer, cache] : data.layerCaches) {
                if (cache.sampleBuffer) {
                    clReleaseMemObject(cache.sampleBuffer);
                    cache.sampleBuffer = nullptr;
                }
                if (cache.colorBuffer) {
                    clReleaseMemObject(cache.colorBuffer);
                    cache.colorBuffer = nullptr;
                }
                cache.dirty = true;
            }
        }
    }

    /// Iterate all nodes in the tree, calling fn(ChunkData&) for each.
    /// The callback must not modify the tree structure.
    template<typename Fn>
    void forEachNode(Fn&& fn) {
        std::lock_guard<std::mutex> lk(mutex_);
        for (auto& [coord, data] : nodes_) {
            fn(data);
        }
    }

    /// Const version: iterate all nodes read-only.
    template<typename Fn>
    void forEachNode(Fn&& fn) const {
        std::lock_guard<std::mutex> lk(mutex_);
        for (const auto& [coord, data] : nodes_) {
            fn(data);
        }
    }

private:
    mutable std::mutex mutex_;
    std::unordered_map<ChunkCoord, ChunkData, ChunkCoordHash> nodes_;

    /// Ensure node and all its ancestors exist.  Returns pointer to the node.
    ChunkData* ensureNode(const ChunkCoord& coord) {
        if (coord.depth < 0 || coord.depth > CHUNK_MAX_DEPTH) return nullptr;

        // Ensure ancestors first (non-recursive, bottom-up collection)
        std::vector<ChunkCoord> toCreate;
        ChunkCoord c = coord;
        while (c.depth > 0) {
            if (nodes_.find(c) != nodes_.end()) break;
            toCreate.push_back(c);
            c = c.parent();
        }
        // Create from top (ancestor) to bottom
        for (int i = static_cast<int>(toCreate.size()) - 1; i >= 0; --i) {
            auto& cc = toCreate[i];
            // Mark parent as non-leaf
            ChunkCoord par = cc.parent();
            auto pit = nodes_.find(par);
            if (pit != nodes_.end()) pit->second.isLeaf = false;

            ChunkData newData;
            newData.coord = cc;
            newData.isLeaf = true;
            nodes_[cc] = std::move(newData);
        }

        // Ensure the target node exists (may already exist)
        auto it = nodes_.find(coord);
        if (it == nodes_.end()) {
            ChunkCoord par = coord.parent();
            auto pit = nodes_.find(par);
            if (pit != nodes_.end()) pit->second.isLeaf = false;

            ChunkData newData;
            newData.coord = coord;
            newData.isLeaf = true;
            nodes_[coord] = std::move(newData);
            it = nodes_.find(coord);
        }
        return &it->second;
    }

    /// Recursive collection of leaves that overlap the viewport.
    void collectLeaves(const ChunkCoord& coord,
                       float lonMin, float lonMax,
                       float latMin, float latMax,
                       int maxDepth,
                       std::vector<ChunkCoord>& out) {
        auto it = nodes_.find(coord);
        if (it == nodes_.end()) return;

        // Check if this node overlaps the viewport
        float cLonMin, cLonMax, cLatMin, cLatMax;
        coord.getBoundsRadians(cLonMin, cLonMax, cLatMin, cLatMax);
        if (cLonMax <= lonMin || cLonMin >= lonMax ||
            cLatMax <= latMin || cLatMin >= latMax)
            return; // no overlap

        bool atMaxDepth = (maxDepth >= 0 && coord.depth >= maxDepth);

        if (it->second.isLeaf || atMaxDepth) {
            out.push_back(coord);
            return;
        }

        // Recurse into children
        auto kids = coord.children();
        for (auto& kid : kids) {
            collectLeaves(kid, lonMin, lonMax, latMin, latMax, maxDepth, out);
        }
    }
};
