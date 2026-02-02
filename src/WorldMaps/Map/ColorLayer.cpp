#include <WorldMaps/Map/ColorLayer.hpp>
#include <WorldMaps/WorldMap.hpp>

ColorLayer::~ColorLayer()
{
    if (colorBuffer != nullptr)
    {
        OpenCLContext::get().releaseMem(colorBuffer);
        colorBuffer = nullptr;
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
        //cl_mem temp = parentWorld->getLayer("temperature")->getColor();

        multiplyColor(colorBuffer, landtype, elevation,
                                   parentWorld->getWorldLatitudeResolution(),
                                   parentWorld->getWorldLongitudeResolution());

        alphaBlend(colorBuffer, colorBuffer, watertable,
                                   parentWorld->getWorldLatitudeResolution(),
                                   parentWorld->getWorldLongitudeResolution());

        alphaBlend(colorBuffer, colorBuffer, river,
                                   parentWorld->getWorldLatitudeResolution(),
                                   parentWorld->getWorldLongitudeResolution());
        
        // alphaBlend(colorBuffer, colorBuffer, temp,
        //                            parentWorld->getWorldLatitudeResolution(),
        //                            parentWorld->getWorldLongitudeResolution());
    }
    return colorBuffer;
}