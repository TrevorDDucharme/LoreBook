#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class WaterLayer : public MapLayer
{
public:
    WaterLayer() {}
    ~WaterLayer() override = default;
    SampleData sample(const World &) override
    {
        SampleData data;
        data.channels.push_back(perlin(256, 256, 256, .01f, 2.0f, 8, 0.5f, 12345u));
        return data;
    }

    cl_mem getColor(const World &world) override
    {
        return perlin(256, 256, 256, .01f, 2.0f, 8, 0.5f, 12345u);
    }
};
