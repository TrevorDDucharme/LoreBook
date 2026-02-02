#include <WorldMaps/Map/HumidityLayer.hpp>
#include <WorldMaps/WorldMap.hpp>
#include <tracy/Tracy.hpp>
#include <plog/Log.h>

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
    static std::vector<cl_float4> grayRamp = {
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
    // Get scalar data from other layers
    cl_mem elevation = parentWorld->getLayer("elevation")->sample();
    cl_mem watertable = parentWorld->getLayer("watertable")->sample();
    cl_mem rivers = parentWorld->getLayer("rivers")->sample();
    cl_mem temperature = parentWorld->getLayer("temperature")->sample();
    
    // Get LandTypeLayer to access properties
    LandTypeLayer* landtypeLayer = dynamic_cast<LandTypeLayer*>(parentWorld->getLayer("landtype"));
    if (!landtypeLayer) {
        throw std::runtime_error("HumidityLayer: landtype layer not found or wrong type");
    }

    if (humidityBuffer == nullptr)
    {
        // Perlin parameters for landtype sampling (match LandTypeLayer defaults)
        std::vector<float> frequency(5, 1.5f);
        std::vector<float> lacunarity(5, 2.0f);
        std::vector<int> octaves(5, 8);
        std::vector<float> persistence(5, 0.5f);
        std::vector<unsigned int> seed(5, 12345u);
        for(int i=0; i<5; ++i) {
            seed[i] += i * 100;
        }
        
        humidityMap(humidityBuffer,
                    parentWorld->getWorldLatitudeResolution(),
                    parentWorld->getWorldLongitudeResolution(),
                    landtypeLayer->getLandtypes(),
                    landtypeLayer->getLandtypeCount(),
                    frequency,
                    lacunarity,
                    octaves,
                    persistence,
                    seed,
                    elevation,
                    watertable,
                    rivers,
                    temperature);
    }
    return humidityBuffer;
}

void HumidityLayer::humidityMap(cl_mem &output,
                                int latitudeResolution,
                                int longitudeResolution,
                                const std::vector<LandTypeLayer::LandTypeProperties> &landtypeProperties,
                                int landtypeCount,
                                const std::vector<float> &frequency,
                                const std::vector<float> &lacunarity,
                                const std::vector<int> &octaves,
                                const std::vector<float> &persistence,
                                const std::vector<unsigned int> &seed,
                                cl_mem elevation,
                                cl_mem watertable,
                                cl_mem rivers,
                                cl_mem temperature)
{
    ZoneScopedN("HumidityLayer::humidityMap");
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
        OpenCLContext::get().createProgram(program, "Kernels/Humidity.cl");
        OpenCLContext::get().createKernelFromProgram(kernel, program, "humidity_map");
    }
    catch (const std::runtime_error &e)
    {
        printf("Error initializing HumidityLayer OpenCL: %s\n", e.what());
        return;
    }

    size_t outSize = voxels * sizeof(cl_float);
    size_t bufferSize;
    if (output != nullptr)
    {
        cl_int err = clGetMemObjectInfo(output,
                                        CL_MEM_SIZE,
                                        sizeof(size_t),
                                        &bufferSize,
                                        NULL);
        if (err != CL_SUCCESS)
        {
            printf("HumidityLayer::humidityMap: Failed to query output buffer size\n");
        }
        if (bufferSize < outSize)
        {
            OpenCLContext::get().releaseMem(output);
            output = nullptr;
        }
    }

    if (output == nullptr)
    {
        output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, outSize, nullptr, &err, "HumidityMap output");
        if (err != CL_SUCCESS || output == nullptr)
        {
            throw std::runtime_error("clCreateBuffer failed for HumidityMap output");
        }
    }

    // Create buffers for landtype properties and perlin parameters
    cl_mem propertiesBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 
        sizeof(LandTypeLayer::LandTypeProperties) * landtypeProperties.size(), 
        (void*)landtypeProperties.data(), &err, "HumidityMap propertiesBuf");
    if (err != CL_SUCCESS) throw std::runtime_error("Failed to create properties buffer");
    
    cl_mem frequencyBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        sizeof(float) * frequency.size(), (void*)frequency.data(), &err, "HumidityMap frequencyBuf");
    if (err != CL_SUCCESS) { OpenCLContext::get().releaseMem(propertiesBuf); throw std::runtime_error("Failed to create frequency buffer"); }
    
    cl_mem lacunarityBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        sizeof(float) * lacunarity.size(), (void*)lacunarity.data(), &err, "HumidityMap lacunarityBuf");
    if (err != CL_SUCCESS) { OpenCLContext::get().releaseMem(propertiesBuf); OpenCLContext::get().releaseMem(frequencyBuf); throw std::runtime_error("Failed to create lacunarity buffer"); }
    
    cl_mem octavesBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        sizeof(int) * octaves.size(), (void*)octaves.data(), &err, "HumidityMap octavesBuf");
    if (err != CL_SUCCESS) { OpenCLContext::get().releaseMem(propertiesBuf); OpenCLContext::get().releaseMem(frequencyBuf); OpenCLContext::get().releaseMem(lacunarityBuf); throw std::runtime_error("Failed to create octaves buffer"); }
    
    cl_mem persistenceBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        sizeof(float) * persistence.size(), (void*)persistence.data(), &err, "HumidityMap persistenceBuf");
    if (err != CL_SUCCESS) { OpenCLContext::get().releaseMem(propertiesBuf); OpenCLContext::get().releaseMem(frequencyBuf); OpenCLContext::get().releaseMem(lacunarityBuf); OpenCLContext::get().releaseMem(octavesBuf); throw std::runtime_error("Failed to create persistence buffer"); }
    
    cl_mem seedBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        sizeof(unsigned int) * seed.size(), (void*)seed.data(), &err, "HumidityMap seedBuf");
    if (err != CL_SUCCESS) { OpenCLContext::get().releaseMem(propertiesBuf); OpenCLContext::get().releaseMem(frequencyBuf); OpenCLContext::get().releaseMem(lacunarityBuf); OpenCLContext::get().releaseMem(octavesBuf); OpenCLContext::get().releaseMem(persistenceBuf); throw std::runtime_error("Failed to create seed buffer"); }

    clSetKernelArg(kernel, 0, sizeof(int), &latitudeResolution);
    clSetKernelArg(kernel, 1, sizeof(int), &longitudeResolution);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &propertiesBuf);
    clSetKernelArg(kernel, 3, sizeof(int), &landtypeCount);
    clSetKernelArg(kernel, 4, sizeof(cl_mem), &frequencyBuf);
    clSetKernelArg(kernel, 5, sizeof(cl_mem), &lacunarityBuf);
    clSetKernelArg(kernel, 6, sizeof(cl_mem), &octavesBuf);
    clSetKernelArg(kernel, 7, sizeof(cl_mem), &persistenceBuf);
    clSetKernelArg(kernel, 8, sizeof(cl_mem), &seedBuf);
    clSetKernelArg(kernel, 9, sizeof(cl_mem), &elevation);
    clSetKernelArg(kernel, 10, sizeof(cl_mem), &watertable);
    clSetKernelArg(kernel, 11, sizeof(cl_mem), &rivers);
    clSetKernelArg(kernel, 12, sizeof(cl_mem), &temperature);
    clSetKernelArg(kernel, 13, sizeof(cl_mem), &output);

    size_t global[2] = {(size_t)latitudeResolution, (size_t)longitudeResolution};
    {
        ZoneScopedN("HumidityLayer::humidityMap enqueue kernel");
        err = clEnqueueNDRangeKernel(queue, kernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS)
        {
            printf("HumidityLayer::humidityMap: Failed to enqueue kernel: %d\n", err);
        }
    }
    
    // Clean up parameter buffers
    OpenCLContext::get().releaseMem(propertiesBuf);
    OpenCLContext::get().releaseMem(frequencyBuf);
    OpenCLContext::get().releaseMem(lacunarityBuf);
    OpenCLContext::get().releaseMem(octavesBuf);
    OpenCLContext::get().releaseMem(persistenceBuf);
    OpenCLContext::get().releaseMem(seedBuf);
}