#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class WaterLayer : public MapLayer
{
public:
 WaterLayer() = default;
    ~WaterLayer() override
    {
        if (waterBuffer != nullptr)
        {
            OpenCLContext::get().releaseMem(waterBuffer);
            waterBuffer = nullptr;
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
        data.channels.push_back(getWaterBuffer());
        return data;
    }

    cl_mem getColor() override
    {
        // build new cl_mem buffer with RGBA colors based on elevation data (gray scale, full alpha)
        cl_mem waterBuffer = getWaterBuffer();
        cl_int err = CL_SUCCESS;
        // Convert elevation scalar values to grayscale RGBA colors
        static std::vector<std::array<uint8_t, 4>> grayRamp = {
            MapLayer::rgba(0, 0, 0, 255),
            MapLayer::rgba(0, 0, 255, 255)};
        if (coloredBuffer == nullptr)
        {
            scalarToColor(coloredBuffer, waterBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), 2, grayRamp);
        }
        return coloredBuffer;
    }

private:
    cl_mem getWaterBuffer()
    {
        if (waterBuffer == nullptr)
        {
            perlin(waterBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), .01f, 2.0f, 8, 0.5f, 12345u);
        }
        return waterBuffer;
    }

    cl_mem waterBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
};
