#include <WorldMaps/Map/TemperatureLayer.hpp>
#include <WorldMaps/WorldMap.hpp>
#include <ctime>

TemperatureLayer::TemperatureLayer()
{
    seed = time(0);
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
        float frequency = 1.5f;
        float lacunarity = 2.0f;
        int octaves = 4;
        float persistence = 0.5f;
        unsigned int seed = seed;
        tempMap(temperatureBuffer,
                parentWorld->getWorldLatitudeResolution(),
                parentWorld->getWorldLongitudeResolution(),
                frequency,
                lacunarity,
                octaves,
                persistence,
                seed);
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