#include <WorldMaps/Map/ColorLayer.hpp>
#include <WorldMaps/WorldMap.hpp>

ColorLayer::~ColorLayer()
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
cl_mem ColorLayer::sample()
{
    return getColorBuffer();
}

cl_mem ColorLayer::getColor()
{
    // build new cl_mem buffer with RGBA colors based on color data (gray scale, full alpha)
    cl_mem colorBuffer = getColorBuffer();
    cl_int err = CL_SUCCESS;
    // Convert color scalar values to grayscale RGBA colors
    static std::vector<std::array<uint8_t, 4>> grayRamp = {
        MapLayer::rgba(0, 0, 0, 255),
        MapLayer::rgba(255, 255, 255, 255)};
    if (coloredBuffer == nullptr)
    {
        scalarToColor(coloredBuffer, colorBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), 2, grayRamp);
    }
    return coloredBuffer;
}

cl_mem ColorLayer::getColorBuffer()
{
    if (colorBuffer == nullptr)
    {
        perlin(colorBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), .01f, 2.0f, 8, 0.5f, 12345u);
    }
    return colorBuffer;
}