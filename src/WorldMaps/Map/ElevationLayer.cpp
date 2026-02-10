#include <WorldMaps/Map/ElevationLayer.hpp>
#include <WorldMaps/WorldMap.hpp>
#include <WorldMaps/World/LayerDelta.hpp>

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
cl_mem ElevationLayer::sample()
{
    return getElevationBuffer();
}

cl_mem ElevationLayer::getColor()
{
    // build new cl_mem buffer with RGBA colors based on elevation data (gray scale, full alpha)
    cl_mem elevationBuffer = getElevationBuffer();
    cl_int err = CL_SUCCESS;
    // Convert elevation scalar values to grayscale RGBA colors
    static std::vector<cl_float4> grayRamp = {
        MapLayer::rgba(0, 0, 0, 255),
        MapLayer::rgba(255, 255, 255, 255)};
    if (coloredBuffer == nullptr)
    {
        scalarToColor(coloredBuffer, elevationBuffer, parentWorld->getWorldLatitudeResolution(), parentWorld->getWorldLongitudeResolution(), 2, grayRamp);
    }
    return coloredBuffer;
}

cl_mem ElevationLayer::getElevationBuffer()
{
    if (elevationBuffer == nullptr)
    {
        perlin(elevationBuffer,
            parentWorld->getWorldLatitudeResolution(), 
            parentWorld->getWorldLongitudeResolution(), 
            1.5f, 
            2.0f, 
            8, 
            0.5f, 
            12345u);
    }
    return elevationBuffer;
}

// ── Region-bounded generation ────────────────────────────────────

cl_mem ElevationLayer::sampleRegion(float lonMinRad, float lonMaxRad,
                                     float latMinRad, float latMaxRad,
                                     int resX, int resY,
                                     const LayerDelta* delta)
{
    // Convert lon/lat bounds to theta/phi for the Perlin sphere sampling
    float thetaMin, thetaMax, phiMin, phiMax;
    // theta = colatitude: 0 at north pole (lat = π/2), π at south pole (lat = -π/2)
    // For the buffer: row 0 should be the northernmost → smallest theta
    thetaMin = static_cast<float>(M_PI / 2.0) - latMaxRad; // north edge → small theta
    thetaMax = static_cast<float>(M_PI / 2.0) - latMinRad; // south edge → large theta
    // phi = azimuth: lon ∈ [-π,π] → phi ∈ [0,2π]
    phiMin = lonMinRad + static_cast<float>(M_PI);
    phiMax = lonMaxRad + static_cast<float>(M_PI);

    // Apply parameter overrides from delta if present
    float freq = frequency_;
    float lac  = lacunarity_;
    int   oct  = octaves_;
    float pers = persistence_;
    unsigned int sd = seed_;
    if (delta) {
        freq = delta->getParam("frequency",   freq);
        lac  = delta->getParam("lacunarity",  lac);
        oct  = static_cast<int>(delta->getParam("octaves", static_cast<float>(oct)));
        pers = delta->getParam("persistence", pers);
        sd   = static_cast<unsigned int>(delta->getParam("seed", static_cast<float>(sd)));
    }

    // Generate noise for this sub-region
    cl_mem regionBuf = nullptr;
    perlinRegion(regionBuf, resY, resX,
                 thetaMin, thetaMax, phiMin, phiMax,
                 freq, lac, oct, pers, sd);

    // Apply per-sample deltas if present
    if (delta && !delta->data.empty() &&
        delta->resolution == resX && delta->resolution == resY) {
        cl_int err = CL_SUCCESS;
        size_t deltaSize = delta->data.size() * sizeof(float);
        cl_mem deltaBuf = OpenCLContext::get().createBuffer(
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            deltaSize, const_cast<float*>(delta->data.data()),
            &err, "elevation delta upload");
        if (err == CL_SUCCESS && deltaBuf) {
            applyDeltaScalar(regionBuf, deltaBuf, resY, resX,
                              static_cast<int>(delta->mode));
            OpenCLContext::get().releaseMem(deltaBuf);
        }
    }

    return regionBuf;
}

cl_mem ElevationLayer::getColorRegion(float lonMinRad, float lonMaxRad,
                                       float latMinRad, float latMaxRad,
                                       int resX, int resY,
                                       const LayerDelta* delta)
{
    // Generate scalar elevation for this region
    cl_mem scalarBuf = sampleRegion(lonMinRad, lonMaxRad,
                                     latMinRad, latMaxRad,
                                     resX, resY, delta);
    if (!scalarBuf) return nullptr;

    // Convert to grayscale RGBA
    static std::vector<cl_float4> grayRamp = {
        MapLayer::rgba(0, 0, 0, 255),
        MapLayer::rgba(255, 255, 255, 255)
    };

    cl_mem colorBuf = nullptr;
    scalarToColor(colorBuf, scalarBuf, resY, resX, 2, grayRamp);

    // The scalarBuf was allocated by perlinRegion — it's returned as the
    // chunk's sampleBuffer and will be managed by the chunk cache.
    // The colorBuf is what we return as the chunk's colorBuffer.

    return colorBuf;
}