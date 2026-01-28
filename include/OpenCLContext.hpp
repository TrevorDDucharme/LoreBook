#pragma once
#include <CL/cl.h>
#include <string>
#include <stdexcept>
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

    size_t global[3] = {(size_t)width, (size_t)height, (size_t)depth};
    err = clEnqueueNDRangeKernel(queue, kernel, 3, nullptr, global, nullptr, 0, nullptr, nullptr);
    clFinish(queue);
    clReleaseKernel(kernel);

    return output;
}

static cl_program spherePerspectiveProgram = nullptr;

static cl_mem spherePerspectiveSample(
    cl_mem field3d,
    int fieldW,
    int fieldH,
    int fieldD,

    int screenW,
    int screenH,

    float camPosX,
    float camPosY,
    float camPosZ,

    float camForwardX,
    float camForwardY,
    float camForwardZ,

    float camRightX,
    float camRightY,
    float camRightZ,

    float camUpX,
    float camUpY,
    float camUpZ,

    float fovY, // radians
    float sphereCenterX,
    float sphereCenterY,
    float sphereCenterZ,
    float sphereRadius)
{
    if (!OpenCLContext::get().isReady())
        return nullptr;

    cl_context ctx = OpenCLContext::get().getContext();
    cl_device_id device = OpenCLContext::get().getDevice();
    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;

    if (spherePerspectiveProgram == nullptr)
    {
        std::string kernel_code = loadLoreBook_ResourcesEmbeddedFileAsString("Kernels/FieldToSphere.cl");
        const char* src = kernel_code.c_str();
        size_t len = kernel_code.length();
        spherePerspectiveProgram = clCreateProgramWithSource(ctx, 1, &src, &len, &err);
        if (err != CL_SUCCESS || spherePerspectiveProgram == nullptr)
            throw std::runtime_error("clCreateProgramWithSource failed");

        err = clBuildProgram(spherePerspectiveProgram, 1, &device, nullptr, nullptr, nullptr);
        if (err != CL_SUCCESS)
        {
            // retrieve build log
            size_t log_size = 0;
            clGetProgramBuildInfo(spherePerspectiveProgram, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
            std::string log;
            log.resize(log_size);
            clGetProgramBuildInfo(spherePerspectiveProgram, device, CL_PROGRAM_BUILD_LOG, log_size, &log[0], nullptr);
            throw std::runtime_error(std::string("Failed to build OpenCL program: ") + log);
        }
    }

    cl_kernel kernel = clCreateKernel(spherePerspectiveProgram, "sphere_perspective_sample", &err);
    if (err != CL_SUCCESS)
        throw std::runtime_error("clCreateKernel failed for sphere_perspective_sample");

    size_t outSize = (size_t)screenW * (size_t)screenH * sizeof(float);
    cl_mem output = clCreateBuffer(ctx, CL_MEM_READ_WRITE, outSize, nullptr, &err);
    if (err != CL_SUCCESS)
    {
        clReleaseKernel(kernel);
        throw std::runtime_error("clCreateBuffer failed for spherePerspective output");
    }

    clSetKernelArg(kernel, 0, sizeof(cl_mem), &field3d);
    clSetKernelArg(kernel, 1, sizeof(int), &fieldW);
    clSetKernelArg(kernel, 2, sizeof(int), &fieldH);
    clSetKernelArg(kernel, 3, sizeof(int), &fieldD);
    clSetKernelArg(kernel, 4, sizeof(cl_mem), &output);
    clSetKernelArg(kernel, 5, sizeof(int), &screenW);
    clSetKernelArg(kernel, 6, sizeof(int), &screenH);

    float camPos[3] = {camPosX, camPosY, camPosZ};
    float camForward[3] = {camForwardX, camForwardY, camForwardZ};
    float camRight[3] = {camRightX, camRightY, camRightZ};
    float camUp[3] = {camUpX, camUpY, camUpZ};
    float sphereCenter[3] = {sphereCenterX, sphereCenterY, sphereCenterZ};

    clSetKernelArg(kernel, 7, sizeof(camPos), camPos);
    clSetKernelArg(kernel, 8, sizeof(camForward), camForward);
    clSetKernelArg(kernel, 9, sizeof(camRight), camRight);
    clSetKernelArg(kernel, 10, sizeof(camUp), camUp);
    clSetKernelArg(kernel, 11, sizeof(float), &fovY);
    clSetKernelArg(kernel, 12, sizeof(sphereCenter), sphereCenter);
    clSetKernelArg(kernel, 13, sizeof(float), &sphereRadius);

    size_t global[2] = {(size_t)screenW, (size_t)screenH};
    err = clEnqueueNDRangeKernel(queue, kernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
    clFinish(queue);
    clReleaseKernel(kernel);

    return output;
}