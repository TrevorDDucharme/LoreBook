#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class LandTypeLayer : public MapLayer {
public:
    LandTypeLayer() = default;
    ~LandTypeLayer() override;

    cl_mem sample() override;

    cl_mem getColor() override;

    //count:2,colors:[{0,0,255,255},{0,255,0,255}]
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
    std::vector<std::array<uint8_t,4>> landtypeColors = {
        MapLayer::rgba(194, 178, 128, 255), // Sand
        MapLayer::rgba(101, 67, 33, 255),   // Loam
        MapLayer::rgba(150, 75, 0, 255),    // Clay
        MapLayer::rgba(128, 128, 128, 255), // Rock
        MapLayer::rgba(224, 255, 255, 255)  // Permafrost
    };

    static void landtypeColorMap(
    cl_mem &output,
    int latitudeResolution, int longitudeResolution,
    int landtypeCount, 
    const std::vector<float> &frequency,
    const std::vector<float> &lacunarity,
    const std::vector<int> &octaves,
    const std::vector<float> &persistence,
    const std::vector<unsigned int> &seed,
    const std::vector<std::array<uint8_t, 4>> &landtypeColors,
    int mode
);
};