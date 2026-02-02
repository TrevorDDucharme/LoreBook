#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class TemperatureLayer : public MapLayer
{
public:
    TemperatureLayer();
    ~TemperatureLayer() override;
    cl_mem sample() override;

    cl_mem getColor() override;

private:
    cl_mem getTemperatureBuffer();

    cl_mem temperatureBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
    unsigned int seed=231354u;

    static void tempMap(cl_mem &output,
             int latitudeResolution,
             int longitudeResolution,
             float frequency,
             float lacunarity,
             int octaves,
             float persistence,
             unsigned int seed);
};