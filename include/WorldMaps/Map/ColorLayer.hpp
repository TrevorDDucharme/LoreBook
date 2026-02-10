#pragma once
#include <WorldMaps/Map/MapLayer.hpp>
#include <vector>

struct LayerDelta;

// Composite color layer that blends elevation, humidity, temperature and water
class ColorLayer : public MapLayer {
public:
 ColorLayer() = default;
    ~ColorLayer() override;
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

private:
    cl_mem getColorBuffer();

    cl_mem colorBuffer = nullptr;
    cl_mem tempColorBuffer = nullptr;
};