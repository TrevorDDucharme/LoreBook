#include <Editors/Markdown/MarkdownPreview.hpp>
#include <Editors/Markdown/Effect.hpp>
#include <Editors/Markdown/Effects/BloodEffect.hpp>
#include <OpenCLContext.hpp>
#include <LoreBook_Resources/LoreBook_ResourcesEmbeddedVFS.hpp>
#include <LuaScriptManager.hpp>
#include <LuaEngine.hpp>
#include <stringUtils.hpp>
#include <Icons.hpp>
#include <Vault.hpp>
#include <plog/Log.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <sstream>

namespace Markdown {

// Static member definition
std::unordered_map<std::string, MarkdownPreview::CachedWorldState> MarkdownPreview::s_worldCache;
std::unordered_map<std::string, MarkdownPreview::CachedOrbitalState> MarkdownPreview::s_orbitalCache;

// ────────────────────────────────────────────────────────────────────
// Collision shader sources
// ────────────────────────────────────────────────────────────────────

static const char* s_collisionVert = R"(
#version 330 core
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;

uniform mat4 uMVP;

out vec2 v_uv;

void main() {
    gl_Position = uMVP * vec4(in_pos, 1.0);
    v_uv = in_uv;
}
)";

static const char* s_collisionFrag = R"(
#version 330 core
uniform sampler2D uFontAtlas;

in vec2 v_uv;

layout(location = 0) out vec4 fragColor;

void main() {
    float a = texture(uFontAtlas, v_uv).r;
    fragColor = vec4(a, 0.0, 0.0, 1.0);
}
)";

// ────────────────────────────────────────────────────────────────────
// Embedded content (Lua canvas) shader sources
// ────────────────────────────────────────────────────────────────────

static const char* s_embedVert = R"(
#version 330 core
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;

uniform mat4 uMVP;

out vec2 v_uv;

void main() {
    gl_Position = uMVP * vec4(in_pos, 1.0);
    v_uv = in_uv;
}
)";

static const char* s_embedFrag = R"(
#version 330 core
uniform sampler2D uTexture;

in vec2 v_uv;

layout(location = 0) out vec4 fragColor;

void main() {
    fragColor = texture(uTexture, v_uv);
}
)";

// ────────────────────────────────────────────────────────────────────
// Particle shader sources
// ────────────────────────────────────────────────────────────────────

static const char* s_particleVert = R"(
#version 330 core
layout(location = 0) in vec2 in_pos;
layout(location = 1) in float in_z;
layout(location = 2) in vec4 in_color;
layout(location = 3) in float in_size;
layout(location = 4) in float in_life;

out float v_z;
out vec4 v_color;
out float v_size;
out float v_life;

void main() {
    gl_Position = vec4(in_pos, 0.0, 1.0);
    v_z = in_z;
    v_color = in_color;
    v_size = in_size;
    v_life = in_life;
}
)";

static const char* s_particleGeom = R"(
#version 330 core
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

uniform mat4 uMVP;

in float v_z[];
in vec4 v_color[];
in float v_size[];
in float v_life[];

out vec2 g_uv;
out vec4 g_color;

void main() {
    if (v_life[0] <= 0) return;
    
    vec3 pos = vec3(gl_in[0].gl_Position.xy, v_z[0]);
    float size = v_size[0];
    vec4 color = v_color[0];
    color.a *= clamp(v_life[0] * 2.0, 0.0, 1.0);  // Fade out
    
    vec3 right = vec3(1, 0, 0) * size;
    vec3 up = vec3(0, 1, 0) * size;
    
    g_color = color;
    
    g_uv = vec2(0, 0);
    gl_Position = uMVP * vec4(pos - right - up, 1.0);
    EmitVertex();
    
    g_uv = vec2(1, 0);
    gl_Position = uMVP * vec4(pos + right - up, 1.0);
    EmitVertex();
    
    g_uv = vec2(0, 1);
    gl_Position = uMVP * vec4(pos - right + up, 1.0);
    EmitVertex();
    
    g_uv = vec2(1, 1);
    gl_Position = uMVP * vec4(pos + right + up, 1.0);
    EmitVertex();
    
    EndPrimitive();
}
)";

static const char* s_particleFrag = R"(
#version 330 core
in vec2 g_uv;
in vec4 g_color;

out vec4 fragColor;

void main() {
    // Soft circle
    float dist = length(g_uv - vec2(0.5));
    float alpha = 1.0 - smoothstep(0.3, 0.5, dist);
    fragColor = vec4(g_color.rgb, g_color.a * alpha);
}
)";

// ────────────────────────────────────────────────────────────────────
// Bloom / Glow post-processing shaders
// ────────────────────────────────────────────────────────────────────

// Renders glyph as a bright white silhouette tinted with glow color
static const char* s_bloomGlowVert = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

uniform mat4 uMVP;

out vec2 v_uv;
out vec4 v_color;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    v_uv = aUV;
    v_color = aColor;
}
)";

static const char* s_bloomGlowFrag = R"(
#version 330 core
uniform sampler2D uFontAtlas;
uniform vec4 uColor1;
uniform float uIntensity;

in vec2 v_uv;
in vec4 v_color;

out vec4 fragColor;

void main() {
    float alpha = texture(uFontAtlas, v_uv).a;
    // Create a bright, saturated glow source using white + tint
    vec3 brightColor = mix(vec3(1.0), uColor1.rgb, 0.6);
    float glowAlpha = alpha * clamp(uIntensity * 1.5, 0.0, 3.0);
    fragColor = vec4(brightColor, glowAlpha);
}
)";

// Gaussian blur shader (horizontal or vertical pass)
static const char* s_bloomBlurVert = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

out vec2 v_uv;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    v_uv = aUV;
}
)";

static const char* s_bloomBlurFrag = R"(
#version 330 core
uniform sampler2D uTexture;
uniform vec2 uDirection;  // (1/w, 0) for horizontal, (0, 1/h) for vertical
uniform float uRadius;

in vec2 v_uv;
out vec4 fragColor;

void main() {
    // 9-tap Gaussian blur with weights for sigma ~= 4
    float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    
    vec4 result = texture(uTexture, v_uv) * weights[0];
    
    for (int i = 1; i < 5; ++i) {
        vec2 offset = uDirection * float(i) * uRadius;
        result += texture(uTexture, v_uv + offset) * weights[i];
        result += texture(uTexture, v_uv - offset) * weights[i];
    }
    
    fragColor = result;
}
)";

// Additive composite shader (blends bloom texture onto scene)
static const char* s_bloomCompositeVert = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

out vec2 v_uv;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    v_uv = aUV;
}
)";

static const char* s_bloomCompositeFrag = R"(
#version 330 core
uniform sampler2D uBloom;
uniform float uBloomStrength;

in vec2 v_uv;
out vec4 fragColor;

void main() {
    vec4 bloom = texture(uBloom, v_uv);
    // Output only the bloom contribution; additive blending adds it to the scene
    fragColor = vec4(bloom.rgb * uBloomStrength, bloom.a * uBloomStrength);
}
)";

// ────────────────────────────────────────────────────────────────────
// SPH Fluid Simulation kernel (OpenCL) — with spatial hash grid
// ────────────────────────────────────────────────────────────────────

static const char* s_sphKernelSource = R"(
#include "common.cl"

// ═══════════════════════════════════════════════════════════════════
// Spatial Hash Grid — O(N) neighbor lookup
// ═══════════════════════════════════════════════════════════════════
// Grid maps 2D space into cells of size = smoothingRadius.
// Each cell stores a range [start, start+count) into a sorted
// particle entry array.  Building the grid is done on CPU (simple
// for <1000 particles); kernels only read it.

// Grid cell: (start, count) packed as uint2
// gridEntries[]: particle indices sorted by cell

// Hash a 2D cell coordinate to a flat index with wrapping
uint cellHash(int cx, int cy, uint tableSize) {
    // Simple spatial hash (prime mixing)
    uint h = (uint)((cx * 92837111) ^ (cy * 689287499));
    return h % tableSize;
}

// ── SPH kernel functions ──

float poly6_2d(float r2, float h2) {
    if (r2 >= h2) return 0.0f;
    float diff = h2 - r2;
    return diff * diff * diff;
}

float2 spikyGrad_2d(float2 rij, float r, float h) {
    if (r >= h || r < 0.001f) return (float2)(0.0f, 0.0f);
    float diff = h - r;
    return (diff * diff) * (rij / r);
}

float viscLaplacian_2d(float r, float h) {
    if (r >= h) return 0.0f;
    return (h - r);
}

// ── Pass 1: Compute density + pressure using spatial hash ──
__kernel void sphDensityPressure(
    __global Particle* particles,
    __global float* density,
    __global float* pressure,
    __global const uint2* grid,        // [tableSize] cells: (start, count)
    __global const uint* gridEntries,  // sorted particle indices
    const uint count,
    const uint tableSize,
    const float smoothingRadius,
    const float restDensity,
    const float stiffness,
    const float particleMass
) {
    uint i = get_global_id(0);
    if (i >= count) return;

    Particle pi = particles[i];
    if (pi.life <= 0.0f || pi.behaviorID != BEHAVIOR_BLOOD) {
        density[i] = 0.0f;
        pressure[i] = 0.0f;
        return;
    }

    float h = smoothingRadius;
    float h2 = h * h;
    float poly6Norm = 4.0f / (M_PI_F * pown(h, 8));
    float rho = 0.0f;

    // Grid cell of this particle
    int cx = (int)floor(pi.pos.x / h);
    int cy = (int)floor(pi.pos.y / h);

    // Search 3x3 neighborhood
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dy = -1; dy <= 1; ++dy) {
            uint ch = cellHash(cx + dx, cy + dy, tableSize);
            uint2 cell = grid[ch];
            uint start = cell.x;
            uint cellCount = cell.y;
            for (uint k = 0; k < cellCount; ++k) {
                uint j = gridEntries[start + k];
                if (j >= count) continue;
                Particle pj = particles[j];
                if (pj.life <= 0.0f || pj.behaviorID != BEHAVIOR_BLOOD) continue;

                float2 rij = pi.pos - pj.pos;
                float r2 = dot(rij, rij);
                rho += particleMass * poly6Norm * poly6_2d(r2, h2);
            }
        }
    }

    density[i] = rho;
    pressure[i] = max(stiffness * (rho - restDensity), 0.0f);
}

// ── Pass 2: Pressure + viscosity + cohesion forces ──
__kernel void sphForces(
    __global Particle* particles,
    __global const float* density,
    __global const float* pressure,
    __global const uint2* grid,
    __global const uint* gridEntries,
    const float deltaTime,
    const uint count,
    const uint tableSize,
    const float smoothingRadius,
    const float viscosity,
    const float cohesionStrength,
    const float particleMass
) {
    uint i = get_global_id(0);
    if (i >= count) return;

    Particle pi = particles[i];
    if (pi.life <= 0.0f || pi.behaviorID != BEHAVIOR_BLOOD) return;

    float rho_i = density[i];
    if (rho_i < 0.0001f) return;

    float p_i = pressure[i];
    float h = smoothingRadius;
    float h2 = h * h;

    float spikyNorm = -30.0f / (M_PI_F * pown(h, 5));
    float viscNorm = 20.0f / (3.0f * M_PI_F * pown(h, 5));
    float poly6Norm = 4.0f / (M_PI_F * pown(h, 8));

    float2 fPressure = (float2)(0.0f);
    float2 fViscosity = (float2)(0.0f);
    float2 fCohesion = (float2)(0.0f);

    int cx = (int)floor(pi.pos.x / h);
    int cy = (int)floor(pi.pos.y / h);

    for (int ddx = -1; ddx <= 1; ++ddx) {
        for (int ddy = -1; ddy <= 1; ++ddy) {
            uint ch = cellHash(cx + ddx, cy + ddy, tableSize);
            uint2 cell = grid[ch];
            uint start = cell.x;
            uint cellCount = cell.y;
            for (uint k = 0; k < cellCount; ++k) {
                uint j = gridEntries[start + k];
                if (j == i || j >= count) continue;
                Particle pj = particles[j];
                if (pj.life <= 0.0f || pj.behaviorID != BEHAVIOR_BLOOD) continue;

                float rho_j = density[j];
                if (rho_j < 0.0001f) continue;
                float p_j = pressure[j];

                float2 rij = pi.pos - pj.pos;
                float r2 = dot(rij, rij);
                if (r2 >= h2) continue;
                float r = sqrt(r2);

                // Pressure force (symmetric)
                float2 pGrad = spikyNorm * spikyGrad_2d(rij, r, h);
                fPressure -= particleMass * (p_i / (rho_i * rho_i) + p_j / (rho_j * rho_j)) * pGrad;

                // Viscosity force
                float vLap = viscNorm * viscLaplacian_2d(r, h);
                fViscosity += particleMass * (pj.vel - pi.vel) / rho_j * vLap;

                // Cohesion (surface tension)
                if (r > 0.001f) {
                    float w = poly6Norm * poly6_2d(r2, h2);
                    fCohesion -= cohesionStrength * particleMass * (rij / r) * w;
                }
            }
        }
    }

    fViscosity *= viscosity;

    float2 accel = (fPressure + fViscosity + fCohesion) / rho_i;

    // Stability clamp
    float accelLen = length(accel);
    if (accelLen > 800.0f) {
        accel = accel / accelLen * 800.0f;
    }

    pi.vel += accel * deltaTime;
    particles[i] = pi;
}
)";

// ────────────────────────────────────────────────────────────────────
// Blood fluid post-process shader (density field → fluid surface)
// ────────────────────────────────────────────────────────────────────

static const char* s_bloodFluidFrag = R"(
#version 330 core
uniform sampler2D uDensity;
uniform vec2 uTexelSize;

in vec2 v_uv;
out vec4 fragColor;

void main() {
    float d = texture(uDensity, v_uv).r;

    // Isosurface threshold — merges nearby blobs into cohesive fluid
    float threshold = 0.35;
    float surface = smoothstep(threshold - 0.12, threshold + 0.03, d);
    if (surface < 0.01) discard;

    // Gradient-based normals from density field
    float dx = texture(uDensity, v_uv + vec2(uTexelSize.x, 0)).r
             - texture(uDensity, v_uv - vec2(uTexelSize.x, 0)).r;
    float dy = texture(uDensity, v_uv + vec2(0, uTexelSize.y)).r
             - texture(uDensity, v_uv - vec2(0, uTexelSize.y)).r;
    vec3 normal = normalize(vec3(-dx * 4.0, -dy * 4.0, 0.25));

    // Specular highlights (top-left light) — wet/glossy look
    vec3 lightDir = normalize(vec3(-0.3, -0.5, 1.0));
    vec3 viewDir = vec3(0, 0, 1);
    vec3 halfDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfDir), 0.0), 48.0);

    // Fresnel-like rim
    float rim = 1.0 - max(dot(normal, viewDir), 0.0);
    rim = pow(rim, 3.0) * 0.3;

    // Blood coloring — deep where thick, brighter at edges
    float depthFactor = smoothstep(threshold, threshold + 0.6, d);
    vec3 deepBlood  = vec3(0.30, 0.01, 0.01);
    vec3 edgeBlood  = vec3(0.65, 0.04, 0.02);
    vec3 bloodColor = mix(edgeBlood, deepBlood, depthFactor);

    // Subsurface scattering approximation
    float sss = (1.0 - depthFactor) * 0.15;
    bloodColor += vec3(0.8, 0.1, 0.05) * sss;

    // Apply specular + rim
    bloodColor += vec3(1.0, 0.9, 0.85) * spec * 0.6;
    bloodColor += vec3(0.5, 0.05, 0.02) * rim;

    fragColor = vec4(bloodColor, surface * 0.92);
}
)";

// ────────────────────────────────────────────────────────────────────
// MarkdownPreview implementation
// ────────────────────────────────────────────────────────────────────

MarkdownPreview::MarkdownPreview() = default;

MarkdownPreview::~MarkdownPreview() {
    cleanup();
}

bool MarkdownPreview::init() {
    if (m_initialized) return true;
    
    // Initialize OpenCL with GL interop
    if (!OpenCLContext::get().isReady()) {
        if (!OpenCLContext::get().init()) {
            PLOG_ERROR << "Failed to initialize OpenCL";
            return false;
        }
    }
    
    if (!OpenCLContext::get().hasGLInterop()) {
        OpenCLContext::get().initGLInterop();
    }
    
    // Initialize effect system
    if (!m_effectSystem.init(OpenCLContext::get().getContext(),
                              OpenCLContext::get().getDevice())) {
        PLOG_ERROR << "Failed to initialize effect system";
        return false;
    }
    
    // Initialize collision mask
    if (!m_collisionMask.init(OpenCLContext::get().getContext())) {
        PLOG_ERROR << "Failed to initialize collision mask";
        return false;
    }
    
    // Set up layout engine
    m_layoutEngine.setEffectSystem(&m_effectSystem);
    
    // Initialize shaders and VAOs
    initShaders();
    initVAOs();
    initOpenCL();
    initParticleSystem();
    
    // Get font atlas texture from ImGui
    ImGuiIO& io = ImGui::GetIO();
    m_fontAtlasTexture = (GLuint)(intptr_t)io.Fonts->TexID;
    
    m_initialized = true;
    PLOG_INFO << "MarkdownPreview initialized";
    return true;
}

void MarkdownPreview::cleanup() {
    if (m_fbo) {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    if (m_colorTex) {
        glDeleteTextures(1, &m_colorTex);
        m_colorTex = 0;
    }
    if (m_depthTex) {
        glDeleteTextures(1, &m_depthTex);
        m_depthTex = 0;
    }
    if (m_collisionShader) {
        glDeleteProgram(m_collisionShader);
        m_collisionShader = 0;
    }
    if (m_embedShader) {
        glDeleteProgram(m_embedShader);
        m_embedShader = 0;
    }
    if (m_particleShader) {
        glDeleteProgram(m_particleShader);
        m_particleShader = 0;
    }
    if (m_glyphVAO) {
        glDeleteVertexArrays(1, &m_glyphVAO);
        m_glyphVAO = 0;
    }
    if (m_glyphVBO) {
        glDeleteBuffers(1, &m_glyphVBO);
        m_glyphVBO = 0;
    }
    if (m_particleVAO) {
        glDeleteVertexArrays(1, &m_particleVAO);
        m_particleVAO = 0;
    }
    if (m_particleVBO) {
        glDeleteBuffers(1, &m_particleVBO);
        m_particleVBO = 0;
    }
    if (m_particleEBO) {
        glDeleteBuffers(1, &m_particleEBO);
        m_particleEBO = 0;
    }
    if (m_clParticleBuffer) {
        OpenCLContext::get().releaseMem(m_clParticleBuffer);
        m_clParticleBuffer = nullptr;
    }
    if (m_clCollisionImage) {
        clReleaseMemObject(m_clCollisionImage);
        m_clCollisionImage = nullptr;
        m_clCollisionWidth = 0;
        m_clCollisionHeight = 0;
    }
    
    // Cleanup bloom resources
    for (int i = 0; i < 2; ++i) {
        if (m_bloomFBO[i]) { glDeleteFramebuffers(1, &m_bloomFBO[i]); m_bloomFBO[i] = 0; }
        if (m_bloomTex[i]) { glDeleteTextures(1, &m_bloomTex[i]); m_bloomTex[i] = 0; }
    }
    if (m_bloomSrcFBO) { glDeleteFramebuffers(1, &m_bloomSrcFBO); m_bloomSrcFBO = 0; }
    if (m_bloomSrcTex) { glDeleteTextures(1, &m_bloomSrcTex); m_bloomSrcTex = 0; }
    if (m_bloomBlurShader) { glDeleteProgram(m_bloomBlurShader); m_bloomBlurShader = 0; }
    if (m_bloomCompositeShader) { glDeleteProgram(m_bloomCompositeShader); m_bloomCompositeShader = 0; }
    if (m_bloomGlowShader) { glDeleteProgram(m_bloomGlowShader); m_bloomGlowShader = 0; }
    if (m_quadVAO) { glDeleteVertexArrays(1, &m_quadVAO); m_quadVAO = 0; }
    if (m_quadVBO) { glDeleteBuffers(1, &m_quadVBO); m_quadVBO = 0; }
    
    // Cleanup SPH fluid resources
    if (m_sphDensityKernel) { clReleaseKernel(m_sphDensityKernel); m_sphDensityKernel = nullptr; }
    if (m_sphForcesKernel) { clReleaseKernel(m_sphForcesKernel); m_sphForcesKernel = nullptr; }
    if (m_sphProgram) { clReleaseProgram(m_sphProgram); m_sphProgram = nullptr; }
    if (m_clSPHDensity) { OpenCLContext::get().releaseMem(m_clSPHDensity); m_clSPHDensity = nullptr; }
    if (m_clSPHPressure) { OpenCLContext::get().releaseMem(m_clSPHPressure); m_clSPHPressure = nullptr; }
    if (m_clSPHGrid) { OpenCLContext::get().releaseMem(m_clSPHGrid); m_clSPHGrid = nullptr; }
    if (m_clSPHGridEntries) { OpenCLContext::get().releaseMem(m_clSPHGridEntries); m_clSPHGridEntries = nullptr; }
    if (m_bloodDensityFBO) { glDeleteFramebuffers(1, &m_bloodDensityFBO); m_bloodDensityFBO = 0; }
    if (m_bloodDensityTex) { glDeleteTextures(1, &m_bloodDensityTex); m_bloodDensityTex = 0; }
    if (m_bloodFluidShader) { glDeleteProgram(m_bloodFluidShader); m_bloodFluidShader = 0; }
    
    // Clear CPU particle buffers
    m_cpuParticles.clear();
    m_cpuDeadIndices.clear();
    m_particleCount = 0;
    m_deadCount = 0;
    
    m_collisionMask.cleanup();
    m_effectSystem.cleanup();
    
    m_initialized = false;
}

// Parse a hex color string (#RGB, #RRGGBB, or #RRGGBBAA) into a glm::vec4.
// Returns false if the format is invalid.
static bool parseHexColor(const std::string& hex, glm::vec4& out) {
    if (hex.empty() || hex[0] != '#') return false;
    std::string h = hex.substr(1);
    uint32_t val = 0;
    for (char c : h) {
        val <<= 4;
        if (c >= '0' && c <= '9') val |= (c - '0');
        else if (c >= 'a' && c <= 'f') val |= (c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') val |= (c - 'A' + 10);
        else return false;
    }
    if (h.size() == 3) {
        out = {((val >> 8) & 0xF) / 15.0f, ((val >> 4) & 0xF) / 15.0f, (val & 0xF) / 15.0f, 1.0f};
    } else if (h.size() == 6) {
        out = {((val >> 16) & 0xFF) / 255.0f, ((val >> 8) & 0xFF) / 255.0f, (val & 0xFF) / 255.0f, 1.0f};
    } else if (h.size() == 8) {
        out = {((val >> 24) & 0xFF) / 255.0f, ((val >> 16) & 0xFF) / 255.0f, ((val >> 8) & 0xFF) / 255.0f, (val & 0xFF) / 255.0f};
    } else {
        return false;
    }
    return true;
}

void MarkdownPreview::setSource(const std::string& markdown) {
    static std::string s_lastRawSource;
    if (markdown == s_lastRawSource) return;
    s_lastRawSource = markdown;
    
    m_document.markDirty();
    
    // Parse YAML-like frontmatter for preview metadata
    // Format: lines between opening "---" and closing "---"
    m_clearColor = {0.1f, 0.1f, 0.12f, 1.0f};  // default
    std::string body = markdown;
    if (markdown.size() >= 3 && markdown.compare(0, 3, "---") == 0) {
        size_t end = markdown.find("\n---", 3);
        if (end != std::string::npos) {
            std::string front = markdown.substr(3, end - 3);
            // Simple key: value parsing (one per line)
            std::istringstream ss(front);
            std::string line;
            while (std::getline(ss, line)) {
                // Trim leading whitespace
                size_t ks = line.find_first_not_of(" \t");
                if (ks == std::string::npos) continue;
                size_t colon = line.find(':', ks);
                if (colon == std::string::npos) continue;
                std::string key = line.substr(ks, colon - ks);
                size_t vs = line.find_first_not_of(" \t", colon + 1);
                if (vs == std::string::npos) continue;
                std::string value = line.substr(vs);
                // Trim trailing whitespace
                size_t ve = value.find_last_not_of(" \t\r\n");
                if (ve != std::string::npos) value = value.substr(0, ve + 1);
                
                if (key == "background") {
                    glm::vec4 c;
                    if (parseHexColor(value, c)) {
                        m_clearColor = c;
                    }
                }
            }
            // Strip frontmatter from rendered content
            size_t bodyStart = end + 4; // skip "\n---"
            if (bodyStart < markdown.size() && markdown[bodyStart] == '\n') ++bodyStart;
            body = markdown.substr(bodyStart);
        }
    }
    m_sourceText = body;
}

void MarkdownPreview::initShaders() {
    // Compile collision shader
    {
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &s_collisionVert, nullptr);
        glCompileShader(vs);
        
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &s_collisionFrag, nullptr);
        glCompileShader(fs);
        
        m_collisionShader = glCreateProgram();
        glAttachShader(m_collisionShader, vs);
        glAttachShader(m_collisionShader, fs);
        glLinkProgram(m_collisionShader);
        
        glDeleteShader(vs);
        glDeleteShader(fs);
    }
    
    // Compile embed shader (textured quad for Lua canvases)
    {
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &s_embedVert, nullptr);
        glCompileShader(vs);
        
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &s_embedFrag, nullptr);
        glCompileShader(fs);
        
        m_embedShader = glCreateProgram();
        glAttachShader(m_embedShader, vs);
        glAttachShader(m_embedShader, fs);
        glLinkProgram(m_embedShader);
        
        glDeleteShader(vs);
        glDeleteShader(fs);
    }
    
    // Compile particle shader
    {
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &s_particleVert, nullptr);
        glCompileShader(vs);
        
        GLuint gs = glCreateShader(GL_GEOMETRY_SHADER);
        glShaderSource(gs, 1, &s_particleGeom, nullptr);
        glCompileShader(gs);
        
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &s_particleFrag, nullptr);
        glCompileShader(fs);
        
        m_particleShader = glCreateProgram();
        glAttachShader(m_particleShader, vs);
        glAttachShader(m_particleShader, gs);
        glAttachShader(m_particleShader, fs);
        glLinkProgram(m_particleShader);
        
        glDeleteShader(vs);
        glDeleteShader(gs);
        glDeleteShader(fs);
    }
    
    // Compile bloom glow shader (renders glyph silhouettes for bloom source)
    {
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &s_bloomGlowVert, nullptr);
        glCompileShader(vs);
        
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &s_bloomGlowFrag, nullptr);
        glCompileShader(fs);
        
        m_bloomGlowShader = glCreateProgram();
        glAttachShader(m_bloomGlowShader, vs);
        glAttachShader(m_bloomGlowShader, fs);
        glLinkProgram(m_bloomGlowShader);
        
        glDeleteShader(vs);
        glDeleteShader(fs);
    }
    
    // Compile bloom blur shader
    {
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &s_bloomBlurVert, nullptr);
        glCompileShader(vs);
        
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &s_bloomBlurFrag, nullptr);
        glCompileShader(fs);
        
        m_bloomBlurShader = glCreateProgram();
        glAttachShader(m_bloomBlurShader, vs);
        glAttachShader(m_bloomBlurShader, fs);
        glLinkProgram(m_bloomBlurShader);
        
        glDeleteShader(vs);
        glDeleteShader(fs);
    }
    
    // Compile bloom composite shader
    {
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &s_bloomCompositeVert, nullptr);
        glCompileShader(vs);
        
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &s_bloomCompositeFrag, nullptr);
        glCompileShader(fs);
        
        m_bloomCompositeShader = glCreateProgram();
        glAttachShader(m_bloomCompositeShader, vs);
        glAttachShader(m_bloomCompositeShader, fs);
        glLinkProgram(m_bloomCompositeShader);
        
        glDeleteShader(vs);
        glDeleteShader(fs);
    }
    
    // Full-screen quad for bloom blur/composite
    {
        float quadVerts[] = {
            // pos       uv
            -1, -1,    0, 0,
             1, -1,    1, 0,
             1,  1,    1, 1,
            -1, -1,    0, 0,
             1,  1,    1, 1,
            -1,  1,    0, 1,
        };
        glGenVertexArrays(1, &m_quadVAO);
        glGenBuffers(1, &m_quadVBO);
        glBindVertexArray(m_quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
    }
    
    // Compile blood fluid post-process shader
    {
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &s_bloomCompositeVert, nullptr);  // Reuse full-screen quad vert
        glCompileShader(vs);
        
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &s_bloodFluidFrag, nullptr);
        glCompileShader(fs);
        
        m_bloodFluidShader = glCreateProgram();
        glAttachShader(m_bloodFluidShader, vs);
        glAttachShader(m_bloodFluidShader, fs);
        glLinkProgram(m_bloodFluidShader);
        
        GLint linked;
        glGetProgramiv(m_bloodFluidShader, GL_LINK_STATUS, &linked);
        if (!linked) {
            char log[1024];
            glGetProgramInfoLog(m_bloodFluidShader, sizeof(log), nullptr, log);
            PLOG_ERROR << "Blood fluid shader link failed: " << log;
        }
        
        glDeleteShader(vs);
        glDeleteShader(fs);
    }
}

void MarkdownPreview::initVAOs() {
    // Glyph VAO
    glGenVertexArrays(1, &m_glyphVAO);
    glGenBuffers(1, &m_glyphVBO);
    
    glBindVertexArray(m_glyphVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_glyphVBO);
    
    // GlyphVertex layout: pos(3), uv(2), color(4), effectID(1), pad(3)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(GlyphVertex),
                          (void*)offsetof(GlyphVertex, pos));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(GlyphVertex),
                          (void*)offsetof(GlyphVertex, uv));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(GlyphVertex),
                          (void*)offsetof(GlyphVertex, color));
    glEnableVertexAttribArray(3);
    glVertexAttribIPointer(3, 1, GL_UNSIGNED_INT, sizeof(GlyphVertex),
                           (void*)offsetof(GlyphVertex, effectID));
    
    glBindVertexArray(0);
    
    // Particle VAO
    glGenVertexArrays(1, &m_particleVAO);
    glGenBuffers(1, &m_particleVBO);
    glGenBuffers(1, &m_particleEBO);
    
    glBindVertexArray(m_particleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_particleVBO);
    
    // Particle layout for rendering
    glEnableVertexAttribArray(0);  // pos.xy
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Particle),
                          (void*)offsetof(Particle, pos));
    glEnableVertexAttribArray(1);  // z
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Particle),
                          (void*)offsetof(Particle, z));
    glEnableVertexAttribArray(2);  // color
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Particle),
                          (void*)offsetof(Particle, color));
    glEnableVertexAttribArray(3);  // size
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Particle),
                          (void*)offsetof(Particle, size));
    glEnableVertexAttribArray(4);  // life
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Particle),
                          (void*)offsetof(Particle, life));
    
    glBindVertexArray(0);
}

void MarkdownPreview::initOpenCL() {
    // OpenCL is already initialized via OpenCLContext singleton
    // Particle kernels are now compiled by the Effect system during init
}

void MarkdownPreview::updateCollisionCLImage() {
    if (!OpenCLContext::get().isReady()) return;
    if (!m_collisionMask.hasCPUData() || m_collisionMask.getWidth() <= 0) return;
    
    int w = m_collisionMask.getWidth();
    int h = m_collisionMask.getHeight();
    
    // Recreate if size changed
    if (m_clCollisionImage && (m_clCollisionWidth != w || m_clCollisionHeight != h)) {
        clReleaseMemObject(m_clCollisionImage);
        m_clCollisionImage = nullptr;
    }
    
    cl_image_format fmt;
    fmt.image_channel_order = CL_R;
    fmt.image_channel_data_type = CL_UNORM_INT8;
    
    cl_command_queue q = OpenCLContext::get().getQueue();
    const uint8_t* data = m_collisionMask.getCPUData();
    
    if (!m_clCollisionImage) {
        // Create new CL image from CPU data
        cl_image_desc desc = {};
        desc.image_type = CL_MEM_OBJECT_IMAGE2D;
        desc.image_width = static_cast<size_t>(w);
        desc.image_height = static_cast<size_t>(h);
        desc.image_row_pitch = static_cast<size_t>(w);  // R8: 1 byte per pixel
        
        cl_int err;
        m_clCollisionImage = clCreateImage(
            OpenCLContext::get().getContext(),
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            &fmt, &desc,
            const_cast<uint8_t*>(data),
            &err
        );
        
        if (err != CL_SUCCESS || !m_clCollisionImage) {
            PLOG_WARNING << "Failed to create CL collision image: " << err;
            m_clCollisionImage = nullptr;
            return;
        }
        
        m_clCollisionWidth = w;
        m_clCollisionHeight = h;
    } else {
        // Update existing image with new CPU data
        size_t origin[3] = {0, 0, 0};
        size_t region[3] = {static_cast<size_t>(w), static_cast<size_t>(h), 1};
        cl_int writeErr = clEnqueueWriteImage(q, m_clCollisionImage, CL_FALSE,
                            origin, region, static_cast<size_t>(w), 0,
                            data, 0, nullptr, nullptr);
        if (writeErr != CL_SUCCESS) {
            PLOG_WARNING << "Failed to update CL collision image: " << writeErr;
        }
    }
}

void MarkdownPreview::initParticleSystem() {
    // Initialize CPU buffers regardless of OpenCL availability
    m_cpuParticles.resize(MAX_PARTICLES);
    m_cpuDeadIndices.resize(MAX_PARTICLES);
    
    // Initially, all particles are dead
    for (size_t i = 0; i < MAX_PARTICLES; ++i) {
        m_cpuParticles[i].life = 0.0f;
        m_cpuParticles[i].maxLife = -1.0f;  // sentinel: already in dead list
        m_cpuDeadIndices[i] = static_cast<uint32_t>(i);
    }
    m_deadCount = static_cast<uint32_t>(MAX_PARTICLES);
    m_particleCount = 0;
    
    // OpenCL buffer is optional
    if (!OpenCLContext::get().isReady()) return;
    
    cl_int err;
    m_clParticleBuffer = OpenCLContext::get().createBuffer(
        CL_MEM_READ_WRITE,
        MAX_PARTICLES * sizeof(Particle),
        nullptr, &err, "MarkdownPreview particles"
    );
    
    if (err != CL_SUCCESS) {
        PLOG_ERROR << "Failed to create particle buffer: " << err;
        m_clParticleBuffer = nullptr;
    }
    
    initSPH();
}

void MarkdownPreview::initSPH() {
    auto& cl = OpenCLContext::get();
    if (!cl.isReady()) return;
    
    // Load common.cl for #include replacement
    std::string commonCL = loadLoreBook_ResourcesEmbeddedFileAsString("Kernels/particles/common.cl");
    
    // Replace #include with actual common.cl content
    std::string fullSource = s_sphKernelSource;
    size_t includePos = fullSource.find("#include \"common.cl\"");
    if (includePos != std::string::npos) {
        fullSource.replace(includePos, 20, commonCL);
    }
    
    const char* src = fullSource.c_str();
    size_t len = fullSource.size();
    cl_int err;
    
    m_sphProgram = clCreateProgramWithSource(cl.getContext(), 1, &src, &len, &err);
    if (err != CL_SUCCESS) {
        PLOG_ERROR << "SPH: clCreateProgramWithSource failed: " << err;
        return;
    }
    
    cl_device_id dev = cl.getDevice();
    err = clBuildProgram(m_sphProgram, 1, &dev, "-cl-fast-relaxed-math", nullptr, nullptr);
    if (err != CL_SUCCESS) {
        size_t logSize;
        clGetProgramBuildInfo(m_sphProgram, dev, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
        std::string log(logSize, '\0');
        clGetProgramBuildInfo(m_sphProgram, dev, CL_PROGRAM_BUILD_LOG, logSize, &log[0], nullptr);
        PLOG_ERROR << "SPH kernel build failed: " << log;
        clReleaseProgram(m_sphProgram);
        m_sphProgram = nullptr;
        return;
    }
    
    m_sphDensityKernel = clCreateKernel(m_sphProgram, "sphDensityPressure", &err);
    if (err != CL_SUCCESS) {
        PLOG_ERROR << "SPH density kernel failed: " << err;
        m_sphDensityKernel = nullptr;
    }
    
    m_sphForcesKernel = clCreateKernel(m_sphProgram, "sphForces", &err);
    if (err != CL_SUCCESS) {
        PLOG_ERROR << "SPH forces kernel failed: " << err;
        m_sphForcesKernel = nullptr;
    }
    
    // Allocate density/pressure buffers
    m_clSPHDensity = cl.createBuffer(CL_MEM_READ_WRITE, MAX_PARTICLES * sizeof(float),
                                      nullptr, &err, "SPH density");
    m_clSPHPressure = cl.createBuffer(CL_MEM_READ_WRITE, MAX_PARTICLES * sizeof(float),
                                       nullptr, &err, "SPH pressure");
    
    // Grid buffers — table size is a prime that accommodates sparse spatial cells
    constexpr uint32_t SPH_GRID_TABLE_SIZE = 4099;
    m_clSPHGrid = cl.createBuffer(CL_MEM_READ_WRITE,
                                   SPH_GRID_TABLE_SIZE * sizeof(cl_uint2),
                                   nullptr, &err, "SPH grid");
    m_clSPHGridEntries = cl.createBuffer(CL_MEM_READ_WRITE,
                                          MAX_PARTICLES * sizeof(cl_uint),
                                          nullptr, &err, "SPH grid entries");
    
    PLOG_INFO << "SPH fluid simulation initialized (grid table size: " << SPH_GRID_TABLE_SIZE << ")";
}

void MarkdownPreview::ensureFBO(int width, int height) {
    if (width == m_fboWidth && height == m_fboHeight && m_fbo != 0) {
        return;
    }
    
    // Add hysteresis to avoid rapid reallocations during resize
    // Only resize if the change is significant (>10% or FBO doesn't exist)
    bool needResize = (m_fbo == 0);
    if (!needResize && m_fboWidth > 0 && m_fboHeight > 0) {
        float widthRatio = static_cast<float>(width) / m_fboWidth;
        float heightRatio = static_cast<float>(height) / m_fboHeight;
        // Resize if grew beyond current size or shrank below 70%
        needResize = (width > m_fboWidth || height > m_fboHeight ||
                      widthRatio < 0.7f || heightRatio < 0.7f);
    } else {
        needResize = true;
    }
    
    if (!needResize) {
        return;  // Use existing FBO, it's big enough
    }
    
    // Round up to next multiple of 64 for better alignment
    int allocWidth = ((width + 63) / 64) * 64;
    int allocHeight = ((height + 63) / 64) * 64;
    
    // Ensure GL operations complete before cleanup
    glFinish();
    
    // Cleanup old FBO
    if (m_fbo) {
        glDeleteFramebuffers(1, &m_fbo);
        m_fbo = 0;
    }
    if (m_colorTex) {
        glDeleteTextures(1, &m_colorTex);
        m_colorTex = 0;
    }
    if (m_depthTex) {
        glDeleteTextures(1, &m_depthTex);
        m_depthTex = 0;
    }
    
    m_fboWidth = allocWidth;
    m_fboHeight = allocHeight;
    
    if (allocWidth <= 0 || allocHeight <= 0) return;
    
    // Create color texture
    glGenTextures(1, &m_colorTex);
    glBindTexture(GL_TEXTURE_2D, m_colorTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, allocWidth, allocHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    
    // Create depth texture
    glGenTextures(1, &m_depthTex);
    glBindTexture(GL_TEXTURE_2D, m_depthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH24_STENCIL8, allocWidth, allocHeight, 0,
                 GL_DEPTH_STENCIL, GL_UNSIGNED_INT_24_8, nullptr);
    
    // Create FBO
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTex, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_depthTex, 0);
    
    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        PLOG_ERROR << "FBO incomplete: " << status;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Bloom FBOs at half resolution for performance
    int bloomW = allocWidth / 2;
    int bloomH = allocHeight / 2;
    
    // Cleanup old bloom resources
    for (int i = 0; i < 2; ++i) {
        if (m_bloomFBO[i]) { glDeleteFramebuffers(1, &m_bloomFBO[i]); m_bloomFBO[i] = 0; }
        if (m_bloomTex[i]) { glDeleteTextures(1, &m_bloomTex[i]); m_bloomTex[i] = 0; }
    }
    if (m_bloomSrcFBO) { glDeleteFramebuffers(1, &m_bloomSrcFBO); m_bloomSrcFBO = 0; }
    if (m_bloomSrcTex) { glDeleteTextures(1, &m_bloomSrcTex); m_bloomSrcTex = 0; }
    
    // Create bloom source FBO (full res — glow silhouettes rendered here)
    glGenTextures(1, &m_bloomSrcTex);
    glBindTexture(GL_TEXTURE_2D, m_bloomSrcTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, allocWidth, allocHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glGenFramebuffers(1, &m_bloomSrcFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_bloomSrcFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bloomSrcTex, 0);
    // Share depth buffer from main FBO for correct depth testing
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_depthTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Create ping-pong FBOs for blur (half res)
    for (int i = 0; i < 2; ++i) {
        glGenTextures(1, &m_bloomTex[i]);
        glBindTexture(GL_TEXTURE_2D, m_bloomTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, bloomW, bloomH, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        glGenFramebuffers(1, &m_bloomFBO[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFBO[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bloomTex[i], 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    
    // Resize collision mask at 2x resolution for finer glyph boundary detail
    m_collisionMask.resize(
        static_cast<int>(allocWidth * COLLISION_SCALE),
        static_cast<int>(allocHeight * COLLISION_SCALE));
    
    // ── Blood fluid density FBO (full res, R16F for density accumulation) ──
    if (m_bloodDensityFBO) { glDeleteFramebuffers(1, &m_bloodDensityFBO); m_bloodDensityFBO = 0; }
    if (m_bloodDensityTex) { glDeleteTextures(1, &m_bloodDensityTex); m_bloodDensityTex = 0; }
    
    glGenTextures(1, &m_bloodDensityTex);
    glBindTexture(GL_TEXTURE_2D, m_bloodDensityTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, allocWidth, allocHeight, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glGenFramebuffers(1, &m_bloodDensityFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_bloodDensityFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bloodDensityTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

ImVec2 MarkdownPreview::render() {
    if (!m_initialized) {
        // Save GL state before init (which creates shaders, VAOs, textures)
        GLint preInitProgram, preInitVAO, preInitVBO, preInitTex, preInitActiveTex;
        GLint preInitFBO;
        glGetIntegerv(GL_CURRENT_PROGRAM, &preInitProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &preInitVAO);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &preInitVBO);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &preInitTex);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &preInitActiveTex);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &preInitFBO);
        
        init();
        
        // Restore GL state after init
        glUseProgram(preInitProgram);
        glBindVertexArray(preInitVAO);
        glBindBuffer(GL_ARRAY_BUFFER, preInitVBO);
        glActiveTexture(preInitActiveTex);
        glBindTexture(GL_TEXTURE_2D, preInitTex);
        glBindFramebuffer(GL_FRAMEBUFFER, preInitFBO);
    }
    
    float dt = ImGui::GetIO().DeltaTime;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    
    if (avail.x <= 0 || avail.y <= 0) {
        return avail;
    }
    
    // 1. Parse if dirty
    if (m_document.isDirty()) {
        m_document.parseString(m_sourceText);
    }
    
    // 2. Layout
    m_layoutGlyphs.clear();
    m_overlayWidgets.clear();
    m_layoutEngine.layout(m_document, avail.x, m_layoutGlyphs, m_overlayWidgets);
    
    // Clamp scroll to content bounds
    float contentHeight = m_layoutEngine.getContentHeight();
    float maxScroll = std::max(0.0f, contentHeight - avail.y);
    m_scrollY = std::clamp(m_scrollY, 0.0f, maxScroll);
    
    // 3. Batch glyphs by effect
    std::vector<EffectBatch> batches;
    m_effectSystem.buildBatches(m_layoutGlyphs, batches);
    
    // 4. Resize FBOs if needed
    ensureFBO(static_cast<int>(avail.x), static_cast<int>(avail.y));
    
    // 5. Save ALL relevant GL state to prevent corruption of other renderers
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevProgram;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    GLint prevVAO;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
    GLint prevVBO;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevVBO);
    GLint prevTex;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    GLint prevActiveTex;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
    GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLint prevBlendSrc, prevBlendDst;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDst);
    GLint prevBlendSrcRGB, prevBlendDstRGB;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRGB);
    
    // 6. Render collision mask
    renderCollisionMask(batches);
    m_collisionMask.readback();
    
    // 7. Setup 2.5D camera – compute Z so viewport dimensions match layout at Z=0
    float aspect = avail.x / avail.y;
    float halfFovRad = glm::radians(m_fovY) * 0.5f;
    float viewCameraZ = (avail.y * 0.5f) / std::tan(halfFovRad);
    m_projection = glm::perspective(glm::radians(m_fovY), aspect, 0.1f, viewCameraZ * 4.0f);
    m_view = glm::lookAt(
        glm::vec3(avail.x / 2, avail.y / 2 + m_scrollY, viewCameraZ),
        glm::vec3(avail.x / 2, avail.y / 2 + m_scrollY, 0),
        glm::vec3(0, -1, 0)
    );
    glm::mat4 mvp = m_projection * m_view;
    
    // 8. Render scene to FBO
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_fboWidth, m_fboHeight);
    glClearColor(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // 9. Render batched glyphs
    renderGlyphBatches(batches, mvp);
    
    // 10. Render embedded content
    renderEmbeddedContent(mvp);
    
    // 11. Emit new particles
    emitParticles(dt, batches);
    
    // 12. Update particles on GPU
    updateParticlesGPU(dt);
    
    // 13. Render particles
    renderParticlesFromGPU(mvp);
    
    // 13.5. Blood fluid post-process (density → isosurface composite)
    renderBloodFluid();
    
    // 14. Glow bloom post-process
    renderGlowBloom(batches, mvp);
    
    // 15. Restore ALL GL state
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glUseProgram(prevProgram);
    glBindVertexArray(prevVAO);
    glBindBuffer(GL_ARRAY_BUFFER, prevVBO);
    glActiveTexture(prevActiveTex);
    glBindTexture(GL_TEXTURE_2D, prevTex);
    if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFuncSeparate(prevBlendSrcRGB, prevBlendDstRGB, prevBlendSrc, prevBlendDst);
    
    // 15. Display via ImGui
    ImGui::Image((ImTextureID)(intptr_t)m_colorTex, avail, ImVec2(1, 1), ImVec2(0, 0));
    
    // Handle mouse wheel: Ctrl+Scroll = zoom, Scroll = scroll
    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            if (ImGui::GetIO().KeyCtrl) {
                // Zoom
                m_zoomLevel *= (wheel > 0) ? 1.1f : (1.0f / 1.1f);
                m_zoomLevel = std::clamp(m_zoomLevel, 0.25f, 4.0f);
                m_layoutEngine.setBaseScale(m_zoomLevel);
            } else {
                m_scrollY -= wheel * 40.0f;
                m_scrollY = std::clamp(m_scrollY, 0.0f, maxScroll);
            }
        }
    }
    
    // Draw scrollbar indicator when content exceeds viewport
    if (contentHeight > avail.y && contentHeight > 0.0f) {
        ImVec2 imgMin = ImGui::GetItemRectMin();
        ImVec2 imgMax = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float barW = 6.0f;
        float trackH = imgMax.y - imgMin.y;
        float thumbH = std::max(20.0f, trackH * (avail.y / contentHeight));
        float thumbY = imgMin.y + (m_scrollY / contentHeight) * trackH;
        dl->AddRectFilled(ImVec2(imgMax.x - barW, imgMin.y), imgMax,
                          IM_COL32(40, 40, 40, 100), barW * 0.5f);
        dl->AddRectFilled(ImVec2(imgMax.x - barW, thumbY),
                          ImVec2(imgMax.x, thumbY + thumbH),
                          IM_COL32(180, 180, 180, 150), barW * 0.5f);
    }
    
    // 16. Overlay ImGui widgets
    ImVec2 origin = ImGui::GetItemRectMin();
    renderOverlayWidgets(origin, m_scrollY);
    
    return avail;
}

void MarkdownPreview::renderCollisionMask(const std::vector<EffectBatch>& batches) {
    if (!m_collisionShader || !m_glyphVAO || !m_glyphVBO) return;
    
    // Refresh font atlas texture ID from ImGui (it may have been rebuilt)
    m_fontAtlasTexture = (GLuint)(intptr_t)ImGui::GetIO().Fonts->TexID;
    
    m_collisionMask.bindForRendering();
    
    // Reset ALL relevant GL state — inherited state from ImGui or other renderers
    // can silently discard fragments (e.g. GL_SAMPLE_ALPHA_TO_COVERAGE with alpha=0)
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);
    glDisable(GL_SAMPLE_COVERAGE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    
    m_collisionMask.clear();
    
    // Additive blending so overlapping glyphs accumulate
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);
    
    glUseProgram(m_collisionShader);
    
    // Orthographic projection offset by scroll position
    glm::mat4 ortho = glm::ortho(0.0f, (float)m_fboWidth,
                                 m_scrollY + (float)m_fboHeight, m_scrollY,
                                 -1.0f, 1.0f);
    glUniformMatrix4fv(glGetUniformLocation(m_collisionShader, "uMVP"), 1, GL_FALSE, &ortho[0][0]);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fontAtlasTexture);
    glUniform1i(glGetUniformLocation(m_collisionShader, "uFontAtlas"), 0);
    
    // Render all glyphs (we just need their alpha)
    int totalVerts = 0;
    for (const auto& batch : batches) {
        if (batch.vertices.empty()) continue;
        uploadGlyphBatch(batch.vertices);
        glBindVertexArray(m_glyphVAO);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.vertices.size()));
        totalVerts += static_cast<int>(batch.vertices.size());
    }
    
    // ── One-time diagnostic ──
    {
        static bool diagDone = false;
        if (!diagDone) {
            diagDone = true;
            glFinish();
            
            int cx = m_collisionMask.getWidth() / 2;
            int cy = m_collisionMask.getHeight() / 2;
            uint8_t afterDrawPixel = 0;
            glReadPixels(cx, cy, 1, 1, GL_RED, GL_UNSIGNED_BYTE, &afterDrawPixel);
            PLOG_INFO << "[CollMaskDiag] pixel after draws=" << (int)afterDrawPixel
                      << " totalVerts=" << totalVerts
                      << " batches=" << batches.size()
                      << " fboW=" << m_fboWidth << " fboH=" << m_fboHeight;
        }
    }
    
    m_collisionMask.unbind();
}

void MarkdownPreview::renderGlyphBatches(const std::vector<EffectBatch>& batches, const glm::mat4& mvp) {
    if (!m_glyphVAO || !m_glyphVBO) return;
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fontAtlasTexture);
    
    float time = static_cast<float>(glfwGetTime());
    
    for (const auto& batch : batches) {
        if (batch.vertices.empty()) continue;
        
        GLuint shader = m_effectSystem.getGlyphShader(batch.effect);
        if (!shader) continue;
        
        glUseProgram(shader);
        glUniformMatrix4fv(glGetUniformLocation(shader, "uMVP"), 1, GL_FALSE, &mvp[0][0]);
        glUniform1i(glGetUniformLocation(shader, "uFontAtlas"), 0);
        
        m_effectSystem.uploadEffectUniforms(shader, batch.effect, time);
        
        uploadGlyphBatch(batch.vertices);
        glBindVertexArray(m_glyphVAO);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.vertices.size()));
    }
}

void MarkdownPreview::uploadGlyphBatch(const std::vector<GlyphVertex>& vertices) {
    if (vertices.empty() || !m_glyphVBO) return;
    glBindBuffer(GL_ARRAY_BUFFER, m_glyphVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(GlyphVertex),
                 vertices.data(), GL_DYNAMIC_DRAW);
}

void MarkdownPreview::renderGlowBloom(const std::vector<EffectBatch>& batches, const glm::mat4& mvp) {
    if (!m_bloomGlowShader || !m_bloomBlurShader || !m_bloomCompositeShader ||
        !m_bloomSrcFBO || !m_quadVAO || m_fboWidth <= 0 || m_fboHeight <= 0) {
        return;
    }
    
    // Check if any batches contribute to bloom
    bool hasGlow = false;
    for (const auto& batch : batches) {
        if (!batch.effect) continue;
        // Check bloomEffect pointer
        Effect* fx = batch.effect->bloomEffect ? batch.effect->bloomEffect : batch.effect->effect;
        if (fx && fx->getCapabilities().contributesToBloom) {
            hasGlow = true;
            break;
        }
        // Also check any effect in the composite stack
        for (const auto* stackFx : batch.effect->effectStack) {
            if (stackFx && stackFx->getCapabilities().contributesToBloom) {
                hasGlow = true;
                break;
            }
        }
        if (hasGlow) break;
    }
    if (!hasGlow) return;
    
    float time = static_cast<float>(glfwGetTime());
    
    // === Pass 1: Render glow glyph silhouettes to bloom source FBO ===
    glBindFramebuffer(GL_FRAMEBUFFER, m_bloomSrcFBO);
    glViewport(0, 0, m_fboWidth, m_fboHeight);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    
    glUseProgram(m_bloomGlowShader);
    glUniformMatrix4fv(glGetUniformLocation(m_bloomGlowShader, "uMVP"), 1, GL_FALSE, &mvp[0][0]);
    glUniform1i(glGetUniformLocation(m_bloomGlowShader, "uFontAtlas"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fontAtlasTexture);
    
    for (const auto& batch : batches) {
        if (!batch.effect || batch.vertices.empty()) continue;
        
        // Find the bloom-contributing effect: check bloomEffect, then stack, then primary
        Effect* bloomFx = batch.effect->bloomEffect;
        if (!bloomFx || !bloomFx->getCapabilities().contributesToBloom) {
            bloomFx = nullptr;
            for (const auto* stackFx : batch.effect->effectStack) {
                if (stackFx && stackFx->getCapabilities().contributesToBloom) {
                    bloomFx = const_cast<Effect*>(stackFx);
                    break;
                }
            }
        }
        if (!bloomFx && batch.effect->effect && batch.effect->effect->getCapabilities().contributesToBloom) {
            bloomFx = batch.effect->effect;
        }
        if (!bloomFx) continue;
        
        glUniform4fv(glGetUniformLocation(m_bloomGlowShader, "uColor1"), 1, &bloomFx->color1[0]);
        glUniform1f(glGetUniformLocation(m_bloomGlowShader, "uIntensity"), bloomFx->intensity);
        
        uploadGlyphBatch(batch.vertices);
        glBindVertexArray(m_glyphVAO);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.vertices.size()));
    }
    
    // === Pass 2: Downsample bloom source to half-res ping texture ===
    int bloomW = m_fboWidth / 2;
    int bloomH = m_fboHeight / 2;
    
    // Blit/downsample bloom source -> bloomTex[0] via blur shader with zero radius
    glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFBO[0]);
    glViewport(0, 0, bloomW, bloomH);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND);
    
    glUseProgram(m_bloomBlurShader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_bloomSrcTex);
    glUniform1i(glGetUniformLocation(m_bloomBlurShader, "uTexture"), 0);
    glUniform2f(glGetUniformLocation(m_bloomBlurShader, "uDirection"), 0, 0);
    glUniform1f(glGetUniformLocation(m_bloomBlurShader, "uRadius"), 0);
    
    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    // === Pass 3 & 4: Two-pass Gaussian blur (horizontal + vertical), repeated for wider glow ===
    int blurPasses = 3; // Multiple passes for a very wide, soft glow
    for (int pass = 0; pass < blurPasses; ++pass) {
        // Horizontal blur: bloomTex[0] -> bloomFBO[1]
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFBO[1]);
        glViewport(0, 0, bloomW, bloomH);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_bloomTex[0]);
        glUniform1i(glGetUniformLocation(m_bloomBlurShader, "uTexture"), 0);
        glUniform2f(glGetUniformLocation(m_bloomBlurShader, "uDirection"), 1.0f / bloomW, 0);
        glUniform1f(glGetUniformLocation(m_bloomBlurShader, "uRadius"), 2.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        // Vertical blur: bloomTex[1] -> bloomFBO[0]
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFBO[0]);
        glViewport(0, 0, bloomW, bloomH);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_bloomTex[1]);
        glUniform2f(glGetUniformLocation(m_bloomBlurShader, "uDirection"), 0, 1.0f / bloomH);
        glUniform1f(glGetUniformLocation(m_bloomBlurShader, "uRadius"), 2.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    
    // === Pass 5: Composite blurred bloom onto main scene FBO ===
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_fboWidth, m_fboHeight);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE); // Additive blend
    glDisable(GL_DEPTH_TEST);
    
    glUseProgram(m_bloomCompositeShader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_bloomTex[0]);
    glUniform1i(glGetUniformLocation(m_bloomCompositeShader, "uBloom"), 0);
    glUniform1f(glGetUniformLocation(m_bloomCompositeShader, "uBloomStrength"), 1.0f);
    
    // Draw fullscreen quad — additive blend adds bloom glow halo onto the scene
    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    // Restore blend mode
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0); // Reset active texture unit
}

void MarkdownPreview::renderEmbeddedContent(const glm::mat4& mvp) {
    if (!m_embedShader) return;

    // Clear active lists (rebuilt each frame for overlay input)
    m_activeCanvases.clear();
    m_activeWorldMaps.clear();
    m_activeOrbitalViews.clear();

    // Evict stale world cache entries
    {
        auto now = std::chrono::steady_clock::now();
        for (auto it = s_worldCache.begin(); it != s_worldCache.end();) {
            if (now - it->second.last_used > std::chrono::seconds(30))
                it = s_worldCache.erase(it);
            else
                ++it;
        }
        for (auto it = s_orbitalCache.begin(); it != s_orbitalCache.end();) {
            if (now - it->second.last_used > std::chrono::seconds(30)) {
                if (it->second.texture) { glDeleteTextures(1, &it->second.texture); }
                it = s_orbitalCache.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Helper lambda: draw a textured quad in document space
    auto drawTexturedQuad = [&](GLuint texID, float x0, float y0, float displayW, float displayH,
                                float u0, float v0, float u1, float v1) {
        float x1 = x0 + displayW;
        float y1 = y0 + displayH;
        float z  = 0.0f;

        float verts[] = {
            x0, y0, z,    u0, v0,
            x1, y0, z,    u1, v0,
            x1, y1, z,    u1, v1,

            x0, y0, z,    u0, v0,
            x1, y1, z,    u1, v1,
            x0, y1, z,    u0, v1,
        };

        GLuint tmpVAO, tmpVBO;
        glGenVertexArrays(1, &tmpVAO);
        glGenBuffers(1, &tmpVBO);
        glBindVertexArray(tmpVAO);
        glBindBuffer(GL_ARRAY_BUFFER, tmpVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STREAM_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));

        glUseProgram(m_embedShader);
        glUniformMatrix4fv(glGetUniformLocation(m_embedShader, "uMVP"), 1, GL_FALSE, &mvp[0][0]);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, texID);
        glUniform1i(glGetUniformLocation(m_embedShader, "uTexture"), 0);

        glDrawArrays(GL_TRIANGLES, 0, 6);

        glDeleteBuffers(1, &tmpVBO);
        glDeleteVertexArrays(1, &tmpVAO);
    };

    m_embedCounter = 0;
    for (const auto& widget : m_overlayWidgets) {
        // ── Lua Canvas ───────────────────────────────────────────
        if (widget.type == OverlayWidget::LuaCanvas) {
            if (!m_scriptManager) continue;

            const std::string& scriptName = widget.data;
            std::string embedID = std::to_string(widget.sourceOffset) + ":" + std::to_string(++m_embedCounter);

            LuaEngine* eng = m_scriptManager->getOrCreateEngine(scriptName, embedID, 0);
            if (!eng) continue;

            ScriptConfig cfg = eng->callConfig();
            if (cfg.type != ScriptConfig::Type::Canvas) continue;

            int canvasW = (widget.nativeSize.x > 0) ? (int)widget.nativeSize.x : cfg.width;
            int canvasH = (widget.nativeSize.y > 0) ? (int)widget.nativeSize.y : cfg.height;

            float dt = ImGui::GetIO().DeltaTime;
            unsigned int texID = eng->renderCanvasFrame(embedID, canvasW, canvasH, dt);
            if (!texID) continue;

            float displayW = widget.size.x;
            float displayH = widget.size.y;

            // Lua FBOs are Y-flipped: top-left=(0,1), bottom-right=(1,0)
            drawTexturedQuad(texID, widget.docPos.x, widget.docPos.y, displayW, displayH,
                             0.0f, 1.0f, 1.0f, 0.0f);

            m_activeCanvases.push_back({eng, embedID,
                                        widget.docPos, {displayW, displayH},
                                        {(float)canvasW, (float)canvasH}});
            continue;
        }

        // ── World Map ────────────────────────────────────────────
        if (widget.type == OverlayWidget::WorldMap) {
            std::vector<std::string> parts = splitBracketAware(widget.data, "/");
            if (parts.size() < 2) continue;

            std::string worldName, config;
            splitNameConfig(parts[0], worldName, config);
            std::string projection = parts.back();
            std::transform(projection.begin(), projection.end(), projection.begin(), ::tolower);

            // Get or create cached world + camera state
            auto it = s_worldCache.try_emplace(worldName, config).first;
            CachedWorldState& cw = it->second;
            if (cw.config != config) {
                try { cw.world.parseConfig(config); cw.config = config; }
                catch (const std::exception& e) {
                    PLOGW << "preview:world parseConfig failed: " << e.what();
                }
            }
            cw.last_used = std::chrono::steady_clock::now();

            // Determine which layer to render
            std::vector<std::string> layerNames = cw.world.getLayerNames();
            if (layerNames.empty()) {
                PLOGW << "preview:world '" << worldName << "' has no layers (config='" << config << "')";
                continue;
            }
            int layerIdx = std::clamp(cw.selectedLayer, 0, (int)layerNames.size() - 1);
            std::string selectedLayer = layerNames[layerIdx];

            // Use native (unscaled) size for projection resolution
            int projW = (int)widget.nativeSize.x;
            int projH = (int)widget.nativeSize.y;
            if (projW <= 0 || projH <= 0) continue;

            // Save/restore FBO binding since projections may use OpenCL/GL interop
            GLint prevFBO = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

            // Ensure texture operations happen on unit 0
            glActiveTexture(GL_TEXTURE0);

            GLuint texID = 0;
            float u0, v0, u1, v1;

            if (projection == "globe") {
                cw.sphereProj.setViewCenterRadians(
                    cw.globeCenterLon * static_cast<float>(M_PI) / 180.0f,
                    cw.globeCenterLat * static_cast<float>(M_PI) / 180.0f);
                cw.sphereProj.setZoomLevel(cw.globeZoom);
                cw.sphereProj.setFov(cw.globeFovDeg * static_cast<float>(M_PI) / 180.0f);

                cw.sphereProj.project(cw.world, projW, projH, cw.globeTexture, selectedLayer);
                texID = cw.globeTexture;
                // Globe uses standard UV orientation
                u0 = 0.0f; v0 = 0.0f; u1 = 1.0f; v1 = 1.0f;
            } else {
                cw.mercProj.setViewCenterRadians(
                    cw.mercCenterLon * static_cast<float>(M_PI) / 180.0f,
                    cw.mercCenterLat * static_cast<float>(M_PI) / 180.0f);
                cw.mercProj.setZoomLevel(cw.mercZoom);

                cw.mercProj.project(cw.world, projW, projH, cw.mercTexture, selectedLayer);
                texID = cw.mercTexture;
                // Mercator: UV0=(1,0), UV1=(0,1) matches ImGui::Image convention used by mercatorMap()
                u0 = 1.0f; v0 = 0.0f; u1 = 0.0f; v1 = 1.0f;
            }

            // Restore our preview FBO + viewport (project() may have altered GL state)
            glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
            glViewport(0, 0, m_fboWidth, m_fboHeight);

            if (!texID) {
                PLOGW << "preview:world texture 0 after project() — projection='" << projection
                      << "' layer='" << selectedLayer << "' size=" << projW << "x" << projH;
                continue;
            }

            float displayW = widget.size.x;
            float displayH = widget.size.y;

            // Disable depth test for world map quad — avoids z-fighting with glyph depth
            glDisable(GL_DEPTH_TEST);
            drawTexturedQuad(texID, widget.docPos.x, widget.docPos.y, displayW, displayH,
                             u0, v0, u1, v1);
            glEnable(GL_DEPTH_TEST);

            m_activeWorldMaps.push_back({worldName, projection,
                                          widget.docPos, {displayW, displayH},
                                          {(float)projW, (float)projH}});
            continue;
        }

        // ── Orbital View ─────────────────────────────────────────
        if (widget.type == OverlayWidget::OrbitalView) {
            std::string systemName = widget.data;
            if (systemName.empty()) continue;

            // Get or create cached orbital state
            auto it = s_orbitalCache.try_emplace(systemName).first;
            CachedOrbitalState& co = it->second;
            co.last_used = std::chrono::steady_clock::now();

            // Load system from vault if not yet loaded
            if (!co.system.isLoaded() && m_vault) {
                auto sysInfo = m_vault->findOrbitalSystemByName(systemName);
                if (sysInfo.id >= 0) {
                    co.system.loadFromVault(m_vault, sysInfo.id);
                }
            }

            if (!co.system.isLoaded()) continue;

            int projW = (int)widget.nativeSize.x;
            int projH = (int)widget.nativeSize.y;
            if (projW <= 0 || projH <= 0) continue;

            // Advance time if playing
            if (co.playing) {
                co.time += (double)ImGui::GetIO().DeltaTime * co.timeSpeed;
                co.projection.setTime(co.time);
            }

            GLint prevFBO = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
            glActiveTexture(GL_TEXTURE0);

            co.projection.project(co.system, projW, projH, co.texture);

            glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
            glViewport(0, 0, m_fboWidth, m_fboHeight);

            if (!co.texture) continue;

            float displayW = widget.size.x;
            float displayH = widget.size.y;

            glDisable(GL_DEPTH_TEST);
            drawTexturedQuad(co.texture, widget.docPos.x, widget.docPos.y, displayW, displayH,
                             0.0f, 0.0f, 1.0f, 1.0f);
            glEnable(GL_DEPTH_TEST);

            m_activeOrbitalViews.push_back({systemName,
                                            widget.docPos, {displayW, displayH},
                                            {(float)projW, (float)projH}});
            continue;
        }

        // ── Image ────────────────────────────────────────────────
        if (widget.type == OverlayWidget::Image) {
            const std::string& src = widget.data;
            GLuint texID = 0;
            int texW = 0, texH = 0;

            // Try vault://Assets/ resolution
            const std::string assetsPrefix = "vault://Assets/";
            if (m_vault && src.rfind(assetsPrefix, 0) == 0) {
                int64_t aid = m_vault->findAttachmentByExternalPath(src);
                if (aid != -1) {
                    std::string key = std::string("vault:assets:") + std::to_string(aid);
                    IconTexture cached = GetDynamicTexture(key);
                    if (cached.loaded) {
                        texID = cached.textureID;
                        texW = cached.width;
                        texH = cached.height;
                    } else {
                        auto meta = m_vault->getAttachmentMeta(aid);
                        if (meta.size > 0) {
                            auto data = m_vault->getAttachmentData(aid);
                            if (!data.empty()) {
                                auto result = LoadTextureFromMemory(key, data);
                                texID = result.textureID;
                                texW = result.width;
                                texH = result.height;
                            }
                        }
                    }
                }
            }

            if (texID) {
                float displayW = widget.size.x;
                float displayH = widget.size.y;

                // Maintain aspect ratio if only width or height was specified
                if (texW > 0 && texH > 0) {
                    float aspect = (float)texW / (float)texH;
                    // If using default sizing, fit to actual image aspect
                    if (widget.nativeSize.x == 300 && widget.nativeSize.y == 200) {
                        displayH = displayW / aspect;
                    }
                }

                glDisable(GL_DEPTH_TEST);
                drawTexturedQuad(texID, widget.docPos.x, widget.docPos.y,
                                 displayW, displayH, 0.0f, 0.0f, 1.0f, 1.0f);
                glEnable(GL_DEPTH_TEST);
            }
            continue;
        }
    }
}

void MarkdownPreview::emitParticles(float dt, const std::vector<EffectBatch>& batches) {
    // Safety check - ensure particle buffers are initialized
    if (m_cpuParticles.empty() || m_cpuDeadIndices.empty()) {
        return;
    }
    
    // Find effects that emit particles
    for (size_t batchIdx = 0; batchIdx < batches.size(); ++batchIdx) {
        const EffectBatch& batch = batches[batchIdx];
        if (!batch.effect || !batch.effect->hasParticles) continue;
        
        const EmissionConfig& emission = batch.effect->emission;
        
        // Use behaviorID for accumulator index (stable across frames,
        // unlike batchIdx which depends on unordered_map iteration order)
        uint32_t bid = batch.effect->effect ? batch.effect->effect->getBehaviorID() : 0;
        if (bid >= 16) bid = 0;
        
        // Accumulate emission time
        m_emitAccumulators[bid] += dt * emission.rate;
        
        // Emit particles based on accumulated time
        int toEmit = static_cast<int>(m_emitAccumulators[bid]);
        if (toEmit <= 0) continue;
        // Cap particles emitted per batch per frame
        toEmit = std::min(toEmit, 4);
        m_emitAccumulators[bid] -= toEmit;
        
        // Calculate bounds of this effect batch for emission area
        glm::vec2 minPos(FLT_MAX), maxPos(-FLT_MAX);
        for (const auto& vert : batch.vertices) {
            minPos.x = std::min(minPos.x, vert.pos.x);
            minPos.y = std::min(minPos.y, vert.pos.y);
            maxPos.x = std::max(maxPos.x, vert.pos.x);
            maxPos.y = std::max(maxPos.y, vert.pos.y);
        }
        
        // Emit from within the glyph region
        for (int i = 0; i < toEmit && m_deadCount > 0; ++i) {
            uint32_t idx = m_cpuDeadIndices[--m_deadCount];
            
            // Bounds check
            if (idx >= m_cpuParticles.size()) continue;
            
            Particle& p = m_cpuParticles[idx];
            
            // Position: random within glyph bounds
            float rx = static_cast<float>(rand()) / RAND_MAX;
            float ry = static_cast<float>(rand()) / RAND_MAX;
            p.pos = glm::vec2(
                minPos.x + rx * (maxPos.x - minPos.x),
                minPos.y + ry * (maxPos.y - minPos.y)
            );
            
            // Add velocity variation
            float vx = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f;
            float vy = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f;
            p.vel = emission.velocity + glm::vec2(vx * emission.velocityVar.x, vy * emission.velocityVar.y);
            
            // Z-depth (slight variation)
            p.z = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 10.0f;
            p.zVel = 0.0f;
            
            // Life
            float lifeVar = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f;
            p.life = emission.lifetime + lifeVar * emission.lifetimeVar;
            p.maxLife = p.life;
            
            // Size
            float sizeVar = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f;
            p.size = emission.size + sizeVar * emission.sizeVar;
            
            p.meshID = emission.meshID;
            p.rotation = glm::vec3(0, 0, static_cast<float>(rand()) / RAND_MAX * 6.28f);
            p.rotVel = glm::vec3(0, 0, (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f);
            p.behaviorID = batch.effect->effect ? batch.effect->effect->getBehaviorID() : 0;
            
            // Color from effect
            if (batch.effect->effect) {
                const auto& c = batch.effect->effect->color1;
                p.color = glm::vec4(c[0], c[1], c[2], c[3]);
            }
            
            m_particleCount = std::max(m_particleCount, static_cast<size_t>(idx + 1));
        }
    }
}

void MarkdownPreview::updateParticlesGPU(float dt) {
    if (m_cpuParticles.empty() || m_cpuDeadIndices.empty() || m_particleCount == 0) {
        return;
    }
    
    auto& cl = OpenCLContext::get();
    if (!cl.isReady() || !m_clParticleBuffer) {
        return;
    }
    
    // Get all effects that have compiled particle kernels
    auto particleEffects = m_effectSystem.getParticleEffects();
    if (particleEffects.empty()) {
        return;
    }
    
    // Deduplicate by behaviorID — only dispatch one kernel per behavior type
    // (variants like "lava" share behaviorID with "fire", dispatching both
    //  would double-process the same particles)
    {
        uint32_t seenBitmask = 0;
        auto it = particleEffects.begin();
        while (it != particleEffects.end()) {
            uint32_t bid = (*it)->effect ? (*it)->effect->getBehaviorID() : 0;
            uint32_t bit = 1u << bid;
            if (bid == 0 || (seenBitmask & bit)) {
                it = particleEffects.erase(it);
            } else {
                seenBitmask |= bit;
                ++it;
            }
        }
    }
    
    cl_command_queue q = cl.getQueue();
    uint32_t count = static_cast<uint32_t>(m_particleCount);
    size_t globalSize = m_particleCount;
    float scrollY = m_scrollY;
    float maskH = static_cast<float>(m_collisionMask.getHeight());
    float time = static_cast<float>(glfwGetTime());
    
    // 1. Upload CPU particles to CL buffer
    cl_int err = clEnqueueWriteBuffer(q, m_clParticleBuffer, CL_FALSE, 0,
                                       m_particleCount * sizeof(Particle),
                                       m_cpuParticles.data(), 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        PLOG_ERROR << "Failed to upload particles to CL: " << err;
        return;
    }
    
    // 2. Create/update CL collision image from CPU readback data
    updateCollisionCLImage();
    
    // If collision image failed, create a tiny dummy (kernels require a valid image arg)
    cl_mem collisionImg = m_clCollisionImage;
    cl_mem dummyImg = nullptr;
    if (!collisionImg) {
        cl_image_format fmt;
        fmt.image_channel_order = CL_R;
        fmt.image_channel_data_type = CL_UNORM_INT8;
        cl_image_desc desc = {};
        desc.image_type = CL_MEM_OBJECT_IMAGE2D;
        desc.image_width = 1;
        desc.image_height = 1;
        desc.image_row_pitch = 1;
        uint8_t zero = 0;
        dummyImg = clCreateImage(cl.getContext(), CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                  &fmt, &desc, &zero, &err);
        collisionImg = dummyImg;
        maskH = 1.0f;
    }
    
    // 2.5. Run SPH fluid passes for blood particles (before behavior kernels)
    if (m_sphDensityKernel && m_sphForcesKernel && m_clSPHDensity && m_clSPHPressure
        && m_clSPHGrid && m_clSPHGridEntries) {
        // Check if any blood particles exist
        bool hasBlood = false;
        for (size_t i = 0; i < m_particleCount && !hasBlood; ++i) {
            if (m_cpuParticles[i].life > 0 && m_cpuParticles[i].behaviorID == 2) hasBlood = true;
        }
        
        if (hasBlood) {
            // Get SPH params from the BloodEffect instance
            float sphRadius = 25.0f, restDensity = 1.0f, stiffness = 150.0f;
            float viscosity = 6.0f, cohesion = 0.3f, mass = 1.0f;
            
            for (EffectDef* def : particleEffects) {
                if (def->effect && def->effect->getBehaviorID() == 2) {
                    auto* blood = dynamic_cast<BloodEffect*>(def->effect);
                    if (blood) {
                        sphRadius = blood->sphSmoothingRadius;
                        restDensity = blood->sphRestDensity;
                        stiffness = blood->sphStiffness;
                        viscosity = blood->sphViscosity;
                        cohesion = blood->sphCohesion;
                        mass = blood->sphParticleMass;
                    }
                    break;
                }
            }
            
            // Build spatial hash grid on CPU (fast for <1000 particles)
            constexpr uint32_t SPH_GRID_TABLE_SIZE = 4099;
            
            auto sphCellHash = [](int cx, int cy, uint32_t tableSize) -> uint32_t {
                uint32_t h = static_cast<uint32_t>((cx * 92837111) ^ (cy * 689287499));
                return h % tableSize;
            };
            
            // Count particles per cell
            std::vector<uint32_t> cellCounts(SPH_GRID_TABLE_SIZE, 0);
            for (size_t i = 0; i < m_particleCount; ++i) {
                auto& p = m_cpuParticles[i];
                if (p.life <= 0 || p.behaviorID != 2) continue;
                int cx = static_cast<int>(std::floor(p.pos.x / sphRadius));
                int cy = static_cast<int>(std::floor(p.pos.y / sphRadius));
                cellCounts[sphCellHash(cx, cy, SPH_GRID_TABLE_SIZE)]++;
            }
            
            // Prefix sum → cell start offsets
            std::vector<cl_uint2> gridCells(SPH_GRID_TABLE_SIZE);
            uint32_t runningOffset = 0;
            for (uint32_t c = 0; c < SPH_GRID_TABLE_SIZE; ++c) {
                gridCells[c] = {{runningOffset, cellCounts[c]}};
                runningOffset += cellCounts[c];
            }
            
            // Fill sorted entries
            std::vector<cl_uint> gridEntries(runningOffset > 0 ? runningOffset : 1, 0);
            std::vector<uint32_t> cellInsert(SPH_GRID_TABLE_SIZE, 0);
            for (size_t i = 0; i < m_particleCount; ++i) {
                auto& p = m_cpuParticles[i];
                if (p.life <= 0 || p.behaviorID != 2) continue;
                int cx = static_cast<int>(std::floor(p.pos.x / sphRadius));
                int cy = static_cast<int>(std::floor(p.pos.y / sphRadius));
                uint32_t ch = sphCellHash(cx, cy, SPH_GRID_TABLE_SIZE);
                uint32_t slot = gridCells[ch].s[0] + cellInsert[ch]++;
                if (slot < gridEntries.size()) {
                    gridEntries[slot] = static_cast<cl_uint>(i);
                }
            }
            
            // Upload grid to CL
            clEnqueueWriteBuffer(q, m_clSPHGrid, CL_FALSE, 0,
                                  SPH_GRID_TABLE_SIZE * sizeof(cl_uint2),
                                  gridCells.data(), 0, nullptr, nullptr);
            if (runningOffset > 0) {
                clEnqueueWriteBuffer(q, m_clSPHGridEntries, CL_FALSE, 0,
                                      runningOffset * sizeof(cl_uint),
                                      gridEntries.data(), 0, nullptr, nullptr);
            }
            
            uint32_t tableSize = SPH_GRID_TABLE_SIZE;
            
            // Pass 1: density + pressure
            clSetKernelArg(m_sphDensityKernel, 0, sizeof(cl_mem), &m_clParticleBuffer);
            clSetKernelArg(m_sphDensityKernel, 1, sizeof(cl_mem), &m_clSPHDensity);
            clSetKernelArg(m_sphDensityKernel, 2, sizeof(cl_mem), &m_clSPHPressure);
            clSetKernelArg(m_sphDensityKernel, 3, sizeof(cl_mem), &m_clSPHGrid);
            clSetKernelArg(m_sphDensityKernel, 4, sizeof(cl_mem), &m_clSPHGridEntries);
            clSetKernelArg(m_sphDensityKernel, 5, sizeof(uint32_t), &count);
            clSetKernelArg(m_sphDensityKernel, 6, sizeof(uint32_t), &tableSize);
            clSetKernelArg(m_sphDensityKernel, 7, sizeof(float), &sphRadius);
            clSetKernelArg(m_sphDensityKernel, 8, sizeof(float), &restDensity);
            clSetKernelArg(m_sphDensityKernel, 9, sizeof(float), &stiffness);
            clSetKernelArg(m_sphDensityKernel, 10, sizeof(float), &mass);
            err = clEnqueueNDRangeKernel(q, m_sphDensityKernel, 1, nullptr, &globalSize,
                                          nullptr, 0, nullptr, nullptr);
            if (err != CL_SUCCESS) {
                PLOG_ERROR << "SPH density kernel dispatch failed: " << err;
            }
            
            // Pass 2: forces (implicit serialization on in-order queue)
            clSetKernelArg(m_sphForcesKernel, 0, sizeof(cl_mem), &m_clParticleBuffer);
            clSetKernelArg(m_sphForcesKernel, 1, sizeof(cl_mem), &m_clSPHDensity);
            clSetKernelArg(m_sphForcesKernel, 2, sizeof(cl_mem), &m_clSPHPressure);
            clSetKernelArg(m_sphForcesKernel, 3, sizeof(cl_mem), &m_clSPHGrid);
            clSetKernelArg(m_sphForcesKernel, 4, sizeof(cl_mem), &m_clSPHGridEntries);
            clSetKernelArg(m_sphForcesKernel, 5, sizeof(float), &dt);
            clSetKernelArg(m_sphForcesKernel, 6, sizeof(uint32_t), &count);
            clSetKernelArg(m_sphForcesKernel, 7, sizeof(uint32_t), &tableSize);
            clSetKernelArg(m_sphForcesKernel, 8, sizeof(float), &sphRadius);
            clSetKernelArg(m_sphForcesKernel, 9, sizeof(float), &viscosity);
            clSetKernelArg(m_sphForcesKernel, 10, sizeof(float), &cohesion);
            clSetKernelArg(m_sphForcesKernel, 11, sizeof(float), &mass);
            err = clEnqueueNDRangeKernel(q, m_sphForcesKernel, 1, nullptr, &globalSize,
                                          nullptr, 0, nullptr, nullptr);
            if (err != CL_SUCCESS) {
                PLOG_ERROR << "SPH forces kernel dispatch failed: " << err;
            }
        }
    }
    
    // 3. Dispatch each Effect's particle kernel
    // Standard arg order: (particles, collision, dt, scrollY, maskH, time, count, maskScale)
    // Effect-specific args start at index 8 via bindKernelParams()
    float maskScale = COLLISION_SCALE;
    for (EffectDef* def : particleEffects) {
        cl_kernel kernel = def->effectKernel;
        if (!kernel || !def->effect) continue;
        
        // Set standard kernel args 0-7
        clSetKernelArg(kernel, 0, sizeof(cl_mem), &m_clParticleBuffer);
        clSetKernelArg(kernel, 1, sizeof(cl_mem), &collisionImg);
        clSetKernelArg(kernel, 2, sizeof(float), &dt);
        clSetKernelArg(kernel, 3, sizeof(float), &scrollY);
        clSetKernelArg(kernel, 4, sizeof(float), &maskH);
        clSetKernelArg(kernel, 5, sizeof(float), &time);
        clSetKernelArg(kernel, 6, sizeof(uint32_t), &count);
        clSetKernelArg(kernel, 7, sizeof(float), &maskScale);
        
        // Let the Effect bind its specific params from arg 8 onward
        KernelParams params;
        params.particleBuffer = m_clParticleBuffer;
        params.collisionImage = collisionImg;
        params.deltaTime = dt;
        params.scrollY = scrollY;
        params.maskHeight = maskH;
        params.time = time;
        params.particleCount = count;
        
        def->effect->bindKernelSnippetParams(kernel, params, 8);
        
        err = clEnqueueNDRangeKernel(q, kernel, 1, nullptr, &globalSize, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            PLOG_ERROR << def->name << " kernel dispatch failed: " << err;
        }
    }
    
    // 4. Read back particles from CL buffer (blocking read)
    err = clEnqueueReadBuffer(q, m_clParticleBuffer, CL_TRUE, 0,
                               m_particleCount * sizeof(Particle),
                               m_cpuParticles.data(), 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        PLOG_ERROR << "Failed to read back particles from CL: " << err;
    }
    
    // Release dummy collision image if we created one
    if (dummyImg) {
        clReleaseMemObject(dummyImg);
    }
    
    // ── Collision mask diagnostic (every ~120 frames) ──
    {
        static int diagFrame = 0;
        if (++diagFrame >= 120) {
            diagFrame = 0;
            int mw = m_collisionMask.getWidth();
            int mh = m_collisionMask.getHeight();
            const uint8_t* maskData = m_collisionMask.hasCPUData() ? m_collisionMask.getCPUData() : nullptr;
            int nonZero = 0, maxVal = 0;
            if (maskData && mw > 0 && mh > 0) {
                size_t total = static_cast<size_t>(mw) * mh;
                for (size_t j = 0; j < total; ++j) {
                    if (maskData[j] > 0) ++nonZero;
                    if (maskData[j] > maxVal) maxVal = maskData[j];
                }
            }
            PLOG_INFO << "[CollDiag] mask=" << mw << "x" << mh
                      << " nonZero=" << nonZero << "/" << (mw * mh)
                      << " maxVal=" << maxVal
                      << " clImg=" << (m_clCollisionImage ? "yes" : "NO")
                      << " scrollY=" << m_scrollY;

            // Sample 3 active particles against CPU collision mask
            int logged = 0;
            for (size_t i = 0; i < m_particleCount && logged < 3; ++i) {
                auto& p = m_cpuParticles[i];
                if (p.life <= 0.0f) continue;
                float maskH = static_cast<float>(mh);
                float maskX = p.pos.x;
                float maskY = m_scrollY + maskH - 1.0f - p.pos.y;
                float cpuSample = m_collisionMask.sample(maskX, maskY);
                float lifeElapsed = p.maxLife - p.life;
                PLOG_INFO << "[CollDiag] p[" << i << "] bid=" << p.behaviorID
                          << " docPos=(" << p.pos.x << "," << p.pos.y << ")"
                          << " maskPos=(" << maskX << "," << maskY << ")"
                          << " cpuSample=" << cpuSample
                          << " life=" << p.life << "/" << p.maxLife
                          << " elapsed=" << lifeElapsed
                          << " vel=(" << p.vel.x << "," << p.vel.y << ")";
                ++logged;
            }
        }
    }
    
    // 5. Mark dead particles for recycling (only newly-dead, using maxLife sentinel)
    for (size_t i = 0; i < m_particleCount && i < m_cpuParticles.size(); ++i) {
        if (m_cpuParticles[i].life <= 0.0f && m_cpuParticles[i].maxLife > 0.0f
            && m_deadCount < m_cpuDeadIndices.size()) {
            m_cpuDeadIndices[m_deadCount++] = static_cast<uint32_t>(i);
            m_cpuParticles[i].maxLife = -1.0f;  // sentinel: already recycled
        }
    }
    
    // 5b. Build per-behaviorID index groups for per-effect rendering
    m_particleBehaviorGroups.clear();
    for (size_t i = 0; i < m_particleCount && i < m_cpuParticles.size(); ++i) {
        if (m_cpuParticles[i].life > 0.0f) {
            m_particleBehaviorGroups[m_cpuParticles[i].behaviorID].push_back(
                static_cast<uint32_t>(i));
        }
    }
    
    // 6. Upload to GPU for rendering
    if (m_particleCount > 0 && m_particleCount <= m_cpuParticles.size()) {
        glBindBuffer(GL_ARRAY_BUFFER, m_particleVBO);
        glBufferData(GL_ARRAY_BUFFER, m_particleCount * sizeof(Particle),
                     m_cpuParticles.data(), GL_DYNAMIC_DRAW);
    }
}

void MarkdownPreview::renderParticlesFromGPU(const glm::mat4& mvp) {
    if (m_particleCount == 0 || m_particleBehaviorGroups.empty()) return;
    
    // Particles should always render on top of glyphs (no depth test)
    // and use additive blending for fire/glow effects
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // Additive blend
    
    glBindVertexArray(m_particleVAO);
    
    float time = static_cast<float>(glfwGetTime());
    
    // Build behaviorID → EffectDef* map for per-effect shader lookup
    auto particleEffects = m_effectSystem.getParticleEffects();
    std::unordered_map<uint32_t, EffectDef*> effectByBehavior;
    for (auto* def : particleEffects) {
        if (def->effect && def->effectParticleShader) {
            uint32_t bid = def->effect->getBehaviorID();
            if (effectByBehavior.find(bid) == effectByBehavior.end()) {
                effectByBehavior[bid] = def;
            }
        }
    }
    
    // Render each behavior group with its effect's particle shader
    for (auto& [bid, indices] : m_particleBehaviorGroups) {
        if (indices.empty()) continue;
        
        bool isBlood = (bid == 2 && m_bloodDensityFBO);
        
        if (isBlood) {
            // Render blood particles to density FBO as soft gaussian blobs
            glBindFramebuffer(GL_FRAMEBUFFER, m_bloodDensityFBO);
            glViewport(0, 0, m_fboWidth, m_fboHeight);
            glClearColor(0, 0, 0, 0);
            glClear(GL_COLOR_BUFFER_BIT);
            // Pure additive blend for density accumulation (no alpha multiply)
            glBlendFunc(GL_ONE, GL_ONE);
        }
        
        GLuint shader = m_particleShader;  // fallback generic shader
        EffectDef* def = nullptr;
        auto it = effectByBehavior.find(bid);
        if (it != effectByBehavior.end()) {
            shader = it->second->effectParticleShader;
            def = it->second;
        }
        
        glUseProgram(shader);
        glUniformMatrix4fv(glGetUniformLocation(shader, "uMVP"), 1, GL_FALSE, &mvp[0][0]);
        
        if (def && def->effect) {
            def->effect->uploadParticleSnippetUniforms(shader, time);
        }
        
        // Upload index buffer and draw this behavior group
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_particleEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     indices.size() * sizeof(uint32_t),
                     indices.data(), GL_DYNAMIC_DRAW);
        glDrawElements(GL_POINTS, static_cast<GLsizei>(indices.size()),
                       GL_UNSIGNED_INT, nullptr);
        
        if (isBlood) {
            // Restore main FBO and normal particle blend
            glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
            glViewport(0, 0, m_fboWidth, m_fboHeight);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // Back to additive particle blend
        }
    }
    
    // Restore state
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void MarkdownPreview::renderBloodFluid() {
    if (!m_bloodFluidShader || !m_bloodDensityFBO || !m_quadVAO ||
        m_fboWidth <= 0 || m_fboHeight <= 0) return;
    
    // Only run if blood particles exist
    auto it = m_particleBehaviorGroups.find(2);
    if (it == m_particleBehaviorGroups.end() || it->second.empty()) return;
    
    // Composite fluid surface onto main FBO
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_fboWidth, m_fboHeight);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);  // Alpha blend (not additive)
    glDisable(GL_DEPTH_TEST);
    
    glUseProgram(m_bloodFluidShader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_bloodDensityTex);
    glUniform1i(glGetUniformLocation(m_bloodFluidShader, "uDensity"), 0);
    glUniform2f(glGetUniformLocation(m_bloodFluidShader, "uTexelSize"),
                1.0f / m_fboWidth, 1.0f / m_fboHeight);
    
    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    // Restore standard blend
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
}

void MarkdownPreview::renderOverlayWidgets(const ImVec2& origin, float scrollY) {
    for (const auto& widget : m_overlayWidgets) {
        ImVec2 screenPos(origin.x + widget.docPos.x, origin.y + widget.docPos.y - scrollY);
        
        switch (widget.type) {
            case OverlayWidget::Link:
                // Make links clickable
                ImGui::SetCursorScreenPos(screenPos);
                if (ImGui::InvisibleButton(("link_" + std::to_string(widget.sourceOffset)).c_str(),
                                           ImVec2(widget.size.x, widget.size.y))) {
                    // TODO: Handle link click
                    PLOG_INFO << "Link clicked: " << widget.data;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
                    ImGui::SetTooltip("%s", widget.data.c_str());
                }
                break;
                
            case OverlayWidget::Checkbox:
                ImGui::SetCursorScreenPos(screenPos);
                // TODO: Handle checkbox
                break;
                
            case OverlayWidget::Image:
                break;  // rendered as textured quad in renderEmbeddedContent

            default:
                break;
        }
    }

    // Forward mouse input to world map camera controls + overlay UI
    ImGuiIO& io = ImGui::GetIO();
    for (size_t wi = 0; wi < m_activeWorldMaps.size(); ++wi) {
        const auto& aw = m_activeWorldMaps[wi];

        auto cacheIt = s_worldCache.find(aw.worldKey);
        if (cacheIt == s_worldCache.end()) continue;
        CachedWorldState& cw = cacheIt->second;

        ImVec2 mapScreenPos(origin.x + aw.docPos.x, origin.y + aw.docPos.y - scrollY);
        ImVec2 mapSize(aw.size.x, aw.size.y);

        // Invisible button for mouse interaction (zoom/pan/rotate)
        ImGui::SetCursorScreenPos(mapScreenPos);
        std::string btnID = "world_map_" + std::to_string(wi);
        ImGui::InvisibleButton(btnID.c_str(), mapSize);
        ImGui::SetItemAllowOverlap();  // let overlay widgets receive clicks
        bool isHover = ImGui::IsItemHovered();

        if (aw.projection == "globe") {
            // Scroll to zoom
            if (isHover && io.MouseWheel != 0.0f) {
                float factor = std::pow(1.12f, fabsf(io.MouseWheel));
                if (io.MouseWheel > 0.0f)
                    cw.globeZoom = std::clamp(cw.globeZoom / factor, -0.99f, 64.0f);
                else
                    cw.globeZoom = std::clamp(cw.globeZoom * factor, -0.99f, 64.0f);
            }
            // Drag to rotate
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                float rotDeg = 0.25f;
                cw.globeCenterLon += drag.x * rotDeg;
                cw.globeCenterLat += drag.y * rotDeg;
                while (cw.globeCenterLon < -180.0f) cw.globeCenterLon += 360.0f;
                while (cw.globeCenterLon >= 180.0f) cw.globeCenterLon -= 360.0f;
                cw.globeCenterLat = std::clamp(cw.globeCenterLat, -89.9f, 89.9f);
                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            }
        } else {
            // Mercator: scroll to zoom
            if (isHover && io.MouseWheel != 0.0f) {
                float factor = (io.MouseWheel > 0.0f) ? 1.1f : (1.0f / 1.1f);
                cw.mercZoom = std::clamp(cw.mercZoom * factor, 1.0f, 100000.0f);
            }
            // Mercator: drag to pan
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                float texW = aw.size.x;
                float texH = aw.size.y;

                // Convert center to projected u/v
                float u_center = (cw.mercCenterLon + 180.0f) / 360.0f;
                float lat_rad = cw.mercCenterLat * static_cast<float>(M_PI) / 180.0f;
                float mercN = std::log(std::tan(static_cast<float>(M_PI) / 4.0f + lat_rad / 2.0f));
                float v_center = 0.5f * (1.0f - mercN / static_cast<float>(M_PI));

                float du = drag.x / texW / cw.mercZoom;
                float dv = -drag.y / texH / cw.mercZoom;
                u_center += du;
                v_center += dv;

                // Wrap
                u_center -= std::floor(u_center);
                v_center = std::clamp(v_center, 0.0f, 1.0f);

                cw.mercCenterLon = u_center * 360.0f - 180.0f;
                float mercN2 = static_cast<float>(M_PI) * (1.0f - 2.0f * v_center);
                cw.mercCenterLat = 180.0f / static_cast<float>(M_PI) * std::atan(std::sinh(mercN2));

                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            }
        }

        // ── Overlay controls (matching WorldMap.cpp style) ─────
        {
            std::vector<std::string> layerNames = cw.world.getLayerNames();
            std::string layerNamesNullSep;
            for (const auto& n : layerNames) layerNamesNullSep += n + '\0';
            layerNamesNullSep += '\0';

            // Layer combo in its own child so popup opens correctly
            float comboW = mapSize.x * 0.4f;
            float comboH = ImGui::GetFrameHeightWithSpacing() + 4.0f;
            ImVec2 comboPos(mapScreenPos.x + 8.0f, mapScreenPos.y + 4.0f);
            ImGui::SetCursorScreenPos(comboPos);
            std::string comboChildID = "WorldCombo_" + std::to_string(wi);
            ImGui::BeginChild(comboChildID.c_str(), ImVec2(comboW + 8.0f, comboH),
                              false, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground);
            ImGui::PushItemWidth(comboW);
            ImGui::Combo(("##layer_" + std::to_string(wi)).c_str(), &cw.selectedLayer,
                         layerNamesNullSep.c_str());
            ImGui::PopItemWidth();
            ImGui::EndChild();

            // Overlay bar at bottom (matches WorldMap.cpp layout)
            float overlayH = 56.0f;
            ImVec2 overlayPos(mapScreenPos.x + 8.0f,
                              mapScreenPos.y + mapSize.y - overlayH);
            ImGui::SetCursorScreenPos(overlayPos);

            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.3f));
            std::string childID = "WorldOverlay_" + std::to_string(wi);
            ImGui::BeginChild(childID.c_str(), ImVec2(mapSize.x - 16.0f, overlayH),
                              false, ImGuiWindowFlags_NoDecoration);

            if (aw.projection == "globe") {
                ImGui::Text("Lon:%.1f Lat:%.1f Z:%.3f",
                            cw.globeCenterLon, cw.globeCenterLat, cw.globeZoom);
                ImGui::SameLine();
                ImGui::PushItemWidth(110);
                ImGui::DragFloat(("##GlobeFOV_" + std::to_string(wi)).c_str(),
                                 &cw.globeFovDeg, 1.0f, 10.0f, 120.0f, "FOV: %.1f");
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button(("Reset Camera##" + std::to_string(wi)).c_str())) {
                    cw.globeCenterLon = 0.0f; cw.globeCenterLat = 0.0f;
                    cw.globeZoom = 3.0f; cw.globeFovDeg = 45.0f;
                }
            } else {
                ImGui::Text("Lon: %.2f  Lat: %.2f  Zoom: %.3f",
                            cw.mercCenterLon, cw.mercCenterLat, cw.mercZoom);
                ImGui::SameLine();
                ImGui::PushItemWidth(110);
                float mercDragSpeed = std::max(0.1f, cw.mercZoom * 0.01f);
                ImGui::DragFloat(("##MercZoom_" + std::to_string(wi)).c_str(),
                                 &cw.mercZoom, mercDragSpeed, 1.0f, 100000.0f, "Zoom: %.2f");
                ImGui::PopItemWidth();
                ImGui::SameLine();
                if (ImGui::Button(("Reset Camera##" + std::to_string(wi)).c_str())) {
                    cw.mercCenterLon = 0.0f; cw.mercCenterLat = 0.0f;
                    cw.mercZoom = 1.0f;
                }
            }

            ImGui::EndChild();
            ImGui::PopStyleColor();
        }
    }

    // Forward mouse input to orbital view camera controls + overlay UI
    for (size_t oi = 0; oi < m_activeOrbitalViews.size(); ++oi) {
        const auto& ao = m_activeOrbitalViews[oi];

        auto cacheIt = s_orbitalCache.find(ao.systemKey);
        if (cacheIt == s_orbitalCache.end()) continue;
        CachedOrbitalState& co = cacheIt->second;

        ImVec2 orbScreenPos(origin.x + ao.docPos.x, origin.y + ao.docPos.y - scrollY);
        ImVec2 orbSize(ao.size.x, ao.size.y);

        ImGui::SetCursorScreenPos(orbScreenPos);
        std::string btnID = "orbital_view_" + std::to_string(oi);
        ImGui::InvisibleButton(btnID.c_str(), orbSize);
        ImGui::SetItemAllowOverlap();
        bool isHover = ImGui::IsItemHovered();

        // Scroll to zoom
        if (isHover && io.MouseWheel != 0.0f) {
            float z = co.projection.zoom();
            z *= (1.0f - io.MouseWheel * 0.1f);
            z = std::clamp(z, 0.5f, 1000.0f);
            co.projection.setZoom(z);
        }
        // Drag to rotate
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
            ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
            float lon = co.projection.centerLon() - drag.x * 0.005f;
            float lat = co.projection.centerLat() + drag.y * 0.005f;
            lat = std::clamp(lat, -1.5f, 1.5f);
            co.projection.setViewCenter(lon, lat);
            ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
        }

        // Overlay bar at bottom
        float overlayH = 36.0f;
        ImVec2 overlayPos(orbScreenPos.x + 8.0f,
                          orbScreenPos.y + orbSize.y - overlayH);
        ImGui::SetCursorScreenPos(overlayPos);

        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.3f));
        std::string childID = "OrbitalOverlay_" + std::to_string(oi);
        ImGui::BeginChild(childID.c_str(), ImVec2(orbSize.x - 16.0f, overlayH),
                          false, ImGuiWindowFlags_NoDecoration);

        if (ImGui::Button(co.playing ? "Pause" : "Play")) co.playing = !co.playing;
        ImGui::SameLine();
        ImGui::PushItemWidth(80);
        ImGui::DragFloat(("##OrbSpeed_" + std::to_string(oi)).c_str(),
                         &co.timeSpeed, 0.1f, 0.01f, 100.0f, "%.1fx");
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::Text("T=%.2f Z=%.1f", co.time, co.projection.zoom());

        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    // Forward mouse input to Lua canvas engines
    for (size_t ci = 0; ci < m_activeCanvases.size(); ++ci) {
        const auto& ac = m_activeCanvases[ci];
        ImVec2 canvasScreenPos(origin.x + ac.docPos.x, origin.y + ac.docPos.y - scrollY);
        ImVec2 canvasSize(ac.size.x, ac.size.y);

        ImGui::SetCursorScreenPos(canvasScreenPos);
        std::string btnID = "lua_canvas_" + std::to_string(ci);
        ImGui::InvisibleButton(btnID.c_str(), canvasSize);
        bool isHover = ImGui::IsItemHovered();

        // Canvas-relative mouse position, mapped to native (unscaled) coordinates
        float scaleX = (ac.size.x > 0) ? ac.nativeSize.x / ac.size.x : 1.0f;
        float scaleY = (ac.size.y > 0) ? ac.nativeSize.y / ac.size.y : 1.0f;
        float relX = (io.MousePos.x - canvasScreenPos.x) * scaleX;
        float relY = (io.MousePos.y - canvasScreenPos.y) * scaleY;

        // Mouse button events
        for (int b = 0; b < 3; ++b) {
            if (isHover && ImGui::IsMouseClicked((ImGuiMouseButton)b)) {
                ImVec2 cp = io.MouseClickedPos[b];
                LuaEngine::CanvasEvent ev; ev.type = "mousedown";
                ev.data["button"] = std::to_string(b);
                ev.data["x"] = std::to_string((cp.x - canvasScreenPos.x) * scaleX);
                ev.data["y"] = std::to_string((cp.y - canvasScreenPos.y) * scaleY);
                ev.data["ctrl"] = io.KeyCtrl ? "1" : "0";
                ev.data["shift"] = io.KeyShift ? "1" : "0";
                ev.data["alt"] = io.KeyAlt ? "1" : "0";
                ac.engine->callOnCanvasEvent(ev);
            }
            if (isHover && ImGui::IsMouseReleased((ImGuiMouseButton)b)) {
                LuaEngine::CanvasEvent ev; ev.type = "mouseup";
                ev.data["button"] = std::to_string(b);
                ev.data["x"] = std::to_string(relX);
                ev.data["y"] = std::to_string(relY);
                ac.engine->callOnCanvasEvent(ev);
            }
            if (isHover && ImGui::IsMouseDoubleClicked((ImGuiMouseButton)b)) {
                ImVec2 cp = io.MouseClickedPos[b];
                LuaEngine::CanvasEvent ev; ev.type = "doubleclick";
                ev.data["button"] = std::to_string(b);
                ev.data["x"] = std::to_string((cp.x - canvasScreenPos.x) * scaleX);
                ev.data["y"] = std::to_string((cp.y - canvasScreenPos.y) * scaleY);
                ac.engine->callOnCanvasEvent(ev);
            }
        }

        // Scroll events
        if (isHover && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f)) {
            LuaEngine::CanvasEvent ev; ev.type = "scroll";
            ev.data["dx"] = std::to_string(io.MouseWheelH);
            ev.data["dy"] = std::to_string(io.MouseWheel);
            ev.data["x"] = std::to_string(relX);
            ev.data["y"] = std::to_string(relY);
            ac.engine->callOnCanvasEvent(ev);
        }

        // Mousemove / hover
        if (isHover) {
            LuaEngine::CanvasEvent ev; ev.type = "mousemove";
            ev.data["x"] = std::to_string(relX);
            ev.data["y"] = std::to_string(relY);
            ev.data["left"] = io.MouseDown[0] ? "1" : "0";
            ev.data["right"] = io.MouseDown[1] ? "1" : "0";
            ev.data["middle"] = io.MouseDown[2] ? "1" : "0";
            ac.engine->callOnCanvasEvent(ev);
        }
    }
}

} // namespace Markdown
