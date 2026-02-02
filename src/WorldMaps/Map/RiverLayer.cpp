#include <WorldMaps/Map/RiverLayer.hpp>
#include <WorldMaps/WorldMap.hpp>
#include <algorithm>
#include <cmath>

RiverLayer::~RiverLayer()
{
    if (riverBuffer != nullptr)
    {
        OpenCLContext::get().releaseMem(riverBuffer);
        riverBuffer = nullptr;
    }
    if (coloredBuffer != nullptr)
    {
        OpenCLContext::get().releaseMem(coloredBuffer);
        coloredBuffer = nullptr;
    }
}
cl_mem RiverLayer::sample()
{
    return getRiverBuffer();
}

cl_mem RiverLayer::getColor()
{
    // build new cl_mem buffer with RGBA colors based on river data (gray scale, full alpha)
    cl_mem riverBuffer = getRiverBuffer();
    cl_int err = CL_SUCCESS;
    // Convert river scalar values to a blue ramp with transparency
    static std::vector<cl_float4> blueRamp = {
        MapLayer::rgba(0, 0, 0, 0),       // black (no river)
        MapLayer::rgba(0, 128, 255, 255)    // blue
    };
    static std::vector<float> weights = {1.0f, 1.0f};
    if (coloredBuffer == nullptr)
    {
        weightedScalarToColor(coloredBuffer, riverBuffer, parentWorld->getWorldLatitudeResolution(), parentWorld->getWorldLongitudeResolution(), 2, blueRamp, weights);
    }
    return coloredBuffer;
}

cl_mem RiverLayer::getRiverBuffer()
{
    if (riverBuffer == nullptr)
    {
        generateRiverPaths(parentWorld->getLayer("elevation")->sample(),
                           parentWorld->getLayer("watertable")->sample(),
                           riverBuffer,
                           parentWorld->getWorldLatitudeResolution(),
                           parentWorld->getWorldLongitudeResolution(),
                           100); // maxSteps
    }
    return riverBuffer;
}

void RiverLayer::generateRiverPaths(cl_mem elevationBuf,
                               cl_mem waterTableBuf,
                               cl_mem& riverBuf,
                               int latitudeResolution, int longitudeResolution,
                               uint32_t maxSteps)
{
    ZoneScopedN("GenerateRiverPaths");
    if (!OpenCLContext::get().isReady()) return;

    cl_int err = CL_SUCCESS;
    static cl_program gRiverPathsProgram = nullptr;
    static cl_kernel gRiverFlowSourceKernel = nullptr;
    static cl_kernel gRiverFlowAccumulateKernel = nullptr;
    try {
        OpenCLContext::get().createProgram(gRiverPathsProgram, "Kernels/Rivers.cl");
        OpenCLContext::get().createKernelFromProgram(gRiverFlowSourceKernel, gRiverPathsProgram, "river_flow_source");
        OpenCLContext::get().createKernelFromProgram(gRiverFlowAccumulateKernel, gRiverPathsProgram, "river_flow_accumulate");
    } catch (const std::runtime_error &e) {
        printf("Error initializing RiverPaths OpenCL: %s\n", e.what());
        return;
    }

    size_t voxels = (size_t)latitudeResolution * (size_t)longitudeResolution;
    size_t outSize = voxels * sizeof(cl_float);

    // allocate & zero output if needed (use host-zero copy for simplicity)
    if (riverBuf != nullptr) {
        cl_int r = clGetMemObjectInfo(riverBuf, CL_MEM_SIZE, sizeof(size_t), &outSize, NULL);
        if (r != CL_SUCCESS || outSize < voxels * sizeof(cl_float)) {
            OpenCLContext::get().releaseMem(riverBuf);
            riverBuf = nullptr;
        }
    }
    if (riverBuf == nullptr) {
        std::vector<float> zeros(voxels, 0.0f);
        riverBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, outSize, zeros.data(), &err, "generateRiverPaths riverBuf");
        if (err != CL_SUCCESS || riverBuf == nullptr) {
            throw std::runtime_error("clCreateBuffer failed for generateRiverPaths riverBuf");
        }
    }

    cl_mem flowSourceBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, voxels * sizeof(cl_float), nullptr, &err, "generateRiverPaths flowSourceBuf");
    if (err != CL_SUCCESS || flowSourceBuf == nullptr) {
        throw std::runtime_error("clCreateBuffer failed for generateRiverPaths flowSourceBuf");
    }
    cl_mem flowAccBufA = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, voxels * sizeof(cl_float), nullptr, &err, "generateRiverPaths flowAccBufA");
    if (err != CL_SUCCESS || flowAccBufA == nullptr) {
        throw std::runtime_error("clCreateBuffer failed for generateRiverPaths flowAccBufA");
    }
    cl_mem flowAccBufB = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, voxels * sizeof(cl_float), nullptr, &err, "generateRiverPaths flowAccBufB");
    if (err != CL_SUCCESS || flowAccBufB == nullptr) {
        throw std::runtime_error("clCreateBuffer failed for generateRiverPaths flowAccBufB");
    }

    // Momentum buffers (store velocity as float2: dx, dy)
    cl_mem momentumBufA = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, voxels * sizeof(cl_float2), nullptr, &err, "generateRiverPaths momentumBufA");
    if (err != CL_SUCCESS || momentumBufA == nullptr) {
        throw std::runtime_error("clCreateBuffer failed for generateRiverPaths momentumBufA");
    }
    cl_mem momentumBufB = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, voxels * sizeof(cl_float2), nullptr, &err, "generateRiverPaths momentumBufB");
    if (err != CL_SUCCESS || momentumBufB == nullptr) {
        throw std::runtime_error("clCreateBuffer failed for generateRiverPaths momentumBufB");
    }
    // Zero initialize momentum buffers
    std::vector<cl_float2> zeroMomentum(voxels, {0.0f, 0.0f});
    err = clEnqueueWriteBuffer(OpenCLContext::get().getQueue(), momentumBufA, CL_FALSE, 0, voxels * sizeof(cl_float2), zeroMomentum.data(), 0, nullptr, nullptr);
    err = clEnqueueWriteBuffer(OpenCLContext::get().getQueue(), momentumBufB, CL_FALSE, 0, voxels * sizeof(cl_float2), zeroMomentum.data(), 0, nullptr, nullptr);

    // Step 1: create Flow Sources
    {
        float minHeight = 0.6f;
        float maxHeight = .61f;
        float chance = 0.001f;
        clSetKernelArg(gRiverFlowSourceKernel, 0, sizeof(cl_mem), &elevationBuf);
        clSetKernelArg(gRiverFlowSourceKernel, 1, sizeof(cl_mem), &waterTableBuf);
        clSetKernelArg(gRiverFlowSourceKernel, 2, sizeof(int), &latitudeResolution);
        clSetKernelArg(gRiverFlowSourceKernel, 3, sizeof(int), &longitudeResolution);
        clSetKernelArg(gRiverFlowSourceKernel, 4, sizeof(cl_mem), &flowSourceBuf);
        clSetKernelArg(gRiverFlowSourceKernel, 5, sizeof(float), &minHeight);
        clSetKernelArg(gRiverFlowSourceKernel, 6, sizeof(float), &maxHeight);
        clSetKernelArg(gRiverFlowSourceKernel, 7, sizeof(float), &chance);

        size_t global[2] = {(size_t)latitudeResolution, (size_t)longitudeResolution};
        err = clEnqueueNDRangeKernel(OpenCLContext::get().getQueue(), gRiverFlowSourceKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            printf("river_flow_source kernel enqueue failed: %d\n", err);
        }
    }
    // Step 3: Iteratively accumulate flow
    const int flowIterations = maxSteps;
    float river_threshold = 0.1f;
    float slope_epsilon = 0.0002f;
    float height_epsilon = 0.001f;
    float momentum_strength = 0.7f;  // How much rivers maintain their direction (0=no momentum, 1=all momentum)
    for (int iter = 0; iter < flowIterations; ++iter) {

        clSetKernelArg(gRiverFlowAccumulateKernel, 0, sizeof(cl_mem), &elevationBuf);
        clSetKernelArg(gRiverFlowAccumulateKernel, 1, sizeof(cl_mem), &waterTableBuf);
        clSetKernelArg(gRiverFlowAccumulateKernel, 2, sizeof(cl_mem), &flowSourceBuf);
        clSetKernelArg(gRiverFlowAccumulateKernel, 3, sizeof(int), &latitudeResolution);
        clSetKernelArg(gRiverFlowAccumulateKernel, 4, sizeof(int), &longitudeResolution);
        clSetKernelArg(gRiverFlowAccumulateKernel, 5, sizeof(cl_mem), &flowAccBufA);
        clSetKernelArg(gRiverFlowAccumulateKernel, 6, sizeof(cl_mem), &flowAccBufB);
        clSetKernelArg(gRiverFlowAccumulateKernel, 7, sizeof(cl_mem), &momentumBufA);
        clSetKernelArg(gRiverFlowAccumulateKernel, 8, sizeof(cl_mem), &momentumBufB);
        clSetKernelArg(gRiverFlowAccumulateKernel, 9, sizeof(float), &river_threshold);
        clSetKernelArg(gRiverFlowAccumulateKernel, 10, sizeof(float), &slope_epsilon);
        clSetKernelArg(gRiverFlowAccumulateKernel, 11, sizeof(float), &height_epsilon);
        clSetKernelArg(gRiverFlowAccumulateKernel, 12, sizeof(float), &momentum_strength);
        size_t global[2] = {(size_t)latitudeResolution, (size_t)longitudeResolution};
        err = clEnqueueNDRangeKernel(OpenCLContext::get().getQueue(), gRiverFlowAccumulateKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            printf("river_flow_accumulate kernel enqueue failed: %d\n", err);
        }
        // Swap buffers
        std::swap(flowAccBufA, flowAccBufB);
        std::swap(momentumBufA, momentumBufB);
    }
    // set the riverBuffer to the final accumulated flow buffer NO COPY
    OpenCLContext::get().releaseMem(flowSourceBuf);
    OpenCLContext::get().releaseMem(flowAccBufA);
    OpenCLContext::get().releaseMem(momentumBufA);
    OpenCLContext::get().releaseMem(momentumBufB);
    riverBuf = flowAccBufB;
}