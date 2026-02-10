#include <WorldMaps/World/World.hpp>
#include <Vault.hpp>
#include <plog/Log.h>
#include <tracy/Tracy.hpp>

void World::loadDeltasFromVault()
{
    ZoneScopedN("World::loadDeltasFromVault");
    if (!m_vault) return;

    auto records = m_vault->listAllLayerDeltas();
    if (records.empty()) return;

    PLOGI << "Loading " << records.size() << " layer deltas from vault";

    for (const auto& rec : records) {
        ChunkCoord coord;
        coord.x = rec.chunkX;
        coord.y = rec.chunkY;
        coord.depth = rec.chunkDepth;

        ChunkData* cd = quadTree_.getOrCreate(coord);
        if (!cd) {
            PLOGW << "Failed to create chunk node for delta at ("
                  << rec.chunkX << "," << rec.chunkY << "," << rec.chunkDepth << ")";
            continue;
        }

        LayerDelta& delta = cd->layerDeltas[rec.layerName];
        delta.mode = static_cast<DeltaMode>(rec.deltaMode);
        delta.channelCount = rec.channelCount;
        delta.resolution = rec.resolution;

        if (!rec.deltaData.empty()) {
            delta.deserializeData(rec.deltaData.data(), rec.deltaData.size());
        } else {
            delta.initGrid(rec.resolution, rec.channelCount);
        }

        if (!rec.paramOverrides.empty()) {
            delta.deserializeParams(rec.paramOverrides);
        }

        // Mark the chunk as dirty so the next render regenerates with the delta
        cd->markDirty(rec.layerName);
    }

    PLOGI << "Loaded " << records.size() << " layer deltas into quadtree";
}

void World::saveDeltasToVault()
{
    ZoneScopedN("World::saveDeltasToVault");
    if (!m_vault) return;

    int saved = 0;

    quadTree_.forEachNode([&](const ChunkData& data) {
        for (const auto& [layerName, delta] : data.layerDeltas) {
            if (!delta.hasEdits()) continue;

            auto blob = delta.serializeData();
            auto params = delta.serializeParams();

            bool ok = m_vault->saveLayerDelta(
                data.coord.x, data.coord.y, data.coord.depth,
                layerName,
                delta.channelCount, delta.resolution,
                static_cast<int>(delta.mode),
                blob, params);

            if (ok) {
                ++saved;
            } else {
                PLOGW << "Failed to save delta for chunk ("
                      << data.coord.x << "," << data.coord.y
                      << "," << data.coord.depth << ") layer=" << layerName;
            }
        }
    });

    PLOGI << "Saved " << saved << " layer deltas to vault";
}
