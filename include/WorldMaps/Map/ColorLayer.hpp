#pragma once
#include <WorldMaps/Map/MapLayer.hpp>
#include <vector>

// Composite color layer that blends elevation, humidity, temperature and water
class ColorLayer : public MapLayer {
public:
    ColorLayer() = default;
    ~ColorLayer() override = default;

    SampleData sample(const World& world, float longitude, float latitude) const override;
    std::array<uint8_t,4> getColor(const World& world, float longitude, float latitude) const override;
};