#pragma once
#include <WorldMaps/World/Projections/Projection.hpp>
#include <WorldMaps/World/World.hpp>
#include <cmath>
#include <tracy/Tracy.hpp>
#include <sstream>

// Screen-space spherical projection: traces rays from a virtual orbit camera through
// the requested viewport and samples the visible sphere pixels directly on the workers.
class SphericalProjection : public Projection
{
public:
    SphericalProjection() = default;
    // longitude/latitude specify the center of view (the sphere surface point to center on).
    // zoomLevel controls camera distance (smaller value = closer). zoomLevel is linear distance from
    // the surface in sphere radii (camPos = target * (1.0f + zoomLevel)). Values below -0.95 are clamped
    // to avoid placing the camera at or near the sphere center.
    void setViewCenterRadians(float lonRad, float latRad) { centerLon = lonRad; centerLat = latRad; }
    void setZoomLevel(float z) { const float MIN_Z = -0.95f; zoomLevel = (z < MIN_Z) ? MIN_Z : z; }
    void setFov(float f) { fovY = f; }
    //caller must cleanup texture when done
    void project(World &world, int width, int height, GLuint& texture, std::string layerName="") override
    {
        ZoneScopedN("SphericalProjection::project");
        cl_mem fieldBuffer = world.getColor(layerName);

        if (fieldBuffer)
        {
            try
            {
                float targetX, targetY, targetZ;
                float camPosX, camPosY, camPosZ;
                float camForwardX, camForwardY, camForwardZ;
                float camRightX, camRightY, camRightZ;
                float camUpX, camUpY, camUpZ;
                float forwardLen, rightLen, upLen;
                float worldUpX, worldUpY, worldUpZ;
                float dotUpF;
                {
                    ZoneScopedN("Camera Math");
                    // Compute camera orbiting the sphere from lon/lat/zoom
                    targetX = cos(centerLat) * cos(centerLon);
                    targetY = sin(centerLat);
                    targetZ = cos(centerLat) * sin(centerLon);

                    // Place camera a distance "zoomLevel" away from the sphere surface along the target normal
                    camPosX = targetX * (1.0f + zoomLevel);
                    camPosY = targetY * (1.0f + zoomLevel);
                    camPosZ = targetZ * (1.0f + zoomLevel);

                    // Forward points from camera to the target on the sphere
                    camForwardX = targetX - camPosX;
                    camForwardY = targetY - camPosY;
                    camForwardZ = targetZ - camPosZ;
                    forwardLen = sqrt(camForwardX*camForwardX + camForwardY*camForwardY + camForwardZ*camForwardZ);
                    camForwardX /= forwardLen; camForwardY /= forwardLen; camForwardZ /= forwardLen;

                    // Compute right and up vectors (world up = +Y)
                    worldUpX = 0.0f;
                    worldUpY = 1.0f;
                    worldUpZ = 0.0f;
                    dotUpF = camForwardX*worldUpX + camForwardY*worldUpY + camForwardZ*worldUpZ;
                    if (fabs(dotUpF) > 0.9999f) {
                        // camera near pole, pick alternate up
                        worldUpX = 0.0f; worldUpY = 0.0f; worldUpZ = 1.0f;
                    }
                    // right = normalize(cross(camForward, worldUp))
                    camRightX = camForwardY * worldUpZ - camForwardZ * worldUpY;
                    camRightY = camForwardZ * worldUpX - camForwardX * worldUpZ;
                    camRightZ = camForwardX * worldUpY - camForwardY * worldUpX;
                    rightLen = sqrt(camRightX*camRightX + camRightY*camRightY + camRightZ*camRightZ);
                    if (rightLen == 0.0f) rightLen = 1.0f;
                    camRightX /= rightLen; camRightY /= rightLen; camRightZ /= rightLen;

                    // up = cross(right, forward)
                    camUpX = camRightY * camForwardZ - camRightZ * camForwardY;
                    camUpY = camRightZ * camForwardX - camRightX * camForwardZ;
                    camUpZ = camRightX * camForwardY - camRightY * camForwardX;
                    upLen = sqrt(camUpX*camUpX + camUpY*camUpY + camUpZ*camUpZ);
                    if (upLen == 0.0f) upLen = 1.0f;
                    camUpX /= upLen; camUpY /= upLen; camUpZ /= upLen;
                }

                spherePerspectiveSample(
                    sphereBuffer,
                    fieldBuffer,
                    world.getWorldLatitudeResolution(),
                    world.getWorldLongitudeResolution(),
                    width, height,
                    camPosX, camPosY, camPosZ,
                    camForwardX, camForwardY, camForwardZ,
                    camRightX, camRightY, camRightZ,
                    camUpX, camUpY, camUpZ,
                    fovY);
            }
            catch (const std::exception &ex)
            {
                PLOGE << "spherePerspectiveSample() failed: " << ex.what();
                return;
            }
        }
        else
        {
            PLOGE << "fieldBuffer is null; skipping spherePerspectiveSample";
            return;
        }
        // Recreate texture if needed or channels changed
        if (texture == 0)
        {
            ZoneScopedN("SphericalProjection Texture Create");
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
            glBindTexture(GL_TEXTURE_2D, texture);
        }else{
            ZoneScopedN("SphericalProjection Texture Resize Check");
            // Texture exists, make sure its the right size
            glBindTexture(GL_TEXTURE_2D, texture);
            GLint texWidth = 0, texHeight = 0;
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &texWidth);
            glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &texHeight);
            if(texWidth != width || texHeight != height){
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
            }
        }

        // Download data from sphereBuffer to a host buffer and upload to the GL texture
        if (sphereBuffer)
        {
            ZoneScopedN("SphericalProjection Readback");
            cl_int err = CL_SUCCESS;
            size_t bufSize = (size_t)width * (size_t)height * sizeof(float)*4;
            std::vector<float> hostBuf((size_t)width * (size_t)height*4);
            err = clEnqueueReadBuffer(OpenCLContext::get().getQueue(), sphereBuffer, CL_TRUE, 0, bufSize, hostBuf.data(), 0, nullptr, nullptr);
            if (err != CL_SUCCESS)
            {
                PLOGE << "clEnqueueReadBuffer failed: " << err;
            }
            clFinish(OpenCLContext::get().getQueue());
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, hostBuf.data());
        }
        else
        {
            PLOGE << "sphereBuffer is null";
            return;
        }
    }

    ~SphericalProjection() override
    {
        if (sphereBuffer != nullptr)
        {
            OpenCLContext::get().releaseMem(sphereBuffer);
            sphereBuffer = nullptr;
        }
    }

private:
    cl_mem sphereBuffer = nullptr;

    // Camera controls (radians for lon/lat)
    float centerLon = 0.0f;
    float centerLat = 0.0f;
    float zoomLevel = 3.0f;
    float fovY = static_cast<float>(M_PI) / 4.0f;

    static cl_program spherePerspectiveProgram;
    static cl_kernel spherePerspectiveKernel;

public:
    static void spherePerspectiveSample(
        cl_mem& output,
        cl_mem sphereTex,
        int latitudeResolution,
        int longitudeResolution,

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

        float fovY) // radians
    {
        ZoneScopedN("spherePerspectiveSample");
        if (!OpenCLContext::get().isReady())
            return;

        cl_context ctx = OpenCLContext::get().getContext();
        cl_device_id device = OpenCLContext::get().getDevice();
        cl_command_queue queue = OpenCLContext::get().getQueue();
        cl_int err = CL_SUCCESS;

        if (spherePerspectiveProgram == nullptr)
        {
            ZoneScopedN("spherePerspectiveSample Program Init");
            std::string kernel_code = preprocessCLIncludes("Kernels/SpherePerspective.cl");
            const char *src = kernel_code.c_str();
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

        if(spherePerspectiveKernel == nullptr){
            ZoneScopedN("spherePerspectiveSample Kernel Init");
            spherePerspectiveKernel = clCreateKernel(spherePerspectiveProgram, "sphere_perspective_sample_rgba", &err);
            if (err != CL_SUCCESS) {
                // retrieve build log
                size_t log_size = 0;
                clGetProgramBuildInfo(spherePerspectiveProgram, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
                std::string log;
                if (log_size > 0) {
                    log.resize(log_size);
                    clGetProgramBuildInfo(spherePerspectiveProgram, device, CL_PROGRAM_BUILD_LOG, log_size, &log[0], nullptr);
                }
                // retrieve kernel names
                size_t names_size = 0;
                clGetProgramInfo(spherePerspectiveProgram, CL_PROGRAM_KERNEL_NAMES, 0, nullptr, &names_size);
                std::string kernel_names;
                if (names_size > 0) {
                    kernel_names.resize(names_size);
                    clGetProgramInfo(spherePerspectiveProgram, CL_PROGRAM_KERNEL_NAMES, names_size, &kernel_names[0], nullptr);
                }
                std::ostringstream oss;
                oss << "clCreateKernel failed for sphere_perspective_sample_rgba err=" << err;
                if (!kernel_names.empty()) oss << " kernels=\"" << kernel_names << "\"";
                if (!log.empty()) oss << " build_log: " << log;
                throw std::runtime_error(oss.str());
            }
        }
        size_t outSize = (size_t)screenW * (size_t)screenH * sizeof(cl_float4);
        size_t bufSize;
        if(output != nullptr){
            ZoneScopedN("spherePerspectiveSample Output Buffer Check");
            cl_int err = clGetMemObjectInfo(output,
                                            CL_MEM_SIZE,
                                            sizeof(size_t),
                                            &bufSize, NULL);
            if (err != CL_SUCCESS) {    
                throw std::runtime_error("clGetMemObjectInfo failed for spherePerspective output buffer size");
            }
            if(bufSize < outSize){
                OpenCLContext::get().releaseMem(output);
                output = nullptr;
            }
        }

        if(output == nullptr){
            ZoneScopedN("spherePerspectiveSample Output Buffer Alloc");
            output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, outSize, nullptr, &err, "spherePerspective rgba output");
            if (err != CL_SUCCESS)
            {
                throw std::runtime_error("clCreateBuffer failed for spherePerspective rgba output");
            }
        }

        clSetKernelArg(spherePerspectiveKernel, 0, sizeof(cl_mem), &sphereTex);
        clSetKernelArg(spherePerspectiveKernel, 1, sizeof(int), &latitudeResolution);
        clSetKernelArg(spherePerspectiveKernel, 2, sizeof(int), &longitudeResolution);
        clSetKernelArg(spherePerspectiveKernel, 3, sizeof(cl_mem), &output);
        clSetKernelArg(spherePerspectiveKernel, 4, sizeof(int), &screenW);
        clSetKernelArg(spherePerspectiveKernel, 5, sizeof(int), &screenH);

        cl_float3 camPos = {camPosX, camPosY, camPosZ};
        cl_float3 camForward = {camForwardX, camForwardY, camForwardZ};
        cl_float3 camRight = {camRightX, camRightY, camRightZ};
        cl_float3 camUp = {camUpX, camUpY, camUpZ};
        
        clSetKernelArg(spherePerspectiveKernel, 6, sizeof(camPos), &camPos);
        clSetKernelArg(spherePerspectiveKernel, 7, sizeof(camForward), &camForward);
        clSetKernelArg(spherePerspectiveKernel, 8, sizeof(camRight), &camRight);
        clSetKernelArg(spherePerspectiveKernel, 9, sizeof(camUp), &camUp);
        clSetKernelArg(spherePerspectiveKernel, 10, sizeof(float), &fovY);

        size_t global[2] = {(size_t)screenW, (size_t)screenH};
        {
            ZoneScopedN("spherePerspectiveSample Enqueue");
            err = clEnqueueNDRangeKernel(queue, spherePerspectiveKernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        }
    }
};