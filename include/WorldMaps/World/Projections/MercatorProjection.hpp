#pragma once
#include <WorldMaps/World/Projections/Projection.hpp>
#include <WorldMaps/World/World.hpp>

class MercatorProjection : public Projection
{
public:
    MercatorProjection()= default;
    GLuint project(World &world, int width, int height, std::string layerName) override
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
                    1.0f);
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
        if (texture == 0)
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

                // RGBA float texture
                glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);

        }

        glBindTexture(GL_TEXTURE_2D, texture);
        // Download data from mercatorBuffer to a host buffer and upload to the GL texture
        if (mercatorBuffer)
        {
            cl_int err = CL_SUCCESS;
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

    static cl_program mercatorProgram;

    static cl_mem mercatorProject(
        cl_mem field3d,
        int fieldW,
        int fieldH,
        int fieldD,
        int outW,
        int outH,
        float radius)
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

        // Debug buffer for sampling info (5 ints)
        cl_mem debugBuf = clCreateBuffer(ctx, CL_MEM_READ_WRITE, sizeof(int) * 5, nullptr, &err);
        if (err != CL_SUCCESS)
        {
            clReleaseKernel(kernel);
            clReleaseMemObject(output);
            throw std::runtime_error("clCreateBuffer failed for mercator debugBuf");
        }
        int zeros[5] = {0, 0, 0, 0, 0};
        err = clEnqueueWriteBuffer(queue, debugBuf, CL_TRUE, 0, sizeof(zeros), zeros, 0, nullptr, nullptr);
        if (err != CL_SUCCESS)
        {
            clReleaseKernel(kernel);
            clReleaseMemObject(output);
            clReleaseMemObject(debugBuf);
            throw std::runtime_error("clEnqueueWriteBuffer failed for mercator debugBuf");
        }
        clSetKernelArg(kernel, 8, sizeof(cl_mem), &debugBuf);

        size_t global[2] = {(size_t)outW, (size_t)outH};
        err = clEnqueueNDRangeKernel(queue, kernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        clFinish(queue);

        // Read back debug buffer
        int dbg[5] = {0, 0, 0, 0, 0};
        err = clEnqueueReadBuffer(queue, debugBuf, CL_TRUE, 0, sizeof(dbg), dbg, 0, nullptr, nullptr);
        if (err == CL_SUCCESS)
        {
            if (dbg[4] == 1)
            {
                fprintf(stderr, "[OpenCL Debug] mercator center sample ix=%d iy=%d iz=%d val=%d\n", dbg[0], dbg[1], dbg[2], dbg[3]);
            }
            else
            {
                fprintf(stderr, "[OpenCL Debug] mercator debugBuf not written (flag=0)\n");
            }
        }
        else
        {
            fprintf(stderr, "[OpenCL Debug] failed to read mercator debugBuf: %d\n", err);
        }

        clReleaseMemObject(debugBuf);
        clReleaseKernel(kernel);
        return output;
    }
};