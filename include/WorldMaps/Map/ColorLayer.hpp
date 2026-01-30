#pragma once
#include <WorldMaps/Map/MapLayer.hpp>
#include <vector>

// Composite color layer that blends elevation, humidity, temperature and water
class ColorLayer : public MapLayer {
public:
 ColorLayer() = default;
    ~ColorLayer() override
    {
        if (colorBuffer != nullptr)
        {
            OpenCLContext::get().releaseMem(colorBuffer);
            colorBuffer = nullptr;
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
        data.channels.push_back(getColorBuffer());
        return data;
    }

    cl_mem getColor() override
    {
        // build new cl_mem buffer with RGBA colors based on elevation data (gray scale, full alpha)
        cl_mem colorBuffer = getColorBuffer();
        cl_int err = CL_SUCCESS;
        // Convert elevation scalar values to grayscale RGBA colors
        static std::vector<std::array<uint8_t, 4>> grayRamp = {
            MapLayer::rgba(0, 0, 0, 255),
            MapLayer::rgba(0, 0, 255, 255)};
        if (coloredBuffer == nullptr)
        {
            scalarToColor(coloredBuffer, colorBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), 2, grayRamp);
        }
        return coloredBuffer;
    }

private:
    cl_mem getColorBuffer()
    {
        if (colorBuffer == nullptr)
        {
            perlin(colorBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), .01f, 2.0f, 8, 0.5f, 12345u);
        }
        return colorBuffer;
    }

    cl_mem colorBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
};