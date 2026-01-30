#include <WorldMaps/Map/ElevationLayer.hpp>
#include <WorldMaps/WorldMap.hpp>

ElevationLayer::~ElevationLayer()
{
    if (elevationBuffer != nullptr)
    {
        OpenCLContext::get().releaseMem(elevationBuffer);
        elevationBuffer = nullptr;
    }
    if (coloredBuffer != nullptr)
    {
        OpenCLContext::get().releaseMem(coloredBuffer);
        coloredBuffer = nullptr;
    }
}
SampleData ElevationLayer::sample()
{
    SampleData data;
    data.channels.push_back(getElevationBuffer());
    return data;
}

cl_mem ElevationLayer::getColor()
{
    // build new cl_mem buffer with RGBA colors based on elevation data (gray scale, full alpha)
    cl_mem elevationBuffer = getElevationBuffer();
    cl_int err = CL_SUCCESS;
    // Convert elevation scalar values to grayscale RGBA colors
    static std::vector<std::array<uint8_t, 4>> grayRamp = {
        MapLayer::rgba(0, 0, 0, 255),
        MapLayer::rgba(255, 255, 255, 255)};
    if (coloredBuffer == nullptr)
    {
        scalarToColor(coloredBuffer, elevationBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), 2, grayRamp);
    }
    return coloredBuffer;
}

cl_mem ElevationLayer::getElevationBuffer()
{
    if (elevationBuffer == nullptr)
    {
        perlin(elevationBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), .01f, 2.0f, 8, 0.5f, 12345u);
    }
    return elevationBuffer;
}