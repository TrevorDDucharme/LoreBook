#pragma once
#include <unordered_map>
#include <memory>
#include <WorldMaps/Map/MapLayer.hpp>
#include <WorldMaps/Map/ElevationLayer.hpp>
#include <WorldMaps/Map/HumidityLayer.hpp>
#include <WorldMaps/Map/TemperatureLayer.hpp>
#include <WorldMaps/Map/ColorLayer.hpp>
#include <WorldMaps/Map/WaterTableLayer.hpp>
#include <WorldMaps/Map/LandTypeLayer.hpp>
#include <WorldMaps/Map/RiverLayer.hpp>
#include <WorldMaps/Map/LatitudeLayer.hpp>
#include <WorldMaps/Map/TectonicsLayer.hpp>
#include <WorldMaps/Map/BuildingLayer.hpp>
#include <WorldMaps/World/QuadTree.hpp>
#include <WorldMaps/World/ChunkAssembler.hpp>
#include <WorldMaps/World/LayerDelta.hpp>
#include <memory>
#include <stack>
#include <stringUtils.hpp>

class Vault;

class World
{
public:
    World()=default;
    World(std::string config)
    {
        parseConfig(config);
    }
    
    virtual ~World() = default;

    // ── Full-world API (existing, backward-compatible) ───────────
    cl_mem sample(const std::string &layerName = "") const
    {
        auto it = layers.find(layerName);
        if (it != layers.end())
        {
            return it->second->sample();
        }
        if (!layers.empty())
        {
            return layers.begin()->second->sample();
        }
        else
        {
            return cl_mem{}; // empty sample data
        }
    }
    cl_mem getColor(const std::string &layerName = "") const
    {
        ZoneScopedN("World::getColor");
        auto it = layers.find(layerName);
        if (it != layers.end())
        {
            return it->second->getColor();
        }
        if (!layers.empty())
        {
            return layers.begin()->second->getColor();
        }
        else
        {
            return nullptr; // empty color
        }
    }

    // ── Region-based API (dynamic resolution / chunked) ──────────

    /// Get RGBA color data covering a specific world region.
    /// This is the primary API used by chunk-aware projections.
    ///
    /// The returned cl_mem covers [lonMinDeg..lonMaxDeg] × [latMinDeg..latMaxDeg]
    /// at (outW × outH) resolution.  The buffer is assembled from quadtree chunks.
    ///
    /// @param layerName  Layer to render.
    /// @param lonMinDeg  West edge (degrees).
    /// @param lonMaxDeg  East edge (degrees).
    /// @param latMinDeg  South edge (degrees).
    /// @param latMaxDeg  North edge (degrees).
    /// @param depth      Quadtree depth (determines chunk granularity).
    /// @param outW       Desired output buffer width in texels.
    /// @param outH       Desired output buffer height in texels.
    /// @return A cl_mem RGBA float4 buffer (owned by this World, do not free).
    cl_mem getColorForRegion(const std::string& layerName,
                             float lonMinDeg, float lonMaxDeg,
                             float latMinDeg, float latMaxDeg,
                             int depth, int outW, int outH)
    {
        ZoneScopedN("World::getColorForRegion");

        // Resolve the layer
        MapLayer* layer = getLayer(layerName);
        if (!layer) layer = getLayer(""); // fallback
        if (!layer && !layers.empty()) layer = layers.begin()->second.get();
        if (!layer) return nullptr;

        // If the layer doesn't support region generation, fall back to full-world
        if (!layer->supportsRegion()) {
            return layer->getColor();
        }

        const float DEG2RAD = static_cast<float>(M_PI / 180.0);

        // Ensure quadtree nodes exist at the needed depth
        float lonMinRad = lonMinDeg * DEG2RAD;
        float lonMaxRad = lonMaxDeg * DEG2RAD;
        float latMinRad = latMinDeg * DEG2RAD;
        float latMaxRad = latMaxDeg * DEG2RAD;

        quadTree_.ensureDepth(lonMinRad, lonMaxRad, latMinRad, latMaxRad, depth);

        // Get all leaf chunks in the visible region
        auto leaves = quadTree_.getLeavesInBounds(
            lonMinRad, lonMaxRad, latMinRad, latMaxRad, depth);

        // Generate color data for each chunk that needs it
        std::vector<ChunkAssembler::ChunkEntry> entries;
        entries.reserve(leaves.size());

        for (auto& coord : leaves) {
            ChunkData* cd = quadTree_.getOrCreate(coord);
            if (!cd) continue;

            auto& cache = cd->layerCaches[layerName];
            cache.touch();

            // Generate if dirty or not yet generated
            if (cache.dirty || !cache.colorBuffer) {
                float cLonMin, cLonMax, cLatMin, cLatMax;
                coord.getBoundsRadians(cLonMin, cLonMax, cLatMin, cLatMax);

                // Get delta for this chunk+layer if it exists
                const LayerDelta* delta = nullptr;
                auto dit = cd->layerDeltas.find(layerName);
                if (dit != cd->layerDeltas.end() && dit->second.hasEdits()) {
                    delta = &dit->second;
                }

                cache.colorBuffer = layer->getColorRegion(
                    cLonMin, cLonMax, cLatMin, cLatMax,
                    CHUNK_BASE_RES, CHUNK_BASE_RES, delta);
                cache.generatedResX = CHUNK_BASE_RES;
                cache.generatedResY = CHUNK_BASE_RES;
                cache.dirty = false;
            }

            entries.push_back({
                coord,
                cache.colorBuffer,
                cache.generatedResX,
                cache.generatedResY
            });
        }

        // Assemble chunks into viewport buffer
        ChunkAssembler::assembleRGBA(
            entries, regionAssemblyBuffer_,
            outW, outH,
            lonMinDeg, lonMaxDeg, latMinDeg, latMaxDeg);

        return regionAssemblyBuffer_;
    }

    /// Get scalar sample data covering a specific world region.
    cl_mem getSampleForRegion(const std::string& layerName,
                              float lonMinDeg, float lonMaxDeg,
                              float latMinDeg, float latMaxDeg,
                              int depth, int outW, int outH)
    {
        ZoneScopedN("World::getSampleForRegion");
        MapLayer* layer = getLayer(layerName);
        if (!layer && !layers.empty()) layer = layers.begin()->second.get();
        if (!layer) return nullptr;

        if (!layer->supportsRegion()) return layer->sample();

        const float DEG2RAD = static_cast<float>(M_PI / 180.0);
        float lonMinRad = lonMinDeg * DEG2RAD;
        float lonMaxRad = lonMaxDeg * DEG2RAD;
        float latMinRad = latMinDeg * DEG2RAD;
        float latMaxRad = latMaxDeg * DEG2RAD;

        quadTree_.ensureDepth(lonMinRad, lonMaxRad, latMinRad, latMaxRad, depth);
        auto leaves = quadTree_.getLeavesInBounds(
            lonMinRad, lonMaxRad, latMinRad, latMaxRad, depth);

        std::vector<ChunkAssembler::ChunkEntry> entries;
        entries.reserve(leaves.size());

        for (auto& coord : leaves) {
            ChunkData* cd = quadTree_.getOrCreate(coord);
            if (!cd) continue;

            auto& cache = cd->layerCaches[layerName];
            cache.touch();

            if (cache.dirty || !cache.sampleBuffer) {
                float cLonMin, cLonMax, cLatMin, cLatMax;
                coord.getBoundsRadians(cLonMin, cLonMax, cLatMin, cLatMax);

                const LayerDelta* delta = nullptr;
                auto dit = cd->layerDeltas.find(layerName);
                if (dit != cd->layerDeltas.end() && dit->second.hasEdits())
                    delta = &dit->second;

                cache.sampleBuffer = layer->sampleRegion(
                    cLonMin, cLonMax, cLatMin, cLatMax,
                    CHUNK_BASE_RES, CHUNK_BASE_RES, delta);
                cache.generatedResX = CHUNK_BASE_RES;
                cache.generatedResY = CHUNK_BASE_RES;
                cache.dirty = false;
            }

            entries.push_back({
                coord, cache.sampleBuffer,
                cache.generatedResX, cache.generatedResY
            });
        }

        ChunkAssembler::assembleScalar(
            entries, sampleAssemblyBuffer_,
            outW, outH,
            lonMinDeg, lonMaxDeg, latMinDeg, latMaxDeg);

        return sampleAssemblyBuffer_;
    }

    // ── Layer management ─────────────────────────────────────────

    void addLayer(const std::string &name, std::unique_ptr<MapLayer> layer)
    {
        layer->setParentWorld(this);
        layers[name] = std::move(layer);
    }

    MapLayer *getLayer(const std::string &name)
    {
        auto it = layers.find(name);
        return (it != layers.end()) ? it->second.get() : nullptr;
    }
    const MapLayer *getLayer(const std::string &name) const
    {
        auto it = layers.find(name);
        return (it != layers.end()) ? it->second.get() : nullptr;
    }

    std::vector<std::string> getLayerNames()
    {
        std::vector<std::string> names;
        for (const auto &pair : layers)
        {
            names.push_back(pair.first);
        }
        return names;
    }

    int getWorldLatitudeResolution() const { return worldLatitudeResolution; }
    int getWorldLongitudeResolution() const { return worldLongitudeResolution; }

    // ── Vault access ─────────────────────────────────────────────
    void setVault(Vault* v) { m_vault = v; }
    Vault* getVault() const { return m_vault; }

    // ── Quadtree access ──────────────────────────────────────────
    QuadTree& getQuadTree() { return quadTree_; }
    const QuadTree& getQuadTree() const { return quadTree_; }

    /// Mark a chunk dirty for a specific layer (triggers re-generation).
    void markChunkDirty(const ChunkCoord& coord, const std::string& layerName) {
        ChunkData* cd = quadTree_.get(coord);
        if (cd) cd->markDirty(layerName);
    }

    /// Get the delta for a chunk+layer (creates if absent).
    LayerDelta& getOrCreateDelta(const ChunkCoord& coord, const std::string& layerName) {
        ChunkData* cd = quadTree_.getOrCreate(coord);
        auto& delta = cd->layerDeltas[layerName];
        if (delta.data.empty()) delta.initGrid(CHUNK_BASE_RES, 1);
        return delta;
    }

    /// Get delta (const, may return nullptr if not present).
    const LayerDelta* getDelta(const ChunkCoord& coord, const std::string& layerName) const {
        // Note: getOrCreate is non-const so we search directly
        // (read-only version would need the tree's internal map access)
        return nullptr; // simplified for now; editing code uses getOrCreateDelta
    }

    /// Evict least-recently-used chunk GPU caches.
    void evictChunkCaches(size_t maxCached = 512) {
        quadTree_.evictLRU(maxCached);
    }

    /// Load all persisted layer deltas from the vault DB into the quadtree.
    /// Call this after parseConfig() when a vault is available.
    void loadDeltasFromVault();

    /// Save all dirty/edited layer deltas to the vault DB.
    /// Call this when the user requests saving or when closing.
    void saveDeltasToVault();

    // ── Config parsing ───────────────────────────────────────────

    //Biome(count:2,colors:[{0,0,255},{0,255,0}]),Water(Level:1.3),Humidity,Temperature
    void parseConfig(const std::string &config)
    {
        std::vector<std::string> layerConfigs = splitBracketAware(config, ",");
        for (const auto &layerConfig : layerConfigs)
        {
            std::string layerName;
            std::string layerParams;
            splitNameConfig(layerConfig, layerName, layerParams);
            std::transform(layerName.begin(), layerName.end(), layerName.begin(), ::tolower);

            if (layerName == "elevation")
            {
                addLayer(layerName, std::make_unique<ElevationLayer>());
            }
            else if (layerName == "humidity")
            {
                addLayer(layerName, std::make_unique<HumidityLayer>());
            }
            else if (layerName == "temperature")
            {
                addLayer(layerName, std::make_unique<TemperatureLayer>());
            }
            else if (layerName == "color")
            {
                addLayer(layerName, std::make_unique<ColorLayer>());
            }
            else if (layerName == "watertable")
            {
                auto layer = std::make_unique<WaterTableLayer>();
                layer->parseParameters(layerParams);
                addLayer(layerName, std::move(layer));
            }
            else if (layerName == "landtype")
            {
                auto layer = std::make_unique<LandTypeLayer>();
                layer->parseParameters(layerParams);
                addLayer(layerName, std::move(layer));
            }
            else if (layerName == "river")
            {
                auto layer = std::make_unique<RiverLayer>();
                layer->parseParameters(layerParams);
                addLayer(layerName, std::move(layer));
            }
            else if (layerName == "tectonics")
            {
                auto layer = std::make_unique<TectonicsLayer>();
                layer->parseParameters(layerParams);
                addLayer(layerName, std::move(layer));
            }
            else if (layerName == "buildings")
            {
                auto layer = std::make_unique<BuildingLayer>();
                layer->parseParameters(layerParams);
                addLayer(layerName, std::move(layer));
            }
        }     
    }

private:
    int worldLatitudeResolution=4096;
    int worldLongitudeResolution=4096;
    Vault* m_vault = nullptr;

    std::unordered_map<std::string, std::unique_ptr<MapLayer>> layers;

    // Quadtree for adaptive chunking
    QuadTree quadTree_;

    // Viewport assembly buffers (reused across frames)
    cl_mem regionAssemblyBuffer_ = nullptr;
    cl_mem sampleAssemblyBuffer_ = nullptr;
};