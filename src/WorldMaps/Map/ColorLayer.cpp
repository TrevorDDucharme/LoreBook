#include "WorldMaps/Map/ColorLayer.hpp"
#include "WorldMaps/World/World.hpp"
#include <algorithm>
#include <cmath>

SampleData ColorLayer::sample(const World& world, float longitude, float latitude) const {
    SampleData data;
    auto e = world.sample(longitude, latitude, "elevation_model");
    auto h = world.sample(longitude, latitude, "humidity");
    auto t = world.sample(longitude, latitude, "temperature");
    data.channels.push_back(!e.channels.empty() ? e.channels[0] : 0.0f);
    data.channels.push_back(!h.channels.empty() ? h.channels[0] : 0.0f);
    data.channels.push_back(!t.channels.empty() ? t.channels[0] : 0.0f);
    return data;
}

std::array<uint8_t,4> ColorLayer::getColor(const World& world, float longitude, float latitude) const {
    float elevation = 0.0f, humidity = 0.0f, temperature = 0.0f;
    auto e = world.sample(longitude, latitude, "elevation_model"); if(!e.channels.empty()) elevation = e.channels[0];
    auto h = world.sample(longitude, latitude, "humidity"); if(!h.channels.empty()) humidity = h.channels[0];
    auto t = world.sample(longitude, latitude, "temperature"); if(!t.channels.empty()) temperature = t.channels[0];

    // Normalize roughly from [-1,1] to [0,1]
    float elevN = std::clamp((elevation + 1.0f) * 0.5f, 0.0f, 1.0f);
    float humN = std::clamp((humidity + 1.0f) * 0.5f, 0.0f, 1.0f);
    float tempN = std::clamp((temperature + 1.0f) * 0.5f, 0.0f, 1.0f);

    const float seaLevel = 0.45f;
    const float coastRange = 0.035f; // blend width for sandy shores

    auto lerpColor = [](const std::array<uint8_t,4>& a, const std::array<uint8_t,4>& b, float t){
        std::array<uint8_t,4> r;
        t = std::clamp(t, 0.0f, 1.0f);
        for(int i=0;i<3;++i) r[i] = static_cast<uint8_t>(a[i] * (1.0f - t) + b[i] * t + 0.5f);
        r[3] = 255;
        return r;
    };

    // Water: richer palette with shallow/shelf colors
    if(elevN < seaLevel){
        float depth = std::clamp((seaLevel - elevN) / seaLevel, 0.0f, 1.0f);
        // Palette: shallow -> mid -> deep
        const std::array<uint8_t,4> shallow = {160, 200, 220, 255};
        const std::array<uint8_t,4> mid = {20, 100, 170, 255};
        const std::array<uint8_t,4> deep = {6, 28, 78, 255};

        // Smooth interpolation with slight power curve for depth
        float d2 = std::pow(depth, 1.1f);
        std::array<uint8_t,4> water = (d2 < 0.5f) ? lerpColor(shallow, mid, d2 / 0.5f) : lerpColor(mid, deep, (d2 - 0.5f) / 0.5f);

        // Add slight green tint in shallow water where humidity is high
        float humTint = std::clamp(humN * 0.5f, 0.0f, 0.45f);
        if(humTint > 0.001f){
            std::array<uint8_t,4> algae = {40, 150, 100, 255};
            water = lerpColor(water, algae, humTint);
        }

        return water;
    }

    // Land: use a multi-stop elevation ramp that looks natural
    std::vector<std::array<uint8_t,4>> elevColors = {
        {100, 170, 90, 255},  // low grass
        {80, 140, 55, 255},   // dense green
        {120, 100, 70, 255},  // rocky / dirt
        {200, 190, 170, 255}, // highland / scree
        {245, 245, 245, 255}  // snow
    };

    // Remap elevation so seaLevel maps to 0
    float landElev = (elevN - seaLevel) / (1.0f - seaLevel);
    landElev = std::clamp(landElev, 0.0f, 1.0f);
    std::array<uint8_t,4> base = colorRamp(landElev, elevColors);

    // Sandy shores: blend land color with sand near coastline
    if(elevN < seaLevel + coastRange){
        float t = (elevN - seaLevel) / coastRange; t = std::clamp(t, 0.0f, 1.0f);
        const std::array<uint8_t,4> sand = {212, 185, 134, 255};
        base = lerpColor(sand, base, t);
    }

    // Humidity makes areas lusher (shift towards a wet green)
    const std::array<uint8_t,4> wetGreen = {30, 150, 80, 255};
    float wetBlend = std::clamp(humN * 0.9f, 0.0f, 0.85f);
    base = lerpColor(base, wetGreen, wetBlend);

    // Temperature shifts: warm -> slightly yellowish, cold -> bluish tint
    if(tempN > 0.6f){
        float t = (tempN - 0.6f) / 0.4f; t = std::clamp(t, 0.0f, 1.0f);
        const std::array<uint8_t,4> warm = {255, 210, 130, 255};
        base = lerpColor(base, warm, t * 0.45f);
    } else if(tempN < 0.4f){
        float t = (0.4f - tempN) / 0.4f; t = std::clamp(t, 0.0f, 1.0f);
        const std::array<uint8_t,4> cold = {180, 200, 235, 255};
        base = lerpColor(base, cold, t * 0.28f);
    }

    // Elevation-based brightness: higher is a bit brighter (simulates lighting/less vegetation)
    float brightness = 1.0f + 0.06f * landElev;
    std::array<uint8_t,4> finalc = base;
    for(int i=0;i<3;++i){
        float v = std::clamp(static_cast<float>(finalc[i]) * brightness, 0.0f, 255.0f);
        finalc[i] = static_cast<uint8_t>(v + 0.5f);
    }
    finalc[3] = 255;
    return finalc;
}