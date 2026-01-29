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

    // Create persistent debug buffer (always-on). If this fails we log but continue.
    try {
        ensureDebugBuffer();
    } catch (const std::exception &e) {
        fprintf(stderr, "[OpenCL Debug] failed to create persistent debug buffer: %s\n", e.what());
    }
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

// --- Tracked buffer helpers ---
cl_mem OpenCLContext::createBuffer(cl_mem_flags flags, size_t size, void* hostPtr, cl_int* err) {
    cl_int localErr = CL_SUCCESS;
    cl_mem mem = clCreateBuffer(clContext, flags, size, hostPtr, &localErr);
    if (localErr == CL_SUCCESS && mem != nullptr) {
        std::lock_guard<std::mutex> lk(memTrackMutex_);
        memSizes_[mem] = size;
        totalAllocated_ += size;
        //PLOG_INFO << "OpenCL alloc: mem=" << mem << " size=" << size << " totalAllocated=" << totalAllocated_;

        // Check device global memory and warn if we are nearing capacity
        cl_ulong deviceTotal = 0;
        cl_int diErr = clGetDeviceInfo(clDevice, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(deviceTotal), &deviceTotal, nullptr);
        if (diErr == CL_SUCCESS) {
            if (totalAllocated_ > (size_t)(deviceTotal * 9 / 10)) {
                PLOG_WARNING << "OpenCL memory usage high: totalAllocated=" << totalAllocated_ << " deviceTotal=" << deviceTotal;
            }
        } else {
            PLOG_WARNING << "Failed to query device memory (err=" << diErr << ")";
        }
    } else {
        PLOG_ERROR << "clCreateBuffer failed (err=" << localErr << ") size=" << size;
    }
    if (err) *err = localErr;
    return mem;
}

void OpenCLContext::releaseMem(cl_mem mem) {
    if (!mem) return;
    size_t size = 0;
    {
        std::lock_guard<std::mutex> lk(memTrackMutex_);
        auto it = memSizes_.find(mem);
        if (it != memSizes_.end()) { size = it->second; totalAllocated_ -= it->second; memSizes_.erase(it); }
    }
    cl_int e = clReleaseMemObject(mem);
    //PLOG_INFO << "OpenCL free: mem=" << mem << " freed=" << size << " totalAllocated=" << totalAllocated_ << " clReleaseErr=" << e;
}

void OpenCLContext::logMemoryUsage() const {
    size_t total = 0, count = 0;
    {
        std::lock_guard<std::mutex> lk(memTrackMutex_);
        total = totalAllocated_;
        count = memSizes_.size();
    }
    cl_ulong deviceTotal = 0;
    clGetDeviceInfo(clDevice, CL_DEVICE_GLOBAL_MEM_SIZE, sizeof(deviceTotal), &deviceTotal, nullptr);
    PLOG_INFO << "OpenCL Memory: allocated=" << total << " bytes in " << count << " buffers; deviceTotal=" << deviceTotal;
}

size_t OpenCLContext::getTotalAllocated() const { std::lock_guard<std::mutex> lk(memTrackMutex_); return totalAllocated_; }
