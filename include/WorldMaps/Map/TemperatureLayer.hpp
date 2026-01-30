#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class TemperatureLayer : public MapLayer
{
public:
    TemperatureLayer() = default;
    ~TemperatureLayer() override;
    SampleData sample() override;

    cl_mem getColor() override;

private:
    cl_mem getTemperatureBuffer();

    cl_mem temperatureBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
};