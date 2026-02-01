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
    static std::vector<std::array<uint8_t, 4>> blueRamp = {
        MapLayer::rgba(0, 0, 0, 255),       // black (no river)
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
                               cl_mem& riverVolumeBuf,
                               int latitudeResolution, int longitudeResolution,
                               uint32_t maxSteps)
{
    ZoneScopedN("GenerateRiverPaths");
    if (!OpenCLContext::get().isReady()) return;

    cl_int err = CL_SUCCESS;
    static cl_program gRiverPathsProgram = nullptr;
    static cl_kernel gRiverFlowDirKernel = nullptr;
    static cl_kernel gRiverFlowInitKernel = nullptr;
    static cl_kernel gRiverFlowAccumulateKernel = nullptr;
    static cl_kernel gRiverVolumeKernel = nullptr;
    try {
        OpenCLContext::get().createProgram(gRiverPathsProgram, "Kernels/Rivers.cl");
        OpenCLContext::get().createKernelFromProgram(gRiverFlowDirKernel, gRiverPathsProgram, "river_flow_dir");
        OpenCLContext::get().createKernelFromProgram(gRiverFlowInitKernel, gRiverPathsProgram, "river_flow_init");
        OpenCLContext::get().createKernelFromProgram(gRiverFlowAccumulateKernel, gRiverPathsProgram, "river_flow_accumulate");
        OpenCLContext::get().createKernelFromProgram(gRiverVolumeKernel, gRiverPathsProgram, "river_volume");
    } catch (const std::runtime_error &e) {
        printf("Error initializing RiverPaths OpenCL: %s\n", e.what());
        return;
    }

    size_t voxels = (size_t)latitudeResolution * (size_t)longitudeResolution;
    size_t outSize = voxels * sizeof(cl_float);

    // allocate & zero output if needed (use host-zero copy for simplicity)
    if (riverVolumeBuf != nullptr) {
        cl_int r = clGetMemObjectInfo(riverVolumeBuf, CL_MEM_SIZE, sizeof(size_t), &outSize, NULL);
        if (r != CL_SUCCESS || outSize < voxels * sizeof(cl_float)) {
            OpenCLContext::get().releaseMem(riverVolumeBuf);
            riverVolumeBuf = nullptr;
        }
    }
    if (riverVolumeBuf == nullptr) {
        std::vector<float> zeros(voxels, 0.0f);
        riverVolumeBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, outSize, zeros.data(), &err, "generateRiverPaths riverVolumeBuf");
        if (err != CL_SUCCESS || riverVolumeBuf == nullptr) {
            throw std::runtime_error("clCreateBuffer failed for generateRiverPaths riverVolumeBuf");
        }
    }

    //initialize flow buffers for ping-pong
    cl_mem flowDirBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, voxels * sizeof(cl_int), nullptr, &err, "generateRiverPaths flowDirBuf");
    if (err != CL_SUCCESS || flowDirBuf == nullptr) {
        throw std::runtime_error("clCreateBuffer failed for generateRiverPaths flowDirBuf");
    }
    cl_mem flowAccBufA = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, voxels * sizeof(cl_float), nullptr, &err, "generateRiverPaths flowAccBufA");
    if (err != CL_SUCCESS || flowAccBufA == nullptr) {
        throw std::runtime_error("clCreateBuffer failed for generateRiverPaths flowAccBufA");
    }
    cl_mem flowAccBufB = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, voxels * sizeof(cl_float), nullptr, &err, "generateRiverPaths flowAccBufB");
    if (err != CL_SUCCESS || flowAccBufB == nullptr) {
        throw std::runtime_error("clCreateBuffer failed for generateRiverPaths flowAccBufB");
    }
    // Step 1: Compute flow directions
    {
        clSetKernelArg(gRiverFlowDirKernel, 0, sizeof(cl_mem), &elevationBuf);
        clSetKernelArg(gRiverFlowDirKernel, 1, sizeof(cl_mem), &waterTableBuf);
        clSetKernelArg(gRiverFlowDirKernel, 2, sizeof(cl_mem), &flowDirBuf);
        clSetKernelArg(gRiverFlowDirKernel, 3, sizeof(int), &latitudeResolution);
        clSetKernelArg(gRiverFlowDirKernel, 4, sizeof(int), &longitudeResolution);
        size_t global[2] = {(size_t)latitudeResolution, (size_t)longitudeResolution};
        err = clEnqueueNDRangeKernel(OpenCLContext::get().getQueue(), gRiverFlowDirKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            printf("river_flow_dir kernel enqueue failed: %d\n", err);
        }
    }
    // Step 2: Initialize flow accumulation (now slope-aware)
    {
        // slopeScale controls how much flatter cells increase initial flow (>0 increases width on flats)
        float slopeScale = 2.0f;
        // slopeNorm is the normalization value for the local max drop; adjust based on elevation range
        float slopeNorm = 0.02f;

        clSetKernelArg(gRiverFlowInitKernel, 0, sizeof(cl_mem), &elevationBuf);
        clSetKernelArg(gRiverFlowInitKernel, 1, sizeof(cl_mem), &waterTableBuf);
        clSetKernelArg(gRiverFlowInitKernel, 2, sizeof(int), &latitudeResolution);
        clSetKernelArg(gRiverFlowInitKernel, 3, sizeof(int), &longitudeResolution);
        clSetKernelArg(gRiverFlowInitKernel, 4, sizeof(cl_mem), &flowAccBufA);
        clSetKernelArg(gRiverFlowInitKernel, 5, sizeof(float), &slopeScale);
        clSetKernelArg(gRiverFlowInitKernel, 6, sizeof(float), &slopeNorm);

        size_t global[2] = {(size_t)latitudeResolution, (size_t)longitudeResolution};
        err = clEnqueueNDRangeKernel(OpenCLContext::get().getQueue(), gRiverFlowInitKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            printf("river_flow_init kernel enqueue failed: %d\n", err);
        }
    }
    // Step 3: Iteratively accumulate flow
    const int flowIterations = maxSteps;
    for (int iter = 0; iter < flowIterations; ++iter) {
        clSetKernelArg(gRiverFlowAccumulateKernel, 0, sizeof(cl_mem), &flowDirBuf);
        clSetKernelArg(gRiverFlowAccumulateKernel, 1, sizeof(int), &latitudeResolution);
        clSetKernelArg(gRiverFlowAccumulateKernel, 2, sizeof(int), &longitudeResolution);
        clSetKernelArg(gRiverFlowAccumulateKernel, 3, sizeof(cl_mem), &flowAccBufA);
        clSetKernelArg(gRiverFlowAccumulateKernel, 4, sizeof(cl_mem), &flowAccBufB);
        size_t global[2] = {(size_t)latitudeResolution, (size_t)longitudeResolution};
        err = clEnqueueNDRangeKernel(OpenCLContext::get().getQueue(), gRiverFlowAccumulateKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            printf("river_flow_accumulate kernel enqueue failed: %d\n", err);
        }
        // Swap buffers
        std::swap(flowAccBufA, flowAccBufB);
    }
    // Step 4: Compute river volume based on accumulated flow
    // Determine an adaptive threshold using the top percentile of accumulated flow values.
    float flowThreshold = 1.0f;
    float flowMax = 1.0f;
    const float percentile = 0.999f; // tune if needed
    {
        std::vector<float> hostFlows(voxels);
        err = clEnqueueReadBuffer(OpenCLContext::get().getQueue(), flowAccBufA, CL_TRUE, 0, voxels * sizeof(float), hostFlows.data(), 0, nullptr, nullptr);
        if (err == CL_SUCCESS && voxels > 0) {
            size_t idx = (size_t)std::floor((voxels - 1) * percentile);
            std::nth_element(hostFlows.begin(), hostFlows.begin() + idx, hostFlows.end());
            float pval = hostFlows[idx];
            float maxv = *std::max_element(hostFlows.begin(), hostFlows.end());
            flowThreshold = std::max(1.0f, pval); // allow smaller thresholds (not forced to 100)
            flowMax = std::max(flowThreshold * 1.001f, maxv); // ensure flowMax > threshold
            printf("river flow threshold set to %f (percentile %.3f), max flow %f\n", flowThreshold, percentile, flowMax);
        } else {
            printf("Warning: failed to read flow buffer to compute threshold: %d\n", err);
        }
    }
    {
        clSetKernelArg(gRiverVolumeKernel, 0, sizeof(cl_mem), &flowAccBufA);
        clSetKernelArg(gRiverVolumeKernel, 1, sizeof(int), &latitudeResolution);
        clSetKernelArg(gRiverVolumeKernel, 2, sizeof(int), &longitudeResolution);
        clSetKernelArg(gRiverVolumeKernel, 3, sizeof(cl_mem), &riverVolumeBuf);
        float widthScale = 1.0f; // try ~0.5..1.5 and adjust visually
        clSetKernelArg(gRiverVolumeKernel, 4, sizeof(float), &flowThreshold);
        clSetKernelArg(gRiverVolumeKernel, 5, sizeof(float), &widthScale);
        clSetKernelArg(gRiverVolumeKernel, 6, sizeof(float), &flowMax);
        size_t global[2] = {(size_t)latitudeResolution, (size_t)longitudeResolution};
        err = clEnqueueNDRangeKernel(OpenCLContext::get().getQueue(), gRiverVolumeKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            printf("river_volume kernel enqueue failed: %d\n", err);
        }
    }
    // Cleanup
    OpenCLContext::get().releaseMem(flowDirBuf);
    OpenCLContext::get().releaseMem(flowAccBufA);
    OpenCLContext::get().releaseMem(flowAccBufB);
}