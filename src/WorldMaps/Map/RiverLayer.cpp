#include <WorldMaps/Map/RiverLayer.hpp>
#include <WorldMaps/WorldMap.hpp>

RiverLayer::~RiverLayer()
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
SampleData RiverLayer::sample()
{
    SampleData data;
    data.channels.push_back(getRiverBuffer());
    return data;
}

cl_mem RiverLayer::getColor()
{
    // build new cl_mem buffer with RGBA colors based on river data (gray scale, full alpha)
    cl_mem riverBuffer = getRiverBuffer();
    cl_int err = CL_SUCCESS;
    // Convert river scalar values to grayscale RGBA colors
    static std::vector<std::array<uint8_t, 4>> grayRamp = {
        MapLayer::rgba(0, 0, 0, 255),
        MapLayer::rgba(255, 255, 255, 255)};
    if (coloredBuffer == nullptr)
    {
        scalarToColor(coloredBuffer, riverBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), 2, grayRamp);
    }
    return coloredBuffer;
}

cl_mem RiverLayer::getRiverBuffer()
{
    if (riverBuffer == nullptr)
    {
        perlin(riverBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), .01f, 2.0f, 8, 0.5f, 12345u);
    }
    return riverBuffer;
}