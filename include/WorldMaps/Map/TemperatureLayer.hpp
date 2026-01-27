#pragma once
#include <WorldMaps/Map/MapLayer.hpp>
#include <noise/noise.h>

class TemperatureLayer : public MapLayer {
public:
    TemperatureLayer() = default;
    ~TemperatureLayer() override = default;
    SampleData sample(float longitude, float latitude) const override{
        //multi-layer 3d noise sampling for temperature
        SampleData data;
        noise::module::Perlin perlinModule;
        perlinModule.SetFrequency(1.0);
        perlinModule.SetLacunarity(2.0);
        perlinModule.SetOctaveCount(6);
        perlinModule.SetPersistence(0.5);
        perlinModule.SetSeed(42*3);
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
        float temperature = data.channels[0];
        //use color ramp from blue (cold) to red (hot)
        std::vector<std::array<uint8_t,4>> colors = {
            {0, 0, 255, 255},   // Blue
            {0, 255, 255, 255}, // Cyan
            {0, 255, 0, 255},   // Green
            {255, 255, 0, 255}, // Yellow
            {255, 0, 0, 255}    // Red
        };
        // Normalize temperature to [0,1] for color ramp
        float normalizedTemp = (temperature + 1.0f) / 2.0f;
        return colorRamp(normalizedTemp, colors);
    }
private:
    // Elevation layer specific members
};