#include <WorldMaps/Map/HumidityLayer.hpp>
#include <WorldMaps/WorldMap.hpp>

HumidityLayer::~HumidityLayer()
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
cl_mem HumidityLayer::sample()
{
    return getHumidityBuffer();
}

cl_mem HumidityLayer::getColor()
{
    // build new cl_mem buffer with RGBA colors based on humidity data (gray scale, full alpha)
    cl_mem humidityBuffer = getHumidityBuffer();
    cl_int err = CL_SUCCESS;
    // Convert humidity scalar values to grayscale RGBA colors
    static std::vector<std::array<uint8_t, 4>> grayRamp = {
        MapLayer::rgba(0, 0, 0, 255),
        MapLayer::rgba(255, 255, 255, 255)};
    if (coloredBuffer == nullptr)
    {
        scalarToColor(coloredBuffer, humidityBuffer, parentWorld->getWorldLatitudeResolution(), parentWorld->getWorldLongitudeResolution(), 2, grayRamp);
    }
    return coloredBuffer;
}

cl_mem HumidityLayer::getHumidityBuffer()
{
    cl_mem landtype= parentWorld->getLayer("landtype")->getColor();
    cl_mem elevation= parentWorld->getLayer("elevation")->getColor();
    cl_mem watertable= parentWorld->getLayer("watertable")->getColor();
    cl_mem river = parentWorld->getLayer("rivers")->getColor();
    cl_mem temp = parentWorld->getLayer("temperature")->getColor();


    if (humidityBuffer == nullptr)
    {
        perlin(humidityBuffer, parentWorld->getWorldLatitudeResolution(), parentWorld->getWorldLongitudeResolution(), .01f, 2.0f, 8, 0.5f, 12345u);
    }
    return humidityBuffer;
}