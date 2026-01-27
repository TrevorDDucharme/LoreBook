#include "WorldMaps/Map/RiverLayer.hpp"
#include "WorldMaps/World/World.hpp"
#include <cmath>

SampleData RiverLayer::sample(const World& world, float longitude, float latitude) const {
    SampleData out;
    auto h = world.sample(longitude, latitude, "humidity");
    auto e = world.sample(longitude, latitude, "elevation");
    float hum = h.channels.empty() ? 0.0f : h.channels[0];
    float elev = e.channels.empty() ? 0.0f : e.channels[0];

    // heuristic river strength: high humidity and downhill slope
    // compute small slope by sampling small offset in latitude
    float dlat = 0.1f; // small step
    auto e2 = world.sample(longitude, latitude + dlat, "elevation");
    float elev2 = e2.channels.empty() ? elev : e2.channels[0];
    float slope = elev - elev2; // positive means going downwards toward increasing lat

    float strength = std::max(0.0f, hum * std::max(0.0f, slope * 5.0f));
    out.channels.push_back(strength);
    return out;
}

std::array<uint8_t,4> RiverLayer::getColor(const World& world, float longitude, float latitude) const {
    auto s = sample(world, longitude, latitude);
    float strength = s.channels.empty() ? 0.0f : s.channels[0];
    if(strength < 0.01f) return {0,0,0,0};
    uint8_t v = static_cast<uint8_t>(std::min(255.0f, 100.0f + strength * 155.0f));
    return {10, 120, v, 255};
}