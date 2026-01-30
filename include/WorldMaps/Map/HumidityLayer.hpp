#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class HumidityLayer : public MapLayer
{
public:
    HumidityLayer() = default;
    ~HumidityLayer() override;
    SampleData sample() override;

    cl_mem getColor() override;

private:
    cl_mem getHumidityBuffer();

    cl_mem humidityBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
};