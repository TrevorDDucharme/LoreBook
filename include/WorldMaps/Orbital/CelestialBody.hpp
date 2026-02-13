#pragma once
#include <WorldMaps/Orbital/OrbitalMechanics.hpp>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

namespace Orbital {

enum class BodyType {
    Star,
    Planet,
    Moon,
    AsteroidBelt,
    Comet,
    Station
};

inline const char* bodyTypeName(BodyType t) {
    switch (t) {
        case BodyType::Star:         return "Star";
        case BodyType::Planet:       return "Planet";
        case BodyType::Moon:         return "Moon";
        case BodyType::AsteroidBelt: return "AsteroidBelt";
        case BodyType::Comet:        return "Comet";
        case BodyType::Station:      return "Station";
    }
    return "Unknown";
}

inline BodyType bodyTypeFromString(const std::string& s) {
    if (s == "Star")         return BodyType::Star;
    if (s == "Planet")       return BodyType::Planet;
    if (s == "Moon")         return BodyType::Moon;
    if (s == "AsteroidBelt") return BodyType::AsteroidBelt;
    if (s == "Comet")        return BodyType::Comet;
    if (s == "Station")      return BodyType::Station;
    return BodyType::Planet;
}

// ── Celestial body definition ────────────────────────────────────────
struct CelestialBody {
    int64_t id = -1;            // DB primary key (-1 = unsaved)
    int64_t systemID = -1;      // FK → OrbitalSystems.ID
    int64_t parentBodyID = -1;  // FK → CelestialBodies.ID (-1 = root/star)
    
    std::string name;
    BodyType bodyType = BodyType::Planet;
    
    // Physical properties
    double radius = 1.0;        // in arbitrary units (stars ~100, planets ~1-10, moons ~0.1-1)
    double mass = 1.0;          // for lore purposes
    double axialTilt = 0.0;     // radians
    double rotationPeriod = 1.0;
    
    // Orbital elements (not used for the primary star of a system)
    KeplerianElements orbit;
    
    // World surface configuration (layers for the existing World system)
    std::string worldConfig;    // e.g. "Elevation,Humidity,Color"
    int64_t worldItemID = -1;   // FK → VaultItems.ID (optional link to vault node)
    
    // Visual
    float colorTint[3] = {1.0f, 1.0f, 1.0f};  // RGB tint for rendering
    float luminosity = 0.0f;    // >0 for stars (emissive glow)
    
    // Extended properties stored as JSON
    std::string bodyJSON;       // atmosphere, rings, misc metadata
    
    std::string tags;
    
    // Parent/children (populated by OrbitalSystem::load)
    CelestialBody* parent = nullptr;
    std::vector<CelestialBody*> children;
    
    // Compute world-space position at time t (recursively adds parent positions)
    glm::dvec3 worldPositionAt(double t) const {
        glm::dvec3 localPos = (parentBodyID >= 0) ? orbitalPosition(orbit, t) : glm::dvec3(0.0);
        if (parent)
            return parent->worldPositionAt(t) + localPos;
        return localPos;
    }
    
    bool hasSurface() const {
        return !worldConfig.empty() && bodyType != BodyType::AsteroidBelt;
    }
};

} // namespace Orbital
