#include <WorldMaps/World/Projections/MercatorProjection.hpp>
#include <WorldMaps/World/World.hpp>
#include <tracy/Tracy.hpp>

cl_program MercatorProjection::mercatorProgram = nullptr;
cl_kernel MercatorProjection::mercatorKernel = nullptr;

MercatorProjection::~MercatorProjection()
{
    if (mercatorBuffer != nullptr)
    {
        OpenCLContext::get().releaseMem(mercatorBuffer);
        mercatorBuffer = nullptr;
    }
}

// Camera controls (center in radians, zoom > 0: larger = closer)
void MercatorProjection::setViewCenterRadians(float lonRad, float latRad)
{
    centerLon = lonRad;
    centerLat = latRad;
    centerMercY = std::log(std::tan(static_cast<float>(M_PI) / 4.0f + latRad / 2.0f));
}
void MercatorProjection::setZoomLevel(float z) { zoomLevel = z; }

void MercatorProjection::project(World &world, int width, int height, GLuint &texture, std::string layerName)
{
    ZoneScopedN("MercatorProjection::project");
    cl_mem fieldBuffer = world.getColor(layerName);

    if (fieldBuffer)
    {
        try
        {
            // Pass in camera center and zoom
            mercatorProject(mercatorBuffer,
                            fieldBuffer,
                            world.getWorldLatitudeResolution(),
                            world.getWorldLongitudeResolution(),
                            width, height,
                            centerLon, centerMercY, zoomLevel);
        }
        catch (const std::exception &ex)
        {
            //PLOGE << "mercatorProject() failed: " << ex.what();
            return;
        }
    }
    else
    {
        //PLOGE << "fieldBuffer is null; skipping mercatorProject";
        return;
    }

    // Recreate texture if needed or channels changed
    if (texture == 0)
    {
        ZoneScopedN("MercatorProjection Texture Create");
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        // RGBA float texture
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        glBindTexture(GL_TEXTURE_2D, texture);
    }
    else
    {
        ZoneScopedN("MercatorProjection Texture Resize Check");
        // Texture exists, make sure its the right size
        glBindTexture(GL_TEXTURE_2D, texture);
        GLint texWidth = 0, texHeight = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &texWidth);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &texHeight);
        if (texWidth != width || texHeight != height)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        }
    }

    // Download data from mercatorBuffer to a host buffer and upload to the GL texture
    if (mercatorBuffer)
    {
        ZoneScopedN("MercatorProjection Readback");
        cl_int err = CL_SUCCESS;
        size_t bufSize = (size_t)width * (size_t)height * sizeof(cl_float4);
        std::vector<cl_float4> hostBuf((size_t)width * (size_t)height);
        err = clEnqueueReadBuffer(OpenCLContext::get().getQueue(), mercatorBuffer, CL_TRUE, 0, bufSize, hostBuf.data(), 0, nullptr, nullptr);
        if (err != CL_SUCCESS)
        {
            PLOGE << "clEnqueueReadBuffer failed: " << err;
        }
        clFinish(OpenCLContext::get().getQueue());
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, hostBuf.data());
    }
    else
    {
        PLOGE << "mercatorBuffer is null";
    }
}

void MercatorProjection::mercatorProject(
    cl_mem &output,
    cl_mem sphereTex,
    int latitudeResolution,
    int longitudeResolution,
    int outW,
    int outH,
    float centerLon,
    float centerMercY,
    float zoom)
{
    ZoneScopedN("MercatorProjection::mercatorProject");
    if (!OpenCLContext::get().isReady())
        return;

    cl_context ctx = OpenCLContext::get().getContext();
    cl_device_id device = OpenCLContext::get().getDevice();
    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;

    if (mercatorProgram == nullptr)
    {
        ZoneScopedN("MercatorProjection Program Init");
        std::string kernel_code = preprocessCLIncludes("Kernels/Mercator.cl");
        const char *src = kernel_code.c_str();
        size_t len = kernel_code.length();
        mercatorProgram = clCreateProgramWithSource(ctx, 1, &src, &len, &err);
        if (err != CL_SUCCESS || mercatorProgram == nullptr)
            throw std::runtime_error("clCreateProgramWithSource failed for MercatorProjection");

        err = clBuildProgram(mercatorProgram, 1, &device, nullptr, nullptr, nullptr);
        if (err != CL_SUCCESS)
        {
            // retrieve build log
            size_t log_size = 0;
            clGetProgramBuildInfo(mercatorProgram, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
            std::string log;
            log.resize(log_size);
            clGetProgramBuildInfo(mercatorProgram, device, CL_PROGRAM_BUILD_LOG, log_size, &log[0], nullptr);
            throw std::runtime_error(std::string("Failed to build MercatorProjection OpenCL program: ") + log);
        }
    }

    if (mercatorKernel == nullptr)
    {
        ZoneScopedN("MercatorProjection Kernel Init");
        mercatorKernel = clCreateKernel(mercatorProgram, "sphere_to_mercator_rgba", &err);
        if (err != CL_SUCCESS)
        {
            // Retrieve build log for diagnostics
            size_t log_size = 0;
            clGetProgramBuildInfo(mercatorProgram, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
            std::string log;
            if (log_size > 0)
            {
                log.resize(log_size);
                clGetProgramBuildInfo(mercatorProgram, device, CL_PROGRAM_BUILD_LOG, log_size, &log[0], nullptr);
            }
            throw std::runtime_error(std::string("clCreateKernel failed for sphere_to_mercator_rgba: err=") + std::to_string(err) + std::string(" build_log:\n") + log);
        }
    }

    size_t total = (size_t)outW * (size_t)outH * sizeof(cl_float4);
    size_t bufSize;

    if (output != nullptr)
    {
        ZoneScopedN("MercatorProjection Output Buffer Check");
        cl_int err = clGetMemObjectInfo(output,
                                        CL_MEM_SIZE,
                                        sizeof(size_t),
                                        &bufSize, NULL);
        if (err != CL_SUCCESS)
        {
            throw std::runtime_error("clGetMemObjectInfo failed for mercator output buffer size");
        }
        if (bufSize < total)
        {
            OpenCLContext::get().releaseMem(output);
            output = nullptr;
        }
    }

    if (output == nullptr)
    {
        ZoneScopedN("MercatorProjection Output Buffer Alloc");
        output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, total, nullptr, &err, "mercator rgba output");
        if (err != CL_SUCCESS)
        {
            throw std::runtime_error("clCreateBuffer failed for mercator rgba output");
        }
    }

    float centerLonDeg = centerLon * 180.0f / static_cast<float>(M_PI);
    clSetKernelArg(mercatorKernel, 0, sizeof(cl_mem), &sphereTex);
    clSetKernelArg(mercatorKernel, 1, sizeof(int), &latitudeResolution);
    clSetKernelArg(mercatorKernel, 2, sizeof(int), &longitudeResolution);
    clSetKernelArg(mercatorKernel, 3, sizeof(cl_mem), &output);
    clSetKernelArg(mercatorKernel, 4, sizeof(int), &outW);
    clSetKernelArg(mercatorKernel, 5, sizeof(int), &outH);
    clSetKernelArg(mercatorKernel, 6, sizeof(float), &centerLonDeg);
    clSetKernelArg(mercatorKernel, 7, sizeof(float), &centerMercY);
    clSetKernelArg(mercatorKernel, 8, sizeof(float), &zoom);

    size_t global[2] = {(size_t)outW, (size_t)outH};
    {
        ZoneScopedN("MercatorProjection Enqueue");
        err = clEnqueueNDRangeKernel(queue, mercatorKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
    }
}