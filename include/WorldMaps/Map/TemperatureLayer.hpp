#pragma once
#include <WorldMaps/Map/MapLayer.hpp>
#include <noise/noise.h>

class TemperatureLayer : public MapLayer {
public:
    TemperatureLayer();
    ~TemperatureLayer() override = default;
    SampleData sample(const World& /*world*/, float longitude, float latitude) const override{
        SampleData data;
        double x = std::cos(latitude * M_PI / 180.0) * std::cos(longitude * M_PI / 180.0);
        double y = std::cos(latitude * M_PI / 180.0) * std::sin(longitude * M_PI / 180.0);
        double z = std::sin(latitude * M_PI / 180.0);
        double temperature = perlinModule_.GetValue(x * 10.0, y * 10.0, z * 10.0)/ ComputeMaxAmplitude(6,0.5);
        data.channels.push_back(static_cast<float>(temperature));
        return data;
    }

    void reseed(int seed) override { perlinModule_.SetSeed(seed * 3); }

    bool setParameter(const std::string& name, float value) override {
        if(name == "seed"){ perlinModule_.SetSeed(static_cast<int>(value)); return true; }
        if(name == "frequency"){ perlinModule_.SetFrequency(value); return true; }
        if(name == "lacunarity"){ perlinModule_.SetLacunarity(value); return true; }
        if(name == "persistence"){ perlinModule_.SetPersistence(value); return true; }
        if(name == "octaves"){ perlinModule_.SetOctaveCount(static_cast<int>(value)); return true; }
        return false;
    }

    std::map<std::string, float> getParameters() const override {
        return {
            {"seed", static_cast<float>(perlinModule_.GetSeed())},
            {"frequency", static_cast<float>(perlinModule_.GetFrequency())},
            {"lacunarity", static_cast<float>(perlinModule_.GetLacunarity())},
            {"persistence", static_cast<float>(perlinModule_.GetPersistence())},
            {"octaves", static_cast<float>(perlinModule_.GetOctaveCount())}
        };
    }
    std::array<uint8_t, 4> getColor(const World& world, float longitude, float latitude) const override{
        SampleData data = sample(world, longitude, latitude);
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
    mutable noise::module::Perlin perlinModule_;
};

// Inline constructor implementation
inline TemperatureLayer::TemperatureLayer()
{
    perlinModule_.SetFrequency(.2);
    perlinModule_.SetLacunarity(2.0);
    perlinModule_.SetOctaveCount(10);
    perlinModule_.SetPersistence(0.5);
    perlinModule_.SetSeed(42*3);
}