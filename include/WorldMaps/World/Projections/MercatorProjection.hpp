#pragma once
#include <WorldMaps/World/Projections/Projection.hpp>
#include <WorldMaps/World/World.hpp>

class MercatorProjection : public Projection
{
public:
    MercatorProjection()= default;
    GLuint project(World &world, int width, int height, int channels, std::string layerName, GLuint existingTexture) override
    {
        cl_mem fieldBuffer = world.getColor(layerName);
        cl_mem mercatorBuffer = nullptr;

        if (fieldBuffer)
        {
            try
            {
                mercatorBuffer = mercatorProject(
                    fieldBuffer,
                    256, 256, 256,
                    width, height,
                    1.0f,
                    channels);
                OpenCLContext::get().releaseMem(fieldBuffer);
            }
            catch (const std::exception &ex)
            {
                PLOGE << "mercatorProject() failed: " << ex.what();
                return 0;
            }
        }
        else
        {
            PLOGE << "fieldBuffer is null; skipping mercatorProject";
            return 0;
        }

        // Recreate texture if needed or channels changed
        if (texture == 0 || textureChannels != channels)
        {
            if (texture != 0)
            {
                glDeleteTextures(1, &texture);
                texture = 0;
            }

            glGenTextures(1, &texture);
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            if (channels == 4)
            {
                // RGBA float texture
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
            }
            else
            {
                // Use single-channel float texture and swizzle R -> RGB so the image appears grayscale
                glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, width, height, 0, GL_RED, GL_FLOAT, nullptr);
                GLint swizzleMask[] = {GL_RED, GL_RED, GL_RED, GL_ONE};
                glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_SWIZZLE_RGBA, swizzleMask);
            }

            textureChannels = channels;
        }

        glBindTexture(GL_TEXTURE_2D, texture);
        // Download data from mercatorBuffer to a host buffer and upload to the GL texture
        if (mercatorBuffer)
        {
            cl_int err = CL_SUCCESS;
            if (channels == 4)
            {
                size_t bufSize = (size_t)width * (size_t)height * sizeof(cl_float4);
                std::vector<cl_float4> hostBuf((size_t)width * (size_t)height);
                err = clEnqueueReadBuffer(OpenCLContext::get().getQueue(), mercatorBuffer, CL_TRUE, 0, bufSize, hostBuf.data(), 0, nullptr, nullptr);
                if (err != CL_SUCCESS)
                {
                    PLOGE << "clEnqueueReadBuffer failed: " << err;
                }
                else
                {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, hostBuf.data());
                }
            }
            else
            {
                size_t bufSize = (size_t)width * (size_t)height * sizeof(float);
                std::vector<float> hostBuf((size_t)width * (size_t)height);
                err = clEnqueueReadBuffer(OpenCLContext::get().getQueue(), mercatorBuffer, CL_TRUE, 0, bufSize, hostBuf.data(), 0, nullptr, nullptr);
                if (err != CL_SUCCESS)
                {
                    PLOGE << "clEnqueueReadBuffer failed: " << err;
                }
                else
                {
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RED, GL_FLOAT, hostBuf.data());
                }
            }
            OpenCLContext::get().releaseMem(mercatorBuffer);
        }
        else
        {
            PLOGE << "mercatorBuffer is null";
        }
        return texture;
    }

private:
    GLuint texture = 0;
    int textureChannels = 0;

    static cl_program mercatorProgram;

    static cl_mem mercatorProject(
        cl_mem field3d,
        int fieldW,
        int fieldH,
        int fieldD,
        int outW,
        int outH,
        float radius,
        int channels)
    {
        if (!OpenCLContext::get().isReady())
            return nullptr;

        cl_context ctx = OpenCLContext::get().getContext();
        cl_device_id device = OpenCLContext::get().getDevice();
        cl_command_queue queue = OpenCLContext::get().getQueue();
        cl_int err = CL_SUCCESS;

        if (mercatorProgram == nullptr)
        {
            std::string kernel_code = loadLoreBook_ResourcesEmbeddedFileAsString("Kernels/FeildToMercator.cl");
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

        cl_kernel kernel = nullptr;
        cl_mem output = nullptr;

        if (channels == 4)
        {
            kernel = clCreateKernel(mercatorProgram, "field3d_to_mercator_rgba", &err);
            if (err != CL_SUCCESS)
                throw std::runtime_error("clCreateKernel failed for field3d_to_mercator_rgba");

            size_t total = (size_t)outW * (size_t)outH * sizeof(cl_float4);
            output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, total, nullptr, &err);
            if (err != CL_SUCCESS)
            {
                clReleaseKernel(kernel);
                throw std::runtime_error("clCreateBuffer failed for mercator rgba output");
            }

            clSetKernelArg(kernel, 0, sizeof(cl_mem), &field3d);
            clSetKernelArg(kernel, 1, sizeof(int), &fieldW);
            clSetKernelArg(kernel, 2, sizeof(int), &fieldH);
            clSetKernelArg(kernel, 3, sizeof(int), &fieldD);
            clSetKernelArg(kernel, 4, sizeof(cl_mem), &output);
            clSetKernelArg(kernel, 5, sizeof(int), &outW);
            clSetKernelArg(kernel, 6, sizeof(int), &outH);
            clSetKernelArg(kernel, 7, sizeof(float), &radius);
        }
        else
        {
            kernel = clCreateKernel(mercatorProgram, "field3d_to_mercator", &err);
            if (err != CL_SUCCESS)
                throw std::runtime_error("clCreateKernel failed for field3d_to_mercator");

            size_t total = (size_t)outW * (size_t)outH * sizeof(float);
            output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, total, nullptr, &err);
            if (err != CL_SUCCESS)
            {
                clReleaseKernel(kernel);
                throw std::runtime_error("clCreateBuffer failed for mercator output");
            }

            clSetKernelArg(kernel, 0, sizeof(cl_mem), &field3d);
            clSetKernelArg(kernel, 1, sizeof(int), &fieldW);
            clSetKernelArg(kernel, 2, sizeof(int), &fieldH);
            clSetKernelArg(kernel, 3, sizeof(int), &fieldD);
            clSetKernelArg(kernel, 4, sizeof(cl_mem), &output);
            clSetKernelArg(kernel, 5, sizeof(int), &outW);
            clSetKernelArg(kernel, 6, sizeof(int), &outH);
            clSetKernelArg(kernel, 7, sizeof(float), &radius);
        }

        size_t global[2] = {(size_t)outW, (size_t)outH};
        err = clEnqueueNDRangeKernel(queue, kernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        clFinish(queue);
        clReleaseKernel(kernel);
        return output;
    }
};