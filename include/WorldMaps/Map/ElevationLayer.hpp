#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class ElevationLayer : public MapLayer {
public:
    ElevationLayer()= default;
    ~ElevationLayer() override = default;
    SampleData sample(const World&) override{
        SampleData data;
        data.channels.push_back(perlin(256, 256, 256, .01f, 2.0f, 8, 0.5f, 12345u));
        return data;
    }

    cl_mem getColor(const World& world) override{
        //build new cl_mem buffer with RGBA colors based on elevation data (gray scale, full alpha)
        SampleData elevationData = sample(world);
        cl_mem elevationBuffer = elevationData.channels[0];
        cl_int err = CL_SUCCESS;
        // Convert elevation scalar values to grayscale RGBA colors
        static std::vector<std::array<uint8_t,4>> grayRamp = {
            MapLayer::rgba(0,0,0,255),
            MapLayer::rgba(255,255,255,255)
        };
        cl_mem coloredBuffer = scalarToColor(elevationBuffer, 256, 256, 256, 2, grayRamp);
        OpenCLContext::get().releaseMem(elevationBuffer);
        return coloredBuffer;
    }
};