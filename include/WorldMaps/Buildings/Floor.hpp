#pragma once
#include <WorldMaps/Buildings/FloorPlan.hpp>
#include <string>

// ============================================================
// Floor - One level of a building
// ============================================================
struct Floor {
    int id = 0;
    int level = 0;             // Floor number (0 = ground, negative = basement)
    std::string name = "Ground Floor";
    float height = 3.0f;       // Floor-to-ceiling height in meters
    float elevation = 0.0f;    // Height above ground level
    FloorPlan plan;
    
    // Get display name
    std::string displayName() const {
        if (!name.empty()) return name;
        if (level == 0) return "Ground Floor";
        if (level < 0) return "Basement " + std::to_string(-level);
        return "Floor " + std::to_string(level);
    }
};
