#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class RiverLayer : public MapLayer
{
public:
    RiverLayer() = default;
    ~RiverLayer() override
    {
        if (riverBuffer != nullptr)
        {
            OpenCLContext::get().releaseMem(riverBuffer);
            riverBuffer = nullptr;
        }
        if (coloredBuffer != nullptr)
        {
            OpenCLContext::get().releaseMem(coloredBuffer);
            coloredBuffer = nullptr;
        }
    }
    SampleData sample(const World &) override
    {
        SampleData data;
        data.channels.push_back(getriverBuffer());
        return data;
    }

    cl_mem getColor(const World &world) override
    {
        // build new cl_mem buffer with RGBA colors based on elevation data (gray scale, full alpha)
        cl_mem riverBuffer = getriverBuffer();
        cl_int err = CL_SUCCESS;
        // Convert elevation scalar values to grayscale RGBA colors
        static std::vector<std::array<uint8_t, 4>> grayRamp = {
            MapLayer::rgba(0, 0, 0, 255),
            MapLayer::rgba(0, 0, 255, 255)};
        if (coloredBuffer == nullptr)
        {
            scalarToColor(coloredBuffer, riverBuffer, 256, 256, 256, 2, grayRamp);
        }
        return coloredBuffer;
    }

private:
    cl_mem getriverBuffer()
    {
        if (riverBuffer == nullptr)
        {
            perlin(riverBuffer, 256, 256, 256, .01f, 2.0f, 8, 0.5f, 12345u);
        }
        return riverBuffer;
    }

    cl_mem riverBuffer = nullptr;
    cl_mem coloredBuffer = nullptr;
};