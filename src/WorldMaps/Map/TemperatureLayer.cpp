#include <WorldMaps/Map/TemperatureLayer.hpp>
#include <WorldMaps/WorldMap.hpp>

TemperatureLayer::~TemperatureLayer()
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
SampleData TemperatureLayer::sample()
{
    SampleData data;
    data.channels.push_back(getTemperatureBuffer());
    return data;
}

cl_mem TemperatureLayer::getColor()
{
    // build new cl_mem buffer with RGBA colors based on temperature data (gray scale, full alpha)
    cl_mem temperatureBuffer = getTemperatureBuffer();
    cl_int err = CL_SUCCESS;
    // Convert temperature scalar values to grayscale RGBA colors
    static std::vector<std::array<uint8_t, 4>> grayRamp = {
        MapLayer::rgba(0, 0, 0, 255),
        MapLayer::rgba(255, 255, 255, 255)};
    if (coloredBuffer == nullptr)
    {
        scalarToColor(coloredBuffer, temperatureBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), 2, grayRamp);
    }
    return coloredBuffer;
}

cl_mem TemperatureLayer::getTemperatureBuffer()
{
    if (temperatureBuffer == nullptr)
    {
        perlin(temperatureBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), .01f, 2.0f, 8, 0.5f, 12345u);
    }
    return temperatureBuffer;
}