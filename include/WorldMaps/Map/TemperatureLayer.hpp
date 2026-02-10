#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

struct LayerDelta;

class TemperatureLayer : public MapLayer
{
public:
    TemperatureLayer();
    ~TemperatureLayer() override;
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

    // Procedural parameters
    float frequency_   = 1.5f;
    float lacunarity_  = 2.0f;
    int   octaves_     = 4;
    float persistence_ = 0.5f;
    unsigned int seed_ = 231354u;

private:
    cl_mem getTemperatureBuffer();

    cl_mem temperatureBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;

    static void tempMap(cl_mem &output,
             int latitudeResolution,
             int longitudeResolution,
             float frequency,
             float lacunarity,
             int octaves,
             float persistence,
             unsigned int seed);
};