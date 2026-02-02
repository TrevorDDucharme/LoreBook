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

    static cl_float4 rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a)
    {
        return {r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f};
    }

    static cl_float4 hsva(float h, float s, float v, float a)
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

    static cl_float4 rgb(float r, float g, float b)
    {
        return {r/255.0f, g/255.0f, b/255.0f, 1.0f};
    }

    static cl_float4 hsv(float h, float s, float v)
    {
        return hsva(h, s, v, 1.0f);
    }

    static cl_float4 colorRamp(float value, const std::vector<cl_float4> &colors)
    {
        if (colors.empty())
            return {0, 0, 0, 1.0f};
        if (value <= 0.0f)
            return colors.front();
        if (value >= 1.0f)
            return colors.back();
        float scaledValue = value * (colors.size() - 1);
        size_t index = static_cast<size_t>(scaledValue);
        float t = scaledValue - index;
        const auto &c1 = colors[index];
        const auto &c2 = colors[index + 1];
        cl_float4 result;
        for (size_t i = 0; i < 4; ++i)
        {
            result.s[i] = c1.s[i] * (1.0f - t) + c2.s[i] * t;
        }
        return result;
    }

    static cl_float4 weightedColorRamp(
    float t,
    const std::vector<cl_float4>& palette,
    const std::vector<float>& weights)
    {
        assert(palette.size() == weights.size());
        size_t n = palette.size();
        assert(n > 0);

        // Clamp t
        t = std::clamp(t, 0.0f, 1.0f);

        // Compute cumulative weights
        std::vector<float> cumulative(n);
        cumulative[0] = weights[0];
        for(size_t i = 1; i < n; ++i)
            cumulative[i] = cumulative[i-1] + weights[i];

        float totalWeight = cumulative.back();
        float scaledT = t * totalWeight;

        // Find which segment t falls into
        size_t idx = 0;
        while(idx < n && scaledT > cumulative[idx]) ++idx;

        if(idx == 0) {
            return palette[0]; // t is in the first segment
        } else if(idx >= n) {
            return palette[n-1]; // t is at the very end
        } else {
            // Lerp between palette[idx-1] and palette[idx]
            float segmentStart = cumulative[idx-1];
            float segmentWeight = weights[idx];
            float localT = (scaledT - segmentStart) / segmentWeight;

            cl_float4 result;
            for(int c = 0; c < 4; ++c) {
                result.s[c] = palette[idx-1].s[c] * (1.0f - localT) + palette[idx].s[c] * localT;
            }
            return result;
        }
    }
    
    void setParentWorld(World* world){
        parentWorld = world;
    }

protected:
    mutable std::mutex parameterMutex_;
    std::unique_lock<std::mutex> lockParameters() const { return std::unique_lock<std::mutex>(parameterMutex_); }
    World* parentWorld = nullptr;
};
