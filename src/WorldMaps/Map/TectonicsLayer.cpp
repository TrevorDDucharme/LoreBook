#include <WorldMaps/Map/TectonicsLayer.hpp>
#include <WorldMaps/World/World.hpp>
#include <tracy/Tracy.hpp>
#include <plog/Log.h>
#include <sstream>

// Static OpenCL program and kernel handles
static cl_program gTectonicsProgram = nullptr;
static cl_kernel gInitKernel = nullptr;
static cl_kernel gStepKernel = nullptr;
static cl_kernel gExtractKernel = nullptr;
static cl_kernel gProjectKernel = nullptr;

TectonicsLayer::~TectonicsLayer()
{
    releaseBuffers();
}

void TectonicsLayer::releaseBuffers()
{
    if (voxelBufferA) { OpenCLContext::get().releaseMem(voxelBufferA); voxelBufferA = nullptr; }
    if (voxelBufferB) { OpenCLContext::get().releaseMem(voxelBufferB); voxelBufferB = nullptr; }
    if (heightmapBuffer) { OpenCLContext::get().releaseMem(heightmapBuffer); heightmapBuffer = nullptr; }
    if (coloredBuffer) { OpenCLContext::get().releaseMem(coloredBuffer); coloredBuffer = nullptr; }
    if (surfaceBuffer) { OpenCLContext::get().releaseMem(surfaceBuffer); surfaceBuffer = nullptr; }
}

void TectonicsLayer::parseParameters(const std::string &params)
{
    auto lock = lockParameters();
    
    auto trim = [](std::string s) {
        auto not_ws = [](int ch) { return !std::isspace(ch); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_ws));
        s.erase(std::find_if(s.rbegin(), s.rend(), not_ws).base(), s.end());
        return s;
    };

    auto tokens = splitBracketAware(params, ",");
    for (const auto &token : tokens) {
        auto kv = splitBracketAware(token, ":");
        if (kv.size() != 2) continue;

        std::string key = trim(kv[0]);
        std::string value = trim(kv[1]);

        try {
            if (key == "steps") { simulationSteps = std::stoi(value); }
            else if (key == "uvres") { uvResolution = std::stoul(value); }
            else if (key == "dt") { dt = std::stof(value); }
            else if (key == "seed") { seed = std::stoul(value); }
            else if (key == "plates") { numPlates = std::stoi(value); }
        } catch (const std::exception &e) {
            PLOGW << "TectonicsLayer::parseParameters: failed to parse " << key << "=" << value;
        }
    }
}

cl_mem TectonicsLayer::sample()
{
    ZoneScopedN("TectonicsLayer::sample");
    
    if (!simulationComplete) {
        PLOGW << "TectonicsLayer::sample - starting simulation";
        if (initializeSimulation()) {
            runSimulation(simulationSteps);
            extractHeightmap();
            simulationComplete = true;
            PLOGW << "TectonicsLayer::sample - simulation complete";
        } else {
            PLOGE << "TectonicsLayer::sample - initialization failed";
            return nullptr;
        }
    }
    
    return heightmapBuffer;
}

cl_mem TectonicsLayer::getColor()
{
    // Convert scalar heightmap to grayscale RGBA
    cl_mem heights = sample();
    if (heights == nullptr) return nullptr;
    
    if (coloredBuffer == nullptr) {
        static std::vector<cl_float4> grayRamp = {
            MapLayer::rgba(0, 0, 0, 255),
            MapLayer::rgba(255, 255, 255, 255)
        };
        scalarToColor(coloredBuffer, heights,
                      parentWorld->getWorldLatitudeResolution(),
                      parentWorld->getWorldLongitudeResolution(),
                      2, grayRamp);
    }
    return coloredBuffer;
}

bool TectonicsLayer::initializeSimulation()
{
    ZoneScopedN("TectonicsLayer::initializeSimulation");
    
    if (!OpenCLContext::get().isReady()) {
        PLOGE << "TectonicsLayer: OpenCL not ready";
        return false;
    }
    
    cl_int err = CL_SUCCESS;
    
    // Build program if needed
    if (gTectonicsProgram == nullptr) {
        PLOGW << "TectonicsLayer: Building OpenCL program";
        OpenCLContext::get().createProgram(gTectonicsProgram, "Kernels/Tectonics.cl");
        if (gTectonicsProgram == nullptr) {
            PLOGE << "TectonicsLayer: Failed to build program";
            return false;
        }
        
        // Create kernels
        OpenCLContext::get().createKernelFromProgram(gInitKernel, gTectonicsProgram, "tec_init");
        OpenCLContext::get().createKernelFromProgram(gStepKernel, gTectonicsProgram, "tec_step");
        OpenCLContext::get().createKernelFromProgram(gExtractKernel, gTectonicsProgram, "tec_extract_height");
        OpenCLContext::get().createKernelFromProgram(gProjectKernel, gTectonicsProgram, "tec_cubemap_to_latlon");
        PLOGW << "TectonicsLayer: Kernels created";
    }
    
    // Allocate buffers
    size_t numVoxels = 6 * uvResolution * uvResolution;
    size_t voxelSize = numVoxels * sizeof(TecVoxel);
    
    PLOGW << "TectonicsLayer: Allocating " << (voxelSize / 1024) << " KB per voxel buffer";
    
    voxelBufferA = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, voxelSize, nullptr, &err, "tec voxels A");
    if (err != CL_SUCCESS) { PLOGE << "Failed to allocate voxelBufferA: " << err; return false; }
    
    voxelBufferB = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, voxelSize, nullptr, &err, "tec voxels B");
    if (err != CL_SUCCESS) { PLOGE << "Failed to allocate voxelBufferB: " << err; return false; }
    
    surfaceBuffer = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, numVoxels * sizeof(float), nullptr, &err, "tec surface");
    if (err != CL_SUCCESS) { PLOGE << "Failed to allocate surfaceBuffer: " << err; return false; }
    
    // Run initialization kernel
    cl_command_queue queue = OpenCLContext::get().getQueue();
    
    int uvRes = static_cast<int>(uvResolution);
    int nPlates = numPlates;
    cl_uint seedVal = static_cast<cl_uint>(seed);
    
    clSetKernelArg(gInitKernel, 0, sizeof(cl_mem), &voxelBufferA);
    clSetKernelArg(gInitKernel, 1, sizeof(int), &uvRes);
    clSetKernelArg(gInitKernel, 2, sizeof(int), &nPlates);
    clSetKernelArg(gInitKernel, 3, sizeof(cl_uint), &seedVal);
    
    size_t global[2] = { 6 * uvResolution, uvResolution };
    PLOGW << "TectonicsLayer: Running tec_init (" << global[0] << "x" << global[1] << ")";
    
    err = clEnqueueNDRangeKernel(queue, gInitKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        PLOGE << "TectonicsLayer: tec_init failed: " << err;
        return false;
    }
    
    clFinish(queue);
    PLOGW << "TectonicsLayer: Initialization complete";
    return true;
}

void TectonicsLayer::runSimulation(int steps)
{
    ZoneScopedN("TectonicsLayer::runSimulation");
    
    if (!OpenCLContext::get().isReady() || voxelBufferA == nullptr) {
        PLOGE << "TectonicsLayer: Cannot run simulation - not initialized";
        return;
    }
    
    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;
    
    int uvRes = static_cast<int>(uvResolution);
    
    cl_mem* inputBuffer = &voxelBufferA;
    cl_mem* outputBuffer = &voxelBufferB;
    
    PLOGW << "TectonicsLayer: Running " << steps << " simulation steps";
    
    size_t global[2] = { 6 * uvResolution, uvResolution };
    
    for (int step = 0; step < steps; step++) {
        clSetKernelArg(gStepKernel, 0, sizeof(cl_mem), inputBuffer);
        clSetKernelArg(gStepKernel, 1, sizeof(cl_mem), outputBuffer);
        clSetKernelArg(gStepKernel, 2, sizeof(int), &uvRes);
        clSetKernelArg(gStepKernel, 3, sizeof(float), &dt);
        
        err = clEnqueueNDRangeKernel(queue, gStepKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            PLOGE << "TectonicsLayer: tec_step failed at step " << step << ": " << err;
            return;
        }
        
        // Ping-pong buffers
        std::swap(inputBuffer, outputBuffer);
    }
    
    // Make sure voxelBufferA has the final result
    if (inputBuffer != &voxelBufferA) {
        std::swap(voxelBufferA, voxelBufferB);
    }
    
    clFinish(queue);
    PLOGW << "TectonicsLayer: Simulation complete";
}

void TectonicsLayer::extractHeightmap()
{
    ZoneScopedN("TectonicsLayer::extractHeightmap");
    
    if (!OpenCLContext::get().isReady() || voxelBufferA == nullptr) {
        PLOGE << "TectonicsLayer: Cannot extract heightmap - not ready";
        return;
    }
    
    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;
    
    int uvRes = static_cast<int>(uvResolution);
    int nPlates = numPlates;
    cl_uint seedVal = static_cast<cl_uint>(seed);
    
    // Extract height from pressure
    clSetKernelArg(gExtractKernel, 0, sizeof(cl_mem), &voxelBufferA);
    clSetKernelArg(gExtractKernel, 1, sizeof(cl_mem), &surfaceBuffer);
    clSetKernelArg(gExtractKernel, 2, sizeof(int), &uvRes);
    clSetKernelArg(gExtractKernel, 3, sizeof(int), &nPlates);
    clSetKernelArg(gExtractKernel, 4, sizeof(cl_uint), &seedVal);
    
    size_t global[2] = { 6 * uvResolution, uvResolution };
    PLOGW << "TectonicsLayer: Running tec_extract_height";
    
    err = clEnqueueNDRangeKernel(queue, gExtractKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        PLOGE << "TectonicsLayer: tec_extract_height failed: " << err;
        return;
    }
    
    // Project to lat/lon
    int latRes = parentWorld ? parentWorld->getWorldLatitudeResolution() : 4096;
    int lonRes = parentWorld ? parentWorld->getWorldLongitudeResolution() : 4096;
    
    cubemapToLatLon(surfaceBuffer, heightmapBuffer, latRes, lonRes);
    
    clFinish(queue);
    PLOGW << "TectonicsLayer: Heightmap extraction complete";
}

void TectonicsLayer::cubemapToLatLon(cl_mem cubemapSurface, cl_mem& latlonOutput,
                                      int latRes, int lonRes)
{
    ZoneScopedN("TectonicsLayer::cubemapToLatLon");
    
    cl_int err = CL_SUCCESS;
    
    // Allocate output buffer if needed
    size_t outputSize = static_cast<size_t>(latRes) * static_cast<size_t>(lonRes) * sizeof(float);
    
    if (latlonOutput == nullptr) {
        PLOGW << "TectonicsLayer: Allocating lat/lon buffer (" << (outputSize / (1024*1024)) << " MB)";
        latlonOutput = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, outputSize,
                                                          nullptr, &err, "tec latlon");
        if (err != CL_SUCCESS) {
            PLOGE << "TectonicsLayer: Failed to allocate lat/lon buffer: " << err;
            return;
        }
    }
    
    int uvRes = static_cast<int>(uvResolution);
    
    clSetKernelArg(gProjectKernel, 0, sizeof(cl_mem), &cubemapSurface);
    clSetKernelArg(gProjectKernel, 1, sizeof(cl_mem), &latlonOutput);
    clSetKernelArg(gProjectKernel, 2, sizeof(int), &uvRes);
    clSetKernelArg(gProjectKernel, 3, sizeof(int), &latRes);
    clSetKernelArg(gProjectKernel, 4, sizeof(int), &lonRes);
    
    size_t global[2] = { static_cast<size_t>(latRes), static_cast<size_t>(lonRes) };
    PLOGW << "TectonicsLayer: Running tec_cubemap_to_latlon";
    
    err = clEnqueueNDRangeKernel(OpenCLContext::get().getQueue(), gProjectKernel, 
                                  2, nullptr, global, nullptr, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        PLOGE << "TectonicsLayer: tec_cubemap_to_latlon failed: " << err;
    }
}
