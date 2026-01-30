#pragma once
#include <WorldMaps/Map/MapLayer.hpp>
#include <WorldMaps/World/World.hpp>
class ElevationLayer : public MapLayer {
public:
    ElevationLayer()= default;
    ~ElevationLayer() override{
        if(elevationBuffer != nullptr){
            OpenCLContext::get().releaseMem(elevationBuffer);
            elevationBuffer = nullptr;
        }
        if(coloredBuffer != nullptr){
            OpenCLContext::get().releaseMem(coloredBuffer);
            coloredBuffer = nullptr;
        }
    }
    SampleData sample() override{
        SampleData data;
        data.channels.push_back(getElevationBuffer());
        return data;
    }

    cl_mem getColor() override{
        //build new cl_mem buffer with RGBA colors based on elevation data (gray scale, full alpha)
        cl_mem elevationBuffer = getElevationBuffer();
        cl_int err = CL_SUCCESS;
        // Convert elevation scalar values to grayscale RGBA colors
        static std::vector<std::array<uint8_t,4>> grayRamp = {
            MapLayer::rgba(0,0,0,255),
            MapLayer::rgba(255,255,255,255)
        };
        if(coloredBuffer == nullptr){
            scalarToColor(coloredBuffer, elevationBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), 2, grayRamp);
        }
        return coloredBuffer;
    }

private:
    cl_mem getElevationBuffer(){
        if(elevationBuffer == nullptr){
            perlin(elevationBuffer,parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), .01f, 2.0f, 8, 0.5f, 12345u);
        }
        return elevationBuffer;
    }

    cl_mem elevationBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
};