#include "WorldMaps/Map/BiomeLayer.hpp"
#include "WorldMaps/World/World.hpp"
#include <algorithm>

SampleData BiomeLayer::sample(const World& world, float longitude, float latitude) const {
    SampleData out;
    auto e = world.sample(longitude, latitude, "elevation");
    auto h = world.sample(longitude, latitude, "humidity");
    auto t = world.sample(longitude, latitude, "temperature");
    float elev = e.channels.empty() ? 0.0f : e.channels[0];
    float hum = h.channels.empty() ? 0.0f : h.channels[0];
    float tmp = t.channels.empty() ? 0.0f : t.channels[0];

    // Simple biome classification: 0=water(not here),1=desert,2=grassland,3=forest,4=taiga,5=tundra
    float elevN = (elev + 1.0f) * 0.5f;
    float humN = (hum + 1.0f) * 0.5f;
    float tmpN = (tmp + 1.0f) * 0.5f;

    int biome = 2; // grassland
    if (elevN < 0.45f) biome = 0; // low - water may exist but that's separate
    else if (tmpN < 0.2f) biome = 5; // tundra
    else if (tmpN < 0.35f) biome = 4; // taiga
    else if (humN > 0.7f) biome = 3; // forest
    else if (humN < 0.25f) biome = 1; // desert

    out.channels.push_back(static_cast<float>(biome));
    return out;
}

std::array<uint8_t,4> BiomeLayer::getColor(const World& world, float longitude, float latitude) const {
    auto s = sample(world, longitude, latitude);
    int biome = s.channels.empty() ? 2 : static_cast<int>(s.channels[0]);
    switch(biome){
        case 0: return {0, 100, 180, 255}; // water-ish
        case 1: return {210, 180, 100, 255}; // desert
        case 2: return {180, 220, 120, 255}; // grass
        case 3: return {34, 139, 34, 255}; // forest
        case 4: return {102, 153, 102, 255}; // taiga-ish
        case 5: return {200, 200, 220, 255}; // tundra
        default: return {128,128,128,255};
    }
}