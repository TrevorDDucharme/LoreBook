#include "WorldMaps/Map/ColorLayer.hpp"
#include "WorldMaps/World/World.hpp"
#include <algorithm>

SampleData ColorLayer::sample(const World& world, float longitude, float latitude) const {
    SampleData data;
    auto e = world.sample(longitude, latitude, "elevation");
    auto h = world.sample(longitude, latitude, "humidity");
    auto t = world.sample(longitude, latitude, "temperature");
    data.channels.push_back(!e.channels.empty() ? e.channels[0] : 0.0f);
    data.channels.push_back(!h.channels.empty() ? h.channels[0] : 0.0f);
    data.channels.push_back(!t.channels.empty() ? t.channels[0] : 0.0f);
    return data;
}

std::array<uint8_t,4> ColorLayer::getColor(const World& world, float longitude, float latitude) const {
    float elevation = 0.0f, humidity = 0.0f, temperature = 0.0f;
    auto e = world.sample(longitude, latitude, "elevation"); if(!e.channels.empty()) elevation = e.channels[0];
    auto h = world.sample(longitude, latitude, "humidity"); if(!h.channels.empty()) humidity = h.channels[0];
    auto t = world.sample(longitude, latitude, "temperature"); if(!t.channels.empty()) temperature = t.channels[0];

    // Normalize roughly from [-1,1] to [0,1]
    float elevN = (elevation + 1.0f) * 0.5f;
    float humN = std::clamp((humidity + 1.0f) * 0.5f, 0.0f, 1.0f);
    float tempN = std::clamp((temperature + 1.0f) * 0.5f, 0.0f, 1.0f);

    // Water detection
    const float seaLevel = 0.45f;
    if(elevN < seaLevel){
        float depth = (seaLevel - elevN) / seaLevel; // 0..1
        uint8_t r = static_cast<uint8_t>(10 + depth * 30.0f);
        uint8_t g = static_cast<uint8_t>(30 + depth * 60.0f);
        uint8_t b = static_cast<uint8_t>(150 + depth * 105.0f);
        return {r,g,b,255};
    }

    std::vector<std::array<uint8_t,4>> elevColors = {
        {34, 139, 34, 255},   // forest green
        {139, 69, 19, 255},   // brown
        {210, 180, 140, 255}, // tan
        {255, 255, 255, 255}  // white
    };
    std::array<uint8_t,4> base = colorRamp(elevN, elevColors);

    std::array<uint8_t,4> wet = {0, 120, 200, 255};
    float wetBlend = humN * 0.6f;
    std::array<uint8_t,4> blended;
    for(size_t i=0;i<3;++i){
        blended[i] = static_cast<uint8_t>(base[i] * (1.0f - wetBlend) + wet[i] * wetBlend);
    }
    blended[3] = 255;

    if(tempN > 0.6f){
        float tblend = (tempN - 0.6f) / 0.4f;
        blended[0] = static_cast<uint8_t>(blended[0] * (1.0f - tblend) + 255 * tblend);
        blended[1] = static_cast<uint8_t>(blended[1] * (1.0f - tblend) + 160 * tblend);
    } else if(tempN < 0.4f){
        float tblend = (0.4f - tempN) / 0.4f;
        blended[2] = static_cast<uint8_t>(blended[2] * (1.0f - tblend) + 200 * tblend);
    }

    return blended;
}