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
        cl_mem colorBuffer = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, totalSize, nullptr, &err);
        if (err != CL_SUCCESS || colorBuffer == nullptr)
        {
            throw std::runtime_error("clCreateBuffer failed for BiomeLayer colorBuffer");
        }
        // Diagnostics: log memory usage after allocating the combined color buffer
        OpenCLContext::get().logMemoryUsage();
        // Validate channels and copy each biome mask into the colorBuffer at the appropriate offset
        size_t voxels = (size_t)fieldW * (size_t)fieldH * (size_t)fieldD;
        size_t sliceBytes = voxels * sizeof(float);
        if (sampleData.channels.size() != (size_t)biomeCount) {
            for (auto &c : sampleData.channels) if (c) OpenCLContext::get().releaseMem(c);
            OpenCLContext::get().releaseMem(colorBuffer);
            throw std::runtime_error("BiomeLayer: unexpected number of perlin channels");
        }

        auto releaseChannels = [&](int upto) {
            for (int j = 0; j < upto; ++j) {
                if (sampleData.channels[j]) {
                    OpenCLContext::get().releaseMem(sampleData.channels[j]);
                    sampleData.channels[j] = nullptr;
                }
            }
        };

        for (int i = 0; i < biomeCount; ++i) {
            cl_mem src = sampleData.channels[i];
            if (src == nullptr) {
                OpenCLContext::get().releaseMem(colorBuffer);
                releaseChannels(i);
                throw std::runtime_error(std::string("BiomeLayer: perlin channel ") + std::to_string(i) + " is null");
            }

            // verify same context
            cl_context srcCtx = nullptr;
            err = clGetMemObjectInfo(src, CL_MEM_CONTEXT, sizeof(cl_context), &srcCtx, nullptr);
            if (err != CL_SUCCESS || srcCtx != ctx) {
                OpenCLContext::get().releaseMem(colorBuffer);
                releaseChannels(i+1);
                throw std::runtime_error(std::string("BiomeLayer: channel context mismatch or clGetMemObjectInfo failed (err=") + std::to_string(err) + ")");
            }

            // verify size
            size_t srcSize = 0;
            err = clGetMemObjectInfo(src, CL_MEM_SIZE, sizeof(size_t), &srcSize, nullptr);
            if (err != CL_SUCCESS || srcSize < sliceBytes) {
                OpenCLContext::get().releaseMem(colorBuffer);
                releaseChannels(i+1);
                throw std::runtime_error(std::string("BiomeLayer: channel size too small or clGetMemObjectInfo failed (err=") + std::to_string(err) + ")");
            }

            size_t offset = sliceBytes * (size_t)i;
            err = clEnqueueCopyBuffer(queue, src, colorBuffer, 0, offset, sliceBytes, 0, nullptr, nullptr);
            if (err != CL_SUCCESS) {
                OpenCLContext::get().releaseMem(colorBuffer);
                releaseChannels(biomeCount);
                throw std::runtime_error(std::string("clEnqueueCopyBuffer failed for BiomeLayer colorBuffer (err=") + std::to_string(err) + ")");
            }
        }
        // Ensure copies finish before freeing source buffers to avoid races
        clFinish(queue);

        //release individual biome mask buffers
        for(auto& ch : sampleData.channels){
            OpenCLContext::get().releaseMem(ch);
        }
        
        //perform Biome color mapping
        cl_mem outColor = nullptr;
        try {
            outColor = biomeColorMap(colorBuffer, biomeCount, biomeColors);
        } catch (const std::exception &ex) {
            OpenCLContext::get().releaseMem(colorBuffer);
            throw;
        }
        OpenCLContext::get().releaseMem(colorBuffer);
        return outColor;
    }

    //count:2,colors:[{0,0,255,255},{0,255,0,255}]
    void parseParameters(const std::string &params) override{
        auto trim = [](std::string s){
            auto not_ws = [](int ch){ return !std::isspace(ch); };
            s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_ws));
            s.erase(std::find_if(s.rbegin(), s.rend(), not_ws).base(), s.end());
            return s;
        };

        auto tokens = splitBracketAware(params, ",");
        std::vector<std::array<uint8_t,4>> colorsParsed;
        for(const auto& token : tokens){
            auto kv = splitBracketAware(token, ":");
            if(kv.size() != 2)
                throw std::runtime_error(std::string("BiomeLayer::parseParameters: invalid token: ") + token);

            std::string key = trim(kv[0]);
            std::string value = trim(kv[1]);

            if(key == "count"){
                try{
                    biomeCount = std::stoi(value);
                } catch(...) {
                    throw std::runtime_error(std::string("BiomeLayer::parseParameters: invalid count value: ") + value);
                }
                if(biomeCount <= 0)
                    throw std::runtime_error("BiomeLayer::parseParameters: count must be > 0");
            } else if(key == "colors"){
                //parse colors
                //expected format: [{r,g,b,a},{r,g,b,a},...]
                if(value.size() < 2 || value.front() != '[' || value.back() != ']'){
                    throw std::runtime_error("BiomeLayer::parseParameters: colors must be in format '[{r,g,b,a},...]'");
                }

                std::string inner = value.substr(1, value.size() - 2);
                // split on top-level commas (splitBracketAware will ignore commas inside matched brackets)
                auto colorTokens = splitBracketAware(inner, ",");

                for(auto colorToken : colorTokens){
                    std::string t = trim(colorToken);
                    // if the token is wrapped in matching brackets (any of {} () []), strip them
                    if(t.size() >= 2){
                        char f = t.front();
                        char l = t.back();
                        if((f == '{' && l == '}') || (f == '(' && l == ')') || (f == '[' && l == ']')){
                            t = t.substr(1, t.size() - 2);
                        }
                    }

                    auto rgbTokens = splitBracketAware(t, ",");
                    if(rgbTokens.size() != 4)
                        throw std::runtime_error(std::string("BiomeLayer::parseParameters: color entry must have 4 components: ") + colorToken);

                    int rc, gc, bc, ac;
                    try{
                        rc = std::stoi(trim(rgbTokens[0]));
                        gc = std::stoi(trim(rgbTokens[1]));
                        bc = std::stoi(trim(rgbTokens[2]));
                        ac = std::stoi(trim(rgbTokens[3]));
                    }catch(...){
                        throw std::runtime_error(std::string("BiomeLayer::parseParameters: invalid color component in: ") + colorToken);
                    }

                    auto checkRange = [](int v, const char* name){
                        if(v < 0 || v > 255) throw std::runtime_error(std::string("BiomeLayer::parseParameters: color component out of range (0-255): ") + name);
                    };
                    checkRange(rc, "r"); checkRange(gc, "g"); checkRange(bc, "b"); checkRange(ac, "a");

                    colorsParsed.push_back({static_cast<uint8_t>(rc), static_cast<uint8_t>(gc), static_cast<uint8_t>(bc), static_cast<uint8_t>(ac)});
                }
            } else {
                throw std::runtime_error(std::string("BiomeLayer::parseParameters: unknown key: ") + key);
            }
        }

        if(!colorsParsed.empty()){
            if((int)colorsParsed.size() != biomeCount)
                throw std::runtime_error(std::string("BiomeLayer::parseParameters: number of colors (") + std::to_string(colorsParsed.size()) + ") does not match count (" + std::to_string(biomeCount) + ")");

            //replace biomeColors with parsed colors
            biomeColors = colorsParsed;
        }
    }

private:
    int biomeCount = 5;
    std::vector<std::array<uint8_t,4>> biomeColors = {
        {34,139,34,255},    // Forest Green
        {210,180,140,255},  // Tan (Desert)
        {255,250,250,255},  // Snow
        {160,82,45,255},    // Brown (Mountain)
        {70,130,180,255}    // Steel Blue (Water)
    };

    static cl_mem biomeColorMap(cl_mem biomeMasks, int biomeCount, const std::vector<std::array<uint8_t,4>>& biomeColors){
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

        cl_mem paletteBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(float) * paletteFloats.size(), paletteFloats.data(), &err);
        if (err != CL_SUCCESS || paletteBuf == nullptr)
        {
            clReleaseKernel(kernel);
            throw std::runtime_error("clCreateBuffer failed for BiomeColorMap paletteBuf");
        }

        size_t outSize = voxels * sizeof(cl_float4);
        cl_mem output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, outSize, nullptr, &err);
        if (err != CL_SUCCESS || output == nullptr)
        {
            clReleaseKernel(kernel);
            OpenCLContext::get().releaseMem(paletteBuf);
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
        OpenCLContext::get().releaseMem(paletteBuf);

        return output;
    }
};