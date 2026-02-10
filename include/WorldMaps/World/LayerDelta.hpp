#pragma once
#include <vector>
#include <string>
#include <unordered_map>
#include <cstdint>
#include <cstring>

/// How a delta value is applied to the procedural base
enum class DeltaMode : int {
    Add = 0,  // base + delta
    Set = 1   // replace with delta value (where delta != 0)
};

/// Per-layer per-chunk edit data.
/// Stores a dense grid of floating-point edits that overlay on top of
/// the procedurally generated base values.
struct LayerDelta {
    DeltaMode mode = DeltaMode::Add;
    int channelCount = 1;
    int resolution = 32; // samples per side (32×32)
    std::vector<float> data; // resolution * resolution * channelCount

    /// Per-chunk parameter overrides (e.g. "seed" -> 99999, "frequency" -> 3.0)
    std::unordered_map<std::string, float> paramOverrides;

    /// Check if any edits exist
    bool hasEdits() const {
        if (!paramOverrides.empty()) return true;
        for (float v : data) {
            if (v != 0.0f) return true;
        }
        return false;
    }

    /// Initialize data grid to zeros
    void initGrid(int res, int channels) {
        resolution = res;
        channelCount = channels;
        data.assign(static_cast<size_t>(res) * res * channels, 0.0f);
    }

    /// Get delta value at position
    float getDelta(int x, int y, int channel = 0) const {
        if (data.empty()) return 0.0f;
        int idx = (y * resolution + x) * channelCount + channel;
        if (idx < 0 || idx >= static_cast<int>(data.size())) return 0.0f;
        return data[idx];
    }

    /// Set delta value at position
    void setDelta(int x, int y, int channel, float value) {
        if (data.empty()) initGrid(resolution, channelCount);
        int idx = (y * resolution + x) * channelCount + channel;
        if (idx >= 0 && idx < static_cast<int>(data.size()))
            data[idx] = value;
    }

    /// Get parameter override, returns defaultVal if not overridden
    float getParam(const std::string& key, float defaultVal) const {
        auto it = paramOverrides.find(key);
        return (it != paramOverrides.end()) ? it->second : defaultVal;
    }

    /// Set parameter override
    void setParam(const std::string& key, float value) {
        paramOverrides[key] = value;
    }

    /// Clear all edits
    void clear() {
        data.clear();
        paramOverrides.clear();
    }

    /// Serialize data to binary blob for DB storage
    std::vector<uint8_t> serializeData() const {
        std::vector<uint8_t> blob(data.size() * sizeof(float));
        if (!data.empty()) {
            std::memcpy(blob.data(), data.data(), blob.size());
        }
        return blob;
    }

    /// Deserialize data from binary blob
    void deserializeData(const uint8_t* blob, size_t blobSize) {
        size_t floatCount = blobSize / sizeof(float);
        data.resize(floatCount);
        if (floatCount > 0) {
            std::memcpy(data.data(), blob, floatCount * sizeof(float));
        }
    }

    /// Serialize param overrides to a simple "key=value;key=value" string
    std::string serializeParams() const {
        std::string result;
        for (const auto& [k, v] : paramOverrides) {
            if (!result.empty()) result += ';';
            result += k + '=' + std::to_string(v);
        }
        return result;
    }

    /// Deserialize param overrides from "key=value;key=value" string
    void deserializeParams(const std::string& str) {
        paramOverrides.clear();
        if (str.empty()) return;
        size_t pos = 0;
        while (pos < str.size()) {
            size_t semi = str.find(';', pos);
            if (semi == std::string::npos) semi = str.size();
            size_t eq = str.find('=', pos);
            if (eq != std::string::npos && eq < semi) {
                std::string key = str.substr(pos, eq - pos);
                std::string val = str.substr(eq + 1, semi - eq - 1);
                try { paramOverrides[key] = std::stof(val); } catch (...) {}
            }
            pos = semi + 1;
        }
    }
};
