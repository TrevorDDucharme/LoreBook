#pragma once
#include <WorldMaps/Map/MapLayer.hpp>

class WaterLayer : public MapLayer {
public:
    WaterLayer() : waterLevelNormalized_(0.45f) {}
    ~WaterLayer() override = default;

    SampleData sample(const World& /*world*/, float /*longitude*/, float /*latitude*/) const override {
        SampleData d; d.channels.push_back(waterLevelNormalized_); return d;
    }
    std::array<uint8_t,4> getColor(const World& /*world*/, float /*longitude*/, float /*latitude*/) const override{
        uint8_t v = static_cast<uint8_t>(std::clamp(waterLevelNormalized_, 0.0f, 1.0f) * 255.0f);
        return {0, 0, v, 255};
    }

    void setWaterLevel(float n) { std::unique_lock<std::shared_mutex> lg(g_layerMutationMutex); waterLevelNormalized_ = n; }
    float getWaterLevel() const { return waterLevelNormalized_; }

    bool setParameter(const std::string& name, float value) override {
        std::unique_lock<std::shared_mutex> lg(g_layerMutationMutex);
        if(name == "waterLevel") { setWaterLevel(value); return true; }
        return false;
    }

private:
    float waterLevelNormalized_;
};
