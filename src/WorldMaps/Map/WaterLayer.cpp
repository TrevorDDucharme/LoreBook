#include <WorldMaps/Map/WaterLayer.hpp>
#include <WorldMaps/WorldMap.hpp>

WaterLayer::~WaterLayer()
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
SampleData WaterLayer::sample()
{
    SampleData data;
    data.channels.push_back(getWaterBuffer());
    return data;
}

cl_mem WaterLayer::getColor()
{
    // build new cl_mem buffer with RGBA colors based on water data (gray scale, full alpha)
    cl_mem waterBuffer = getWaterBuffer();
    cl_int err = CL_SUCCESS;
    // Convert water scalar values to grayscale RGBA colors
    static std::vector<std::array<uint8_t, 4>> grayRamp = {
        MapLayer::rgba(0, 0, 0, 255),
        MapLayer::rgba(255, 255, 255, 255)};
    if (coloredBuffer == nullptr)
    {
        scalarToColor(coloredBuffer, waterBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), 2, grayRamp);
    }
    return coloredBuffer;
}

cl_mem WaterLayer::getWaterBuffer()
{
    if (waterBuffer == nullptr)
    {
        perlin(waterBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), .01f, 2.0f, 8, 0.5f, 12345u);
    }
    return waterBuffer;
}