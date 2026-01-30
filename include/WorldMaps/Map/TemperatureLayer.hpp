#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class TemperatureLayer : public MapLayer
{
public:
    TemperatureLayer() = default;
    ~TemperatureLayer() override
    {
        if (temperatureBuffer != nullptr)
        {
            OpenCLContext::get().releaseMem(temperatureBuffer);
            temperatureBuffer = nullptr;
        }
        if (coloredBuffer != nullptr)
        {
            OpenCLContext::get().releaseMem(coloredBuffer);
            coloredBuffer = nullptr;
        }
    }
    SampleData sample() override
    {
        SampleData data;
        data.channels.push_back(getTemperatureBuffer());
        return data;
    }

    cl_mem getColor() override
    {
        // build new cl_mem buffer with RGBA colors based on elevation data (gray scale, full alpha)
        cl_mem temperatureBuffer = getTemperatureBuffer();
        cl_int err = CL_SUCCESS;
        // Convert elevation scalar values to grayscale RGBA colors
        static std::vector<std::array<uint8_t, 4>> grayRamp = {
            MapLayer::rgba(0, 0, 0, 255),
            MapLayer::rgba(0, 0, 255, 255)};
        if (coloredBuffer == nullptr)
        {
            scalarToColor(coloredBuffer, temperatureBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), 2, grayRamp);
        }
        return coloredBuffer;
    }

private:
    cl_mem getTemperatureBuffer()
    {
        if (temperatureBuffer == nullptr)
        {
            perlin(temperatureBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), .01f, 2.0f, 8, 0.5f, 12345u);
        }
        return temperatureBuffer;
    }

    cl_mem temperatureBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
};