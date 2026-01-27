#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class BiomeLayer : public MapLayer {
public:
    BiomeLayer() = default;
    ~BiomeLayer() override = default;

    SampleData sample(const World& world, float longitude, float latitude) const override;
    std::array<uint8_t,4> getColor(const World& world, float longitude, float latitude) const override;
};