#pragma once
#include <WorldMaps/Map/MapLayer.hpp>
#include <noise/noise.h>

class HumidityLayer : public MapLayer {
public:
    HumidityLayer() = default;
    ~HumidityLayer() override = default;
    SampleData sample(float longitude, float latitude) const override{
        //multi-layer 3d noise sampling for humidity
        SampleData data;
        noise::module::Perlin perlinModule;
        perlinModule.SetFrequency(1.0);
        perlinModule.SetLacunarity(2.0);
        perlinModule.SetOctaveCount(6);
        perlinModule.SetPersistence(0.5);
        perlinModule.SetSeed(42*2);
        //convert lon/lat to x,y,z coordinates to polar sphere
        double x = std::cos(latitude * M_PI / 180.0) * std::cos(longitude * M_PI / 180.0);
        double y = std::cos(latitude * M_PI / 180.0) * std::sin(longitude * M_PI / 180.0);
        double z = std::sin(latitude * M_PI / 180.0);
        double elevation = perlinModule.GetValue(x * 10.0, y * 10.0, z * 10.0)/ ComputeMaxAmplitude(6,0.5);
        data.channels.push_back(static_cast<float>(elevation));
        return data;
    }
    std::array<uint8_t, 4> getColor(float longitude, float latitude) const override{
        SampleData data = sample(longitude, latitude);
        float humidity = data.channels[0];
        // Map humidity to color (simple blue scale for example)
        uint8_t blueValue = static_cast<uint8_t>((humidity + 1.0f) / 2.0f * 255.0f);
        return {0, 0, blueValue, 255};
    }
private:
    // Elevation layer specific members
};