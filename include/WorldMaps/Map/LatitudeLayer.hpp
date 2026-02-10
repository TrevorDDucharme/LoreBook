#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

struct LayerDelta;

class LatitudeLayer : public MapLayer
{
public:
    LatitudeLayer() = default;
    ~LatitudeLayer() override;
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
    cl_mem getLatitudeBuffer();

    cl_mem latitudeBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;

    static void latitude(
        cl_mem &output,
        int latitudeResolution,
        int longitudeResolution
    );
};