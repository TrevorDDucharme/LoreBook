#include <WorldMaps/Orbital/OrbitalProjection.hpp>
#include <WorldMaps/Orbital/OrbitalMechanics.hpp>
#include <cmath>
#include <algorithm>
#include <stdexcept>
#include <sstream>
#include <plog/Log.h>

namespace Orbital {

cl_program OrbitalProjection::s_program = nullptr;
cl_kernel OrbitalProjection::s_sphereKernel = nullptr;
cl_kernel OrbitalProjection::s_orbitKernel = nullptr;

OrbitalProjection::~OrbitalProjection() {
    if (m_outputBuffer) { OpenCLContext::get().releaseMem(m_outputBuffer); m_outputBuffer = nullptr; }
    if (m_bodyDefBuffer) { OpenCLContext::get().releaseMem(m_bodyDefBuffer); m_bodyDefBuffer = nullptr; }
    if (m_orbitPointsBuffer) { OpenCLContext::get().releaseMem(m_orbitPointsBuffer); m_orbitPointsBuffer = nullptr; }
}

void OrbitalProjection::ensureProgram() {
    if (s_program) return;
    if (!OpenCLContext::get().isReady()) return;

    cl_context ctx = OpenCLContext::get().getContext();
    cl_device_id device = OpenCLContext::get().getDevice();
    cl_int err = CL_SUCCESS;

    std::string src = preprocessCLIncludes("Kernels/OrbitalView.cl");
    const char* srcPtr = src.c_str();
    size_t len = src.length();
    s_program = clCreateProgramWithSource(ctx, 1, &srcPtr, &len, &err);
    if (err != CL_SUCCESS || !s_program)
        throw std::runtime_error("Failed to create OrbitalView program");

    err = clBuildProgram(s_program, 1, &device, nullptr, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        size_t logSize = 0;
        clGetProgramBuildInfo(s_program, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
        std::string log(logSize, ' ');
        clGetProgramBuildInfo(s_program, device, CL_PROGRAM_BUILD_LOG, logSize, &log[0], nullptr);
        throw std::runtime_error(std::string("Failed to build OrbitalView: ") + log);
    }
}

void OrbitalProjection::ensureOutputBuffer(int width, int height) {
    size_t needed = (size_t)width * (size_t)height * sizeof(cl_float4);
    if (m_outputBuffer) {
        size_t existing = 0;
        clGetMemObjectInfo(m_outputBuffer, CL_MEM_SIZE, sizeof(existing), &existing, nullptr);
        if (existing >= needed) return;
        OpenCLContext::get().releaseMem(m_outputBuffer);
        m_outputBuffer = nullptr;
    }
    cl_int err = CL_SUCCESS;
    m_outputBuffer = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, needed, nullptr, &err, "orbital output");
    if (err != CL_SUCCESS)
        throw std::runtime_error("Failed to create orbital output buffer");
}

void OrbitalProjection::project(OrbitalSystem& system, int width, int height, GLuint& texture) {
    if (!OpenCLContext::get().isReady()) return;
    if (width <= 0 || height <= 0) return;

    ensureProgram();
    ensureOutputBuffer(width, height);

    // Camera math — orbit around origin
    float targetX = 0.0f, targetY = 0.0f, targetZ = 0.0f;
    float camPosX = m_zoom * cosf(m_centerLat) * cosf(m_centerLon);
    float camPosY = m_zoom * sinf(m_centerLat);
    float camPosZ = m_zoom * cosf(m_centerLat) * sinf(m_centerLon);

    float fwdX = targetX - camPosX;
    float fwdY = targetY - camPosY;
    float fwdZ = targetZ - camPosZ;
    float fwdLen = sqrtf(fwdX*fwdX + fwdY*fwdY + fwdZ*fwdZ);
    if (fwdLen < 1e-6f) fwdLen = 1.0f;
    fwdX /= fwdLen; fwdY /= fwdLen; fwdZ /= fwdLen;

    float wuX = 0.0f, wuY = 1.0f, wuZ = 0.0f;
    float dotUF = fwdX*wuX + fwdY*wuY + fwdZ*wuZ;
    if (fabsf(dotUF) > 0.9999f) { wuX = 0.0f; wuY = 0.0f; wuZ = 1.0f; }

    float rX = fwdY*wuZ - fwdZ*wuY;
    float rY = fwdZ*wuX - fwdX*wuZ;
    float rZ = fwdX*wuY - fwdY*wuX;
    float rLen = sqrtf(rX*rX + rY*rY + rZ*rZ);
    if (rLen < 1e-6f) rLen = 1.0f;
    rX /= rLen; rY /= rLen; rZ /= rLen;

    float uX = rY*fwdZ - rZ*fwdY;
    float uY = rZ*fwdX - rX*fwdZ;
    float uZ = rX*fwdY - rY*fwdX;
    float uLen = sqrtf(uX*uX + uY*uY + uZ*uZ);
    if (uLen < 1e-6f) uLen = 1.0f;
    uX /= uLen; uY /= uLen; uZ /= uLen;

    // Compute body positions at current time
    auto positions = system.bodyPositionsAt(m_time);

    // Dispatch sphere kernel
    dispatchSpheres(positions, system, width, height,
                    camPosX, camPosY, camPosZ,
                    fwdX, fwdY, fwdZ,
                    rX, rY, rZ,
                    uX, uY, uZ);

    // Dispatch orbit lines on top
    if (m_drawOrbits) {
        dispatchOrbitLines(system, width, height,
                           camPosX, camPosY, camPosZ,
                           fwdX, fwdY, fwdZ,
                           rX, rY, rZ,
                           uX, uY, uZ);
    }

    // GL texture management
    if (texture == 0) {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    } else {
        glBindTexture(GL_TEXTURE_2D, texture);
        GLint tw = 0, th = 0;
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &tw);
        glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &th);
        if (tw != width || th != height) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
        }
    }

    // Readback
    size_t bufSize = (size_t)width * (size_t)height * sizeof(float) * 4;
    std::vector<float> hostBuf((size_t)width * (size_t)height * 4);
    cl_int err = clEnqueueReadBuffer(OpenCLContext::get().getQueue(), m_outputBuffer, CL_TRUE,
                                     0, bufSize, hostBuf.data(), 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        PLOGE << "Orbital readback failed: " << err;
        return;
    }
    clFinish(OpenCLContext::get().getQueue());
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, GL_RGBA, GL_FLOAT, hostBuf.data());
}

void OrbitalProjection::dispatchSpheres(
    const std::vector<OrbitalSystem::BodyPosition>& positions,
    const OrbitalSystem& system,
    int width, int height,
    float camPosX, float camPosY, float camPosZ,
    float camFwdX, float camFwdY, float camFwdZ,
    float camRightX, float camRightY, float camRightZ,
    float camUpX, float camUpY, float camUpZ)
{
    if (!s_program) return;
    cl_int err = CL_SUCCESS;

    if (!s_sphereKernel) {
        s_sphereKernel = clCreateKernel(s_program, "orbital_view_multisphere", &err);
        if (err != CL_SUCCESS)
            throw std::runtime_error("Failed to create orbital_view_multisphere kernel");
    }

    int bodyCount = std::min((int)positions.size(), 32);
    if (bodyCount == 0) return;

    // Pack body definitions: 2 float4s per body
    //   [0] = (cx, cy, cz, radius)
    //   [1] = (luminosity, 0, 0, 0)  — tex atlas info not used yet (procedural colors)
    // Skip AsteroidBelt bodies — they are rendered as orbit bands, not solid spheres
    std::vector<cl_float4> bodyDefs;
    std::vector<const OrbitalSystem::BodyPosition*> renderedBodies;
    for (int i = 0; i < bodyCount; ++i) {
        auto& bp = positions[i];
        if (bp.body->bodyType == BodyType::AsteroidBelt) continue;

        float displayRadius = (float)bp.body->radius;
        displayRadius = std::max(displayRadius, 0.05f);
        if (bp.body->bodyType == BodyType::Star) displayRadius = std::max(displayRadius, 0.3f);

        bodyDefs.push_back({(float)bp.position.x, (float)bp.position.y, (float)bp.position.z, displayRadius});
        bodyDefs.push_back({bp.body->luminosity, 0.0f, 0.0f, 0.0f});
        renderedBodies.push_back(&bp);
    }

    int renderCount = (int)renderedBodies.size();
    if (renderCount == 0) return;

    // Upload body defs
    size_t defSize = bodyDefs.size() * sizeof(cl_float4);
    if (m_bodyDefBuffer) { OpenCLContext::get().releaseMem(m_bodyDefBuffer); m_bodyDefBuffer = nullptr; }
    m_bodyDefBuffer = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                                        defSize, bodyDefs.data(), &err, "orbital bodyDefs");

    // For now, no texture atlas — we use a simple 1-pixel procedural color per body.
    // Create a tiny atlas: 1 pixel per body with body's color tint.
    std::vector<cl_float4> miniAtlas(renderCount);
    std::vector<cl_int2> texRegions(renderCount);
    std::vector<cl_int> texWidths(renderCount);
    std::vector<cl_int> texHeights(renderCount);

    for (int i = 0; i < renderCount; ++i) {
        auto* bp = renderedBodies[i];
        miniAtlas[i] = {bp->body->colorTint[0], bp->body->colorTint[1], bp->body->colorTint[2], 1.0f};
        texRegions[i] = {i, 1};      // offset=i, size=1
        texWidths[i] = 1;
        texHeights[i] = 1;
    }

    cl_mem atlasBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        miniAtlas.size() * sizeof(cl_float4), miniAtlas.data(), &err, "orbital atlas");
    cl_mem regionBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        texRegions.size() * sizeof(cl_int2), texRegions.data(), &err, "orbital regions");
    cl_mem widthBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        texWidths.size() * sizeof(cl_int), texWidths.data(), &err, "orbital widths");
    cl_mem heightBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
        texHeights.size() * sizeof(cl_int), texHeights.data(), &err, "orbital heights");

    // Set kernel args
    clSetKernelArg(s_sphereKernel, 0, sizeof(cl_mem), &m_bodyDefBuffer);
    clSetKernelArg(s_sphereKernel, 1, sizeof(int), &renderCount);
    clSetKernelArg(s_sphereKernel, 2, sizeof(cl_mem), &atlasBuf);
    clSetKernelArg(s_sphereKernel, 3, sizeof(cl_mem), &regionBuf);
    clSetKernelArg(s_sphereKernel, 4, sizeof(cl_mem), &widthBuf);
    clSetKernelArg(s_sphereKernel, 5, sizeof(cl_mem), &heightBuf);
    clSetKernelArg(s_sphereKernel, 6, sizeof(cl_mem), &m_outputBuffer);
    clSetKernelArg(s_sphereKernel, 7, sizeof(int), &width);
    clSetKernelArg(s_sphereKernel, 8, sizeof(int), &height);

    cl_float3 camPos = {camPosX, camPosY, camPosZ};
    cl_float3 camFwd = {camFwdX, camFwdY, camFwdZ};
    cl_float3 camRight = {camRightX, camRightY, camRightZ};
    cl_float3 camUp = {camUpX, camUpY, camUpZ};

    clSetKernelArg(s_sphereKernel, 9, sizeof(camPos), &camPos);
    clSetKernelArg(s_sphereKernel, 10, sizeof(camFwd), &camFwd);
    clSetKernelArg(s_sphereKernel, 11, sizeof(camRight), &camRight);
    clSetKernelArg(s_sphereKernel, 12, sizeof(camUp), &camUp);
    clSetKernelArg(s_sphereKernel, 13, sizeof(float), &m_fovY);

    cl_float4 bg = {0.02f, 0.02f, 0.05f, 1.0f}; // dark space
    clSetKernelArg(s_sphereKernel, 14, sizeof(bg), &bg);

    size_t global[2] = {(size_t)width, (size_t)height};
    err = clEnqueueNDRangeKernel(OpenCLContext::get().getQueue(), s_sphereKernel, 2,
                                  nullptr, global, nullptr, 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        PLOGE << "orbital sphere kernel dispatch failed: " << err;
    }

    // Cleanup temp buffers
    OpenCLContext::get().releaseMem(atlasBuf);
    OpenCLContext::get().releaseMem(regionBuf);
    OpenCLContext::get().releaseMem(widthBuf);
    OpenCLContext::get().releaseMem(heightBuf);
}

void OrbitalProjection::dispatchOrbitLines(
    const OrbitalSystem& system,
    int width, int height,
    float camPosX, float camPosY, float camPosZ,
    float camFwdX, float camFwdY, float camFwdZ,
    float camRightX, float camRightY, float camRightZ,
    float camUpX, float camUpY, float camUpZ)
{
    if (!s_program) return;
    cl_int err = CL_SUCCESS;

    if (!s_orbitKernel) {
        s_orbitKernel = clCreateKernel(s_program, "orbital_view_orbit_lines", &err);
        if (err != CL_SUCCESS) {
            PLOGE << "Failed to create orbital_view_orbit_lines kernel: " << err;
            return;
        }
    }

    // Collect orbit path points for all non-root bodies
    const int POINTS_PER_ORBIT = 128;
    std::vector<cl_float4> allPoints;
    std::vector<cl_float4> beltPoints; // asteroid belt orbits rendered separately (thicker band)

    for (auto& body : system.bodies()) {
        if (body->parentBodyID < 0) continue;  // root bodies (stars) don't orbit
        if (body->orbit.period <= 0.0) continue;

        // Get parent position for offset
        glm::dvec3 parentPos(0.0);
        if (body->parent) {
            parentPos = body->parent->worldPositionAt(m_time);
        }

        auto pts = orbitPathPoints(body->orbit, POINTS_PER_ORBIT);

        if (body->bodyType == BodyType::AsteroidBelt) {
            // Asteroid belts: output as band points (rendered thicker)
            for (auto& p : pts) {
                glm::dvec3 worldPt = parentPos + p;
                beltPoints.push_back({(float)worldPt.x, (float)worldPt.y, (float)worldPt.z, 0.0f});
            }
        } else {
            for (auto& p : pts) {
                glm::dvec3 worldPt = parentPos + p;
                allPoints.push_back({(float)worldPt.x, (float)worldPt.y, (float)worldPt.z, 0.0f});
            }
        }
    }

    if (allPoints.empty() && beltPoints.empty()) return;

    cl_float3 camPos = {camPosX, camPosY, camPosZ};
    cl_float3 camFwd = {camFwdX, camFwdY, camFwdZ};
    cl_float3 camRight = {camRightX, camRightY, camRightZ};
    cl_float3 camUp = {camUpX, camUpY, camUpZ};
    size_t global[2] = {(size_t)width, (size_t)height};

    // Dispatch normal orbit lines
    if (!allPoints.empty()) {
        int pointCount = (int)allPoints.size();

        if (m_orbitPointsBuffer) { OpenCLContext::get().releaseMem(m_orbitPointsBuffer); m_orbitPointsBuffer = nullptr; }
        m_orbitPointsBuffer = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            allPoints.size() * sizeof(cl_float4), allPoints.data(), &err, "orbital orbit points");

        clSetKernelArg(s_orbitKernel, 0, sizeof(cl_mem), &m_orbitPointsBuffer);
        clSetKernelArg(s_orbitKernel, 1, sizeof(int), &pointCount);
        clSetKernelArg(s_orbitKernel, 2, sizeof(cl_mem), &m_outputBuffer);
        clSetKernelArg(s_orbitKernel, 3, sizeof(int), &width);
        clSetKernelArg(s_orbitKernel, 4, sizeof(int), &height);
        clSetKernelArg(s_orbitKernel, 5, sizeof(camPos), &camPos);
        clSetKernelArg(s_orbitKernel, 6, sizeof(camFwd), &camFwd);
        clSetKernelArg(s_orbitKernel, 7, sizeof(camRight), &camRight);
        clSetKernelArg(s_orbitKernel, 8, sizeof(camUp), &camUp);
        clSetKernelArg(s_orbitKernel, 9, sizeof(float), &m_fovY);

        cl_float4 lineCol = {0.3f, 0.5f, 0.8f, 0.6f};
        float lineWidth = 2.0f;
        clSetKernelArg(s_orbitKernel, 10, sizeof(lineCol), &lineCol);
        clSetKernelArg(s_orbitKernel, 11, sizeof(float), &lineWidth);

        err = clEnqueueNDRangeKernel(OpenCLContext::get().getQueue(), s_orbitKernel, 2,
                                      nullptr, global, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            PLOGE << "orbital orbit lines kernel dispatch failed: " << err;
        }
    }

    // Dispatch asteroid belt orbits as wider, semi-transparent bands
    if (!beltPoints.empty()) {
        int beltCount = (int)beltPoints.size();

        cl_mem beltBuf = OpenCLContext::get().createBuffer(CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            beltPoints.size() * sizeof(cl_float4), beltPoints.data(), &err, "orbital belt points");

        clSetKernelArg(s_orbitKernel, 0, sizeof(cl_mem), &beltBuf);
        clSetKernelArg(s_orbitKernel, 1, sizeof(int), &beltCount);
        clSetKernelArg(s_orbitKernel, 2, sizeof(cl_mem), &m_outputBuffer);
        clSetKernelArg(s_orbitKernel, 3, sizeof(int), &width);
        clSetKernelArg(s_orbitKernel, 4, sizeof(int), &height);
        clSetKernelArg(s_orbitKernel, 5, sizeof(camPos), &camPos);
        clSetKernelArg(s_orbitKernel, 6, sizeof(camFwd), &camFwd);
        clSetKernelArg(s_orbitKernel, 7, sizeof(camRight), &camRight);
        clSetKernelArg(s_orbitKernel, 8, sizeof(camUp), &camUp);
        clSetKernelArg(s_orbitKernel, 9, sizeof(float), &m_fovY);

        cl_float4 beltCol = {0.55f, 0.45f, 0.35f, 0.35f}; // brownish semi-transparent
        float beltWidth = 8.0f; // much wider band
        clSetKernelArg(s_orbitKernel, 10, sizeof(beltCol), &beltCol);
        clSetKernelArg(s_orbitKernel, 11, sizeof(float), &beltWidth);

        err = clEnqueueNDRangeKernel(OpenCLContext::get().getQueue(), s_orbitKernel, 2,
                                      nullptr, global, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            PLOGE << "orbital belt lines kernel dispatch failed: " << err;
        }

        OpenCLContext::get().releaseMem(beltBuf);
    }
}

} // namespace Orbital
