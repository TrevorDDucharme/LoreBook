#pragma once
#include <WorldMaps/Orbital/CelestialBody.hpp>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

class Vault;

namespace Orbital {

enum class SystemType {
    Galaxy,     // contains star systems at galactic positions
    Stellar     // a single star system (star + planets + moons)
};

inline const char* systemTypeName(SystemType t) {
    return (t == SystemType::Galaxy) ? "Galaxy" : "Stellar";
}
inline SystemType systemTypeFromString(const std::string& s) {
    return (s == "Galaxy") ? SystemType::Galaxy : SystemType::Stellar;
}

// ── Orbital system (galaxy or star system) ───────────────────────────
struct OrbitalSystemInfo {
    int64_t id = -1;
    std::string name;
    SystemType type = SystemType::Stellar;
    int64_t parentSystemID = -1;  // for star systems within a galaxy
    double posX = 0.0, posY = 0.0, posZ = 0.0;  // galactic coords (for stellar systems)
    std::string tags;
    std::string systemJSON;       // extended metadata
};

class OrbitalSystem {
public:
    OrbitalSystem() = default;
    ~OrbitalSystem() = default;
    OrbitalSystem(OrbitalSystem&&) = default;
    OrbitalSystem& operator=(OrbitalSystem&&) = default;
    OrbitalSystem(const OrbitalSystem&) = delete;
    OrbitalSystem& operator=(const OrbitalSystem&) = delete;
    
    // Load from vault DB
    bool loadFromVault(Vault* vault, int64_t systemID);
    
    // Accessors
    const OrbitalSystemInfo& info() const { return m_info; }
    OrbitalSystemInfo& info() { return m_info; }
    
    const std::vector<std::unique_ptr<CelestialBody>>& bodies() const { return m_bodies; }
    std::vector<std::unique_ptr<CelestialBody>>& bodies() { return m_bodies; }
    
    CelestialBody* findBody(const std::string& name) const;
    CelestialBody* findBody(int64_t id) const;
    
    // Get all root bodies (stars, or top-level objects with no parent)
    std::vector<CelestialBody*> rootBodies() const;
    
    // Compute all body positions at a given time
    struct BodyPosition {
        CelestialBody* body;
        glm::dvec3 position;
    };
    std::vector<BodyPosition> bodyPositionsAt(double t) const;
    
    // Child stellar systems (when this is a galaxy)
    const std::vector<OrbitalSystemInfo>& childSystems() const { return m_childSystems; }
    
    bool isLoaded() const { return m_loaded; }
    
private:
    OrbitalSystemInfo m_info;
    std::vector<std::unique_ptr<CelestialBody>> m_bodies;
    std::vector<OrbitalSystemInfo> m_childSystems;  // for galaxies: child stellar systems
    bool m_loaded = false;
    
    void buildHierarchy();
};

} // namespace Orbital
