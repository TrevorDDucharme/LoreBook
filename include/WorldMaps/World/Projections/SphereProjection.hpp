#pragma once
#include <WorldMaps/World/Projections/Projection.hpp>
#include <WorldMaps/World/World.hpp>
#include <cmath>

// Screen-space spherical projection: traces rays from a virtual orbit camera through
// the requested viewport and samples the visible sphere pixels directly on the workers.
class SphericalProjection : public Projection
{
public:
    SphericalProjection() = default;
    // longitude/latitude specify the center of view (the sphere surface point to center on),
    // zoomLevel controls camera distance (larger zoomLevel = closer).
    void setViewCenterRadians(float lonRad, float latRad) { centerLon = lonRad; centerLat = latRad; }
    void setZoomLevel(float z) { zoomLevel = z; }
    void setFov(float f) { fovY = f; }
    GLuint project(World &world, int width, int height, std::string layerName) override
    {
        cl_mem fieldBuffer = world.getColor(layerName);
        cl_mem sphereBuffer = nullptr;

        if (fieldBuffer)
        {
            try
            {
                // Compute camera orbiting the sphere from lon/lat/zoom
                float targetX = cos(centerLat) * cos(centerLon);
                float targetY = sin(centerLat);
                float targetZ = cos(centerLat) * sin(centerLon);

                // Place camera a distance "zoomLevel" away from the sphere surface along the target normal
                float camPosX = targetX * (1.0f + zoomLevel);
                float camPosY = targetY * (1.0f + zoomLevel);
                float camPosZ = targetZ * (1.0f + zoomLevel);

                // Forward points from camera to the target on the sphere
                float camForwardX = targetX - camPosX;
                float camForwardY = targetY - camPosY;
                float camForwardZ = targetZ - camPosZ;
                float forwardLen = sqrt(camForwardX*camForwardX + camForwardY*camForwardY + camForwardZ*camForwardZ);
                camForwardX /= forwardLen; camForwardY /= forwardLen; camForwardZ /= forwardLen;

                // Compute right and up vectors (world up = +Y)
                float worldUpX = 0.0f, worldUpY = 1.0f, worldUpZ = 0.0f;
                float dotUpF = camForwardX*worldUpX + camForwardY*worldUpY + camForwardZ*worldUpZ;
                if (fabs(dotUpF) > 0.9999f) {
                    // camera near pole, pick alternate up
                    worldUpX = 0.0f; worldUpY = 0.0f; worldUpZ = 1.0f;
                }
                // right = normalize(cross(camForward, worldUp))
                float camRightX = camForwardY * worldUpZ - camForwardZ * worldUpY;
                float camRightY = camForwardZ * worldUpX - camForwardX * worldUpZ;
                float camRightZ = camForwardX * worldUpY - camForwardY * worldUpX;
                float rightLen = sqrt(camRightX*camRightX + camRightY*camRightY + camRightZ*camRightZ);
                if (rightLen == 0.0f) rightLen = 1.0f;
                camRightX /= rightLen; camRightY /= rightLen; camRightZ /= rightLen;

                // up = cross(right, forward)
                float camUpX = camRightY * camForwardZ - camRightZ * camForwardY;
                float camUpY = camRightZ * camForwardX - camRightX * camForwardZ;
                float camUpZ = camRightX * camForwardY - camRightY * camForwardX;
                float upLen = sqrt(camUpX*camUpX + camUpY*camUpY + camUpZ*camUpZ);
                if (upLen == 0.0f) upLen = 1.0f;
                camUpX /= upLen; camUpY /= upLen; camUpZ /= upLen;

                sphereBuffer = spherePerspectiveSample(
                    fieldBuffer,
                    256, 256, 256,
                    width, height,
                    camPosX, camPosY, camPosZ,
                    camForwardX, camForwardY, camForwardZ,
                    camRightX, camRightY, camRightZ,
                    camUpX, camUpY, camUpZ,
                    fovY,
                    0.0f, 0.0f, 0.0f,
                    1.0f);
                OpenCLContext::get().releaseMem(fieldBuffer);
            }
            catch (const std::exception &ex)
            {
                PLOGE << "spherePerspectiveSample() failed: " << ex.what();
                return 0;
            }
        }
        else
        {
            PLOGE << "fieldBuffer is null; skipping spherePerspectiveSample";
            return 0;
        }
        // Recreate texture if needed or channels changed
        if (texture == 0)
        {
            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        }

        glBindTexture(GL_TEXTURE_2D, texture);
        // Download data from sphereBuffer to a host buffer and upload to the GL texture
        if (sphereBuffer)
        {
            cl_int err = CL_SUCCESS;
            size_t bufSize = (size_t)width * (size_t)height * sizeof(cl_float4);
            std::vector<cl_float4> hostBuf((size_t)width * (size_t)height);
            err = clEnqueueReadBuffer(OpenCLContext::get().getQueue(), sphereBuffer, CL_TRUE, 0, bufSize, hostBuf.data(), 0, nullptr, nullptr);
            if (err != CL_SUCCESS)
            {
                PLOGE << "clEnqueueReadBuffer failed: " << err;
            }
            else
            {
                glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, hostBuf.data());
            }
            OpenCLContext::get().releaseMem(sphereBuffer);
        }
        else
        {
            PLOGE << "sphereBuffer is null";
            return 0;
        }

        return texture;
    }

    ~SphericalProjection() override
    {
    }

private:
    GLuint texture = 0;

    // Camera controls (radians for lon/lat)
    float centerLon = 0.0f;
    float centerLat = 0.0f;
    float zoomLevel = 3.0f;
    float fovY = static_cast<float>(M_PI) / 4.0f;

    static cl_program spherePerspectiveProgram;

public:
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

        cl_kernel kernel = nullptr;
        cl_mem output = nullptr;

        kernel = clCreateKernel(spherePerspectiveProgram, "sphere_perspective_sample_rgba", &err);
        if (err != CL_SUCCESS)
            throw std::runtime_error("clCreateKernel failed for sphere_perspective_sample_rgba");

        size_t outSize = (size_t)screenW * (size_t)screenH * sizeof(cl_float4);
        output = clCreateBuffer(ctx, CL_MEM_READ_WRITE, outSize, nullptr, &err);
        if (err != CL_SUCCESS)
        {
            clReleaseKernel(kernel);
            throw std::runtime_error("clCreateBuffer failed for spherePerspective rgba output");
        }

        clSetKernelArg(kernel, 0, sizeof(cl_mem), &field3d);
        clSetKernelArg(kernel, 1, sizeof(int), &fieldW);
        clSetKernelArg(kernel, 2, sizeof(int), &fieldH);
        clSetKernelArg(kernel, 3, sizeof(int), &fieldD);
        clSetKernelArg(kernel, 4, sizeof(cl_mem), &output);
        clSetKernelArg(kernel, 5, sizeof(int), &screenW);
        clSetKernelArg(kernel, 6, sizeof(int), &screenH);

        cl_float3 camPos = {camPosX, camPosY, camPosZ};
        cl_float3 camForward = {camForwardX, camForwardY, camForwardZ};
        cl_float3 camRight = {camRightX, camRightY, camRightZ};
        cl_float3 camUp = {camUpX, camUpY, camUpZ};
        cl_float3 sphereCenter = {sphereCenterX, sphereCenterY, sphereCenterZ};
        
        clSetKernelArg(kernel, 7, sizeof(camPos), &camPos);
        clSetKernelArg(kernel, 8, sizeof(camForward), &camForward);
        clSetKernelArg(kernel, 9, sizeof(camRight), &camRight);
        clSetKernelArg(kernel, 10, sizeof(camUp), &camUp);
        clSetKernelArg(kernel, 11, sizeof(float), &fovY);
        clSetKernelArg(kernel, 12, sizeof(sphereCenter), &sphereCenter);
        clSetKernelArg(kernel, 13, sizeof(float), &sphereRadius);

        // Debug buffer for sphere sampling (5 ints)
        cl_mem debugBuf = clCreateBuffer(ctx, CL_MEM_READ_WRITE, sizeof(int) * 5, nullptr, &err);
        if (err != CL_SUCCESS)
        {
            clReleaseKernel(kernel);
            clReleaseMemObject(output);
            throw std::runtime_error("clCreateBuffer failed for spherePerspective debugBuf");
        }
        int zeros[5] = {0, 0, 0, 0, 0};
        err = clEnqueueWriteBuffer(queue, debugBuf, CL_TRUE, 0, sizeof(zeros), zeros, 0, nullptr, nullptr);
        if (err != CL_SUCCESS)
        {
            clReleaseKernel(kernel);
            clReleaseMemObject(output);
            clReleaseMemObject(debugBuf);
            throw std::runtime_error("clEnqueueWriteBuffer failed for spherePerspective debugBuf");
        }
        clSetKernelArg(kernel, 14, sizeof(cl_mem), &debugBuf);

        size_t global[2] = {(size_t)screenW, (size_t)screenH};
        err = clEnqueueNDRangeKernel(queue, kernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        clFinish(queue);

        // Read back debug buffer
        int dbg[5] = {0, 0, 0, 0, 0};
        err = clEnqueueReadBuffer(queue, debugBuf, CL_TRUE, 0, sizeof(dbg), dbg, 0, nullptr, nullptr);
        if (err == CL_SUCCESS)
        {
            if (dbg[4] == 1)
            {
                if (dbg[2] == 0)
                {
                    fprintf(stderr, "[OpenCL Debug] sphere sample top-left (%d,%d) no-hit (t bits = %d)\n", dbg[0], dbg[1], dbg[3]);
                }
                else
                {
                    float t = 0.0f;
                    memcpy(&t, &dbg[3], sizeof(t));
                    fprintf(stderr, "[OpenCL Debug] sphere sample top-left (%d,%d) hit t = %f\n", dbg[0], dbg[1], t);
                }
            }
            else
            {
                fprintf(stderr, "[OpenCL Debug] sphere debugBuf not written (flag=0)\n");
            }

            // Also sample center pixel value from output for extra context
            size_t centerIdx = ((size_t)screenW / 2) + ((size_t)screenH / 2) * (size_t)screenW;
            cl_float4 pixelCenter = {0,0,0,0};
            err = clEnqueueReadBuffer(queue, output, CL_TRUE, centerIdx * sizeof(cl_float4), sizeof(pixelCenter), &pixelCenter, 0, nullptr, nullptr);
            if (err == CL_SUCCESS)
            {
                fprintf(stderr, "[OpenCL Debug] sphere output center pixel = %f %f %f %f\n", pixelCenter.s[0], pixelCenter.s[1], pixelCenter.s[2], pixelCenter.s[3]);
            }
            else
            {
                fprintf(stderr, "[OpenCL Debug] failed to read sphere output center pixel sample: %d\n", err);
            }
        }
        else
        {
            fprintf(stderr, "[OpenCL Debug] failed to read spherePerspective debugBuf: %d\n", err);
        }

        clReleaseMemObject(debugBuf);
        clReleaseKernel(kernel);

        return output;
    }
};