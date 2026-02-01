#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class LatitudeLayer : public MapLayer
{
public:
    LatitudeLayer() = default;
    ~LatitudeLayer() override;
    cl_mem sample() override;

    cl_mem getColor() override;

private:
    cl_mem getLatitudeBuffer();

    cl_mem latitudeBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;

    static void latitude(
        cl_mem &output,
        int fieldW, 
        int fieldH, 
        int fieldD
    );
};