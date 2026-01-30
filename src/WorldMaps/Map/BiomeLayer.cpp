#include <WorldMaps/Map/MapLayer.hpp>
#include <WorldMaps/World/World.hpp>

BiomeLayer::~BiomeLayer()
{
    if (outColor != nullptr)
    {
        OpenCLContext::get().releaseMem(outColor);
        outColor = nullptr;
    }
    if (colorBuffer != nullptr)
    {
        OpenCLContext::get().releaseMem(colorBuffer);
        colorBuffer = nullptr;
    }
    for (auto &mask : biomeMasks)
    {
        if (mask != nullptr)
        {
            OpenCLContext::get().releaseMem(mask);
            mask = nullptr;
        }
    }
}

SampleData BiomeLayer::sample()
{
    SampleData data;
    biomeMasks.resize(biomeCount);
    for (int i = 0; i < biomeCount; ++i)
    {
        // generate biome mask per biome
        perlin(biomeMasks[i], 256, 256, 256, .01f, 2.0f, 8, 0.5f, static_cast<unsigned int>(i * 1000 + 12345u));
        data.channels.push_back(biomeMasks[i]);
    }
    return data;
}

cl_mem BiomeLayer::getColor()
{
    SampleData sampleData = sample();
    concatVolumes(colorBuffer, sampleData.channels, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth());

    // perform Biome color mapping
    try
    {
        biomeColorMap(outColor, colorBuffer, parentWorld->getWorldWidth(), parentWorld->getWorldHeight(), parentWorld->getWorldDepth(), biomeCount, biomeColors);
    }
    catch (const std::exception &ex)
    {
        throw;
    }
    return outColor;
}

// count:2,colors:[{0,0,255,255},{0,255,0,255}]
void BiomeLayer::parseParameters(const std::string &params)
{
    auto trim = [](std::string s)
    {
        auto not_ws = [](int ch)
        { return !std::isspace(ch); };
        s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_ws));
        s.erase(std::find_if(s.rbegin(), s.rend(), not_ws).base(), s.end());
        return s;
    };

    auto tokens = splitBracketAware(params, ",");
    std::vector<std::array<uint8_t, 4>> colorsParsed;
    for (const auto &token : tokens)
    {
        auto kv = splitBracketAware(token, ":");
        if (kv.size() != 2)
            throw std::runtime_error(std::string("BiomeLayer::parseParameters: invalid token: ") + token);

        std::string key = trim(kv[0]);
        std::string value = trim(kv[1]);

        if (key == "count")
        {
            try
            {
                biomeCount = std::stoi(value);
            }
            catch (...)
            {
                throw std::runtime_error(std::string("BiomeLayer::parseParameters: invalid count value: ") + value);
            }
            if (biomeCount <= 0)
                throw std::runtime_error("BiomeLayer::parseParameters: count must be > 0");
        }
        else if (key == "colors")
        {
            // parse colors
            // expected format: [{r,g,b,a},{r,g,b,a},...]
            if (value.size() < 2 || value.front() != '[' || value.back() != ']')
            {
                throw std::runtime_error("BiomeLayer::parseParameters: colors must be in format '[{r,g,b,a},...]'");
            }

            std::string inner = value.substr(1, value.size() - 2);
            // split on top-level commas (splitBracketAware will ignore commas inside matched brackets)
            auto colorTokens = splitBracketAware(inner, ",");

            for (auto colorToken : colorTokens)
            {
                std::string t = trim(colorToken);
                // if the token is wrapped in matching brackets (any of {} () []), strip them
                if (t.size() >= 2)
                {
                    char f = t.front();
                    char l = t.back();
                    if ((f == '{' && l == '}') || (f == '(' && l == ')') || (f == '[' && l == ']'))
                    {
                        t = t.substr(1, t.size() - 2);
                    }
                }

                auto rgbTokens = splitBracketAware(t, ",");
                if (rgbTokens.size() != 4)
                    throw std::runtime_error(std::string("BiomeLayer::parseParameters: color entry must have 4 components: ") + colorToken);

                int rc, gc, bc, ac;
                try
                {
                    rc = std::stoi(trim(rgbTokens[0]));
                    gc = std::stoi(trim(rgbTokens[1]));
                    bc = std::stoi(trim(rgbTokens[2]));
                    ac = std::stoi(trim(rgbTokens[3]));
                }
                catch (...)
                {
                    throw std::runtime_error(std::string("BiomeLayer::parseParameters: invalid color component in: ") + colorToken);
                }

                auto checkRange = [](int v, const char *name)
                {
                    if (v < 0 || v > 255)
                        throw std::runtime_error(std::string("BiomeLayer::parseParameters: color component out of range (0-255): ") + name);
                };
                checkRange(rc, "r");
                checkRange(gc, "g");
                checkRange(bc, "b");
                checkRange(ac, "a");

                colorsParsed.push_back({static_cast<uint8_t>(rc), static_cast<uint8_t>(gc), static_cast<uint8_t>(bc), static_cast<uint8_t>(ac)});
            }
        }
        else
        {
            throw std::runtime_error(std::string("BiomeLayer::parseParameters: unknown key: ") + key);
        }
    }

    if (!colorsParsed.empty())
    {
        if ((int)colorsParsed.size() != biomeCount)
            throw std::runtime_error(std::string("BiomeLayer::parseParameters: number of colors (") + std::to_string(colorsParsed.size()) + ") does not match count (" + std::to_string(biomeCount) + ")");

        // replace biomeColors with parsed colors
        biomeColors = colorsParsed;
    }
}

void BiomeLayer::biomeColorMap(cl_mem &output, cl_mem biomeMasks, int fieldW, int fieldH, int fieldD, int biomeCount, const std::vector<std::array<uint8_t, 4>> &biomeColors)
{
    if (!OpenCLContext::get().isReady())
        return;

    cl_context ctx = OpenCLContext::get().getContext();
    cl_device_id device = OpenCLContext::get().getDevice();
    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;

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

    static cl_kernel kernel = nullptr;
    if (kernel == nullptr)
    {
        kernel = clCreateKernel(program, "biome_masks_to_rgba_float4", &err);
        if (err != CL_SUCCESS)
            throw std::runtime_error("clCreateKernel failed for biome_masks_to_rgba_float4");
    }
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

    cl_mem paletteBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(float) * paletteFloats.size(), paletteFloats.data(), &err, "BiomeColorMap paletteBuf");
    if (err != CL_SUCCESS || paletteBuf == nullptr)
    {
        throw std::runtime_error("clCreateBuffer failed for BiomeColorMap paletteBuf");
    }

    size_t outSize = voxels * sizeof(cl_float4);
    size_t bufferSize;
    if (output != nullptr)
    {
        cl_int err = clGetMemObjectInfo(output,
                                        CL_MEM_SIZE,
                                        sizeof(size_t),
                                        &bufferSize,
                                        NULL);
        if (err != CL_SUCCESS)
        {
            OpenCLContext::get().releaseMem(paletteBuf);
            throw std::runtime_error("clGetMemObjectInfo failed for BiomeColorMap output buffer size");
        }
        if (bufferSize < outSize)
        {
            OpenCLContext::get().releaseMem(output);
            output = nullptr;
        }
    }

    if (output == nullptr)
    {
        output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, outSize, nullptr, &err, "BiomeColorMap output");
        if (err != CL_SUCCESS || output == nullptr)
        {
            OpenCLContext::get().releaseMem(paletteBuf);
            throw std::runtime_error("clCreateBuffer failed for BiomeColorMap output");
        }
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

    OpenCLContext::get().releaseMem(paletteBuf);
}