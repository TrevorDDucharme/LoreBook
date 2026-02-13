#pragma once
#include <cmath>
#include <glm/glm.hpp>

namespace Orbital {

// ── Keplerian orbital elements ───────────────────────────────────────
struct KeplerianElements {
    double semiMajorAxis   = 1.0;   // AU or arbitrary distance units
    double eccentricity    = 0.0;   // 0 = circular, <1 = elliptical
    double inclination     = 0.0;   // radians, relative to reference plane
    double longAscNode     = 0.0;   // longitude of ascending node (radians)
    double argPeriapsis    = 0.0;   // argument of periapsis (radians)
    double meanAnomalyEpoch = 0.0;  // mean anomaly at epoch t=0 (radians)
    double period          = 1.0;   // orbital period in time units
};

// ── Kepler equation solver ───────────────────────────────────────────
// Solves M = E - e*sin(E) for E using Newton-Raphson iteration.
// M = mean anomaly, e = eccentricity. Returns eccentric anomaly E.
inline double solveKeplerEquation(double M, double e, int maxIter = 30, double tol = 1e-10) {
    // Normalize M to [0, 2*pi)
    M = std::fmod(M, 2.0 * M_PI);
    if (M < 0.0) M += 2.0 * M_PI;

    // Initial guess
    double E = (e < 0.8) ? M : M_PI;

    for (int i = 0; i < maxIter; ++i) {
        double dE = (E - e * std::sin(E) - M) / (1.0 - e * std::cos(E));
        E -= dE;
        if (std::fabs(dE) < tol) break;
    }
    return E;
}

// ── Position from Keplerian elements at time t ───────────────────────
// Returns position in the parent body's reference frame.
inline glm::dvec3 orbitalPosition(const KeplerianElements& elem, double t) {
    if (elem.period <= 0.0 || elem.semiMajorAxis <= 0.0)
        return glm::dvec3(0.0);

    // Mean anomaly at time t
    double n = 2.0 * M_PI / elem.period;  // mean motion
    double M = elem.meanAnomalyEpoch + n * t;

    // Solve Kepler equation for eccentric anomaly
    double E = solveKeplerEquation(M, elem.eccentricity);

    // True anomaly from eccentric anomaly
    double cosE = std::cos(E);
    double sinE = std::sin(E);
    double e = elem.eccentricity;
    double cosV = (cosE - e) / (1.0 - e * cosE);
    double sinV = std::sqrt(1.0 - e * e) * sinE / (1.0 - e * cosE);
    double v = std::atan2(sinV, cosV);

    // Distance from focus
    double r = elem.semiMajorAxis * (1.0 - e * cosE);

    // Position in orbital plane (periapsis along x)
    double xOrb = r * std::cos(v);
    double yOrb = r * std::sin(v);

    // Rotate by argument of periapsis, inclination, longitude of ascending node
    double cosW = std::cos(elem.argPeriapsis);
    double sinW = std::sin(elem.argPeriapsis);
    double cosI = std::cos(elem.inclination);
    double sinI = std::sin(elem.inclination);
    double cosO = std::cos(elem.longAscNode);
    double sinO = std::sin(elem.longAscNode);

    // 3D position (rotation matrix P = R_z(-Omega) * R_x(-i) * R_z(-omega))
    double x = (cosO * cosW - sinO * sinW * cosI) * xOrb +
               (-cosO * sinW - sinO * cosW * cosI) * yOrb;
    double y = (sinO * cosW + cosO * sinW * cosI) * xOrb +
               (-sinO * sinW + cosO * cosW * cosI) * yOrb;
    double z = (sinW * sinI) * xOrb + (cosW * sinI) * yOrb;

    return glm::dvec3(x, y, z);
}

// ── Orbit path points (for rendering orbit lines) ────────────────────
// Returns N evenly-spaced positions along the full orbit.
inline std::vector<glm::dvec3> orbitPathPoints(const KeplerianElements& elem, int segments = 64) {
    std::vector<glm::dvec3> pts;
    pts.reserve(segments);
    for (int i = 0; i < segments; ++i) {
        double t = elem.period * static_cast<double>(i) / static_cast<double>(segments);
        pts.push_back(orbitalPosition(elem, t));
    }
    return pts;
}

// Compute orbital velocity in the parent reference frame for elements at time t.
// Assumes gravitational parameter mu (in AU^3 / yr^2) is provided (see note on units).
// Units: positions in AU, time in years, mass in solar masses, so mu = 4*pi^2*(M_parent + M_body).
inline glm::dvec3 orbitalVelocity(const KeplerianElements& elem, double t, double mu) {
    if (elem.period <= 0.0 || elem.semiMajorAxis <= 0.0)
        return glm::dvec3(0.0);

    double a = elem.semiMajorAxis;
    double e = elem.eccentricity;

    // Mean motion (rad / yr)
    double n = std::sqrt(mu / (a * a * a));

    // Mean anomaly
    double M = elem.meanAnomalyEpoch + n * t;
    double E = solveKeplerEquation(M, e);
    double cosE = std::cos(E);
    double sinE = std::sin(E);

    double denom = 1.0 - e * cosE;
    if (std::fabs(denom) < 1e-12) denom = 1e-12;

    // Perifocal (orbital plane) velocity components
    double factor = a * n / denom;
    double vx_orb = -factor * sinE;
    double vy_orb =  factor * std::sqrt(std::max(0.0, 1.0 - e * e)) * cosE;

    // Rotate from orbital plane to inertial using same angles as orbitalPosition
    double cosW = std::cos(elem.argPeriapsis);
    double sinW = std::sin(elem.argPeriapsis);
    double cosI = std::cos(elem.inclination);
    double sinI = std::sin(elem.inclination);
    double cosO = std::cos(elem.longAscNode);
    double sinO = std::sin(elem.longAscNode);

    // Rotation matrix components (applied to vector [vx_orb, vy_orb, 0])
    double rx = (cosO * cosW - sinO * sinW * cosI) * vx_orb + (-cosO * sinW - sinO * cosW * cosI) * vy_orb;
    double ry = (sinO * cosW + cosO * sinW * cosI) * vx_orb + (-sinO * sinW + cosO * cosW * cosI) * vy_orb;
    double rz = (sinW * sinI) * vx_orb + (cosW * sinI) * vy_orb;

    return glm::dvec3(rx, ry, rz);
}

} // namespace Orbital
