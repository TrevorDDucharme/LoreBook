#pragma once
#include <WorldMaps/Orbital/OrbitalSystem.hpp>
#include <WorldMaps/Orbital/OrbitalMechanics.hpp>
#include <OpenCLContext.hpp>
#include <GL/glew.h>
#include <CL/cl.h>
#include <glm/glm.hpp>
#include <vector>
#include <cmath>

namespace Orbital {

// Projects an OrbitalSystem into a 2D image using OpenCL multi-sphere ray tracing.
// Manages GPU buffers, kernel dispatch, and GL texture readback.
class OrbitalProjection {
public:
    OrbitalProjection() = default;
    ~OrbitalProjection();
    OrbitalProjection(const OrbitalProjection&) = delete;
    OrbitalProjection& operator=(const OrbitalProjection&) = delete;
    OrbitalProjection(OrbitalProjection&& o) noexcept
        : m_centerLon(o.m_centerLon), m_centerLat(o.m_centerLat), m_zoom(o.m_zoom),
          m_fovY(o.m_fovY), m_time(o.m_time), m_drawOrbits(o.m_drawOrbits),
          m_outputBuffer(o.m_outputBuffer), m_bodyDefBuffer(o.m_bodyDefBuffer),
          m_orbitPointsBuffer(o.m_orbitPointsBuffer) {
        o.m_outputBuffer = nullptr; o.m_bodyDefBuffer = nullptr; o.m_orbitPointsBuffer = nullptr;
    }
    OrbitalProjection& operator=(OrbitalProjection&& o) noexcept {
        if (this != &o) {
            if (m_outputBuffer) OpenCLContext::get().releaseMem(m_outputBuffer);
            if (m_bodyDefBuffer) OpenCLContext::get().releaseMem(m_bodyDefBuffer);
            if (m_orbitPointsBuffer) OpenCLContext::get().releaseMem(m_orbitPointsBuffer);
            m_centerLon = o.m_centerLon; m_centerLat = o.m_centerLat; m_zoom = o.m_zoom;
            m_fovY = o.m_fovY; m_time = o.m_time; m_drawOrbits = o.m_drawOrbits;
            m_outputBuffer = o.m_outputBuffer; m_bodyDefBuffer = o.m_bodyDefBuffer;
            m_orbitPointsBuffer = o.m_orbitPointsBuffer;
            o.m_outputBuffer = nullptr; o.m_bodyDefBuffer = nullptr; o.m_orbitPointsBuffer = nullptr;
        }
        return *this;
    }

    // Camera controls
    void setViewCenter(float lonRad, float latRad) { m_centerLon = lonRad; m_centerLat = latRad; }
    void setZoom(float z) { m_zoom = std::max(z, 0.5f); }
    void setFov(float f) { m_fovY = f; }
    float zoom() const { return m_zoom; }
    float fovY() const { return m_fovY; }
    float centerLon() const { return m_centerLon; }
    float centerLat() const { return m_centerLat; }

    // Time control (in simulation units, e.g. years)
    void setTime(double t) { m_time = t; }
    double time() const { return m_time; }

    // Whether to draw orbit lines
    void setDrawOrbits(bool d) { m_drawOrbits = d; }
    bool drawOrbits() const { return m_drawOrbits; }

    // Render the system into the given GL texture.
    // Creates/resizes the texture as needed.
    void project(OrbitalSystem& system, int width, int height, GLuint& texture);

private:
    // Camera state
    float m_centerLon = 0.0f;
    float m_centerLat = 0.4f;  // slightly tilted view
    float m_zoom = 20.0f;       // distance from origin in AU
    float m_fovY = static_cast<float>(M_PI) / 4.0f;
    double m_time = 0.0;
    bool m_drawOrbits = true;

    // OpenCL resources
    cl_mem m_outputBuffer = nullptr;
    cl_mem m_bodyDefBuffer = nullptr;
    cl_mem m_orbitPointsBuffer = nullptr;

    static cl_program s_program;
    static cl_kernel s_sphereKernel;
    static cl_kernel s_orbitKernel;

    void ensureProgram();
    void dispatchSpheres(const std::vector<OrbitalSystem::BodyPosition>& positions,
                         const OrbitalSystem& system,
                         int width, int height,
                         float camPosX, float camPosY, float camPosZ,
                         float camFwdX, float camFwdY, float camFwdZ,
                         float camRightX, float camRightY, float camRightZ,
                         float camUpX, float camUpY, float camUpZ);
    void dispatchOrbitLines(const OrbitalSystem& system,
                            int width, int height,
                            float camPosX, float camPosY, float camPosZ,
                            float camFwdX, float camFwdY, float camFwdZ,
                            float camRightX, float camRightY, float camRightZ,
                            float camUpX, float camUpY, float camUpZ);
    void ensureOutputBuffer(int width, int height);
};

} // namespace Orbital
