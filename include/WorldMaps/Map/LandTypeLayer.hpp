#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class LandTypeLayer : public MapLayer
{
public:
    struct LandTypeProperties
    {
        cl_float4 color;
        float water_absorption;
        float water_retention;
        float nutrient_content;
        float thermal_reflectivity;
        float heating_capacity;
        float permeability;
        float erodibility;
        float salinity;
    };

    LandTypeLayer() = default;
    ~LandTypeLayer() override;

    cl_mem sample() override;

    cl_mem getColor() override;

    // count:2,colors:[{0,0,255,255},{0,255,0,255}]
    void parseParameters(const std::string &params) override;

private:
    cl_mem outColor = nullptr;
    bool outColorDirty = true;

    int landtypeCount = 5;
    /*
        Sand
        Loam
        Clay
        Rock
        Permafrost
    */
    std::vector<LandTypeProperties> landtypes = {
        {MapLayer::rgba(194, 178, 128, 255), 0.8f, 0.2f, 0.2f, 0.6f, 0.2f, 0.5f, 0.7f, 0.1f}, // Sand
        {MapLayer::rgba(101, 67, 33, 255), 0.6f, 0.5f, 0.7f, 0.5f, 0.4f, 0.4f, 0.5f, 0.2f},   // Loam
        {MapLayer::rgba(150, 75, 0, 255), 0.4f, 0.7f, 0.8f, 0.4f, 0.5f, 0.3f, 0.4f, 0.3f},    // Clay
        {MapLayer::rgba(128, 128, 128, 255), 0.2f, 0.1f, 0.1f, 0.3f, 0.8f, 0.1f, 0.2f, 0.4f}, // Rock
        {MapLayer::rgba(224, 255, 255, 255), 0.9f, 0.9f, 0.3f, 0.7f, 0.2f, 0.6f, 0.3f, 0.5f}  // Permafrost
    };

    static void landtypeColorMap(
        cl_mem &output,
        int latitudeResolution, int longitudeResolution,
        const std::vector<LandTypeProperties> &landtypeProperties,
        int landtypeCount,
        const std::vector<float> &frequency,
        const std::vector<float> &lacunarity,
        const std::vector<int> &octaves,
        const std::vector<float> &persistence,
        const std::vector<unsigned int> &seed,
        int mode);

    static void landtypeMap(
        cl_mem &output,
        int latitudeResolution, int longitudeResolution,
        const std::vector<LandTypeProperties> &landtypeProperties,
        int landtypeCount,
        const std::vector<float> &frequency,
        const std::vector<float> &lacunarity,
        const std::vector<int> &octaves,
        const std::vector<float> &persistence,
        const std::vector<unsigned int> &seed,
        int mode);
};