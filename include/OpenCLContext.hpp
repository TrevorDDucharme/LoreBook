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

    // Persistent, always-on debug buffer (8 ints): see layout above in kernels
    cl_mem getDebugBuffer() const { return debugBuf_; }

    // Ensure a debug buffer exists (creates it lazily). Throws on failure.
    cl_mem ensureDebugBuffer()
    {
        if (debugBuf_ != nullptr)
            return debugBuf_;

        cl_int err = CL_SUCCESS;
        debugBuf_ = createBuffer(CL_MEM_READ_WRITE, sizeof(int) * 8, nullptr, &err);
        if (err != CL_SUCCESS || debugBuf_ == nullptr)
            throw std::runtime_error("Failed to create OpenCL debug buffer");

        int zeros[8] = {0,0,0,0,0,0,0,0};
        if (clEnqueueWriteBuffer(clQueue, debugBuf_, CL_TRUE, 0, sizeof(zeros), zeros, 0, nullptr, nullptr) != CL_SUCCESS)
            throw std::runtime_error("Failed to initialize OpenCL debug buffer");

        return debugBuf_;
    }

    // Release the persistent debug buffer (safe to call multiple times)
    void releaseDebugBuffer()
    {
        if (debugBuf_ != nullptr)
        {
            releaseMem(debugBuf_);
            debugBuf_ = nullptr;
        }
    }

    // Tracked allocation helpers (wrap clCreateBuffer / clReleaseMemObject)
    cl_mem createBuffer(cl_mem_flags flags, size_t size, void *hostPtr, cl_int *err = nullptr);
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

static cl_mem perlin(int width,
                     int height,
                     int depth,
                     float frequency,
                     float lacunarity,
                     int octaves,
                     float persistence,
                     unsigned int seed)
{
    if (!OpenCLContext::get().isReady())
        return nullptr;

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

    cl_kernel kernel = clCreateKernel(gPerlinProgram, "perlin_fbm_3d", &err);
    if (err != CL_SUCCESS)
        throw std::runtime_error("clCreateKernel failed for perlin_fbm_3d");

    size_t total = (size_t)width * (size_t)height * (size_t)depth * sizeof(float);
    cl_mem output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, total, nullptr, &err);
    if (err != CL_SUCCESS || output == nullptr)
    {
        clReleaseKernel(kernel);
        throw std::runtime_error("clCreateBuffer failed for perlin output");
    }
    // diagnostics: log memory usage after allocating perlin output buffer
    OpenCLContext::get().logMemoryUsage();

    clSetKernelArg(kernel, 0, sizeof(cl_mem), &output);
    clSetKernelArg(kernel, 1, sizeof(int), &width);
    clSetKernelArg(kernel, 2, sizeof(int), &height);
    clSetKernelArg(kernel, 3, sizeof(int), &depth);
    clSetKernelArg(kernel, 4, sizeof(float), &frequency);
    clSetKernelArg(kernel, 5, sizeof(float), &lacunarity);
    clSetKernelArg(kernel, 6, sizeof(int), &octaves);
    clSetKernelArg(kernel, 7, sizeof(float), &persistence);
    clSetKernelArg(kernel, 8, sizeof(unsigned int), &seed);

    // Debug buffer: use persistent 8-int buffer (center voxel info)
    cl_mem debugBuf = OpenCLContext::get().ensureDebugBuffer();
    int zeros_dbg[8] = {0,0,0,0,0,0,0,0};
    err = clEnqueueWriteBuffer(queue, debugBuf, CL_TRUE, 0, sizeof(zeros_dbg), zeros_dbg, 0, nullptr, nullptr);
    if (err != CL_SUCCESS)
    {
        clReleaseKernel(kernel);
        OpenCLContext::get().releaseMem(output);
        throw std::runtime_error("clEnqueueWriteBuffer failed for perlin debugBuf");
    }
    clSetKernelArg(kernel, 9, sizeof(cl_mem), &debugBuf);

    size_t global[3] = {(size_t)width, (size_t)height, (size_t)depth};
    err = clEnqueueNDRangeKernel(queue, kernel, 3, nullptr, global, nullptr, 0, nullptr, nullptr);
    clFinish(queue);

    // Read back debug info and log it
    int dbg[8] = {0, 0, 0, 0, 0, 0, 0, 0};
    err = clEnqueueReadBuffer(queue, debugBuf, CL_TRUE, 0, sizeof(dbg), dbg, 0, nullptr, nullptr);
    if (err == CL_SUCCESS)
    {
        // dbg[0,1,2] = x,y,z; dbg[3] = float bits of the value; dbg[4] = written flag
        if (dbg[4] == 1)
        {
            float fval = 0.0f;
            memcpy(&fval, &dbg[3], sizeof(fval));
            fprintf(stderr, "[OpenCL Debug] perlin voxel (%d,%d,%d) value = %f\n", dbg[0], dbg[1], dbg[2], fval);
        }
        else
        {
            fprintf(stderr, "[OpenCL Debug] perlin debugBuf not written (flag=0)\n");
        }

        // Also sample first few output values to check whether the buffer contains data (helpful when debug flag not written)
        float sample[8] = {0};
        err = clEnqueueReadBuffer(queue, output, CL_TRUE, 0, sizeof(sample), sample, 0, nullptr, nullptr);
        if (err == CL_SUCCESS)
        {
            fprintf(stderr, "[OpenCL Debug] perlin output sample[0..7] = %f, %f, %f, %f, %f, %f, %f, %f\n",
                    sample[0], sample[1], sample[2], sample[3], sample[4], sample[5], sample[6], sample[7]);
        }
        else
        {
            fprintf(stderr, "[OpenCL Debug] failed to read perlin output sample: %d\n", err);
        }

        // Sample the center voxel value for extra confidence
        size_t centerIdx = ((size_t)width / 2) + ((size_t)height / 2) * (size_t)width + ((size_t)depth / 2) * (size_t)width * (size_t)height;
        float centerVal = 0.0f;
        err = clEnqueueReadBuffer(queue, output, CL_TRUE, centerIdx * sizeof(float), sizeof(centerVal), &centerVal, 0, nullptr, nullptr);
        if (err == CL_SUCCESS)
        {
            fprintf(stderr, "[OpenCL Debug] perlin center voxel value = %f\n", centerVal);
        }
        else
        {
            fprintf(stderr, "[OpenCL Debug] failed to read perlin center voxel: %d\n", err);
        }
    }
    else
    {
        fprintf(stderr, "[OpenCL Debug] failed to read perlin debugBuf: %d\n", err);
    }

    clReleaseKernel(kernel);

    return output;
}

static cl_program gScalarToColorProgram = nullptr;

static cl_mem scalarToColor(cl_mem scalarBuffer,
                            int fieldW,
                            int fieldH,
                            int fieldD,
                            int colorCount,
                            const std::vector<std::array<unsigned char, 4>> &paletteColors)
{
    if (!OpenCLContext::get().isReady())
        return nullptr;

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

    cl_kernel kernel = clCreateKernel(gScalarToColorProgram, "scalar_to_rgba_float4", &err);
    if (err != CL_SUCCESS)
        throw std::runtime_error("clCreateKernel failed for scalar_to_rgba_float4");

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
    cl_mem paletteBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(cl_float4) * paletteFloats.size(), paletteFloats.data(), &err);
    if (err != CL_SUCCESS || paletteBuf == nullptr)
    {
        clReleaseKernel(kernel);
        throw std::runtime_error("clCreateBuffer failed for scalarToColor paletteBuf");
    }

    size_t voxels = (size_t)fieldW * (size_t)fieldH * (size_t)fieldD;
    size_t outSize = voxels * sizeof(cl_float4);
    cl_mem output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, outSize, nullptr, &err);
    if (err != CL_SUCCESS || output == nullptr)
    {
        clReleaseKernel(kernel);
        OpenCLContext::get().releaseMem(paletteBuf);
        throw std::runtime_error("clCreateBuffer failed for scalarToColor output");
    }

    // diagnostics: log memory usage after allocating scalarToColor output buffer
    OpenCLContext::get().logMemoryUsage();

    clSetKernelArg(kernel, 0, sizeof(cl_mem), &scalarBuffer);
    clSetKernelArg(kernel, 1, sizeof(int), &fieldW);
    clSetKernelArg(kernel, 2, sizeof(int), &fieldH);
    clSetKernelArg(kernel, 3, sizeof(int), &fieldD);
    clSetKernelArg(kernel, 4, sizeof(int), &colorCount);
    clSetKernelArg(kernel, 5, sizeof(cl_mem), &paletteBuf);
    clSetKernelArg(kernel, 6, sizeof(cl_mem), &output);

    // Use persistent debug buffer
    cl_mem debugBuf = OpenCLContext::get().ensureDebugBuffer();
    int zeros_dbg[8] = {0,0,0,0,0,0,0,0};
    err = clEnqueueWriteBuffer(queue, debugBuf, CL_TRUE, 0, sizeof(zeros_dbg), zeros_dbg, 0, nullptr, nullptr);
    if (err != CL_SUCCESS)
    {
        clReleaseKernel(kernel);
        OpenCLContext::get().releaseMem(paletteBuf);
        OpenCLContext::get().releaseMem(output);
        throw std::runtime_error("clEnqueueWriteBuffer failed for scalarToColor debugBuf");
    }
    clSetKernelArg(kernel, 7, sizeof(cl_mem), &debugBuf);

    size_t global[3] = {(size_t)fieldW, (size_t)fieldH, (size_t)fieldD};
    err = clEnqueueNDRangeKernel(queue, kernel, 3, nullptr, global, nullptr, 0, nullptr, nullptr);
    clFinish(queue);

    // Read back debug info
    int dbg[8] = {0,0,0,0,0,0,0,0};
    err = clEnqueueReadBuffer(queue, debugBuf, CL_TRUE, 0, sizeof(dbg), dbg, 0, nullptr, nullptr);
    if (err == CL_SUCCESS)
    {
        if (dbg[4] == 1)
        {
            float fval = 0.0f;
            memcpy(&fval, &dbg[3], sizeof(fval));
            fprintf(stderr, "[OpenCL Debug] scalarToColor center (%d,%d,%d) val = %f colorIdx=%d extra=%d\n", dbg[0], dbg[1], dbg[2], fval, dbg[5], dbg[6]);
        }
        else
        {
            fprintf(stderr, "[OpenCL Debug] scalarToColor debugBuf not written (flag=0)\n");
        }

        // Sample first output float4 (if possible)
        cl_float4 sample[1];
        err = clEnqueueReadBuffer(queue, output, CL_TRUE, 0, sizeof(sample), sample, 0, nullptr, nullptr);
        if (err == CL_SUCCESS)
        {
            fprintf(stderr, "[OpenCL Debug] scalarToColor output sample[0] = %f, %f, %f, %f\n",
                    sample[0].s[0], sample[0].s[1], sample[0].s[2], sample[0].s[3]);
        }
        else
        {
            fprintf(stderr, "[OpenCL Debug] failed to read scalarToColor output sample: %d\n", err);
        }
    }
    else
    {
        fprintf(stderr, "[OpenCL Debug] failed to read scalarToColor debugBuf: %d\n", err);
    }

    OpenCLContext::get().releaseMem(paletteBuf);
    clReleaseKernel(kernel);
    return output;
}