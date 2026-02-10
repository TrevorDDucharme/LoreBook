#include <WorldMaps/Map/ColorLayer.hpp>
#include <WorldMaps/WorldMap.hpp>
#include <WorldMaps/World/LayerDelta.hpp>

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

// ── Region-bounded generation ────────────────────────────────────

cl_mem ColorLayer::sampleRegion(float lonMinRad, float lonMaxRad,
                                 float latMinRad, float latMaxRad,
                                 int resX, int resY,
                                 const LayerDelta* delta)
{
    return getColorRegion(lonMinRad, lonMaxRad, latMinRad, latMaxRad,
                          resX, resY, delta);
}

cl_mem ColorLayer::getColorRegion(float lonMinRad, float lonMaxRad,
                                   float latMinRad, float latMaxRad,
                                   int resX, int resY,
                                   const LayerDelta* delta)
{
    ZoneScopedN("ColorLayer::getColorRegion");

    // Get region color buffers from dependent layers
    MapLayer* landtypeLayer = parentWorld->getLayer("landtype");
    MapLayer* elevationLayer = parentWorld->getLayer("elevation");
    MapLayer* watertableLayer = parentWorld->getLayer("watertable");
    MapLayer* temperatureLayer = parentWorld->getLayer("temperature");

    cl_mem result = nullptr;

    // Landtype × elevation base
    cl_mem landtypeRgn = nullptr;
    if (landtypeLayer && landtypeLayer->supportsRegion()) {
        landtypeRgn = landtypeLayer->getColorRegion(
            lonMinRad, lonMaxRad, latMinRad, latMaxRad, resX, resY, nullptr);
    }

    cl_mem elevationRgn = nullptr;
    if (elevationLayer && elevationLayer->supportsRegion()) {
        elevationRgn = elevationLayer->getColorRegion(
            lonMinRad, lonMaxRad, latMinRad, latMaxRad, resX, resY, nullptr);
    }

    if (landtypeRgn && elevationRgn) {
        multiplyColor(result, landtypeRgn, elevationRgn, resY, resX);
        OpenCLContext::get().releaseMem(landtypeRgn);
        OpenCLContext::get().releaseMem(elevationRgn);
    } else {
        // fallback: use whichever is available
        if (landtypeRgn) {
            result = landtypeRgn;
        } else if (elevationRgn) {
            result = elevationRgn;
        }
    }

    // Alpha-blend watertable
    cl_mem watertableRgn = nullptr;
    if (watertableLayer && watertableLayer->supportsRegion()) {
        watertableRgn = watertableLayer->getColorRegion(
            lonMinRad, lonMaxRad, latMinRad, latMaxRad, resX, resY, nullptr);
    }
    if (result && watertableRgn) {
        alphaBlend(result, result, watertableRgn, resY, resX);
        OpenCLContext::get().releaseMem(watertableRgn);
    } else if (watertableRgn) {
        OpenCLContext::get().releaseMem(watertableRgn);
    }

    // Alpha-blend temperature overlay
    if (temperatureLayer && temperatureLayer->supportsRegion()) {
        cl_mem tempSample = temperatureLayer->sampleRegion(
            lonMinRad, lonMaxRad, latMinRad, latMaxRad, resX, resY, nullptr);
        if (tempSample) {
            static std::vector<cl_float4> tempRamp = {
                MapLayer::rgb(255, 255, 255),
                MapLayer::rgba(0, 0, 0, 0)
            };
            static std::vector<float> weights = {0.8f, 1.0f};
            cl_mem tempColor = nullptr;
            weightedScalarToColor(tempColor, tempSample, resY, resX,
                                  static_cast<int>(tempRamp.size()), tempRamp, weights);
            OpenCLContext::get().releaseMem(tempSample);
            if (result && tempColor) {
                alphaBlend(result, result, tempColor, resY, resX);
                OpenCLContext::get().releaseMem(tempColor);
            } else if (tempColor) {
                OpenCLContext::get().releaseMem(tempColor);
            }
        }
    }

    // If result is still null, create a black fallback
    if (!result) {
        cl_int err = CL_SUCCESS;
        size_t count = static_cast<size_t>(resX) * resY;
        std::vector<cl_float4> black(count, {0.0f, 0.0f, 0.0f, 1.0f});
        result = OpenCLContext::get().createBuffer(
            CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
            count * sizeof(cl_float4), black.data(),
            &err, "ColorLayer fallback black");
    }

    return result;
}