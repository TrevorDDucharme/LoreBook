#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class HumidityLayer : public MapLayer
{
public:
    HumidityLayer() = default;
    ~HumidityLayer() override
    {
        if (humidityBuffer != nullptr)
        {
            OpenCLContext::get().releaseMem(humidityBuffer);
            humidityBuffer = nullptr;
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
        data.channels.push_back(getHumidityBuffer());
        return data;
    }

    cl_mem getColor() override
    {
        // build new cl_mem buffer with RGBA colors based on elevation data (gray scale, full alpha)
        cl_mem humidityBuffer = getHumidityBuffer();
        cl_int err = CL_SUCCESS;
        // Convert elevation scalar values to grayscale RGBA colors
        static std::vector<std::array<uint8_t, 4>> grayRamp = {
            MapLayer::rgba(0, 0, 0, 255),
            MapLayer::rgba(0, 0, 255, 255)};
        if (coloredBuffer == nullptr)
        {
            scalarToColor(coloredBuffer, humidityBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), 2, grayRamp);
        }
        return coloredBuffer;
    }

private:
    cl_mem getHumidityBuffer()
    {
        if (humidityBuffer == nullptr)
        {
            perlin(humidityBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), .01f, 2.0f, 8, 0.5f, 12345u);
        }
        return humidityBuffer;
    }

    cl_mem humidityBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
};