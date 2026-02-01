#include <WorldMaps/Map/RiverLayer.hpp>
#include <WorldMaps/WorldMap.hpp>

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
    // Convert river scalar values to grayscale RGBA colors
    static std::vector<std::array<uint8_t, 4>> grayRamp = {
        MapLayer::rgba(0, 0, 0, 255),
        MapLayer::rgba(255, 255, 255, 255)};
    static std::vector<float> weights = {1.0f, 1.0f, 1.0f};
    if (coloredBuffer == nullptr)
    {
        weightedScalarToColor(coloredBuffer, riverBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), 2, grayRamp, weights);
    }
    return coloredBuffer;
}

cl_mem RiverLayer::getRiverBuffer()
{
    if (riverBuffer == nullptr)
    {
        generateRiverPaths(riverBuffer,
                           parentWorld->getLayer("elevation")->sample(),
                           parentWorld->getLayer("watertable")->sample(),
                           parentWorld->getWorldWidth(),
                           parentWorld->getWorldHeight(),
                           parentWorld->getWorldDepth(),
                           parentWorld->getWorldRadius(),
                           10000); // maxSteps
    }
    return riverBuffer;
}

void RiverLayer::generateRiverPaths(cl_mem& outputCounts,
                               cl_mem elevationBuf,
                               cl_mem waterTableBuf,
                               int W, int H, int D,
                               int radius,
                               uint32_t maxSteps)
{
    ZoneScopedN("GenerateRiverPaths");
    if (!OpenCLContext::get().isReady()) return;

    cl_int err = CL_SUCCESS;
    static cl_program gRiverPathsProgram = nullptr;
    static cl_kernel gGenerateRiverPathsKernel = nullptr;
    try {
        OpenCLContext::get().createProgram(gRiverPathsProgram, "Kernels/Rivers.cl");
        OpenCLContext::get().createKernelFromProgram(gGenerateRiverPathsKernel, gRiverPathsProgram, "generate_river_paths");
    } catch (const std::runtime_error &e) {
        printf("Error initializing RiverPaths OpenCL: %s\n", e.what());
        return;
    }

    size_t voxels = (size_t)W * (size_t)H * (size_t)D;
    size_t outSize = voxels * sizeof(cl_uint);

    // allocate & zero output if needed (use host-zero copy for simplicity)
    if (outputCounts != nullptr) {
        cl_int r = clGetMemObjectInfo(outputCounts, CL_MEM_SIZE, sizeof(size_t), &outSize, NULL);
        if (r != CL_SUCCESS || outSize < voxels * sizeof(cl_uint)) {
            OpenCLContext::get().releaseMem(outputCounts);
            outputCounts = nullptr;
        }
    }
    if (outputCounts == nullptr) {
        std::vector<cl_uint> zeros(voxels, 0u);
        outputCounts = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, outSize, zeros.data(), &err, "generateRiverPaths outputCounts");
        if (err != CL_SUCCESS || outputCounts == nullptr) {
            throw std::runtime_error("clCreateBuffer failed for generateRiverPaths outputCounts");
        }
    }

    clSetKernelArg(gGenerateRiverPathsKernel, 0, sizeof(cl_mem), &elevationBuf);
    clSetKernelArg(gGenerateRiverPathsKernel, 1, sizeof(cl_mem), &waterTableBuf);
    clSetKernelArg(gGenerateRiverPathsKernel, 2, sizeof(int), &W);
    clSetKernelArg(gGenerateRiverPathsKernel, 3, sizeof(int), &H);
    clSetKernelArg(gGenerateRiverPathsKernel, 4, sizeof(int), &D);
    clSetKernelArg(gGenerateRiverPathsKernel, 5, sizeof(int), &radius);
    clSetKernelArg(gGenerateRiverPathsKernel, 6, sizeof(uint32_t), &maxSteps);
    clSetKernelArg(gGenerateRiverPathsKernel, 7, sizeof(cl_mem), &outputCounts);
    size_t global[3] = {(size_t)W, (size_t)H, (size_t)D};
    err = clEnqueueNDRangeKernel(OpenCLContext::get().getQueue(), gGenerateRiverPathsKernel, 3, nullptr, global, nullptr, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        printf("generateRiverPaths kernel enqueue failed: %d\n", err);
    }
}