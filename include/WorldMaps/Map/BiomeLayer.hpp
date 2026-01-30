#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class BiomeLayer : public MapLayer {
public:
    BiomeLayer() = default;
    ~BiomeLayer() override;

    SampleData sample() override;

    cl_mem getColor() override;

    //count:2,colors:[{0,0,255,255},{0,255,0,255}]
    void parseParameters(const std::string &params) override;

private:
    cl_mem outColor = nullptr;
    cl_mem colorBuffer = nullptr;

    std::vector<cl_mem> biomeMasks;

    int biomeCount = 5;
    std::vector<std::array<uint8_t,4>> biomeColors = {
        {34,139,34,255},    // Forest Green
        {210,180,140,255},  // Tan (Desert)
        {255,250,250,255},  // Snow
        {160,82,45,255},    // Brown (Mountain)
        {70,130,180,255}    // Steel Blue (Water)
    };

    static void biomeColorMap(cl_mem& output,cl_mem biomeMasks,int fieldW, int fieldH, int fieldD, int biomeCount, const std::vector<std::array<uint8_t,4>>& biomeColors);
};