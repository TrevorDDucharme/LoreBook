#include <WorldMaps/Orbital/OrbitalSystem.hpp>
#include <Vault.hpp>
#include <plog/Log.h>

namespace Orbital {

bool OrbitalSystem::loadFromVault(Vault* vault, int64_t systemID) {
    if (!vault) return false;
    
    m_bodies.clear();
    m_childSystems.clear();
    m_loaded = false;
    
    // Load system info
    m_info = vault->getOrbitalSystem(systemID);
    if (m_info.id < 0) {
        PLOGW << "OrbitalSystem: system " << systemID << " not found";
        return false;
    }
    
    // Load all bodies in this system
    auto bodyList = vault->listCelestialBodies(systemID);
    m_bodies.reserve(bodyList.size());
    for (auto& b : bodyList) {
        m_bodies.push_back(std::make_unique<CelestialBody>(std::move(b)));
    }
    
    buildHierarchy();
    
    // If this is a galaxy, load child stellar systems
    if (m_info.type == SystemType::Galaxy) {
        m_childSystems = vault->listChildSystems(systemID);
    }
    
    m_loaded = true;
    PLOGI << "OrbitalSystem: loaded '" << m_info.name << "' with " << m_bodies.size() << " bodies";
    return true;
}

CelestialBody* OrbitalSystem::findBody(const std::string& name) const {
    for (const auto& b : m_bodies) {
        if (b->name == name) return b.get();
    }
    return nullptr;
}

CelestialBody* OrbitalSystem::findBody(int64_t id) const {
    for (const auto& b : m_bodies) {
        if (b->id == id) return b.get();
    }
    return nullptr;
}

std::vector<CelestialBody*> OrbitalSystem::rootBodies() const {
    std::vector<CelestialBody*> roots;
    for (const auto& b : m_bodies) {
        if (b->parentBodyID < 0) roots.push_back(b.get());
    }
    return roots;
}

std::vector<OrbitalSystem::BodyPosition> OrbitalSystem::bodyPositionsAt(double t) const {
    std::vector<BodyPosition> result;
    result.reserve(m_bodies.size());
    for (const auto& b : m_bodies) {
        result.push_back({b.get(), b->worldPositionAt(t)});
    }
    return result;
}

void OrbitalSystem::buildHierarchy() {
    // Clear existing links
    for (auto& b : m_bodies) {
        b->parent = nullptr;
        b->children.clear();
    }
    
    // Build parent-child links
    for (auto& b : m_bodies) {
        if (b->parentBodyID >= 0) {
            for (auto& candidate : m_bodies) {
                if (candidate->id == b->parentBodyID) {
                    b->parent = candidate.get();
                    candidate->children.push_back(b.get());
                    break;
                }
            }
        }
    }
}

} // namespace Orbital
