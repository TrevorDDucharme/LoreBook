#include <WorldMaps/Map/ColorLayer.hpp>
#include <WorldMaps/WorldMap.hpp>

ColorLayer::~ColorLayer()
{
    if (colorBuffer != nullptr)
    {
        OpenCLContext::get().releaseMem(colorBuffer);
        colorBuffer = nullptr;
    }
    if (tempColorBuffer != nullptr)
    {
        OpenCLContext::get().releaseMem(tempColorBuffer);
        tempColorBuffer = nullptr;
    }
}

cl_mem ColorLayer::sample()
{
    return getColorBuffer();
}

cl_mem ColorLayer::getColor()
{
    return getColorBuffer();
}

cl_mem ColorLayer::getColorBuffer()
{
    if (colorBuffer == nullptr)
    {
        cl_mem landtype= parentWorld->getLayer("landtype")->getColor();
        cl_mem elevation= parentWorld->getLayer("elevation")->getColor();
        cl_mem watertable= parentWorld->getLayer("watertable")->getColor();
        cl_mem river = parentWorld->getLayer("rivers")->getColor();
        cl_mem temp = parentWorld->getLayer("temperature")->sample();
        cl_int err = CL_SUCCESS;
        // Convert temperature scalar values to grayscale RGBA colors
        static std::vector<cl_float4> grayRamp = {
            MapLayer::rgb(255, 255, 255),
            MapLayer::rgba(0, 0, 0, 0)};
        static std::vector<float> weights = {0.8f, 1.0f};
        if (tempColorBuffer == nullptr)
        {
            weightedScalarToColor(tempColorBuffer, temp, parentWorld->getWorldLatitudeResolution(), parentWorld->getWorldLongitudeResolution(), grayRamp.size(), grayRamp, weights);
        }

        multiplyColor(colorBuffer, landtype, elevation,
                                   parentWorld->getWorldLatitudeResolution(),
                                   parentWorld->getWorldLongitudeResolution());

        alphaBlend(colorBuffer, colorBuffer, watertable,
                                   parentWorld->getWorldLatitudeResolution(),
                                   parentWorld->getWorldLongitudeResolution());

        alphaBlend(colorBuffer, colorBuffer, river,
                                   parentWorld->getWorldLatitudeResolution(),
                                   parentWorld->getWorldLongitudeResolution());
        
        alphaBlend(colorBuffer, colorBuffer, tempColorBuffer,
                                   parentWorld->getWorldLatitudeResolution(),
                                   parentWorld->getWorldLongitudeResolution());
    }
    return colorBuffer;
}