#pragma once
#include <WorldMaps/Map/MapLayer.hpp>
class ElevationLayer : public MapLayer {
public:
    ElevationLayer()= default;
    ~ElevationLayer() override;
    cl_mem sample() override;
    cl_mem getColor() override;

    // ── Region support (dynamic resolution) ────────────
    bool supportsRegion() const override { return true; }

    cl_mem sampleRegion(float lonMinRad, float lonMaxRad,
                        float latMinRad, float latMaxRad,
                        int resX, int resY,
                        const LayerDelta* delta = nullptr) override;

    cl_mem getColorRegion(float lonMinRad, float lonMaxRad,
                          float latMinRad, float latMaxRad,
                          int resX, int resY,
                          const LayerDelta* delta = nullptr) override;

    // Procedural parameters (can be overridden per-chunk via delta)
    float frequency_   = 1.5f;
    float lacunarity_  = 2.0f;
    int   octaves_     = 8;
    float persistence_ = 0.5f;
    unsigned int seed_ = 12345u;

private:
    cl_mem getElevationBuffer();

    cl_mem elevationBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
};