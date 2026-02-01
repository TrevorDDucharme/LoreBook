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
    static std::vector<std::array<uint8_t, 4>> grayRamp = {
        MapLayer::rgba(0, 0, 0, 255),
        MapLayer::rgba(0, 128, 128, 255),
        MapLayer::rgba(0, 0, 255, 255)};
    static std::vector<float> weights = {0.0f, 0.0f, 0.0f, 1.0f}; // black background, fully opaque
    if (coloredBuffer == nullptr)
    {
        // pass grayRamp.size() here (3), not 2
        weightedScalarToColor(coloredBuffer, watertableBuffer,
            parentWorld->getWorldWidth(),
            parentWorld->getWorldHeight(),
            parentWorld->getWorldDepth(),
            (int)grayRamp.size(), grayRamp, weights);
    }
    return coloredBuffer;
}

cl_mem WaterTableLayer::getWaterTableBuffer()
{
    if (watertableBuffer == nullptr)
    {
        static cl_program gWaterTableProgram = nullptr;
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
        int fieldW = parentWorld->getWorldWidth();
        int fieldH = parentWorld->getWorldHeight();
        int fieldD = parentWorld->getWorldDepth();
        cl_int err = CL_SUCCESS;
        size_t voxels = (size_t)fieldW * (size_t)fieldH * (size_t)fieldD;
        size_t outSize = voxels * sizeof(cl_float);
        watertableBuffer = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, outSize, nullptr, &err, "WaterTableLayer watertableBuffer");
        if (err != CL_SUCCESS || watertableBuffer == nullptr)
        {
            printf("WaterTableLayer::getWaterTableBuffer: Failed to create watertableBuffer\n");
            return nullptr;
        }
        clSetKernelArg(gWaterTableKernel, 0, sizeof(cl_mem), &watertableBuffer);
        clSetKernelArg(gWaterTableKernel, 1, sizeof(cl_mem), &elevationBuffer);
        clSetKernelArg(gWaterTableKernel, 2, sizeof(int), &fieldW);
        clSetKernelArg(gWaterTableKernel, 3, sizeof(int), &fieldH);
        clSetKernelArg(gWaterTableKernel, 4, sizeof(int), &fieldD);
        clSetKernelArg(gWaterTableKernel, 5, sizeof(float), &water_table_level);
        size_t global[3] = {(size_t)fieldW, (size_t)fieldH, (size_t)fieldD};

        {
            ZoneScopedN("WaterTableLayer::getWaterTableBuffer Enqueue");
            err = clEnqueueNDRangeKernel(
                OpenCLContext::get().getQueue(),
                gWaterTableKernel,
                3,
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