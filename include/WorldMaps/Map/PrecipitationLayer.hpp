#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class PrecipitationLayer : public MapLayer
{
public:
    PrecipitationLayer() = default;
    ~PrecipitationLayer() override;
    cl_mem sample() override;

    cl_mem getColor() override;

private:
    cl_mem getPercipitationBuffer();

    cl_mem percipitationBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
};