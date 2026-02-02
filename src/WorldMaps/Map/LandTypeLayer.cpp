#include <WorldMaps/Map/LandTypeLayer.hpp>
#include <WorldMaps/World/World.hpp>
#include <tracy/Tracy.hpp>
#include <plog/Log.h>

LandTypeLayer::~LandTypeLayer()
{
    if (outColor != nullptr)
    {
        OpenCLContext::get().releaseMem(outColor);
        outColor = nullptr;
    }
}

cl_mem LandTypeLayer::sample()
{
    std::vector<float> frequency(landtypeCount, 1.5f);
    std::vector<float> lacunarity(landtypeCount, 2.0f);
    std::vector<int> octaves(landtypeCount, 8);
    std::vector<float> persistence(landtypeCount, 0.5f);
    std::vector<unsigned int> seed(landtypeCount, 12345u);

    for(int i=0;i<landtypeCount;++i){
        seed[i] += i * 100;
    }

    if(outColorDirty || outColor == nullptr){
        // perform LandType color mapping
        try
        {
            landtypeColorMap(outColor, parentWorld->getWorldLatitudeResolution(), parentWorld->getWorldLongitudeResolution(),landtypes, landtypeCount, frequency, lacunarity, octaves, persistence, seed, 1);
        }
        catch (const std::exception &ex)
        {
            PLOGE << "LandTypeLayer::sample: exception: " << ex.what();
            throw ex;
        }
        outColorDirty = false;
    }
    return outColor;
}

cl_mem LandTypeLayer::getColor()
{
    ZoneScopedN("LandTypeLayer::getColor");
    return sample();
}

// count:2,colors:[{0,0,255,255},{0,255,0,255}]
void LandTypeLayer::parseParameters(const std::string &params)
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
    std::vector<cl_float4> colorsParsed;
    for (const auto &token : tokens)
    {
        auto kv = splitBracketAware(token, ":");
        if (kv.size() != 2)
            throw std::runtime_error(std::string("LandTypeLayer::parseParameters: invalid token: ") + token);

        std::string key = trim(kv[0]);
        std::string value = trim(kv[1]);

        if (key == "count")
        {
            try
            {
                landtypeCount = std::stoi(value);
            }
            catch (...)
            {
                throw std::runtime_error(std::string("LandTypeLayer::parseParameters: invalid count value: ") + value);
            }
            if (landtypeCount <= 0)
                throw std::runtime_error("LandTypeLayer::parseParameters: count must be > 0");
        }
        else if (key == "colors")
        {
            // parse colors
            // expected format: [{r,g,b,a},{r,g,b,a},...]
            if (value.size() < 2 || value.front() != '[' || value.back() != ']')
            {
                throw std::runtime_error("LandTypeLayer::parseParameters: colors must be in format '[{r,g,b,a},...]'");
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
                    throw std::runtime_error(std::string("LandTypeLayer::parseParameters: color entry must have 4 components: ") + colorToken);

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
                    throw std::runtime_error(std::string("LandTypeLayer::parseParameters: invalid color component in: ") + colorToken);
                }

                auto checkRange = [](int v, const char *name)
                {
                    if (v < 0 || v > 255)
                        throw std::runtime_error(std::string("LandTypeLayer::parseParameters: color component out of range (0-255): ") + name);
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
            throw std::runtime_error(std::string("LandTypeLayer::parseParameters: unknown key: ") + key);
        }
    }

    if (!colorsParsed.empty())
    {
        if ((int)colorsParsed.size() != landtypeCount)
            throw std::runtime_error(std::string("LandTypeLayer::parseParameters: number of colors (") + std::to_string(colorsParsed.size()) + ") does not match count (" + std::to_string(landtypeCount) + ")");

        // replace landtypeColors with parsed colors
        //landtypeColors = colorsParsed;
    }
    outColorDirty = true;
}

void LandTypeLayer::landtypeColorMap(
    cl_mem &output,
    int latitudeResolution, int longitudeResolution,
    const std::vector<LandTypeProperties> &landtypeProperties,
    int landtypeCount, 
    const std::vector<float> &frequency,
    const std::vector<float> &lacunarity,
    const std::vector<int> &octaves,
    const std::vector<float> &persistence,
    const std::vector<unsigned int> &seed,
    int mode
)
{
    ZoneScopedN("LandTypeLayer::landtypeColorMap");
    if (!OpenCLContext::get().isReady())
        return;

    cl_context ctx = OpenCLContext::get().getContext();
    cl_device_id device = OpenCLContext::get().getDevice();
    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;

    size_t voxels = (size_t)latitudeResolution * (size_t)longitudeResolution;

    static cl_program program = nullptr;
    static cl_kernel kernel = nullptr;
    try{
        OpenCLContext::get().createProgram(program,"Kernels/LandType.cl");
        OpenCLContext::get().createKernelFromProgram(kernel,program,"landtypeColor");
    }
    catch (const std::runtime_error &e)
    {
        printf("Error initializing LandTypeLayer OpenCL: %s\n", e.what());
        return;
    }

    // create LandTypeProperties buffer
    std::vector<LandTypeProperties> properties;
    for (int i = 0; i < landtypeCount; ++i)
    {
        properties.push_back(landtypeProperties[i]);
    }

    cl_mem propertiesBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(LandTypeProperties) * properties.size(), properties.data(), &err, "LandTypeColorMap propertiesBuf");
    if (err != CL_SUCCESS || propertiesBuf == nullptr)
    {
        throw std::runtime_error("clCreateBuffer failed for LandTypeColorMap propertiesBuf");
    }

    size_t outSize = voxels * sizeof(cl_float4);
    size_t bufferSize;
    if (output != nullptr)
    {
        //ZoneScopedN("LandTypeLayer::landtypeColorMap check output buffer");
        cl_int err = clGetMemObjectInfo(output,
                                        CL_MEM_SIZE,
                                        sizeof(size_t),
                                        &bufferSize,
                                        NULL);
        if (err != CL_SUCCESS)
        {
            OpenCLContext::get().releaseMem(propertiesBuf);
            throw std::runtime_error("clGetMemObjectInfo failed for LandTypeColorMap output buffer size");
        }
        if (bufferSize < outSize)
        {
            OpenCLContext::get().releaseMem(output);
            output = nullptr;
        }
    }

    if (output == nullptr)
    {
        //ZoneScopedN("LandTypeLayer::landtypeColorMap alloc output buffer");
        output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, outSize, nullptr, &err, "LandTypeColorMap output");
        if (err != CL_SUCCESS || output == nullptr)
        {
            OpenCLContext::get().releaseMem(propertiesBuf);
            throw std::runtime_error("clCreateBuffer failed for LandTypeColorMap output");
        }
    }

    cl_mem frequencyBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(float) * frequency.size(), (void*)frequency.data(), &err, "LandTypeColorMap frequencyBuf");
    if (err != CL_SUCCESS || frequencyBuf == nullptr)
    {
        OpenCLContext::get().releaseMem(propertiesBuf);
        throw std::runtime_error("clCreateBuffer failed for LandTypeColorMap frequencyBuf");
    }

    cl_mem lacunarityBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(float) * lacunarity.size(), (void*)lacunarity.data(), &err, "LandTypeColorMap lacunarityBuf");
    if (err != CL_SUCCESS || lacunarityBuf == nullptr)
    {
        OpenCLContext::get().releaseMem(propertiesBuf);
        OpenCLContext::get().releaseMem(frequencyBuf);
        throw std::runtime_error("clCreateBuffer failed for LandTypeColorMap lacunarityBuf");
    }

    cl_mem octavesBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(int) * octaves.size(), (void*)octaves.data(), &err, "LandTypeColorMap octavesBuf");
    if (err != CL_SUCCESS || octavesBuf == nullptr)
    {
        OpenCLContext::get().releaseMem(propertiesBuf);
        OpenCLContext::get().releaseMem(frequencyBuf);
        OpenCLContext::get().releaseMem(lacunarityBuf);
        throw std::runtime_error("clCreateBuffer failed for LandTypeColorMap octavesBuf");
    }

    cl_mem persistenceBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(float) * persistence.size(), (void*)persistence.data(), &err, "LandTypeColorMap persistenceBuf");
    if (err != CL_SUCCESS || persistenceBuf == nullptr)
    {
        OpenCLContext::get().releaseMem(propertiesBuf);
        OpenCLContext::get().releaseMem(frequencyBuf);
        OpenCLContext::get().releaseMem(lacunarityBuf);
        OpenCLContext::get().releaseMem(octavesBuf);
        throw std::runtime_error("clCreateBuffer failed for LandTypeColorMap persistenceBuf");
    }

    cl_mem seedBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof(unsigned int) * seed.size(), (void*)seed.data(), &err, "LandTypeColorMap seedBuf");
    if (err != CL_SUCCESS || seedBuf == nullptr)
    {
        OpenCLContext::get().releaseMem(propertiesBuf);
        OpenCLContext::get().releaseMem(frequencyBuf);
        OpenCLContext::get().releaseMem(lacunarityBuf);
        OpenCLContext::get().releaseMem(octavesBuf);
        OpenCLContext::get().releaseMem(persistenceBuf);
        throw std::runtime_error("clCreateBuffer failed for LandTypeColorMap seedBuf");
    }

    clSetKernelArg(kernel, 0, sizeof(int), &latitudeResolution);
    clSetKernelArg(kernel, 1, sizeof(int), &longitudeResolution);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &propertiesBuf);
    clSetKernelArg(kernel, 3, sizeof(int), &landtypeCount);
    clSetKernelArg(kernel, 4, sizeof(cl_mem), &frequencyBuf);
    clSetKernelArg(kernel, 5, sizeof(cl_mem), &lacunarityBuf);
    clSetKernelArg(kernel, 6, sizeof(cl_mem), &octavesBuf);
    clSetKernelArg(kernel, 7, sizeof(cl_mem), &persistenceBuf);
    clSetKernelArg(kernel, 8, sizeof(cl_mem), &seedBuf);
    clSetKernelArg(kernel, 9, sizeof(int), &mode);
    clSetKernelArg(kernel, 10, sizeof(cl_mem), &output);

    size_t global[2] = {(size_t)latitudeResolution, (size_t)longitudeResolution};
    {
        //ZoneScopedN("LandTypeLayer::landtypeColorMap enqueue kernel");
        err = clEnqueueNDRangeKernel(queue, kernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
    }

    OpenCLContext::get().releaseMem(propertiesBuf);
}