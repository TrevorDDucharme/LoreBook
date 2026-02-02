#pragma once
#include <WorldMaps/Map/MapLayer.hpp>
#include <WorldMaps/Map/LandTypeLayer.hpp>

class HumidityLayer : public MapLayer
{
public:
    HumidityLayer() = default;
    ~HumidityLayer() override;
    cl_mem sample() override;

    cl_mem getColor() override;

private:
    cl_mem getHumidityBuffer();

    void humidityMap(cl_mem &output,
                     int latitudeResolution,
                     int longitudeResolution,
                     const std::vector<LandTypeLayer::LandTypeProperties> &landtypeProperties,
                     int landtypeCount,
                     const std::vector<float> &frequency,
                     const std::vector<float> &lacunarity,
                     const std::vector<int> &octaves,
                     const std::vector<float> &persistence,
                     const std::vector<unsigned int> &seed,
                     cl_mem elevation,
                     cl_mem watertable,
                     cl_mem rivers,
                     cl_mem temperature);

    cl_mem humidityBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
};