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
        MapLayer *landtypeLayer = parentWorld->getLayer("landtype");
        MapLayer *elevationLayer = parentWorld->getLayer("elevation");
        MapLayer *watertableLayer = parentWorld->getLayer("watertable");
        MapLayer *riverLayer = parentWorld->getLayer("rivers");
        MapLayer *temperatureLayer = parentWorld->getLayer("temperature");

        cl_mem landtype = nullptr;
        if (landtypeLayer)
        {
            landtype = landtypeLayer->getColor();
        }
        cl_mem elevation = nullptr;
        if (elevationLayer)
        {
            elevation = elevationLayer->getColor();
        }
        cl_mem watertable = nullptr;
        if (watertableLayer)
        {
            watertable = watertableLayer->getColor();
        }
        cl_mem river = nullptr;
        if (riverLayer)
        {
            river = riverLayer->getColor();
        }
        cl_mem temp = nullptr;
        if (temperatureLayer)
        {
            temp = temperatureLayer->sample();
            static std::vector<cl_float4> grayRamp = {
                MapLayer::rgb(255, 255, 255),
                MapLayer::rgba(0, 0, 0, 0)};
            static std::vector<float> weights = {0.8f, 1.0f};
            if (tempColorBuffer == nullptr)
            {
                weightedScalarToColor(tempColorBuffer, temp, parentWorld->getWorldLatitudeResolution(), parentWorld->getWorldLongitudeResolution(), grayRamp.size(), grayRamp, weights);
            }
        }
        cl_int err = CL_SUCCESS;

        if (landtype && elevation)
        {
            multiplyColor(colorBuffer, landtype, elevation,
                          parentWorld->getWorldLatitudeResolution(),
                          parentWorld->getWorldLongitudeResolution());
        }

        if (watertable)
        {
            alphaBlend(colorBuffer, colorBuffer, watertable,
                       parentWorld->getWorldLatitudeResolution(),
                       parentWorld->getWorldLongitudeResolution());
        }

        if (river)
        {
            alphaBlend(colorBuffer, colorBuffer, river,
                       parentWorld->getWorldLatitudeResolution(),
                       parentWorld->getWorldLongitudeResolution());
        }

        if (tempColorBuffer)
        {
            alphaBlend(colorBuffer, colorBuffer, tempColorBuffer,
                       parentWorld->getWorldLatitudeResolution(),
                       parentWorld->getWorldLongitudeResolution());
        }
    }
    return colorBuffer;
}