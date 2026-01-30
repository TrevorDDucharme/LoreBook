#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class RiverLayer : public MapLayer
{
public:
    RiverLayer() = default;
    ~RiverLayer() override;
    SampleData sample() override;

    cl_mem getColor() override;

private:
    cl_mem getRiverBuffer();

    cl_mem riverBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
};