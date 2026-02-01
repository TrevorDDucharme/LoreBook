#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class WaterTableLayer : public MapLayer
{
public:
 WaterTableLayer() = default;
    ~WaterTableLayer() override;
    cl_mem sample() override;

    cl_mem getColor() override;

private:
    float water_table_level = 0.5f;

    cl_mem getWaterTableBuffer();
    cl_mem getWaterTableColorBuffer();
    static void waterTableWeightedScalarToColor(cl_mem& output, 
                            cl_mem scalarBuffer,
                            int latitudeResolution,
                            int longitudeResolution,
                            int colorCount,
                            const std::vector<std::array<uint8_t, 4>> &paletteColors,
                            const std::vector<float> &weights);

    cl_mem watertableBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
};
