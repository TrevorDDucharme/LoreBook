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

    cl_mem watertableBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
};
