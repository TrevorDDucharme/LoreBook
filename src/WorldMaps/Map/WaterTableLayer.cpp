#include <WorldMaps/Map/WaterTableLayer.hpp>
#include <WorldMaps/WorldMap.hpp>

WaterTableLayer::~WaterTableLayer()
{
    if (watertableBuffer != nullptr)
    {
        OpenCLContext::get().releaseMem(watertableBuffer);
        watertableBuffer = nullptr;
    }
    if (coloredBuffer != nullptr)
    {
        OpenCLContext::get().releaseMem(coloredBuffer);
        coloredBuffer = nullptr;
    }
}
cl_mem WaterTableLayer::sample()
{
    return getWaterTableBuffer();
}

cl_mem WaterTableLayer::getColor()
{
    // build new cl_mem buffer with RGBA colors based on watertable data (gray scale, full alpha)
    cl_mem watertableBuffer = getWaterTableBuffer();
    cl_int err = CL_SUCCESS;
    // Convert watertable scalar values to grayscale RGBA colors
    static std::vector<cl_float4> grayRamp = {
        MapLayer::rgba(0, 0, 0, 0),
        MapLayer::rgba(0, 128, 128, 255),
        MapLayer::rgba(0, 0, 255, 255)};
    static std::vector<float> weights = {0.0f, 0.0f, 0.0f, 0.0f}; // black background, fully opaque
    if (coloredBuffer == nullptr)
    {
        waterTableWeightedScalarToColor(coloredBuffer, watertableBuffer,
            parentWorld->getWorldLatitudeResolution(),
            parentWorld->getWorldLongitudeResolution(),
            (int)grayRamp.size(), grayRamp, weights);
    }
    return coloredBuffer;
}

static cl_program gWaterTableProgram = nullptr;

cl_mem WaterTableLayer::getWaterTableBuffer()
{
    if (watertableBuffer == nullptr)
    {
        static cl_kernel gWaterTableKernel = nullptr;
        ZoneScopedN("WaterTableLayer::getWaterTableBuffer");
        if (!OpenCLContext::get().isReady())
            return nullptr;

        try
        {
            OpenCLContext::get().createProgram(gWaterTableProgram,"Kernels/WaterTable.cl");
            OpenCLContext::get().createKernelFromProgram(gWaterTableKernel,gWaterTableProgram,"water_table");
        }
        catch (const std::runtime_error &e)
        {
            printf("Error initializing WaterTableLayer OpenCL: %s\n", e.what());
            return nullptr;
        }

        // get elevation buffer from parent world
        cl_mem elevationBuffer = nullptr;
        MapLayer* elevationLayer = parentWorld->getLayer("elevation");
        if(elevationLayer){
            elevationBuffer = elevationLayer->sample();
        }
        if(elevationBuffer == nullptr){
            printf("WaterTableLayer::getWaterTableBuffer: elevation layer not found or has no data\n");
            return nullptr;
        }
        int latitudeResolution = parentWorld->getWorldLatitudeResolution();
        int longitudeResolution = parentWorld->getWorldLongitudeResolution();
        cl_int err = CL_SUCCESS;
        size_t voxels = (size_t)latitudeResolution * (size_t)longitudeResolution;
        size_t outSize = voxels * sizeof(cl_float);
        watertableBuffer = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, outSize, nullptr, &err, "WaterTableLayer watertableBuffer");
        if (err != CL_SUCCESS || watertableBuffer == nullptr)
        {
            printf("WaterTableLayer::getWaterTableBuffer: Failed to create watertableBuffer\n");
            return nullptr;
        }
        clSetKernelArg(gWaterTableKernel, 0, sizeof(cl_mem), &watertableBuffer);
        clSetKernelArg(gWaterTableKernel, 1, sizeof(cl_mem), &elevationBuffer);
        clSetKernelArg(gWaterTableKernel, 2, sizeof(int), &latitudeResolution);
        clSetKernelArg(gWaterTableKernel, 3, sizeof(int), &longitudeResolution);
        clSetKernelArg(gWaterTableKernel, 4, sizeof(float), &water_table_level);
        size_t global[2] = {(size_t)latitudeResolution, (size_t)longitudeResolution};

        {
            ZoneScopedN("WaterTableLayer::getWaterTableBuffer Enqueue");
            err = clEnqueueNDRangeKernel(
                OpenCLContext::get().getQueue(),
                gWaterTableKernel,
                2,
                nullptr,
                global,
                nullptr,
                0,
                nullptr,
                nullptr);
            if (err != CL_SUCCESS)
            {
                printf("WaterTableLayer::getWaterTableBuffer: Failed to enqueue water_table kernel\n");
                OpenCLContext::get().releaseMem(watertableBuffer);
                watertableBuffer = nullptr;
                return nullptr;
            }
        }
    }
    return watertableBuffer;
}

void WaterTableLayer::waterTableWeightedScalarToColor(cl_mem& output, 
                            cl_mem scalarBuffer,
                            int latitudeResolution,
                            int longitudeResolution,
                            int colorCount,
                            const std::vector<cl_float4> &paletteColors,
                            const std::vector<float> &weights)
{
    ZoneScopedN("WeightedScalarToColor");
    if (!OpenCLContext::get().isReady())
        return;

    cl_context ctx = OpenCLContext::get().getContext();
    cl_device_id device = OpenCLContext::get().getDevice();
    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;
    static cl_kernel gWeightedScalarToColorKernel = nullptr;
    try{
        OpenCLContext::get().createProgram(gWaterTableProgram,"Kernels/WaterTable.cl");
        OpenCLContext::get().createKernelFromProgram(gWeightedScalarToColorKernel,gWaterTableProgram,"water_table_weighted_scalar_to_rgba_float4");
    }
    catch (const std::runtime_error &e)
    {
        printf("Error initializing WeightedScalarToColor OpenCL: %s\n", e.what());
        return;
    }
    // create palette buffer
    cl_mem paletteBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(cl_float4) * paletteColors.size(), (void*)paletteColors.data(), &err, "weightedScalarToColor paletteBuf");
    if (err != CL_SUCCESS || paletteBuf == nullptr)
    {
        throw std::runtime_error("clCreateBuffer failed for weightedScalarToColor paletteBuf");
    }
    // create weights buffer
    cl_mem weightsBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(float) * weights.size(), (void*)weights.data(), &err, "weightedScalarToColor weightsBuf");
    if (err != CL_SUCCESS || weightsBuf == nullptr)
    {
        OpenCLContext::get().releaseMem(paletteBuf);
        throw std::runtime_error("clCreateBuffer failed for weightedScalarToColor weightsBuf");
    }
    size_t voxels = (size_t)latitudeResolution * (size_t)longitudeResolution;
    size_t outSize = voxels * sizeof(cl_float4);
    if(output != nullptr){
        TracyMessageL("Reallocating weightedScalarToColor output buffer required");
        cl_int err = clGetMemObjectInfo(output,
                                        CL_MEM_SIZE,
                                        sizeof(size_t),
                                        &outSize,
                                        NULL);
        if (err != CL_SUCCESS) {
            throw std::runtime_error("clGetMemObjectInfo failed for weightedScalarToColor output buffer size");
        }
        if(outSize < voxels * sizeof(cl_float4)){
            OpenCLContext::get().releaseMem(output);
            output = nullptr;
        }
    }
    if(output == nullptr){
        TracyMessageL("Allocating weightedScalarToColor output buffer");
        output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, outSize, nullptr, &err, "weightedScalarToColor output");
        if (err != CL_SUCCESS || output == nullptr)
        {
            OpenCLContext::get().releaseMem(paletteBuf);
            OpenCLContext::get().releaseMem(weightsBuf);
            throw std::runtime_error("clCreateBuffer failed for weightedScalarToColor output");
        }
    }
    clSetKernelArg(gWeightedScalarToColorKernel, 0, sizeof(cl_mem), &scalarBuffer);
    clSetKernelArg(gWeightedScalarToColorKernel, 1, sizeof(int), &latitudeResolution);
    clSetKernelArg(gWeightedScalarToColorKernel, 2, sizeof(int), &longitudeResolution);
    clSetKernelArg(gWeightedScalarToColorKernel, 3, sizeof(int), &colorCount);
    clSetKernelArg(gWeightedScalarToColorKernel, 4, sizeof(cl_mem), &paletteBuf);
    clSetKernelArg(gWeightedScalarToColorKernel, 5, sizeof(cl_mem), &weightsBuf);
    clSetKernelArg(gWeightedScalarToColorKernel, 6, sizeof(cl_mem), &output);
    size_t global[2] = {(size_t)latitudeResolution, (size_t)longitudeResolution};
    
    {
        ZoneScopedN("WeightedScalarToColor Enqueue");
        err = clEnqueueNDRangeKernel(queue, gWeightedScalarToColorKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
    }
    OpenCLContext::get().releaseMem(paletteBuf);
    OpenCLContext::get().releaseMem(weightsBuf);
}