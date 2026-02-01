#pragma once
#include <WorldMaps/Map/MapLayer.hpp>
#include <vector>

// Composite color layer that blends elevation, humidity, temperature and water
class ColorLayer : public MapLayer {
public:
 ColorLayer() = default;
    ~ColorLayer() override;
    cl_mem sample() override;

    cl_mem getColor() override;

private:
    cl_mem getColorBuffer();

    cl_mem colorBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
};