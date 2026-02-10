#include <WorldMaps/Map/HumidityLayer.hpp>
#include <WorldMaps/WorldMap.hpp>
#include <WorldMaps/World/LayerDelta.hpp>
#include <tracy/Tracy.hpp>
#include <plog/Log.h>
#include <cmath>

HumidityLayer::~HumidityLayer()
{
    if (humidityBuffer != nullptr)
    {
        OpenCLContext::get().releaseMem(humidityBuffer);
        humidityBuffer = nullptr;
    }
    if (coloredBuffer != nullptr)
    {
        OpenCLContext::get().releaseMem(coloredBuffer);
        coloredBuffer = nullptr;
    }
}
cl_mem HumidityLayer::sample()
{
    return getHumidityBuffer();
}

cl_mem HumidityLayer::getColor()
{
    // build new cl_mem buffer with RGBA colors based on humidity data (gray scale, full alpha)
    cl_mem humidityBuffer = getHumidityBuffer();
    cl_int err = CL_SUCCESS;
    // Convert humidity scalar values to grayscale RGBA colors
    static std::vector<cl_float4> grayRamp = {
        MapLayer::rgba(0, 0, 0, 255),
        MapLayer::rgba(255, 255, 255, 255)};
    if (coloredBuffer == nullptr)
    {
        scalarToColor(coloredBuffer, humidityBuffer, parentWorld->getWorldLatitudeResolution(), parentWorld->getWorldLongitudeResolution(), 2, grayRamp);
    }
    return coloredBuffer;
}

cl_mem HumidityLayer::getHumidityBuffer()
{
    // Get scalar data from other layers
    cl_mem elevation = parentWorld->getLayer("elevation")->sample();
    cl_mem watertable = parentWorld->getLayer("watertable")->sample();
    cl_mem rivers = parentWorld->getLayer("rivers")->sample();
    cl_mem temperature = parentWorld->getLayer("temperature")->sample();
    
    // Get LandTypeLayer to access properties
    LandTypeLayer* landtypeLayer = dynamic_cast<LandTypeLayer*>(parentWorld->getLayer("landtype"));
    if (!landtypeLayer) {
        throw std::runtime_error("HumidityLayer: landtype layer not found or wrong type");
    }

    if (humidityBuffer == nullptr)
    {
        // Perlin parameters for landtype sampling (match LandTypeLayer defaults)
        std::vector<float> frequency(5, 1.5f);
        std::vector<float> lacunarity(5, 2.0f);
        std::vector<int> octaves(5, 8);
        std::vector<float> persistence(5, 0.5f);
        std::vector<unsigned int> seed(5, 12345u);
        for(int i=0; i<5; ++i) {
            seed[i] += i * 100;
        }
        
        humidityMap(humidityBuffer,
                    parentWorld->getWorldLatitudeResolution(),
                    parentWorld->getWorldLongitudeResolution(),
                    landtypeLayer->getLandtypes(),
                    landtypeLayer->getLandtypeCount(),
                    frequency,
                    lacunarity,
                    octaves,
                    persistence,
                    seed,
                    elevation,
                    watertable,
                    rivers,
                    temperature);
    }
    return humidityBuffer;
}

void HumidityLayer::humidityMap(cl_mem &output,
                                int latitudeResolution,
                                int longitudeResolution,
                                const std::vector<LandTypeLayer::LandTypeProperties> &landtypeProperties,
                                int landtypeCount,
                                const std::vector<float> &frequency,
                                const std::vector<float> &lacunarity,
                                const std::vector<int> &octaves,
                                const std::vector<float> &persistence,
                                const std::vector<unsigned int> &seed,
                                cl_mem elevation,
                                cl_mem watertable,
                                cl_mem rivers,
                                cl_mem temperature)
{
    ZoneScopedN("HumidityLayer::humidityMap");
    if (!OpenCLContext::get().isReady())
        return;

    cl_context ctx = OpenCLContext::get().getContext();
    cl_device_id device = OpenCLContext::get().getDevice();
    cl_command_queue queue = OpenCLContext::get().getQueue();
    cl_int err = CL_SUCCESS;

    size_t voxels = (size_t)latitudeResolution * (size_t)longitudeResolution;

    static cl_program program = nullptr;
    static cl_kernel kernel = nullptr;
    try
    {
        OpenCLContext::get().createProgram(program, "Kernels/Humidity.cl");
        OpenCLContext::get().createKernelFromProgram(kernel, program, "humidity_map");
    }
    catch (const std::runtime_error &e)
    {
        printf("Error initializing HumidityLayer OpenCL: %s\n", e.what());
        return;
    }

    size_t outSize = voxels * sizeof(cl_float);
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
            printf("HumidityLayer::humidityMap: Failed to query output buffer size\n");
        }
        if (bufferSize < outSize)
        {
            OpenCLContext::get().releaseMem(output);
            output = nullptr;
        }
    }

    if (output == nullptr)
    {
        output = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, outSize, nullptr, &err, "HumidityMap output");
        if (err != CL_SUCCESS || output == nullptr)
        {
            throw std::runtime_error("clCreateBuffer failed for HumidityMap output");
        }
    }

    // Create buffers for landtype properties and perlin parameters
    cl_mem propertiesBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, 
        sizeof(LandTypeLayer::LandTypeProperties) * landtypeProperties.size(), 
        (void*)landtypeProperties.data(), &err, "HumidityMap propertiesBuf");
    if (err != CL_SUCCESS) throw std::runtime_error("Failed to create properties buffer");
    
    cl_mem frequencyBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        sizeof(float) * frequency.size(), (void*)frequency.data(), &err, "HumidityMap frequencyBuf");
    if (err != CL_SUCCESS) { OpenCLContext::get().releaseMem(propertiesBuf); throw std::runtime_error("Failed to create frequency buffer"); }
    
    cl_mem lacunarityBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        sizeof(float) * lacunarity.size(), (void*)lacunarity.data(), &err, "HumidityMap lacunarityBuf");
    if (err != CL_SUCCESS) { OpenCLContext::get().releaseMem(propertiesBuf); OpenCLContext::get().releaseMem(frequencyBuf); throw std::runtime_error("Failed to create lacunarity buffer"); }
    
    cl_mem octavesBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        sizeof(int) * octaves.size(), (void*)octaves.data(), &err, "HumidityMap octavesBuf");
    if (err != CL_SUCCESS) { OpenCLContext::get().releaseMem(propertiesBuf); OpenCLContext::get().releaseMem(frequencyBuf); OpenCLContext::get().releaseMem(lacunarityBuf); throw std::runtime_error("Failed to create octaves buffer"); }
    
    cl_mem persistenceBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        sizeof(float) * persistence.size(), (void*)persistence.data(), &err, "HumidityMap persistenceBuf");
    if (err != CL_SUCCESS) { OpenCLContext::get().releaseMem(propertiesBuf); OpenCLContext::get().releaseMem(frequencyBuf); OpenCLContext::get().releaseMem(lacunarityBuf); OpenCLContext::get().releaseMem(octavesBuf); throw std::runtime_error("Failed to create persistence buffer"); }
    
    cl_mem seedBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        sizeof(unsigned int) * seed.size(), (void*)seed.data(), &err, "HumidityMap seedBuf");
    if (err != CL_SUCCESS) { OpenCLContext::get().releaseMem(propertiesBuf); OpenCLContext::get().releaseMem(frequencyBuf); OpenCLContext::get().releaseMem(lacunarityBuf); OpenCLContext::get().releaseMem(octavesBuf); OpenCLContext::get().releaseMem(persistenceBuf); throw std::runtime_error("Failed to create seed buffer"); }

    clSetKernelArg(kernel, 0, sizeof(int), &latitudeResolution);
    clSetKernelArg(kernel, 1, sizeof(int), &longitudeResolution);
    clSetKernelArg(kernel, 2, sizeof(cl_mem), &propertiesBuf);
    clSetKernelArg(kernel, 3, sizeof(int), &landtypeCount);
    clSetKernelArg(kernel, 4, sizeof(cl_mem), &frequencyBuf);
    clSetKernelArg(kernel, 5, sizeof(cl_mem), &lacunarityBuf);
    clSetKernelArg(kernel, 6, sizeof(cl_mem), &octavesBuf);
    clSetKernelArg(kernel, 7, sizeof(cl_mem), &persistenceBuf);
    clSetKernelArg(kernel, 8, sizeof(cl_mem), &seedBuf);
    clSetKernelArg(kernel, 9, sizeof(cl_mem), &elevation);
    clSetKernelArg(kernel, 10, sizeof(cl_mem), &watertable);
    clSetKernelArg(kernel, 11, sizeof(cl_mem), &rivers);
    clSetKernelArg(kernel, 12, sizeof(cl_mem), &temperature);
    clSetKernelArg(kernel, 13, sizeof(cl_mem), &output);

    size_t global[2] = {(size_t)latitudeResolution, (size_t)longitudeResolution};
    {
        ZoneScopedN("HumidityLayer::humidityMap enqueue kernel");
        err = clEnqueueNDRangeKernel(queue, kernel, 2, nullptr, global, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS)
        {
            printf("HumidityLayer::humidityMap: Failed to enqueue kernel: %d\n", err);
        }
    }
    
    // Clean up parameter buffers
    OpenCLContext::get().releaseMem(propertiesBuf);
    OpenCLContext::get().releaseMem(frequencyBuf);
    OpenCLContext::get().releaseMem(lacunarityBuf);
    OpenCLContext::get().releaseMem(octavesBuf);
    OpenCLContext::get().releaseMem(persistenceBuf);
    OpenCLContext::get().releaseMem(seedBuf);
}

// ── Region-bounded generation ────────────────────────────────────

cl_mem HumidityLayer::sampleRegion(float lonMinRad, float lonMaxRad,
                                    float latMinRad, float latMaxRad,
                                    int resX, int resY,
                                    const LayerDelta* delta)
{
    ZoneScopedN("HumidityLayer::sampleRegion");
    if (!OpenCLContext::get().isReady()) return nullptr;

    size_t pixelCount = static_cast<size_t>(resX) * resY;

    // ── 1. Get dependency region data ──
    MapLayer* elevLayer = parentWorld->getLayer("elevation");
    MapLayer* waterLayer = parentWorld->getLayer("watertable");
    MapLayer* tempLayer = parentWorld->getLayer("temperature");

    // Elevation
    std::vector<float> elevData(pixelCount, 0.5f);
    if (elevLayer && elevLayer->supportsRegion()) {
        cl_mem buf = elevLayer->sampleRegion(lonMinRad, lonMaxRad, latMinRad, latMaxRad, resX, resY, nullptr);
        if (buf) {
            clEnqueueReadBuffer(OpenCLContext::get().getQueue(), buf, CL_TRUE,
                                0, pixelCount * sizeof(float), elevData.data(), 0, nullptr, nullptr);
            OpenCLContext::get().releaseMem(buf);
        }
    }

    // Watertable
    std::vector<float> waterData(pixelCount, 0.0f);
    if (waterLayer && waterLayer->supportsRegion()) {
        cl_mem buf = waterLayer->sampleRegion(lonMinRad, lonMaxRad, latMinRad, latMaxRad, resX, resY, nullptr);
        if (buf) {
            clEnqueueReadBuffer(OpenCLContext::get().getQueue(), buf, CL_TRUE,
                                0, pixelCount * sizeof(float), waterData.data(), 0, nullptr, nullptr);
            OpenCLContext::get().releaseMem(buf);
        }
    }

    // Temperature
    std::vector<float> tempData(pixelCount, 0.5f);
    if (tempLayer && tempLayer->supportsRegion()) {
        cl_mem buf = tempLayer->sampleRegion(lonMinRad, lonMaxRad, latMinRad, latMaxRad, resX, resY, nullptr);
        if (buf) {
            clEnqueueReadBuffer(OpenCLContext::get().getQueue(), buf, CL_TRUE,
                                0, pixelCount * sizeof(float), tempData.data(), 0, nullptr, nullptr);
            OpenCLContext::get().releaseMem(buf);
        }
    }

    // Rivers: not region-bounded, use zeros (rivers don't appear at chunk level)
    std::vector<float> riverData(pixelCount, 0.0f);

    // ── 2. Get landtype properties and compute landtype distribution via Perlin region ──
    LandTypeLayer* landtypeLayer = dynamic_cast<LandTypeLayer*>(parentWorld->getLayer("landtype"));
    int nTypes = landtypeLayer ? landtypeLayer->getLandtypeCount() : 0;
    const auto& landtypes = landtypeLayer ? landtypeLayer->getLandtypes()
                                           : std::vector<LandTypeLayer::LandTypeProperties>();

    // Generate landtype noise channels
    std::vector<float> ltNoiseData;
    if (nTypes > 0) {
        float thetaMin = static_cast<float>(M_PI / 2.0) - latMaxRad;
        float thetaMax = static_cast<float>(M_PI / 2.0) - latMinRad;
        float phiMin = lonMinRad + static_cast<float>(M_PI);
        float phiMax = lonMaxRad + static_cast<float>(M_PI);

        std::vector<float> freq(nTypes, 1.5f);
        std::vector<float> lac(nTypes, 2.0f);
        std::vector<int>   oct(nTypes, 8);
        std::vector<float> pers(nTypes, 0.5f);
        std::vector<unsigned int> seeds(nTypes, 12345u);
        for (int i = 0; i < nTypes; ++i) seeds[i] += i * 100;

        cl_mem ltBuf = nullptr;
        perlinRegionChannels(ltBuf, resY, resX, thetaMin, thetaMax, phiMin, phiMax,
                              nTypes, freq, lac, oct, pers, seeds);

        ltNoiseData.resize(static_cast<size_t>(nTypes) * pixelCount);
        if (ltBuf) {
            clEnqueueReadBuffer(OpenCLContext::get().getQueue(), ltBuf, CL_TRUE,
                                0, ltNoiseData.size() * sizeof(float), ltNoiseData.data(), 0, nullptr, nullptr);
            OpenCLContext::get().releaseMem(ltBuf);
        }
    }

    // ── 3. Generate weather noise ──
    float thetaMin = static_cast<float>(M_PI / 2.0) - latMaxRad;
    float thetaMax = static_cast<float>(M_PI / 2.0) - latMinRad;
    float phiMin = lonMinRad + static_cast<float>(M_PI);
    float phiMax = lonMaxRad + static_cast<float>(M_PI);

    cl_mem weatherBuf = nullptr;
    perlinRegion(weatherBuf, resY, resX, thetaMin, thetaMax, phiMin, phiMax,
                 0.008f, 2.0f, 6, 0.5f, 98765u);

    std::vector<float> weatherData(pixelCount, 0.5f);
    if (weatherBuf) {
        clEnqueueReadBuffer(OpenCLContext::get().getQueue(), weatherBuf, CL_TRUE,
                            0, pixelCount * sizeof(float), weatherData.data(), 0, nullptr, nullptr);
        OpenCLContext::get().releaseMem(weatherBuf);
    }

    // ── 4. Compute humidity on CPU (mirrors Humidity.cl logic) ──
    const float sharpness = 20.0f;
    std::vector<float> humidityData(pixelCount);

    for (int row = 0; row < resY; ++row) {
        float lat = latMaxRad - static_cast<float>(row) / std::max(resY - 1, 1)
                                * (latMaxRad - latMinRad);
        float latNorm = (static_cast<float>(M_PI / 2.0) - lat) / static_cast<float>(M_PI);
        // latitude_util equivalent: distance from equator effect
        float lat_value = 1.0f - std::fabs(2.0f * latNorm - 1.0f);
        // Distance from equator: 0.5 at center of lat_value range
        float distance_from_equator = std::fabs(lat_value - 0.5f);

        for (int col = 0; col < resX; ++col) {
            int px = row * resX + col;
            float elev = elevData[px];
            float water_depth = waterData[px];
            float river_flow = riverData[px];
            float temp = tempData[px];

            // Blend landtype properties
            float water_retention = 0.3f;
            float permeability = 0.5f;
            if (nTypes > 0 && !ltNoiseData.empty()) {
                float maxW = -1e30f;
                int bestIdx = 0;
                std::vector<float> weights(nTypes);
                float sumExp = 0.0f;
                for (int b = 0; b < nTypes; ++b) {
                    float w = ltNoiseData[b * pixelCount + px];
                    if (w > maxW) { maxW = w; bestIdx = b; }
                    if (w <= 0.0f) { weights[b] = 0.0f; }
                    else { weights[b] = std::exp(sharpness * (w - maxW)); sumExp += weights[b]; }
                }
                water_retention = 0.0f; permeability = 0.0f;
                if (sumExp > 0.0f) {
                    for (int b = 0; b < nTypes; ++b) {
                        float nw = weights[b] / sumExp;
                        water_retention += landtypes[b].water_retention * nw;
                        permeability += landtypes[b].permeability * nw;
                    }
                } else {
                    water_retention = landtypes[bestIdx].water_retention;
                    permeability = landtypes[bestIdx].permeability;
                }
            }

            bool is_ocean = (water_depth > 0.0f);
            if (is_ocean) {
                float temp_normalized = (temp + 1.0f) * 0.5f;
                float evaporation_rate = std::pow(std::max(temp_normalized, 0.0f), 3.5f);
                humidityData[px] = std::clamp(evaporation_rate * 0.95f, 0.0f, 1.0f);
                continue;
            }

            float landtype_aridity = (1.0f - water_retention) * permeability;
            float temp_normalized = (temp + 1.0f) * 0.5f;

            float desert_belt = 1.0f - std::fabs(distance_from_equator - 0.2f) * 5.0f;
            desert_belt = std::clamp(desert_belt, 0.0f, 1.0f);

            float equatorial_humidity = 1.0f - distance_from_equator * 2.0f;
            equatorial_humidity = std::clamp(equatorial_humidity, 0.0f, 1.0f);

            float base_aridity = landtype_aridity * 0.4f + desert_belt * 0.6f - equatorial_humidity * 0.3f;
            base_aridity = std::clamp(base_aridity, 0.0f, 1.0f);

            float moisture_capacity = temp_normalized * std::max(0.0f, 1.0f - elev * 0.7f);
            moisture_capacity = std::clamp(moisture_capacity, 0.0f, 1.0f);

            // Weather noise (perlinRegion returns [0,1], remap)
            float weather_moisture = weatherData[px]; // already [0,1]

            float base_humidity = (1.0f - base_aridity) * moisture_capacity;

            if (river_flow > 0.05f) {
                float river_humidity = std::clamp(river_flow * 2.0f, 0.0f, 0.9f);
                base_humidity = std::max(base_humidity, river_humidity * (1.0f - base_aridity * 0.5f));
            }

            base_humidity = base_humidity * (0.5f + weather_moisture * 1.0f);

            if (base_humidity < 0.05f) base_humidity = 0.0f;

            humidityData[px] = std::clamp(base_humidity, 0.0f, 1.0f);
        }
    }

    // Upload result to GPU
    cl_int err = CL_SUCCESS;
    cl_mem regionBuf = OpenCLContext::get().createBuffer(
        CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
        pixelCount * sizeof(float), humidityData.data(),
        &err, "HumidityLayer sampleRegion");
    if (err != CL_SUCCESS || !regionBuf) return nullptr;

    // Apply per-sample deltas
    if (delta && !delta->data.empty() &&
        delta->resolution == resX && delta->resolution == resY) {
        size_t deltaSize = delta->data.size() * sizeof(float);
        cl_mem deltaBuf = OpenCLContext::get().createBuffer(
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            deltaSize, const_cast<float*>(delta->data.data()),
            &err, "humidity delta upload");
        if (err == CL_SUCCESS && deltaBuf) {
            applyDeltaScalar(regionBuf, deltaBuf, resY, resX,
                              static_cast<int>(delta->mode));
            OpenCLContext::get().releaseMem(deltaBuf);
        }
    }

    return regionBuf;
}

cl_mem HumidityLayer::getColorRegion(float lonMinRad, float lonMaxRad,
                                      float latMinRad, float latMaxRad,
                                      int resX, int resY,
                                      const LayerDelta* delta)
{
    ZoneScopedN("HumidityLayer::getColorRegion");

    cl_mem scalarBuf = sampleRegion(lonMinRad, lonMaxRad,
                                     latMinRad, latMaxRad,
                                     resX, resY, delta);
    if (!scalarBuf) return nullptr;

    static std::vector<cl_float4> grayRamp = {
        MapLayer::rgba(0, 0, 0, 255),
        MapLayer::rgba(255, 255, 255, 255)
    };

    cl_mem colorBuf = nullptr;
    scalarToColor(colorBuf, scalarBuf, resY, resX, 2, grayRamp);

    return colorBuf;
}