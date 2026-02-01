#pragma once
#include <cstdint>
#include <memory>
#include <vector>
#include <map>
#include <string>
#include <mutex>
#include <shared_mutex>
#include <CL/cl2.hpp>
#include <LoreBook_Resources/LoreBook_ResourcesEmbeddedVFS.hpp>
#include <OpenCLContext.hpp>
#include <stringUtils.hpp>

class World;

class MapLayer
{
public:
    MapLayer() = default;
    virtual ~MapLayer() = default;
    // Layers can sample themselves given access to the full World so they can query other layers
    virtual cl_mem sample()= 0;
    virtual cl_mem getColor() = 0;
    virtual void parseParameters(const std::string &params) {}

    static std::array<uint8_t, 4> rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        return {r, g, b, a};
    }

    static std::array<uint8_t, 4> hsva(float h, float s, float v, float a)
    {
        float r, g, b;

        int i = static_cast<int>(h * 6.0f);
        float f = h * 6.0f - i;
        float p = v * (1.0f - s);
        float q = v * (1.0f - f * s);
        float t = v * (1.0f - (1.0f - f) * s);

        switch (i % 6)
        {
        case 0:
            r = v;
            g = t;
            b = p;
            break;
        case 1:
            r = q;
            g = v;
            b = p;
            break;
        case 2:
            r = p;
            g = v;
            b = t;
            break;
        case 3:
            r = p;
            g = q;
            b = v;
            break;
        case 4:
            r = t;
            g = p;
            b = v;
            break;
        case 5:
            r = v;
            g = p;
            b = q;
            break;
        default:
            r = g = b = 0.0f;
            break; // should not happen
        }

        return {static_cast<uint8_t>(r * 255.0f), static_cast<uint8_t>(g * 255.0f),
                static_cast<uint8_t>(b * 255.0f), static_cast<uint8_t>(a * 255.0f)};
    }

    static std::array<uint8_t, 4> rgb(float r, float g, float b)
    {
        return {static_cast<uint8_t>(r * 255.0f), static_cast<uint8_t>(g * 255.0f),
                static_cast<uint8_t>(b * 255.0f), 255};
    }

    static std::array<uint8_t, 4> hsv(float h, float s, float v)
    {
        return hsva(h, s, v, 1.0f);
    }

    static std::array<uint8_t, 4> colorRamp(float value, const std::vector<std::array<uint8_t, 4>> &colors)
    {
        if (colors.empty())
            return {0, 0, 0, 255};
        if (value <= 0.0f)
            return colors.front();
        if (value >= 1.0f)
            return colors.back();
        float scaledValue = value * (colors.size() - 1);
        size_t index = static_cast<size_t>(scaledValue);
        float t = scaledValue - index;
        const auto &c1 = colors[index];
        const auto &c2 = colors[index + 1];
        std::array<uint8_t, 4> result;
        for (size_t i = 0; i < 4; ++i)
        {
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

    void setParentWorld(World* world){
        parentWorld = world;
    }

protected:
    mutable std::mutex parameterMutex_;
    std::unique_lock<std::mutex> lockParameters() const { return std::unique_lock<std::mutex>(parameterMutex_); }
    World* parentWorld = nullptr;
};
