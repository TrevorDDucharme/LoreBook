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

    // Accessors for humidity layer
    const std::vector<LandTypeProperties>& getLandtypes() const { return landtypes; }
    int getLandtypeCount() const { return landtypeCount; }

private:
    cl_mem outColor = nullptr;
    bool outColorDirty = true;

    int landtypeCount = 10;
    /*                                        color                  abs  ret  nutr refl heat perm erod salt */
    std::vector<LandTypeProperties> landtypes = {
        {MapLayer::rgba(230, 200, 140, 255), 0.3f, 0.05f, 0.1f, 0.7f, 0.2f, 0.9f, 0.9f, 0.2f}, // Sand - high aridity: 0.855
        {MapLayer::rgba(90, 60, 30, 255),    0.5f, 0.40f, 0.5f, 0.3f, 0.5f, 0.5f, 0.6f, 0.1f}, // Loam - medium aridity: 0.3
        {MapLayer::rgba(140, 70, 50, 255),   0.4f, 0.70f, 0.6f, 0.3f, 0.5f, 0.3f, 0.4f, 0.1f}, // Clay - low aridity: 0.09
        {MapLayer::rgba(110, 90, 70, 255),   0.6f, 0.50f, 0.4f, 0.4f, 0.4f, 0.6f, 0.7f, 0.05f}, // Silt - medium aridity: 0.3
        {MapLayer::rgba(30, 20, 15, 255),    0.9f, 0.90f, 0.8f, 0.2f, 0.6f, 0.1f, 0.2f, 0.4f}, // Peat - very low aridity: 0.01
        {MapLayer::rgba(100, 100, 100, 255), 0.1f, 0.05f, 0.05f, 0.5f, 0.9f, 0.05f, 0.1f, 0.0f}, // Rock - high aridity: 0.95
        {MapLayer::rgba(130, 130, 120, 255), 0.2f, 0.10f, 0.1f, 0.5f, 0.7f, 0.7f, 0.3f, 0.0f}, // Gravel - high aridity: 0.63
        {MapLayer::rgba(220, 230, 240, 255), 0.1f, 0.05f, 0.05f, 0.9f, 0.1f, 0.02f, 0.1f, 0.0f}, // Permafrost - very high aridity: 0.95
        {MapLayer::rgba(40, 40, 35, 255),    0.5f, 0.60f, 0.9f, 0.2f, 0.6f, 0.4f, 0.3f, 0.0f}, // Volcanic soil - low aridity: 0.16
        {MapLayer::rgba(200, 190, 170, 255), 0.3f, 0.20f, 0.2f, 0.7f, 0.4f, 0.6f, 0.8f, 0.15f}  // Chalk - medium-high aridity: 0.48
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