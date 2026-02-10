#include <WorldMaps/Map/TemperatureLayer.hpp>
#include <WorldMaps/WorldMap.hpp>
#include <WorldMaps/World/LayerDelta.hpp>
#include <ctime>
#include <cmath>

TemperatureLayer::TemperatureLayer()
{
    seed_ = static_cast<unsigned int>(time(0));
}

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
cl_mem TemperatureLayer::sample()
{
    return getTemperatureBuffer();
}

cl_mem TemperatureLayer::getColor()
{
    // build new cl_mem buffer with RGBA colors based on temperature data (gray scale, full alpha)
    cl_mem temperatureBuffer = getTemperatureBuffer();
    cl_int err = CL_SUCCESS;
    // Convert temperature scalar values to grayscale RGBA colors
    static std::vector<cl_float4> grayRamp = {
        MapLayer::rgba(0, 0, 255, 255/3),
        MapLayer::rgba(255, 0, 0, 255/3)};
    if (coloredBuffer == nullptr)
    {
        scalarToColor(coloredBuffer, temperatureBuffer, parentWorld->getWorldLatitudeResolution(), parentWorld->getWorldLongitudeResolution(), grayRamp.size(), grayRamp);
    }
    return coloredBuffer;
}

cl_mem TemperatureLayer::getTemperatureBuffer()
{
    cl_mem landtype = parentWorld->getLayer("landtype")->getColor();
    cl_mem elevation = parentWorld->getLayer("elevation")->getColor();
    cl_mem watertable = parentWorld->getLayer("watertable")->getColor();
    cl_mem river = parentWorld->getLayer("rivers")->getColor();
    cl_mem latitude = parentWorld->getLayer("latitude")->getColor();

    if (temperatureBuffer == nullptr)
    {
        tempMap(temperatureBuffer,
                parentWorld->getWorldLatitudeResolution(),
                parentWorld->getWorldLongitudeResolution(),
                frequency_,
                lacunarity_,
                octaves_,
                persistence_,
                seed_);
    }
    return temperatureBuffer;
}

void TemperatureLayer::tempMap(cl_mem &output,
             int latitudeResolution,
             int longitudeResolution,
             float frequency,
             float lacunarity,
             int octaves,
             float persistence,
             unsigned int seed)
{
    if (!OpenCLContext::get().isReady())
        return;

    cl_context ctx = OpenCLContext::get().getContext();
    cl_device_id device = OpenCLContext::get().getDevice();
    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;

    size_t voxels = (size_t)latitudeResolution * (size_t)longitudeResolution;

    static cl_program program = nullptr;
    static cl_kernel kernel = nullptr;
    try
    {
        OpenCLContext::get().createProgram(program, "Kernels/Temperature.cl");
        OpenCLContext::get().createKernelFromProgram(kernel, program, "temperature_map");
    }
    catch (const std::runtime_error &e)
    {
        printf("Error initializing LandTypeLayer OpenCL: %s\n", e.what());
        return;
    }

    size_t outSize = voxels * sizeof(cl_float4);
    size_t bufferSize;
    if (output != nullptr)
    {
        // ZoneScopedN("TemperatureLayer::tempMap check output buffer");
        cl_int err = clGetMemObjectInfo(output,
                                        CL_MEM_SIZE,
                                        sizeof(size_t),
                                        &bufferSize,
                                        NULL);
        if (bufferSize < outSize)
        {
            OpenCLContext::get().releaseMem(output);
            output = nullptr;
        }
    }

    if (output == nullptr)
    {
        // ZoneScopedN("TemperatureLayer::tempMap alloc output buffer");
        output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, outSize, nullptr, &err, "TemperatureMap output");
        if (err != CL_SUCCESS || output == nullptr)
        {
            throw std::runtime_error("clCreateBuffer failed for TemperatureMap output");
        }
    }


    clSetKernelArg(kernel, 0, sizeof(int), &latitudeResolution);
    clSetKernelArg(kernel, 1, sizeof(int), &longitudeResolution);
    clSetKernelArg(kernel, 2, sizeof(float), &frequency);
    clSetKernelArg(kernel, 3, sizeof(float), &lacunarity);
    clSetKernelArg(kernel, 4, sizeof(int), &octaves);
    clSetKernelArg(kernel, 5, sizeof(float), &persistence);
    clSetKernelArg(kernel, 6, sizeof(unsigned int), &seed);
    clSetKernelArg(kernel, 7, sizeof(cl_mem), &output);

    size_t global[2] = {(size_t)latitudeResolution, (size_t)longitudeResolution};
    {
        // ZoneScopedN("LandTypeLayer::landtypeColorMap enqueue kernel");
        err = clEnqueueNDRangeKernel(queue, kernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
    }
}

// ── Region-bounded generation ────────────────────────────────────

cl_mem TemperatureLayer::sampleRegion(float lonMinRad, float lonMaxRad,
                                       float latMinRad, float latMaxRad,
                                       int resX, int resY,
                                       const LayerDelta* delta)
{
    ZoneScopedN("TemperatureLayer::sampleRegion");
    if (!OpenCLContext::get().isReady()) return nullptr;

    // Apply parameter overrides from delta
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

    // Convert lon/lat bounds to theta/phi
    float thetaMin = static_cast<float>(M_PI / 2.0) - latMaxRad;
    float thetaMax = static_cast<float>(M_PI / 2.0) - latMinRad;
    float phiMin = lonMinRad + static_cast<float>(M_PI);
    float phiMax = lonMaxRad + static_cast<float>(M_PI);

    // Generate Perlin noise on GPU for this region
    cl_mem noiseBuf = nullptr;
    perlinRegion(noiseBuf, resY, resX,
                 thetaMin, thetaMax, phiMin, phiMax,
                 freq, lac, oct, pers, sd);
    if (!noiseBuf) return nullptr;

    // Read noise back to CPU
    size_t count = static_cast<size_t>(resX) * resY;
    std::vector<float> noiseData(count);
    cl_int err = clEnqueueReadBuffer(OpenCLContext::get().getQueue(), noiseBuf,
                                      CL_TRUE, 0, count * sizeof(float),
                                      noiseData.data(), 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        OpenCLContext::get().releaseMem(noiseBuf);
        return nullptr;
    }
    OpenCLContext::get().releaseMem(noiseBuf);

    // Compute temperature on CPU: latFactor * 0.5 + noise * 0.5
    // latFactor = 1.0 - |2 * latNorm - 1|, where latNorm ∈ [0,1] (north→south)
    // latFactor is 0 at poles, 1 at equator
    std::vector<float> tempData(count);
    for (int row = 0; row < resY; ++row) {
        float lat = latMaxRad - static_cast<float>(row) / std::max(resY - 1, 1)
                                * (latMaxRad - latMinRad);
        float latNorm = (static_cast<float>(M_PI / 2.0) - lat) / static_cast<float>(M_PI);
        float latFactor = (1.0f - std::fabs(2.0f * latNorm - 1.0f)) * 0.5f;

        for (int col = 0; col < resX; ++col) {
            int idx = row * resX + col;
            tempData[idx] = latFactor + noiseData[idx] * 0.5f;
        }
    }

    // Upload result to GPU
    cl_mem regionBuf = OpenCLContext::get().createBuffer(
        CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
        count * sizeof(float), tempData.data(),
        &err, "TemperatureLayer sampleRegion");
    if (err != CL_SUCCESS || !regionBuf) return nullptr;

    // Apply per-sample deltas if present
    if (delta && !delta->data.empty() &&
        delta->resolution == resX && delta->resolution == resY) {
        size_t deltaSize = delta->data.size() * sizeof(float);
        cl_mem deltaBuf = OpenCLContext::get().createBuffer(
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            deltaSize, const_cast<float*>(delta->data.data()),
            &err, "temperature delta upload");
        if (err == CL_SUCCESS && deltaBuf) {
            applyDeltaScalar(regionBuf, deltaBuf, resY, resX,
                              static_cast<int>(delta->mode));
            OpenCLContext::get().releaseMem(deltaBuf);
        }
    }

    return regionBuf;
}

cl_mem TemperatureLayer::getColorRegion(float lonMinRad, float lonMaxRad,
                                         float latMinRad, float latMaxRad,
                                         int resX, int resY,
                                         const LayerDelta* delta)
{
    ZoneScopedN("TemperatureLayer::getColorRegion");

    cl_mem scalarBuf = sampleRegion(lonMinRad, lonMaxRad,
                                     latMinRad, latMaxRad,
                                     resX, resY, delta);
    if (!scalarBuf) return nullptr;

    static std::vector<cl_float4> tempRamp = {
        MapLayer::rgba(0, 0, 255, 255/3),
        MapLayer::rgba(255, 0, 0, 255/3)
    };

    cl_mem colorBuf = nullptr;
    scalarToColor(colorBuf, scalarBuf, resY, resX,
                  static_cast<int>(tempRamp.size()), tempRamp);

    return colorBuf;
}