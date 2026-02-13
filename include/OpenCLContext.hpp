#pragma once
#include <CL/cl.h>
#include <CL/cl_gl.h>
#include <string>
#include <stdexcept>
#include <cstring>
#include <LoreBook_Resources/LoreBook_ResourcesEmbeddedVFS.hpp>
#include <unordered_map>
#include <mutex>
#include <cstddef>
#define TRACY_ENABLE
#include <tracy/Tracy.hpp>
#include <tracy/TracyOpenCL.hpp>
#include <regex>
#include <unordered_set>
#include <GL/glew.h>

inline static std::string normalizePath(const std::string &p) {
    std::vector<std::string> parts;
    std::string cur;
    for (char c : p) {
        if (c == '/' || c == '\\') {
            if (!cur.empty()) parts.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    if (!cur.empty()) parts.push_back(cur);
    std::vector<std::string> out;
    for (auto &seg : parts) {
        if (seg == ".") continue;
        if (seg == "..") {
            if (!out.empty()) out.pop_back();
        } else {
            out.push_back(seg);
        }
    }
    std::string res;
    for (size_t i = 0; i < out.size(); ++i) {
        if (i) res += '/';
        res += out[i];
    }
    return res;
}

inline static std::string preprocessCLIncludes(const std::string &entryPath)
{
    std::unordered_set<std::string> included;
    std::vector<std::string> includeStack;

    std::function<std::string(const std::string&)> process = [&](const std::string &path) -> std::string {
        std::string norm = normalizePath(path);
        if (included.count(norm)) {
            return std::string("// [skipped duplicate include] ") + norm + "\n";
        }
        included.insert(norm);
        includeStack.push_back(norm);
        std::string content;
        try {
            content = loadLoreBook_ResourcesEmbeddedFileAsString(norm.c_str());
        } catch (const std::exception &ex) {
            // build a helpful chain for diagnostics
            std::string chain;
            for (size_t i = 0; i < includeStack.size(); ++i) {
                chain += includeStack[i];
                if (i + 1 < includeStack.size()) chain += " -> ";
            }
            throw std::runtime_error(std::string("Failed to load include '") + norm + "' included from: " + chain + " : " + ex.what());
        }

        // directory of current file for resolving relative includes
        std::string dir;
        size_t ppos = norm.find_last_of('/');
        if (ppos != std::string::npos) dir = norm.substr(0, ppos + 1);

        std::regex inc_re(R"(^\s*#\s*include\s*\"([^\"]+)\")");
        std::istringstream iss(content);
        std::string out;
        std::string line;
        while (std::getline(iss, line)) {
            std::smatch m;
            if (std::regex_search(line, m, inc_re)) {
                std::string inc = m[1].str();
                std::string resolved = inc;
                if (!inc.empty() && inc[0] != '/') resolved = dir + inc;
                resolved = normalizePath(resolved);
                out += std::string("// Begin include: ") + resolved + "\n";
                out += process(resolved);
                out += std::string("// End include: ") + resolved + "\n";
            } else {
                out += line;
                out.push_back('\n');
            }
        }
        includeStack.pop_back();
        return out;
    };

    return process(entryPath);
}

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

    //program and kernel helpers
    void createProgram(cl_program& program,std::string file_path);
    void createKernelFromProgram(cl_kernel& kernel,cl_program program, const std::string &kernelName);

    // CL/GL interop — call after GL context is active
    bool initGLInterop();
    bool hasGLInterop() const { return clGLInterop; }

    // Create a CL image from an existing GL texture (requires GL interop)
    cl_mem createFromGLTexture(GLuint texID, cl_mem_flags flags, GLenum target = GL_TEXTURE_2D, GLint mipLevel = 0);

    // Acquire/release GL objects for CL usage
    void acquireGLObjects(cl_mem *memObjects, int count);
    void releaseGLObjects(cl_mem *memObjects, int count);

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
    bool clGLInterop = false;
    bool clGLInteropAttempted = false;

    // Persistent debug buffer (always-on)
    cl_mem debugBuf_ = nullptr;

    // tracking allocations
    mutable std::mutex memTrackMutex_;
    std::unordered_map<cl_mem, size_t> memSizes_;
    size_t totalAllocated_ = 0;
};


static cl_program gPerlinProgram = nullptr;

static void perlin(cl_mem& output,
                     int latitudeResolution,
                     int longitudeResolution,
                     float frequency,
                     float lacunarity,
                     int octaves,
                     float persistence,
                     unsigned int seed)
{
    ZoneScopedN("Perlin");
    if (!OpenCLContext::get().isReady())
        return;

    cl_context ctx = OpenCLContext::get().getContext();
    cl_device_id device = OpenCLContext::get().getDevice();
    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;

    static cl_kernel gPerlinKernel = nullptr;
    try{
        OpenCLContext::get().createProgram(gPerlinProgram,"Kernels/Perlin.cl");
        OpenCLContext::get().createKernelFromProgram(gPerlinKernel,gPerlinProgram,"perlin_fbm_3d_sphere_sample");
    }
    catch (const std::runtime_error &e)
    {
        printf("Error initializing Perlin OpenCL: %s\n", e.what());
        return;
    }

    {
        ZoneScopedN("Perlin Buffer Alloc");
        size_t total = (size_t)latitudeResolution * (size_t)longitudeResolution * sizeof(float);
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
                TracyMessageL("Reallocating perlin output buffer required");
                OpenCLContext::get().releaseMem(output);
                output = nullptr;
            }
        }

        if(output == nullptr){
            TracyMessageL("Allocating perlin output buffer");
            output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, total, nullptr, &err, "perlin output");
            if (err != CL_SUCCESS || output == nullptr)
            {
                throw std::runtime_error("clCreateBuffer failed for perlin output");
            }
        }   
    }

    clSetKernelArg(gPerlinKernel, 0, sizeof(cl_mem), &output);
    clSetKernelArg(gPerlinKernel, 1, sizeof(int), &latitudeResolution);
    clSetKernelArg(gPerlinKernel, 2, sizeof(int), &longitudeResolution);
    clSetKernelArg(gPerlinKernel, 3, sizeof(float), &frequency);
    clSetKernelArg(gPerlinKernel, 4, sizeof(float), &lacunarity);
    clSetKernelArg(gPerlinKernel, 5, sizeof(int), &octaves);
    clSetKernelArg(gPerlinKernel, 6, sizeof(float), &persistence);
    clSetKernelArg(gPerlinKernel, 7, sizeof(unsigned int), &seed);

    size_t global[2] = {(size_t)latitudeResolution, (size_t)longitudeResolution};
    {
        ZoneScopedN("Perlin Enqueue");
        err = clEnqueueNDRangeKernel(queue, gPerlinKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS)
        {
            throw std::runtime_error("clEnqueueNDRangeKernel failed for perlin");
        }
    }
}

static void perlin_channels(cl_mem& output,
                     int latitudeResolution,
                     int longitudeResolution,
                     int channels,
                     std::vector<float> frequency,
                     std::vector<float> lacunarity,
                     std::vector<int> octaves,
                     std::vector<float> persistence,
                     std::vector<unsigned int> seed)
{
    ZoneScopedN("Perlin Channels");
    if (!OpenCLContext::get().isReady())
        return;

    cl_context ctx = OpenCLContext::get().getContext();
    cl_device_id device = OpenCLContext::get().getDevice();
    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;


    static cl_kernel gPerlinChannelsKernel = nullptr;

    try{
        OpenCLContext::get().createProgram(gPerlinProgram,"Kernels/Perlin.cl");
        OpenCLContext::get().createKernelFromProgram(gPerlinChannelsKernel,gPerlinProgram,"perlin_fbm_3d_sphere_sample_channels");
    }
    catch (const std::runtime_error &e)
    {
        printf("Error initializing PerlinChannels OpenCL: %s\n", e.what());
        return;
    }

    {
        ZoneScopedN("Perlin Channels Buffer Alloc");
        size_t total = (size_t)channels * (size_t)latitudeResolution * (size_t)longitudeResolution * sizeof(float);
        size_t buffer_size;
        if(output != nullptr){
            cl_int err = clGetMemObjectInfo(output,
                                            CL_MEM_SIZE,
                                            sizeof(buffer_size),
                                            &buffer_size,
                                            NULL);
            if (err != CL_SUCCESS) {
                throw std::runtime_error("clGetMemObjectInfo failed for perlin channels output buffer size");
            }
            if(buffer_size < total){
                TracyMessageL("Reallocating perlin channels output buffer required");
                OpenCLContext::get().releaseMem(output);
                output = nullptr;
            }
        }

        if(output == nullptr){
            TracyMessageL("Allocating perlin channels output buffer");
            output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, total, nullptr, &err, "perlin channels output");
            if (err != CL_SUCCESS || output == nullptr)
            {
                throw std::runtime_error("clCreateBuffer failed for perlin channels output");
            }
        }   
    }

    //create buffers for parameters
    cl_mem freqBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(float) * frequency.size(), frequency.data(), &err, "perlin channels freqBuf");
    cl_mem lacunarityBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(float) * lacunarity.size(), lacunarity.data(), &err, "perlin channels lacunarityBuf");
    cl_mem octavesBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(int) * octaves.size(), octaves.data(), &err, "perlin channels octavesBuf");
    cl_mem persistenceBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(float) * persistence.size(), persistence.data(), &err, "perlin channels persistenceBuf");
    cl_mem seedBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(unsigned int) * seed.size(), seed.data(), &err, "perlin channels seedBuf");

    clSetKernelArg(gPerlinChannelsKernel, 0, sizeof(cl_mem), &output);
    clSetKernelArg(gPerlinChannelsKernel, 1, sizeof(int), &latitudeResolution);
    clSetKernelArg(gPerlinChannelsKernel, 2, sizeof(int), &longitudeResolution);
    clSetKernelArg(gPerlinChannelsKernel, 3, sizeof(int), &channels);
    clSetKernelArg(gPerlinChannelsKernel, 4, sizeof(cl_mem), &freqBuf);
    clSetKernelArg(gPerlinChannelsKernel, 5, sizeof(cl_mem), &lacunarityBuf);
    clSetKernelArg(gPerlinChannelsKernel, 6, sizeof(cl_mem), &octavesBuf);
    clSetKernelArg(gPerlinChannelsKernel, 7, sizeof(cl_mem), &persistenceBuf);
    clSetKernelArg(gPerlinChannelsKernel, 8, sizeof(cl_mem), &seedBuf);
    
    size_t global[2] = {(size_t)latitudeResolution, (size_t)longitudeResolution};
    {
        ZoneScopedN("Perlin Channels Enqueue");
        err = clEnqueueNDRangeKernel(queue, gPerlinChannelsKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS)
        {
            throw std::runtime_error("clEnqueueNDRangeKernel failed for perlin channels");
        }
    }
}

static cl_program gScalarToColorProgram = nullptr;

static void scalarToColor(cl_mem& output, 
                            cl_mem scalarBuffer,
                            int latitudeResolution,
                            int longitudeResolution,
                            int colorCount,
                            const std::vector<cl_float4> &paletteColors)
{
    ZoneScopedN("ScalarToColor");
    if (!OpenCLContext::get().isReady())
        return;

    cl_context ctx = OpenCLContext::get().getContext();
    cl_device_id device = OpenCLContext::get().getDevice();
    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;

    static cl_kernel gScalarToColorKernel = nullptr;

    try{
        OpenCLContext::get().createProgram(gScalarToColorProgram,"Kernels/ScalarToColor.cl");
        OpenCLContext::get().createKernelFromProgram(gScalarToColorKernel,gScalarToColorProgram,"scalar_to_rgba_float4");
    }
    catch (const std::runtime_error &e)
    {
        printf("Error initializing ScalarToColor OpenCL: %s\n", e.what());
        return;
    }

    // create palette buffer
    cl_mem paletteBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(cl_float4) * paletteColors.size(), (void*)paletteColors.data(), &err, "scalarToColor paletteBuf");
    if (err != CL_SUCCESS || paletteBuf == nullptr)
    {
        throw std::runtime_error("clCreateBuffer failed for scalarToColor paletteBuf");
    }

    size_t voxels = (size_t)latitudeResolution * (size_t)longitudeResolution;
    size_t outSize = voxels * sizeof(cl_float4);

    if(output != nullptr){
        TracyMessageL("Reallocating scalarToColor output buffer required");
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
        TracyMessageL("Allocating scalarToColor output buffer");
        output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, outSize, nullptr, &err, "scalarToColor output");
        if (err != CL_SUCCESS || output == nullptr)
        {
            OpenCLContext::get().releaseMem(paletteBuf);
            throw std::runtime_error("clCreateBuffer failed for scalarToColor output");
        }
    }

    clSetKernelArg(gScalarToColorKernel, 0, sizeof(cl_mem), &scalarBuffer);
    clSetKernelArg(gScalarToColorKernel, 1, sizeof(int), &latitudeResolution);
    clSetKernelArg(gScalarToColorKernel, 2, sizeof(int), &longitudeResolution);
    clSetKernelArg(gScalarToColorKernel, 3, sizeof(int), &colorCount);
    clSetKernelArg(gScalarToColorKernel, 4, sizeof(cl_mem), &paletteBuf);
    clSetKernelArg(gScalarToColorKernel, 5, sizeof(cl_mem), &output);

    size_t global[2] = {(size_t)latitudeResolution, (size_t)longitudeResolution};
    
    {
        ZoneScopedN("ScalarToColor Enqueue");
        err = clEnqueueNDRangeKernel(queue, gScalarToColorKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
    }

    OpenCLContext::get().releaseMem(paletteBuf);
}

static void weightedScalarToColor(cl_mem& output, 
                            cl_mem scalarBuffer,
                            int latitudeResolution,
                            int longitudeResolution,
                            int colorCount,
                            const std::vector<cl_float4> &paletteColors,
                            const std::vector<float> &weights)
{
    ZoneScopedN("WeightedScalarToColor");
    if (!OpenCLContext::get().isReady())
        return;

    cl_context ctx = OpenCLContext::get().getContext();
    cl_device_id device = OpenCLContext::get().getDevice();
    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;
    static cl_kernel gWeightedScalarToColorKernel = nullptr;
    try{
        OpenCLContext::get().createProgram(gScalarToColorProgram,"Kernels/ScalarToColor.cl");
        OpenCLContext::get().createKernelFromProgram(gWeightedScalarToColorKernel,gScalarToColorProgram,"weighted_scalar_to_rgba_float4");
    }
    catch (const std::runtime_error &e)
    {
        printf("Error initializing WeightedScalarToColor OpenCL: %s\n", e.what());
        return;
    }
    // create palette buffer
    cl_mem paletteBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(cl_float4) * paletteColors.size(), (void*)paletteColors.data(), &err, "weightedScalarToColor paletteBuf");
    if (err != CL_SUCCESS || paletteBuf == nullptr)
    {
        throw std::runtime_error("clCreateBuffer failed for weightedScalarToColor paletteBuf");
    }
    // create weights buffer
    cl_mem weightsBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(float) * weights.size(), (void*)weights.data(), &err, "weightedScalarToColor weightsBuf");
    if (err != CL_SUCCESS || weightsBuf == nullptr)
    {
        OpenCLContext::get().releaseMem(paletteBuf);
        throw std::runtime_error("clCreateBuffer failed for weightedScalarToColor weightsBuf");
    }
    size_t voxels = (size_t)latitudeResolution * (size_t)longitudeResolution;
    size_t outSize = voxels * sizeof(cl_float4);
    if(output != nullptr){
        TracyMessageL("Reallocating weightedScalarToColor output buffer required");
        cl_int err = clGetMemObjectInfo(output,
                                        CL_MEM_SIZE,
                                        sizeof(size_t),
                                        &outSize,
                                        NULL);
        if (err != CL_SUCCESS) {
            throw std::runtime_error("clGetMemObjectInfo failed for weightedScalarToColor output buffer size");
        }
        if(outSize < voxels * sizeof(cl_float4)){
            OpenCLContext::get().releaseMem(output);
            output = nullptr;
        }
    }
    if(output == nullptr){
        TracyMessageL("Allocating weightedScalarToColor output buffer");
        output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, outSize, nullptr, &err, "weightedScalarToColor output");
        if (err != CL_SUCCESS || output == nullptr)
        {
            OpenCLContext::get().releaseMem(paletteBuf);
            OpenCLContext::get().releaseMem(weightsBuf);
            throw std::runtime_error("clCreateBuffer failed for weightedScalarToColor output");
        }
    }
    clSetKernelArg(gWeightedScalarToColorKernel, 0, sizeof(cl_mem), &scalarBuffer);
    clSetKernelArg(gWeightedScalarToColorKernel, 1, sizeof(int), &latitudeResolution);
    clSetKernelArg(gWeightedScalarToColorKernel, 2, sizeof(int), &longitudeResolution);
    clSetKernelArg(gWeightedScalarToColorKernel, 3, sizeof(int), &colorCount);
    clSetKernelArg(gWeightedScalarToColorKernel, 4, sizeof(cl_mem), &paletteBuf);
    clSetKernelArg(gWeightedScalarToColorKernel, 5, sizeof(cl_mem), &weightsBuf);
    clSetKernelArg(gWeightedScalarToColorKernel, 6, sizeof(cl_mem), &output);
    size_t global[2] = {(size_t)latitudeResolution, (size_t)longitudeResolution};
    
    {
        ZoneScopedN("WeightedScalarToColor Enqueue");
        err = clEnqueueNDRangeKernel(queue, gWeightedScalarToColorKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
    }
    OpenCLContext::get().releaseMem(paletteBuf);
    OpenCLContext::get().releaseMem(weightsBuf);
}

static cl_program gAlphaBlendProgram = nullptr;

static void alphaBlend(cl_mem& output,
                       cl_mem input_one,
                       cl_mem input_two,
                       int width,
                       int height)
{
    ZoneScopedN("AlphaBlend");
    if (!OpenCLContext::get().isReady())
        return;

    cl_context ctx = OpenCLContext::get().getContext();
    cl_device_id device = OpenCLContext::get().getDevice();
    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;

    static cl_kernel gAlphaBlendKernel = nullptr;

    try{
        OpenCLContext::get().createProgram(gAlphaBlendProgram,"Kernels/AlphaBlend.cl");
        OpenCLContext::get().createKernelFromProgram(gAlphaBlendKernel,gAlphaBlendProgram,"AlphaBlend");
    }
    catch (const std::runtime_error &e)
    {
        printf("Error initializing AlphaBlend OpenCL: %s\n", e.what());
        return;
    }

    {
        ZoneScopedN("AlphaBlend Buffer Alloc");
        size_t total = (size_t)width * (size_t)height * sizeof(cl_float4);
        size_t buffer_size;
        if(output != nullptr){
            cl_int err = clGetMemObjectInfo(output,
                                            CL_MEM_SIZE,
                                            sizeof(buffer_size),
                                            &buffer_size,
                                            NULL);
            if (err != CL_SUCCESS) {
                throw std::runtime_error("clGetMemObjectInfo failed for alphaBlend output buffer size");
            }
            if(buffer_size < total){
                TracyMessageL("Reallocating alphaBlend output buffer required");
                OpenCLContext::get().releaseMem(output);
                output = nullptr;
            }
        }

        if(output == nullptr){
            TracyMessageL("Allocating alphaBlend output buffer");
            output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, total, nullptr, &err, "alphaBlend output");
            if (err != CL_SUCCESS || output == nullptr)
            {
                throw std::runtime_error("clCreateBuffer failed for alphaBlend output");
            }
        }   
    }

    clSetKernelArg(gAlphaBlendKernel, 0, sizeof(cl_mem), &input_one);
    clSetKernelArg(gAlphaBlendKernel, 1, sizeof(cl_mem), &input_two);
    clSetKernelArg(gAlphaBlendKernel, 2, sizeof(cl_mem), &output);
    clSetKernelArg(gAlphaBlendKernel, 3, sizeof(int), &width);
    clSetKernelArg(gAlphaBlendKernel, 4, sizeof(int), &height);

    size_t global[2] = {(size_t)width, (size_t)height};
    {
        ZoneScopedN("AlphaBlend Enqueue");
        err = clEnqueueNDRangeKernel(queue, gAlphaBlendKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS)
        {
            throw std::runtime_error("clEnqueueNDRangeKernel failed for alphaBlend");
        }
    }
}

static cl_program gMultiplyProgram = nullptr;

static void multiplyColor(cl_mem& output,
                          cl_mem input_one,
                          cl_mem input_two,
                          int width,
                          int height)
{
    ZoneScopedN("MultiplyColor");
    if (!OpenCLContext::get().isReady())
        return;

    cl_context ctx = OpenCLContext::get().getContext();
    cl_device_id device = OpenCLContext::get().getDevice();
    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;

    static cl_kernel gMultiplyKernel = nullptr;

    try{
        OpenCLContext::get().createProgram(gMultiplyProgram,"Kernels/Multiply.cl");
        OpenCLContext::get().createKernelFromProgram(gMultiplyKernel,gMultiplyProgram,"multiplyColor");
    }
    catch (const std::runtime_error &e)
    {
        printf("Error initializing MultiplyColor OpenCL: %s\n", e.what());
        return;
    }

    {
        ZoneScopedN("MultiplyColor Buffer Alloc");
        size_t total = (size_t)width * (size_t)height * sizeof(cl_float4);
        size_t buffer_size;
        if(output != nullptr){
            cl_int err = clGetMemObjectInfo(output,
                                            CL_MEM_SIZE,
                                            sizeof(buffer_size),
                                            &buffer_size,
                                            NULL);
            if (err != CL_SUCCESS) {
                throw std::runtime_error("clGetMemObjectInfo failed for multiplyColor output buffer size");
            }
            if(buffer_size < total){
                TracyMessageL("Reallocating multiplyColor output buffer required");
                OpenCLContext::get().releaseMem(output);
                output = nullptr;
            }
        }

        if(output == nullptr){
            TracyMessageL("Allocating multiplyColor output buffer");
            output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, total, nullptr, &err, "multiplyColor output");
            if (err != CL_SUCCESS || output == nullptr)
            {
                throw std::runtime_error("clCreateBuffer failed for multiplyColor output");
            }
        }   
    }

    clSetKernelArg(gMultiplyKernel, 0, sizeof(cl_mem), &input_one);
    clSetKernelArg(gMultiplyKernel, 1, sizeof(cl_mem), &input_two);
    clSetKernelArg(gMultiplyKernel, 2, sizeof(cl_mem), &output);
    clSetKernelArg(gMultiplyKernel, 3, sizeof(int), &width);
    clSetKernelArg(gMultiplyKernel, 4, sizeof(int), &height);

    size_t global[2] = {(size_t)width, (size_t)height};
    {
        ZoneScopedN("MultiplyColor Enqueue");
        err = clEnqueueNDRangeKernel(queue, gMultiplyKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS)
        {
            throw std::runtime_error("clEnqueueNDRangeKernel failed for multiplyColor");
        }
    }
}

// ─────────────────────────────────────────────────────────────────────
// Region-bounded Perlin noise on a sphere sub-region
// ─────────────────────────────────────────────────────────────────────

static cl_program gPerlinRegionProgram = nullptr;

/// Generate Perlin FBM noise for a sub-region of the sphere.
///
/// The region is specified in colatitude (theta ∈ [0,π]) and azimuth
/// (phi ∈ [0,2π]).  Use lonLatToThetaPhi() to convert.
///
/// output: cl_mem float buffer of resLat × resLon.
static void perlinRegion(cl_mem& output,
                         int resLat, int resLon,
                         float thetaMin, float thetaMax,
                         float phiMin,   float phiMax,
                         float frequency, float lacunarity,
                         int octaves, float persistence,
                         unsigned int seed)
{
    ZoneScopedN("PerlinRegion");
    if (!OpenCLContext::get().isReady())
        return;

    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;

    static cl_kernel gPerlinRegionKernel = nullptr;
    try {
        OpenCLContext::get().createProgram(gPerlinRegionProgram, "Kernels/PerlinRegion.cl");
        OpenCLContext::get().createKernelFromProgram(gPerlinRegionKernel,
                                                     gPerlinRegionProgram,
                                                     "perlin_fbm_3d_sphere_region");
    } catch (const std::runtime_error &e) {
        printf("Error initializing PerlinRegion OpenCL: %s\n", e.what());
        return;
    }

    {
        ZoneScopedN("PerlinRegion Buffer Alloc");
        size_t total = (size_t)resLat * (size_t)resLon * sizeof(float);
        size_t existing = 0;
        if (output != nullptr) {
            err = clGetMemObjectInfo(output, CL_MEM_SIZE, sizeof(existing), &existing, NULL);
            if (err != CL_SUCCESS || existing < total) {
                OpenCLContext::get().releaseMem(output);
                output = nullptr;
            }
        }
        if (output == nullptr) {
            output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, total,
                                                       nullptr, &err, "perlinRegion output");
            if (err != CL_SUCCESS || output == nullptr)
                throw std::runtime_error("clCreateBuffer failed for perlinRegion output");
        }
    }

    clSetKernelArg(gPerlinRegionKernel,  0, sizeof(cl_mem), &output);
    clSetKernelArg(gPerlinRegionKernel,  1, sizeof(int),    &resLat);
    clSetKernelArg(gPerlinRegionKernel,  2, sizeof(int),    &resLon);
    clSetKernelArg(gPerlinRegionKernel,  3, sizeof(float),  &thetaMin);
    clSetKernelArg(gPerlinRegionKernel,  4, sizeof(float),  &thetaMax);
    clSetKernelArg(gPerlinRegionKernel,  5, sizeof(float),  &phiMin);
    clSetKernelArg(gPerlinRegionKernel,  6, sizeof(float),  &phiMax);
    clSetKernelArg(gPerlinRegionKernel,  7, sizeof(float),  &frequency);
    clSetKernelArg(gPerlinRegionKernel,  8, sizeof(float),  &lacunarity);
    clSetKernelArg(gPerlinRegionKernel,  9, sizeof(int),    &octaves);
    clSetKernelArg(gPerlinRegionKernel, 10, sizeof(float),  &persistence);
    clSetKernelArg(gPerlinRegionKernel, 11, sizeof(unsigned int), &seed);

    size_t global[2] = { (size_t)resLat, (size_t)resLon };
    {
        ZoneScopedN("PerlinRegion Enqueue");
        err = clEnqueueNDRangeKernel(queue, gPerlinRegionKernel, 2,
                                      nullptr, global, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS)
            throw std::runtime_error("clEnqueueNDRangeKernel failed for perlinRegion");
    }
}

/// Multi-channel region-bounded Perlin noise on a sphere sub-region.
/// Output layout: output[c * resLat * resLon + latIdx * resLon + lonIdx].
static void perlinRegionChannels(cl_mem& output,
                                  int resLat, int resLon,
                                  float thetaMin, float thetaMax,
                                  float phiMin,   float phiMax,
                                  int channels,
                                  const std::vector<float>& frequency,
                                  const std::vector<float>& lacunarity,
                                  const std::vector<int>& octaves,
                                  const std::vector<float>& persistence,
                                  const std::vector<unsigned int>& seed)
{
    ZoneScopedN("PerlinRegionChannels");
    if (!OpenCLContext::get().isReady()) return;

    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;

    static cl_kernel gPerlinRegionChannelsKernel = nullptr;
    try {
        OpenCLContext::get().createProgram(gPerlinRegionProgram, "Kernels/PerlinRegion.cl");
        OpenCLContext::get().createKernelFromProgram(gPerlinRegionChannelsKernel,
                                                     gPerlinRegionProgram,
                                                     "perlin_fbm_3d_sphere_region_channels");
    } catch (const std::runtime_error &e) {
        printf("Error initializing PerlinRegionChannels OpenCL: %s\n", e.what());
        return;
    }

    {
        ZoneScopedN("PerlinRegionChannels Buffer Alloc");
        size_t total = (size_t)channels * resLat * resLon * sizeof(float);
        size_t existing = 0;
        if (output != nullptr) {
            err = clGetMemObjectInfo(output, CL_MEM_SIZE, sizeof(existing), &existing, NULL);
            if (err != CL_SUCCESS || existing < total) {
                OpenCLContext::get().releaseMem(output);
                output = nullptr;
            }
        }
        if (output == nullptr) {
            output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, total,
                                                       nullptr, &err, "perlinRegionChannels output");
            if (err != CL_SUCCESS || output == nullptr)
                throw std::runtime_error("clCreateBuffer failed for perlinRegionChannels output");
        }
    }

    // Upload per-channel parameter arrays
    cl_mem freqBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        sizeof(float) * frequency.size(), (void*)frequency.data(), &err, "perlinRegionCh freqBuf");
    cl_mem lacBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        sizeof(float) * lacunarity.size(), (void*)lacunarity.data(), &err, "perlinRegionCh lacBuf");
    cl_mem octBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        sizeof(int) * octaves.size(), (void*)octaves.data(), &err, "perlinRegionCh octBuf");
    cl_mem persBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        sizeof(float) * persistence.size(), (void*)persistence.data(), &err, "perlinRegionCh persBuf");
    cl_mem seedBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        sizeof(unsigned int) * seed.size(), (void*)seed.data(), &err, "perlinRegionCh seedBuf");

    clSetKernelArg(gPerlinRegionChannelsKernel,  0, sizeof(cl_mem), &output);
    clSetKernelArg(gPerlinRegionChannelsKernel,  1, sizeof(int),    &resLat);
    clSetKernelArg(gPerlinRegionChannelsKernel,  2, sizeof(int),    &resLon);
    clSetKernelArg(gPerlinRegionChannelsKernel,  3, sizeof(float),  &thetaMin);
    clSetKernelArg(gPerlinRegionChannelsKernel,  4, sizeof(float),  &thetaMax);
    clSetKernelArg(gPerlinRegionChannelsKernel,  5, sizeof(float),  &phiMin);
    clSetKernelArg(gPerlinRegionChannelsKernel,  6, sizeof(float),  &phiMax);
    clSetKernelArg(gPerlinRegionChannelsKernel,  7, sizeof(int),    &channels);
    clSetKernelArg(gPerlinRegionChannelsKernel,  8, sizeof(cl_mem), &freqBuf);
    clSetKernelArg(gPerlinRegionChannelsKernel,  9, sizeof(cl_mem), &lacBuf);
    clSetKernelArg(gPerlinRegionChannelsKernel, 10, sizeof(cl_mem), &octBuf);
    clSetKernelArg(gPerlinRegionChannelsKernel, 11, sizeof(cl_mem), &persBuf);
    clSetKernelArg(gPerlinRegionChannelsKernel, 12, sizeof(cl_mem), &seedBuf);

    size_t global[2] = { (size_t)resLat, (size_t)resLon };
    {
        ZoneScopedN("PerlinRegionChannels Enqueue");
        err = clEnqueueNDRangeKernel(queue, gPerlinRegionChannelsKernel, 2,
                                      nullptr, global, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS)
            throw std::runtime_error("clEnqueueNDRangeKernel failed for perlinRegionChannels");
    }

    OpenCLContext::get().releaseMem(freqBuf);
    OpenCLContext::get().releaseMem(lacBuf);
    OpenCLContext::get().releaseMem(octBuf);
    OpenCLContext::get().releaseMem(persBuf);
    OpenCLContext::get().releaseMem(seedBuf);
}

/// Apply a delta buffer to a base buffer on the GPU.
///   mode 0 = Add, mode 1 = Set.
static void applyDeltaScalar(cl_mem base, cl_mem delta,
                              int resLat, int resLon, int mode)
{
    ZoneScopedN("ApplyDeltaScalar");
    if (!OpenCLContext::get().isReady()) return;

    static cl_program prog = nullptr;
    static cl_kernel  kern = nullptr;
    try {
        OpenCLContext::get().createProgram(prog, "Kernels/ApplyDelta.cl");
        OpenCLContext::get().createKernelFromProgram(kern, prog, "apply_delta_scalar");
    } catch (...) { return; }

    clSetKernelArg(kern, 0, sizeof(cl_mem), &base);
    clSetKernelArg(kern, 1, sizeof(cl_mem), &delta);
    clSetKernelArg(kern, 2, sizeof(int),    &resLat);
    clSetKernelArg(kern, 3, sizeof(int),    &resLon);
    clSetKernelArg(kern, 4, sizeof(int),    &mode);

    size_t global[2] = { (size_t)resLat, (size_t)resLon };
    clEnqueueNDRangeKernel(OpenCLContext::get().getQueue(), kern, 2,
                            nullptr, global, nullptr, 0, nullptr, nullptr);
}

/// Utility: convert lon/lat (radians) to theta/phi for the Perlin sphere.
///   theta: colatitude [0, π], 0 = north pole.
///   phi:   azimuth [0, 2π].
static inline void lonLatToThetaPhi(float lonRad, float latRad,
                                     float& theta, float& phi) {
    const float PI = 3.14159265358979323846f;
    theta = PI / 2.0f - latRad; // north pole (lat=π/2) → theta=0
    phi   = lonRad + PI;        // lon ∈ [-π,π] → phi ∈ [0,2π]
}

