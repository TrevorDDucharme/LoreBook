#include <WorldMaps/Orbital/OrbitalSystem.hpp>
#include <Vault.hpp>
#include <plog/Log.h>
#include <functional>

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

// Simple N-body simulator using a symplectic leapfrog (velocity-Verlet) integrator.
// Units assumed: distance = AU, time = years, mass = solar masses.
// Gravitational constant in these units: G = 4 * PI^2 (AU^3 / yr^2 / solarMass).
std::vector<OrbitalSystem::BodyPosition> OrbitalSystem::simulateNBodyPositions(double t, double dt) const {
    const double G = 4.0 * M_PI * M_PI; // units: AU^3 / (yr^2 * solarMass)

    int n = (int)m_bodies.size();
    std::vector<glm::dvec3> pos(n, glm::dvec3(0.0));
    std::vector<glm::dvec3> vel(n, glm::dvec3(0.0));
    std::vector<double> mass(n, 0.0);
    std::vector<int64_t> ids(n);

    // Map index -> body ptr
    for (int i = 0; i < n; ++i) {
        ids[i] = m_bodies[i]->id;
        mass[i] = std::max(0.0, m_bodies[i]->mass); // interpret as solar masses per user's choice
    }

    // Initialize positions & velocities (epoch t=0) from Keplerian elements
    // We compute absolute positions/velocities by accumulating through parent chain.
    std::function<void(int)> computeInitialState = [&](int idx) {
        CelestialBody* b = m_bodies[idx].get();
        if (b->parent == nullptr) {
            // root body: place at its worldPositionAt(0) (usually zero)
            pos[idx] = b->worldPositionAt(0.0);
            vel[idx] = glm::dvec3(0.0);
            return;
        }
        // ensure parent state computed first
        int pIndex = -1;
        for (int j = 0; j < n; ++j) if (m_bodies[j]->id == b->parent->id) { pIndex = j; break; }
        if (pIndex >= 0) {
            if (pos[pIndex] == glm::dvec3(0.0) && m_bodies[pIndex]->parent) computeInitialState(pIndex);
            // local orbital position & velocity in parent frame at t=0
            glm::dvec3 r_local = orbitalPosition(b->orbit, 0.0);
            double mu = G * (mass[pIndex] + mass[idx]);
            glm::dvec3 v_local = orbitalVelocity(b->orbit, 0.0, mu);
            pos[idx] = pos[pIndex] + r_local;
            vel[idx] = vel[pIndex] + v_local;
            return;
        }
        // fallback: use analytic worldPositionAt(0)
        pos[idx] = b->worldPositionAt(0.0);
        vel[idx] = glm::dvec3(0.0);
    };

    // compute for all bodies (parents first by repeated passes)
    for (int i = 0; i < n; ++i) computeInitialState(i);

    // If target time is close to zero, return initial positions
    if (std::fabs(t) < 1e-12) {
        std::vector<BodyPosition> out; out.reserve(n);
        for (int i = 0; i < n; ++i) out.push_back({m_bodies[i].get(), pos[i]});
        return out;
    }

    // Integration parameters
    double tTarget = t;
    double sign = (tTarget >= 0.0) ? 1.0 : -1.0;
    double total = std::fabs(tTarget);
    if (dt <= 0.0) dt = 0.01; // default step = 0.01 years (~3.65 days)
    int steps = (int)std::ceil(total / dt);
    if (steps < 1) steps = 1;
    double h = (total / steps) * sign; // signed step

    auto computeAccelerations = [&](const std::vector<glm::dvec3>& p) {
        std::vector<glm::dvec3> a(n, glm::dvec3(0.0));
        for (int i = 0; i < n; ++i) {
            glm::dvec3 ai(0.0);
            for (int j = 0; j < n; ++j) {
                if (i == j) continue;
                glm::dvec3 r = p[j] - p[i];
                double dist2 = glm::dot(r, r);
                double dist = std::sqrt(dist2) + 1e-12;
                ai += (G * mass[j] / (dist2 * dist)) * r; // G*m_j * r / |r|^3
            }
            a[i] = ai;
        }
        return a;
    };

    // Initial accelerations
    std::vector<glm::dvec3> a = computeAccelerations(pos);

    // Leapfrog integration (velocity Verlet style)
    for (int step = 0; step < steps; ++step) {
        // v_{n+1/2} = v_n + 0.5*h*a_n
        for (int i = 0; i < n; ++i) vel[i] += 0.5 * h * a[i];
        // r_{n+1} = r_n + h*v_{n+1/2}
        for (int i = 0; i < n; ++i) pos[i] += h * vel[i];
        // a_{n+1} = acc(pos_{n+1})
        a = computeAccelerations(pos);
        // v_{n+1} = v_{n+1/2} + 0.5*h*a_{n+1}
        for (int i = 0; i < n; ++i) vel[i] += 0.5 * h * a[i];
    }

    std::vector<BodyPosition> out; out.reserve(n);
    for (int i = 0; i < n; ++i) out.push_back({m_bodies[i].get(), pos[i]});
    return out;
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
            bool foundParent = false;
            for (auto& candidate : m_bodies) {
                if (candidate->id == b->parentBodyID) {
                    b->parent = candidate.get();
                    candidate->children.push_back(b.get());
                    foundParent = true;
                    break;
                }
            }
            if (!foundParent) {
                PLOGW << "OrbitalSystem::buildHierarchy - parent not found for body '" << b->name << "' (ParentBodyID=" << b->parentBodyID << ")";
            }
        }
    }
}

} // namespace Orbital
