#pragma once
#include <CL/cl.h>
#include <string>
#include <stdexcept>
#include <cstring>
#include <LoreBook_Resources/LoreBook_ResourcesEmbeddedVFS.hpp>

/// Global OpenCL context manager - provides shared OpenCL resources
/// for all parts of the application (nodes, utilities, etc.)
class OpenCLContext {
public:
    // Get the singleton instance
    static OpenCLContext& get();

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

private:
    OpenCLContext() = default;
    ~OpenCLContext();

    // Disable copy/move
    OpenCLContext(const OpenCLContext&) = delete;
    OpenCLContext& operator=(const OpenCLContext&) = delete;

    cl_platform_id clPlatform = nullptr;
    cl_device_id clDevice = nullptr;
    cl_context clContext = nullptr;
    cl_command_queue clQueue = nullptr;
    bool clReady = false;
    bool clDeviceIsGPU = false;
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
        const char* src = kernel_code.c_str();
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
    cl_mem output = clCreateBuffer(ctx, CL_MEM_READ_WRITE, total, nullptr, &err);
    if (err != CL_SUCCESS)
    {
        clReleaseKernel(kernel);
        throw std::runtime_error("clCreateBuffer failed for perlin output");
    }

    clSetKernelArg(kernel, 0, sizeof(cl_mem), &output);
    clSetKernelArg(kernel, 1, sizeof(int), &width);
    clSetKernelArg(kernel, 2, sizeof(int), &height);
    clSetKernelArg(kernel, 3, sizeof(int), &depth);
    clSetKernelArg(kernel, 4, sizeof(float), &frequency);
    clSetKernelArg(kernel, 5, sizeof(float), &lacunarity);
    clSetKernelArg(kernel, 6, sizeof(int), &octaves);
    clSetKernelArg(kernel, 7, sizeof(float), &persistence);
    clSetKernelArg(kernel, 8, sizeof(unsigned int), &seed);

    // Debug buffer: small integer buffer for kernel to write diagnostic info (5 ints)
    cl_mem debugBuf = clCreateBuffer(ctx, CL_MEM_READ_WRITE, sizeof(int) * 5, nullptr, &err);
    if (err != CL_SUCCESS)
    {
        clReleaseKernel(kernel);
        clReleaseMemObject(output);
        throw std::runtime_error("clCreateBuffer failed for perlin debugBuf");
    }
    int zeros[5] = {0,0,0,0,0};
    err = clEnqueueWriteBuffer(queue, debugBuf, CL_TRUE, 0, sizeof(zeros), zeros, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) { clReleaseKernel(kernel); clReleaseMemObject(output); clReleaseMemObject(debugBuf); throw std::runtime_error("clEnqueueWriteBuffer failed for perlin debugBuf"); }
    clSetKernelArg(kernel, 9, sizeof(cl_mem), &debugBuf);

    size_t global[3] = {(size_t)width, (size_t)height, (size_t)depth};
    err = clEnqueueNDRangeKernel(queue, kernel, 3, nullptr, global, nullptr, 0, nullptr, nullptr);
    clFinish(queue);

    // Read back debug info and log it
    int dbg[5] = {0,0,0,0,0};
    err = clEnqueueReadBuffer(queue, debugBuf, CL_TRUE, 0, sizeof(dbg), dbg, 0, nullptr, nullptr);
    if (err == CL_SUCCESS) {
        // dbg[0,1,2] = x,y,z; dbg[3] = float bits of the value; dbg[4] = written flag
        if (dbg[4] == 1) {
            float fval = 0.0f;
            memcpy(&fval, &dbg[3], sizeof(fval));
            fprintf(stderr, "[OpenCL Debug] perlin voxel (%d,%d,%d) value = %f\n", dbg[0], dbg[1], dbg[2], fval);
        } else {
            fprintf(stderr, "[OpenCL Debug] perlin debugBuf not written (flag=0)\n");
        }

        // Also sample first few output values to check whether the buffer contains data (helpful when debug flag not written)
        float sample[8] = {0};
        err = clEnqueueReadBuffer(queue, output, CL_TRUE, 0, sizeof(sample), sample, 0, nullptr, nullptr);
        if (err == CL_SUCCESS) {
            fprintf(stderr, "[OpenCL Debug] perlin output sample[0..7] = %f, %f, %f, %f, %f, %f, %f, %f\n",
                    sample[0], sample[1], sample[2], sample[3], sample[4], sample[5], sample[6], sample[7]);
        } else {
            fprintf(stderr, "[OpenCL Debug] failed to read perlin output sample: %d\n", err);
        }

        // Sample the center voxel value for extra confidence
        size_t centerIdx = ((size_t)width/2) + ((size_t)height/2) * (size_t)width + ((size_t)depth/2) * (size_t)width * (size_t)height;
        float centerVal = 0.0f;
        err = clEnqueueReadBuffer(queue, output, CL_TRUE, centerIdx * sizeof(float), sizeof(centerVal), &centerVal, 0, nullptr, nullptr);
        if (err == CL_SUCCESS) {
            fprintf(stderr, "[OpenCL Debug] perlin center voxel value = %f\n", centerVal);
        } else {
            fprintf(stderr, "[OpenCL Debug] failed to read perlin center voxel: %d\n", err);
        }
    } else {
        fprintf(stderr, "[OpenCL Debug] failed to read perlin debugBuf: %d\n", err);
    }

    clReleaseMemObject(debugBuf);
    clReleaseKernel(kernel);

    return output;
}