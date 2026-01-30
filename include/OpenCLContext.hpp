#pragma once
#include <CL/cl.h>
#include <string>
#include <stdexcept>
#include <cstring>
#include <LoreBook_Resources/LoreBook_ResourcesEmbeddedVFS.hpp>
#include <unordered_map>
#include <mutex>
#include <cstddef>

/// Global OpenCL context manager - provides shared OpenCL resources
/// for all parts of the application (nodes, utilities, etc.)
class OpenCLContext
{
public:
    // Get the singleton instance
    static OpenCLContext &get();

    // Initialize OpenCL (call once at startup)
    bool init();

    // Cleanup OpenCL resources (call at shutdown)
    void cleanup();

    // Query methods
    bool isReady() const { return clReady; }
    bool isGPU() const { return clDeviceIsGPU; }

    // Accessors for OpenCL objects
    cl_context getContext() const { return clContext; }
    cl_command_queue getQueue() const { return clQueue; }
    cl_device_id getDevice() const { return clDevice; }
    cl_platform_id getPlatform() const { return clPlatform; }

    // Tracked allocation helpers (wrap clCreateBuffer / clReleaseMemObject)
    cl_mem createBuffer(cl_mem_flags flags, size_t size, void *hostPtr, cl_int *err = nullptr, std::string debugTag = "unknown");
    void releaseMem(cl_mem mem);
    void logMemoryUsage() const;
    size_t getTotalAllocated() const;

private:
    OpenCLContext() = default;
    ~OpenCLContext();

    // Disable copy/move
    OpenCLContext(const OpenCLContext &) = delete;
    OpenCLContext &operator=(const OpenCLContext &) = delete;

    cl_platform_id clPlatform = nullptr;
    cl_device_id clDevice = nullptr;
    cl_context clContext = nullptr;
    cl_command_queue clQueue = nullptr;
    bool clReady = false;
    bool clDeviceIsGPU = false;

    // Persistent debug buffer (always-on)
    cl_mem debugBuf_ = nullptr;

    // tracking allocations
    mutable std::mutex memTrackMutex_;
    std::unordered_map<cl_mem, size_t> memSizes_;
    size_t totalAllocated_ = 0;
};

// perlin noise opencl (C API)
static cl_program gPerlinProgram = nullptr;
static cl_kernel gPerlinKernel = nullptr;

static void perlin(cl_mem& output,int width,
                     int height,
                     int depth,
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

    if (gPerlinProgram == nullptr)
    {
        std::string kernel_code = loadLoreBook_ResourcesEmbeddedFileAsString("Kernels/Perlin.cl");
        const char *src = kernel_code.c_str();
        size_t len = kernel_code.length();
        gPerlinProgram = clCreateProgramWithSource(ctx, 1, &src, &len, &err);
        if (err != CL_SUCCESS || gPerlinProgram == nullptr)
            throw std::runtime_error("clCreateProgramWithSource failed");

        err = clBuildProgram(gPerlinProgram, 1, &device, nullptr, nullptr, nullptr);
        if (err != CL_SUCCESS)
        {
            // retrieve build log
            size_t log_size = 0;
            clGetProgramBuildInfo(gPerlinProgram, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
            std::string log;
            log.resize(log_size);
            clGetProgramBuildInfo(gPerlinProgram, device, CL_PROGRAM_BUILD_LOG, log_size, &log[0], nullptr);
            throw std::runtime_error(std::string("Failed to build OpenCL program: ") + log);
        }
    }

    if(gPerlinKernel == nullptr){
        gPerlinKernel = clCreateKernel(gPerlinProgram, "perlin_fbm_3d", &err);
        if (err != CL_SUCCESS)
            throw std::runtime_error("clCreateKernel failed for perlin_fbm_3d");
    }

    size_t total = (size_t)width * (size_t)height * (size_t)depth * sizeof(float);
    size_t buffer_size;
    if(output != nullptr){
        cl_int err = clGetMemObjectInfo(output,
                                        CL_MEM_SIZE,
                                        sizeof(buffer_size),
                                        &buffer_size,
                                        NULL);
        if (err != CL_SUCCESS) {
            throw std::runtime_error("clGetMemObjectInfo failed for perlin output buffer size");
        }
        if(buffer_size < total){
            OpenCLContext::get().releaseMem(output);
            output = nullptr;
        }
    }

    if(output == nullptr){
        output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, total, nullptr, &err, "perlin output");
        if (err != CL_SUCCESS || output == nullptr)
        {
            throw std::runtime_error("clCreateBuffer failed for perlin output");
        }
    }   
    // diagnostics: log memory usage after allocating perlin output buffer
    OpenCLContext::get().logMemoryUsage();

    clSetKernelArg(gPerlinKernel, 0, sizeof(cl_mem), &output);
    clSetKernelArg(gPerlinKernel, 1, sizeof(int), &width);
    clSetKernelArg(gPerlinKernel, 2, sizeof(int), &height);
    clSetKernelArg(gPerlinKernel, 3, sizeof(int), &depth);
    clSetKernelArg(gPerlinKernel, 4, sizeof(float), &frequency);
    clSetKernelArg(gPerlinKernel, 5, sizeof(float), &lacunarity);
    clSetKernelArg(gPerlinKernel, 6, sizeof(int), &octaves);
    clSetKernelArg(gPerlinKernel, 7, sizeof(float), &persistence);
    clSetKernelArg(gPerlinKernel, 8, sizeof(unsigned int), &seed);

    size_t global[3] = {(size_t)width, (size_t)height, (size_t)depth};
    err = clEnqueueNDRangeKernel(queue, gPerlinKernel, 3, nullptr, global, nullptr, 0, nullptr, nullptr);
    clFinish(queue);
}

static cl_program gScalarToColorProgram = nullptr;
static cl_kernel gScalarToColorKernel = nullptr;

static void scalarToColor(cl_mem& output, 
                            cl_mem scalarBuffer,
                            int fieldW,
                            int fieldH,
                            int fieldD,
                            int colorCount,
                            const std::vector<std::array<unsigned char, 4>> &paletteColors)
{
    if (!OpenCLContext::get().isReady())
        return;

    cl_context ctx = OpenCLContext::get().getContext();
    cl_device_id device = OpenCLContext::get().getDevice();
    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;

    if (gScalarToColorProgram == nullptr)
    {
        std::string kernel_code = loadLoreBook_ResourcesEmbeddedFileAsString("Kernels/ScalarToColor.cl");
        const char *src = kernel_code.c_str();
        size_t len = kernel_code.length();
        gScalarToColorProgram = clCreateProgramWithSource(ctx, 1, &src, &len, &err);
        if (err != CL_SUCCESS || gScalarToColorProgram == nullptr)
            throw std::runtime_error("clCreateProgramWithSource failed");

        err = clBuildProgram(gScalarToColorProgram, 1, &device, nullptr, nullptr, nullptr);
        if (err != CL_SUCCESS)
        {
            size_t log_size = 0;
            clGetProgramBuildInfo(gScalarToColorProgram, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
            std::string log;
            log.resize(log_size);
            clGetProgramBuildInfo(gScalarToColorProgram, device, CL_PROGRAM_BUILD_LOG, log_size, &log[0], nullptr);
            throw std::runtime_error(std::string("Failed to build OpenCL program: ") + log);
        }
    }

    if (gScalarToColorKernel == nullptr){
        gScalarToColorKernel = clCreateKernel(gScalarToColorProgram, "scalar_to_rgba_float4", &err);
        if (err != CL_SUCCESS)
            throw std::runtime_error("clCreateKernel failed for scalar_to_rgba_float4");
    }
    // create palette buffer
    std::vector<cl_float4> paletteFloats;
    paletteFloats.reserve((size_t)colorCount);
    for (int i = 0; i < colorCount; ++i)
    {
        auto &c = paletteColors[i % paletteColors.size()];
        cl_float4 col;
        col.s[0] = static_cast<float>(c[0]) / 255.0f;
        col.s[1] = static_cast<float>(c[1]) / 255.0f;
        col.s[2] = static_cast<float>(c[2]) / 255.0f;
        col.s[3] = static_cast<float>(c[3]) / 255.0f;
        paletteFloats.push_back(col);
    }
    cl_mem paletteBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(cl_float4) * paletteFloats.size(), paletteFloats.data(), &err, "scalarToColor paletteBuf");
    if (err != CL_SUCCESS || paletteBuf == nullptr)
    {
        throw std::runtime_error("clCreateBuffer failed for scalarToColor paletteBuf");
    }

    size_t voxels = (size_t)fieldW * (size_t)fieldH * (size_t)fieldD;
    size_t outSize = voxels * sizeof(cl_float4);

    if(output != nullptr){
        cl_int err = clGetMemObjectInfo(output,
                                        CL_MEM_SIZE,
                                        sizeof(size_t),
                                        &outSize,
                                        NULL);
        if (err != CL_SUCCESS) {
            throw std::runtime_error("clGetMemObjectInfo failed for scalarToColor output buffer size");
        }
        if(outSize < voxels * sizeof(cl_float4)){
            OpenCLContext::get().releaseMem(output);
            output = nullptr;
        }
    }

    if(output == nullptr){
        output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, outSize, nullptr, &err, "scalarToColor output");
        if (err != CL_SUCCESS || output == nullptr)
        {
            OpenCLContext::get().releaseMem(paletteBuf);
            throw std::runtime_error("clCreateBuffer failed for scalarToColor output");
        }
    }

    // diagnostics: log memory usage after allocating scalarToColor output buffer
    OpenCLContext::get().logMemoryUsage();

    clSetKernelArg(gScalarToColorKernel, 0, sizeof(cl_mem), &scalarBuffer);
    clSetKernelArg(gScalarToColorKernel, 1, sizeof(int), &fieldW);
    clSetKernelArg(gScalarToColorKernel, 2, sizeof(int), &fieldH);
    clSetKernelArg(gScalarToColorKernel, 3, sizeof(int), &fieldD);
    clSetKernelArg(gScalarToColorKernel, 4, sizeof(int), &colorCount);
    clSetKernelArg(gScalarToColorKernel, 5, sizeof(cl_mem), &paletteBuf);
    clSetKernelArg(gScalarToColorKernel, 6, sizeof(cl_mem), &output);

    size_t global[3] = {(size_t)fieldW, (size_t)fieldH, (size_t)fieldD};
    err = clEnqueueNDRangeKernel(queue, gScalarToColorKernel, 3, nullptr, global, nullptr, 0, nullptr, nullptr);
    clFinish(queue);

    OpenCLContext::get().releaseMem(paletteBuf);
}


static void concatVolumes(cl_mem& output,
                                std::vector<cl_mem> &inputVolumes,
                                int fieldW,
                                int fieldH,
                                int fieldD)
{
   if (!OpenCLContext::get().isReady())
        return;

    cl_context ctx = OpenCLContext::get().getContext();
    cl_device_id device = OpenCLContext::get().getDevice();
    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;

    int num_channels = static_cast<int>(inputVolumes.size());

    size_t voxel_count = static_cast<size_t>(fieldW) * fieldH * fieldD;
    size_t outSize = (size_t)num_channels * voxel_count * sizeof(float);

    if (output != nullptr)
    {
        size_t current_size = 0;
        cl_int info_err = clGetMemObjectInfo(output,
                                            CL_MEM_SIZE,
                                            sizeof(size_t),
                                            &current_size,
                                            NULL);
        if (info_err != CL_SUCCESS) {
            throw std::runtime_error("clGetMemObjectInfo failed for concatVolumes output buffer size");
        }
        if (current_size < outSize) {
            OpenCLContext::get().releaseMem(output);
            output = nullptr;
        }
    }

    if (output == nullptr)
    {
        output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, outSize, nullptr, &err, "concatVolumes output");
        if (err != CL_SUCCESS || output == nullptr)
        {
            throw std::runtime_error("clCreateBuffer failed for concatVolumes output");
        }
    }
    
    for (int c = 0; c < num_channels; c++) {
        //dowload each channel and copy to the right location in output
        size_t channel_offset = static_cast<size_t>(c) * voxel_count * sizeof(float);
        err = clEnqueueCopyBuffer(queue,
                                  inputVolumes[c],
                                  output,
                                  0,
                                  channel_offset,
                                  voxel_count * sizeof(float),
                                  0,
                                  nullptr,
                                  nullptr);
        if (err != CL_SUCCESS) {
            throw std::runtime_error("clEnqueueCopyBuffer failed in concatVolumes");
        }
    }

    clFinish(queue);
}