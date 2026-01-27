#pragma once
#include <WorldMaps/Map/MapLayer.hpp>
#include <noise/noise.h>

class HumidityLayer : public MapLayer {
public:
    HumidityLayer();
    ~HumidityLayer() override = default;
    SampleData sample(const World& /*world*/, float longitude, float latitude) const override{
        SampleData data;
        double x = std::cos(latitude * M_PI / 180.0) * std::cos(longitude * M_PI / 180.0);
        double y = std::cos(latitude * M_PI / 180.0) * std::sin(longitude * M_PI / 180.0);
        double z = std::sin(latitude * M_PI / 180.0);
        double humidity = perlinModule_.GetValue(x * 10.0, y * 10.0, z * 10.0)/ ComputeMaxAmplitude(6,0.5);
        data.channels.push_back(static_cast<float>(humidity));
        return data;
    }
    std::array<uint8_t, 4> getColor(const World& world, float longitude, float latitude) const override{
        SampleData data = sample(world, longitude, latitude);
        float humidity = data.channels[0];
        uint8_t blueValue = static_cast<uint8_t>((humidity + 1.0f) / 2.0f * 255.0f);
        return {0, 0, blueValue, 255};
    }

    void reseed(int seed) override { perlinModule_.SetSeed(seed * 2); }

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
private:
    mutable noise::module::Perlin perlinModule_;
};

// Inline constructor implementation
inline HumidityLayer::HumidityLayer()
{
    perlinModule_.SetFrequency(.2);
    perlinModule_.SetLacunarity(2.0);
    perlinModule_.SetOctaveCount(10);
    perlinModule_.SetPersistence(0.5);
    perlinModule_.SetSeed(42*2);
}