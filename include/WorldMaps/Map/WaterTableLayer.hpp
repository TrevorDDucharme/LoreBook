#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

struct LayerDelta;

class WaterTableLayer : public MapLayer
{
public:
 WaterTableLayer() = default;
    ~WaterTableLayer() override;
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

    float water_table_level = 0.5f;

private:
    cl_mem getWaterTableBuffer();
    cl_mem getWaterTableColorBuffer();
    static void waterTableWeightedScalarToColor(cl_mem& output, 
                            cl_mem scalarBuffer,
                            int latitudeResolution,
                            int longitudeResolution,
                            int colorCount,
                            const std::vector<cl_float4> &paletteColors,
                            const std::vector<float> &weights);

    cl_mem watertableBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
};
