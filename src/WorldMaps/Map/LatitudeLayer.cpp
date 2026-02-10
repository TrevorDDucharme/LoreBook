#include <WorldMaps/Map/LatitudeLayer.hpp>
#include <WorldMaps/WorldMap.hpp>
#include <WorldMaps/World/LayerDelta.hpp>

LatitudeLayer::~LatitudeLayer()
{
    if (latitudeBuffer != nullptr)
    {
        OpenCLContext::get().releaseMem(latitudeBuffer);
        latitudeBuffer = nullptr;
    }
    if (coloredBuffer != nullptr)
    {
        OpenCLContext::get().releaseMem(coloredBuffer);
        coloredBuffer = nullptr;
    }
}
cl_mem LatitudeLayer::sample()
{
    return getLatitudeBuffer();
}

cl_mem LatitudeLayer::getColor()
{
    // build new cl_mem buffer with RGBA colors based on latitude data (gray scale, full alpha)
    cl_mem latitudeBuffer = getLatitudeBuffer();
    cl_int err = CL_SUCCESS;
    // Convert latitude scalar values to grayscale RGBA colors
    static std::vector<cl_float4> grayRamp = {
        MapLayer::rgba(0, 0, 0, 255),
        MapLayer::rgba(255, 255, 255, 255)};
    if (coloredBuffer == nullptr)
    {
        scalarToColor(coloredBuffer, latitudeBuffer, parentWorld->getWorldLatitudeResolution(), parentWorld->getWorldLongitudeResolution(), 2, grayRamp);
    }
    return coloredBuffer;
}

cl_mem LatitudeLayer::getLatitudeBuffer()
{
    if (latitudeBuffer == nullptr)
    {
        latitude(latitudeBuffer, parentWorld->getWorldLatitudeResolution(), parentWorld->getWorldLongitudeResolution());
    }
    return latitudeBuffer;
}

void LatitudeLayer::latitude(
    cl_mem &output,
    int latitudeResolution,
    int longitudeResolution)
{
    static cl_kernel gLatitude = nullptr;
    static cl_program gLatitudeProgram = nullptr;
    ZoneScopedN("LatitudeLayer::latitude");
    if (!OpenCLContext::get().isReady())
        return;

    try
    {
        OpenCLContext::get().createProgram(gLatitudeProgram, "Kernels/Latitude.cl");
        OpenCLContext::get().createKernelFromProgram(gLatitude, gLatitudeProgram, "latitude");
    }
    catch (const std::runtime_error &e)
    {
        printf("Error initializing LatitudeLayer OpenCL: %s\n", e.what());
        return;
    }
    cl_context ctx = OpenCLContext::get().getContext();
    cl_device_id device = OpenCLContext::get().getDevice();
    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;

    {
        ZoneScopedN("Latitude Buffer Alloc");
        size_t total = (size_t)latitudeResolution * (size_t)longitudeResolution * sizeof(cl_float);
        size_t buffer_size;
        if (output != nullptr)
        {
            cl_int err = clGetMemObjectInfo(output,
                                            CL_MEM_SIZE,
                                            sizeof(buffer_size),
                                            &buffer_size,
                                            NULL);
            if (err != CL_SUCCESS)
            {
                throw std::runtime_error("clGetMemObjectInfo failed for latitude output buffer size");
            }
            if (buffer_size < total)
            {
                TracyMessageL("Reallocating latitude output buffer required");
                OpenCLContext::get().releaseMem(output);
                output = nullptr;
            }
        }

        if (output == nullptr)
        {
            TracyMessageL("Allocating latitude output buffer");
            output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, total, nullptr, &err, "latitude output");
            if (err != CL_SUCCESS || output == nullptr)
            {
                throw std::runtime_error("clCreateBuffer failed for latitude output");
            }
        }
    }

    clSetKernelArg(gLatitude, 0, sizeof(cl_mem), &output);
    clSetKernelArg(gLatitude, 1, sizeof(int), &latitudeResolution);
    clSetKernelArg(gLatitude, 2, sizeof(int), &longitudeResolution);
    size_t global[2] = {(size_t)latitudeResolution, (size_t)longitudeResolution};
    {
        ZoneScopedN("Latitude Enqueue");
        err = clEnqueueNDRangeKernel(queue, gLatitude, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS)
        {
            throw std::runtime_error("clEnqueueNDRangeKernel failed for latitude");
        }
    }
}

// ── Region-bounded generation ────────────────────────────────────

cl_mem LatitudeLayer::sampleRegion(float lonMinRad, float lonMaxRad,
                                    float latMinRad, float latMaxRad,
                                    int resX, int resY,
                                    const LayerDelta* delta)
{
    ZoneScopedN("LatitudeLayer::sampleRegion");
    if (!OpenCLContext::get().isReady()) return nullptr;

    // Latitude is purely analytical: value = (π/2 - lat) / π
    // 0.0 at north pole (lat = π/2), 1.0 at south pole (lat = -π/2)
    size_t count = static_cast<size_t>(resX) * resY;
    std::vector<float> cpuBuf(count);

    for (int row = 0; row < resY; ++row) {
        // Row 0 = northernmost edge (latMax), row resY-1 = southernmost (latMin)
        float lat = latMaxRad - static_cast<float>(row) / std::max(resY - 1, 1)
                                * (latMaxRad - latMinRad);
        float val = (static_cast<float>(M_PI / 2.0) - lat) / static_cast<float>(M_PI);
        for (int col = 0; col < resX; ++col) {
            cpuBuf[row * resX + col] = val;
        }
    }

    cl_int err = CL_SUCCESS;
    cl_mem regionBuf = OpenCLContext::get().createBuffer(
        CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
        count * sizeof(float), cpuBuf.data(),
        &err, "LatitudeLayer sampleRegion");
    if (err != CL_SUCCESS || !regionBuf) return nullptr;

    // Apply per-sample deltas if present
    if (delta && !delta->data.empty() &&
        delta->resolution == resX && delta->resolution == resY) {
        size_t deltaSize = delta->data.size() * sizeof(float);
        cl_mem deltaBuf = OpenCLContext::get().createBuffer(
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            deltaSize, const_cast<float*>(delta->data.data()),
            &err, "latitude delta upload");
        if (err == CL_SUCCESS && deltaBuf) {
            applyDeltaScalar(regionBuf, deltaBuf, resY, resX,
                              static_cast<int>(delta->mode));
            OpenCLContext::get().releaseMem(deltaBuf);
        }
    }

    return regionBuf;
}

cl_mem LatitudeLayer::getColorRegion(float lonMinRad, float lonMaxRad,
                                      float latMinRad, float latMaxRad,
                                      int resX, int resY,
                                      const LayerDelta* delta)
{
    ZoneScopedN("LatitudeLayer::getColorRegion");

    cl_mem scalarBuf = sampleRegion(lonMinRad, lonMaxRad,
                                     latMinRad, latMaxRad,
                                     resX, resY, delta);
    if (!scalarBuf) return nullptr;

    static std::vector<cl_float4> grayRamp = {
        MapLayer::rgba(0, 0, 0, 255),
        MapLayer::rgba(255, 255, 255, 255)
    };

    cl_mem colorBuf = nullptr;
    scalarToColor(colorBuf, scalarBuf, resY, resX, 2, grayRamp);

    return colorBuf;
}