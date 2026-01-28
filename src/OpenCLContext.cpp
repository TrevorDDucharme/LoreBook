#include <OpenCLContext.hpp>
#include <plog/Log.h>

OpenCLContext& OpenCLContext::get() {
    static OpenCLContext instance;
    return instance;
}

OpenCLContext::~OpenCLContext() {
    cleanup();
}

bool OpenCLContext::init() {
    if (clReady) {
        PLOG_WARNING << "OpenCL already initialized";
        return true;
    }

    cl_int err;
    cl_uint numPlatforms = 0;

    // Get number of platforms
    err = clGetPlatformIDs(0, nullptr, &numPlatforms);
    if (err != CL_SUCCESS || numPlatforms == 0) {
        PLOG_WARNING << "No OpenCL platforms found (err=" << err << ", numPlatforms=" << numPlatforms << ")";
        // Diagnostic: try a single-slot call to gather more info and platform name if possible
        {
            cl_platform_id debugPlat = nullptr;
            cl_uint debugCount = 0;
            cl_int err2 = clGetPlatformIDs(1, &debugPlat, &debugCount);
            PLOG_WARNING << "Debug clGetPlatformIDs(1): err=" << err2 << ", numPlatforms=" << debugCount;
            if (err2 == CL_SUCCESS && debugPlat != nullptr) {
                char name[256] = {0};
                clGetPlatformInfo(debugPlat, CL_PLATFORM_NAME, sizeof(name)-1, name, nullptr);
                PLOG_WARNING << "Debug platform name: " << name;
            }
        }
        return false;
    }

    // Get first platform
    err = clGetPlatformIDs(1, &clPlatform, nullptr);
    if (err != CL_SUCCESS) {
        PLOG_ERROR << "Failed to get OpenCL platform";
        return false;
    }

    // Try to get GPU device first
    err = clGetDeviceIDs(clPlatform, CL_DEVICE_TYPE_GPU, 1, &clDevice, nullptr);
    if (err == CL_SUCCESS) {
        clDeviceIsGPU = true;
        PLOG_INFO << "Using OpenCL GPU device";
    } else {
        // Fall back to CPU
        err = clGetDeviceIDs(clPlatform, CL_DEVICE_TYPE_CPU, 1, &clDevice, nullptr);
        if (err != CL_SUCCESS) {
            PLOG_ERROR << "Failed to get OpenCL device";
            return false;
        }
        clDeviceIsGPU = false;
        PLOG_INFO << "Using OpenCL CPU device";
    }

    // Create context
    clContext = clCreateContext(nullptr, 1, &clDevice, nullptr, nullptr, &err);
    if (err != CL_SUCCESS) {
        PLOG_ERROR << "Failed to create OpenCL context";
        clDevice = nullptr;
        return false;
    }

    // Create command queue
    clQueue = clCreateCommandQueue(clContext, clDevice, 0, &err);
    if (err != CL_SUCCESS) {
        PLOG_ERROR << "Failed to create OpenCL command queue";
        clReleaseContext(clContext);
        clContext = nullptr;
        clDevice = nullptr;
        return false;
    }

    clReady = true;
    PLOG_INFO << "OpenCL initialized successfully";
    return true;
}

void OpenCLContext::cleanup() {
    if (clQueue) {
        clReleaseCommandQueue(clQueue);
        clQueue = nullptr;
    }
    if (clContext) {
        clReleaseContext(clContext);
        clContext = nullptr;
    }
    clDevice = nullptr;
    clPlatform = nullptr;
    clReady = false;
    clDeviceIsGPU = false;
}
