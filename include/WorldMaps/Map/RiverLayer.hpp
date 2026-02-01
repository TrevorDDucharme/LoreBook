#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class RiverLayer : public MapLayer
{
public:
    RiverLayer() = default;
    ~RiverLayer() override;
    cl_mem sample() override;

    cl_mem getColor() override;

private:
    cl_mem getRiverBuffer();

    cl_mem riverBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;

    static void generateRiverPaths(cl_mem& outputCounts,
                               cl_mem elevationBuf,
                               cl_mem waterTableBuf,
                               int W, int H, int D,
                               int radius,
                               uint32_t maxSteps);
};