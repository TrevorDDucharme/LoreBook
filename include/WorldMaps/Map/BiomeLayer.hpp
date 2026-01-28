#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class BiomeLayer : public MapLayer {
public:
    BiomeLayer() = default;
    ~BiomeLayer() override = default;

    SampleData sample(const World&) override{
        SampleData data;
        for(int i=0; i<biomeCount; ++i){
            //generate biome mask per biome
            data.channels.push_back(perlin(256, 256, 256, .01f, 2.0f, 8, 0.5f, static_cast<unsigned int>(i*1000 + 12345u)));
        }
        return data;
    }

    cl_mem getColor(const World& world) override{
        SampleData sampleData = sample(world);
        //combine biome masks into a single 4d texture (concat each biome mask as a channel)
        cl_context ctx = OpenCLContext::get().getContext();
        cl_device_id device = OpenCLContext::get().getDevice();
        cl_command_queue queue = OpenCLContext::get().getQueue();
        cl_int err = CL_SUCCESS;
        const int fieldW = 256;
        const int fieldH = 256;
        const int fieldD = 256;
        size_t totalSize = (size_t)fieldW * (size_t)fieldH * (size_t)fieldD * (size_t)biomeCount * sizeof(float);
        cl_mem colorBuffer = clCreateBuffer(ctx, CL_MEM_READ_WRITE, totalSize, nullptr, &err);
        if (err != CL_SUCCESS)
        {
            throw std::runtime_error("clCreateBuffer failed for BiomeLayer colorBuffer");
        }
        //copy each biome mask into the colorBuffer at the appropriate offset
        for(int i=0; i<biomeCount; ++i){
            size_t offset = (size_t)fieldW * (size_t)fieldH * (size_t)fieldD * (size_t)i * sizeof(float);
            err = clEnqueueCopyBuffer(queue, sampleData.channels[i], colorBuffer, 0, offset, (size_t)fieldW * (size_t)fieldH * (size_t)fieldD * sizeof(float), 0, nullptr, nullptr);
            if (err != CL_SUCCESS)
            {
                clReleaseMemObject(colorBuffer);
                throw std::runtime_error("clEnqueueCopyBuffer failed for BiomeLayer colorBuffer");
            }
        }
        //release individual biome mask buffers
        for(auto& ch : sampleData.channels){
            clReleaseMemObject(ch);
        }
        
        //perform Biome color mapping
        cl_mem outColor = nullptr;
        try {
            outColor = biomeColorMap(colorBuffer, biomeCount);
        } catch (const std::exception &ex) {
            clReleaseMemObject(colorBuffer);
            throw;
        }
        clReleaseMemObject(colorBuffer);
        return outColor;
    }
private:
    int biomeCount = 5;
    static inline const std::vector<std::array<uint8_t,4>> biomeColors = {
        {34,139,34,255},    // Forest Green
        {210,180,140,255},  // Tan (Desert)
        {255,250,250,255},  // Snow
        {160,82,45,255},    // Brown (Mountain)
        {70,130,180,255}    // Steel Blue (Water)
    };

    static cl_mem biomeColorMap(cl_mem biomeMasks, int biomeCount){
        if (!OpenCLContext::get().isReady())
            return nullptr;

        cl_context ctx = OpenCLContext::get().getContext();
        cl_device_id device = OpenCLContext::get().getDevice();
        cl_command_queue queue = OpenCLContext::get().getQueue();
        cl_int err = CL_SUCCESS;

        const int fieldW = 256;
        const int fieldH = 256;
        const int fieldD = 256;
        size_t voxels = (size_t)fieldW * (size_t)fieldH * (size_t)fieldD;

        static cl_program program = nullptr;
        if (program == nullptr)
        {
            std::string kernel_code = loadLoreBook_ResourcesEmbeddedFileAsString("Kernels/BiomeColorMap.cl");
            const char *src = kernel_code.c_str();
            size_t len = kernel_code.length();
            program = clCreateProgramWithSource(ctx, 1, &src, &len, &err);
            if (err != CL_SUCCESS || program == nullptr)
                throw std::runtime_error("clCreateProgramWithSource failed for BiomeColorMap");

            err = clBuildProgram(program, 1, &device, nullptr, nullptr, nullptr);
            if (err != CL_SUCCESS)
            {
                size_t log_size = 0;
                clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
                std::string log;
                log.resize(log_size);
                clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, log_size, &log[0], nullptr);
                throw std::runtime_error(std::string("Failed to build BiomeColorMap OpenCL program: ") + log);
            }
        }

        cl_kernel kernel = clCreateKernel(program, "biome_masks_to_rgba_float4", &err);
        if (err != CL_SUCCESS)
            throw std::runtime_error("clCreateKernel failed for biome_masks_to_rgba_float4");

        // create palette buffer
        std::vector<float> paletteFloats;
        paletteFloats.reserve((size_t)biomeCount * 4);
        for (int i = 0; i < biomeCount; ++i)
        {
            auto &c = biomeColors[i % biomeColors.size()];
            paletteFloats.push_back(static_cast<float>(c[0]) / 255.0f);
            paletteFloats.push_back(static_cast<float>(c[1]) / 255.0f);
            paletteFloats.push_back(static_cast<float>(c[2]) / 255.0f);
            paletteFloats.push_back(static_cast<float>(c[3]) / 255.0f);
        }

        cl_mem paletteBuf = clCreateBuffer(ctx, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(float) * paletteFloats.size(), paletteFloats.data(), &err);
        if (err != CL_SUCCESS)
        {
            clReleaseKernel(kernel);
            throw std::runtime_error("clCreateBuffer failed for BiomeColorMap paletteBuf");
        }

        size_t outSize = voxels * sizeof(cl_float4);
        cl_mem output = clCreateBuffer(ctx, CL_MEM_READ_WRITE, outSize, nullptr, &err);
        if (err != CL_SUCCESS)
        {
            clReleaseKernel(kernel);
            clReleaseMemObject(paletteBuf);
            throw std::runtime_error("clCreateBuffer failed for BiomeColorMap output");
        }

        int mode = 0; // 0 = argmax, 1 = blend
        clSetKernelArg(kernel, 0, sizeof(cl_mem), &biomeMasks);
        clSetKernelArg(kernel, 1, sizeof(int), &fieldW);
        clSetKernelArg(kernel, 2, sizeof(int), &fieldH);
        clSetKernelArg(kernel, 3, sizeof(int), &fieldD);
        clSetKernelArg(kernel, 4, sizeof(int), &biomeCount);
        clSetKernelArg(kernel, 5, sizeof(cl_mem), &paletteBuf);
        clSetKernelArg(kernel, 6, sizeof(int), &mode);
        clSetKernelArg(kernel, 7, sizeof(cl_mem), &output);

        size_t global[3] = {(size_t)fieldW, (size_t)fieldH, (size_t)fieldD};
        err = clEnqueueNDRangeKernel(queue, kernel, 3, nullptr, global, nullptr, 0, nullptr, nullptr);
        clFinish(queue);

        clReleaseKernel(kernel);
        clReleaseMemObject(paletteBuf);

        return output;
    }
};