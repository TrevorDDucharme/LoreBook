#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class WaterLayer : public MapLayer
{
public:
 WaterLayer() = default;
    ~WaterLayer() override;
    SampleData sample() override;

    cl_mem getColor() override;

private:
    cl_mem getWaterBuffer();

    cl_mem waterBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
};
