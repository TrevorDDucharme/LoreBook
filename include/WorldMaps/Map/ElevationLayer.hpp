#pragma once
#include <WorldMaps/Map/MapLayer.hpp>
class ElevationLayer : public MapLayer {
public:
    ElevationLayer()= default;
    ~ElevationLayer() override;
    SampleData sample() override;
    cl_mem getColor() override;

private:
    cl_mem getElevationBuffer();

    cl_mem elevationBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
};