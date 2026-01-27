#pragma once
#include <cstdint>
#include <memory>
#include <vector>

struct SampleData{
    std::vector<float> channels;
};


class MapLayer {
public:
    MapLayer() = default;
    virtual ~MapLayer() = default;
    virtual SampleData sample(float longitude, float latitude) const=0;
    virtual std::array<uint8_t, 4> getColor(float longitude, float latitude) const=0;

    static std::array<uint8_t, 4> colorRamp(float value, const std::vector<std::array<uint8_t,4>>& colors) {
        if(colors.empty()) return {0,0,0,255};
        if(value <= 0.0f) return colors.front();
        if(value >= 1.0f) return colors.back();
        float scaledValue = value * (colors.size() - 1);
        size_t index = static_cast<size_t>(scaledValue);
        float t = scaledValue - index;
        const auto& c1 = colors[index];
        const auto& c2 = colors[index + 1];
        std::array<uint8_t,4> result;
        for(size_t i=0;i<4;++i){
            result[i] = static_cast<uint8_t>(c1[i] * (1.0f - t) + c2[i] * t);
        }
        return result;
    }

    static float ComputeMaxAmplitude(int octaves, float persistence)
    {
        float amp = 1.0f;
        float maxAmp = 0.0f;

        for (int i = 0; i < octaves; ++i)
        {
            maxAmp += amp;
            amp *= persistence;
        }

        return maxAmp;
    }
private:
    // Layer data members go here
};