#pragma once
#include <string>
#include <array>
#include <vector>
#include <memory>
#include <unordered_map>
#include <GL/glew.h>
#include <CL/cl.h>

namespace Markdown {

// ────────────────────────────────────────────────────────────────────
// Effect Capabilities - flags indicating what features an effect provides
// ────────────────────────────────────────────────────────────────────

struct EffectCapabilities {
    bool hasParticles = false;        // Has particle emission and physics
    bool hasGlyphShader = false;      // Custom text rendering shader
    bool hasParticleShader = false;   // Custom particle rendering shader
    bool hasPostProcess = false;      // Has post-processing passes
    bool contributesToBloom = false;  // Adds to bloom/glow source
};

// ────────────────────────────────────────────────────────────────────
// Emission Config - particle emission parameters (for Effect interface)
// ────────────────────────────────────────────────────────────────────

struct EffectEmissionConfig {
    enum class Shape { Point, Line, Box, GlyphAlpha, GlyphOutline, ScreenTop };
    
    Shape shape = Shape::GlyphAlpha;  // GlyphAlpha = emit from glyph shape
    float rate = 5.0f;                // Particles per second per glyph
    std::array<float, 2> velocity = {0, -50};    // Base velocity
    std::array<float, 2> velocityVar = {10, 20}; // Velocity variance
    float lifetime = 1.5f;            // Base lifetime
    float lifetimeVar = 0.5f;         // Lifetime variance
    float size = 4.0f;                // Base particle size
    float sizeVar = 2.0f;             // Size variance
    uint32_t meshID = 0;              // 0 = quad, else mesh index
};

// ────────────────────────────────────────────────────────────────────
// Kernel Parameters - common parameters passed to all particle kernels
// Effects can extend this with custom parameters via bindKernelParams()
// ────────────────────────────────────────────────────────────────────

struct KernelParams {
    cl_mem particleBuffer;    // Particle array
    cl_mem collisionImage;    // R8 collision mask
    float deltaTime;
    float scrollY;
    float maskHeight;
    float time;
    uint32_t particleCount;
};

// ════════════════════════════════════════════════════════════════════
// SNIPPET COMPOSITION SYSTEM
// All shader/kernel code expressed as composable snippets
// ════════════════════════════════════════════════════════════════════

// ────────────────────────────────────────────────────────────────────
// ShaderSnippet - a reusable fragment of GLSL code for one stage
// ────────────────────────────────────────────────────────────────────

struct ShaderSnippet {
    std::string uniformDecls;     // Namespaced uniform declarations
    std::string varyingDecls;     // Extra in/out varying declarations
    std::string helpers;          // Namespaced helper functions
    std::string code;             // Code injected into main() body

    bool hasCode() const { return !code.empty(); }
    bool empty() const { return uniformDecls.empty() && varyingDecls.empty()
                             && helpers.empty() && code.empty(); }
};

// ────────────────────────────────────────────────────────────────────
// GlyphSnippets - all shader stages for glyph (text) rendering
// ────────────────────────────────────────────────────────────────────

struct GlyphSnippets {
    ShaderSnippet vertex;         // Modifies `vec3 pos` in vertex main()
    ShaderSnippet fragment;       // Modifies `vec4 color`, has `float alpha`
    ShaderSnippet geometry;       // Geometry processing (optional)
    ShaderSnippet tessControl;    // Tessellation control (optional)
    ShaderSnippet tessEval;       // Tessellation evaluation (optional)

    bool empty() const {
        return vertex.empty() && fragment.empty() && geometry.empty()
            && tessControl.empty() && tessEval.empty();
    }
    bool hasGeometry() const { return geometry.hasCode(); }
    bool hasTessellation() const { return tessControl.hasCode() || tessEval.hasCode(); }
};

// ────────────────────────────────────────────────────────────────────
// ParticleSnippets - all shader stages for particle rendering
// ────────────────────────────────────────────────────────────────────

struct ParticleSnippets {
    ShaderSnippet vertex;         // Modifies particle vertex pass-through
    ShaderSnippet fragment;       // Modifies `float alpha`, `vec4 color`; has `float dist`, `vec2 uv`
    ShaderSnippet geometry;       // Modifies `vec4 color`, `float size`, `float life` before quad emit
    ShaderSnippet tessControl;    // Tessellation control (optional)
    ShaderSnippet tessEval;       // Tessellation evaluation (optional)

    bool empty() const {
        return vertex.empty() && fragment.empty() && geometry.empty()
            && tessControl.empty() && tessEval.empty();
    }
    bool hasGeometry() const { return geometry.hasCode(); }
    bool hasTessellation() const { return tessControl.hasCode() || tessEval.hasCode(); }
};

// ────────────────────────────────────────────────────────────────────
// KernelSnippet - composable OpenCL particle behavior
// ────────────────────────────────────────────────────────────────────

struct KernelSnippet {
    // Extra kernel args as comma-prefixed string
    // e.g. ",\n    const float2 gravity,\n    const float turbulence"
    std::string argDecls;

    // Helper functions (e.g. custom noise, color utils)
    std::string helpers;

    // Main behavior code — has access to: p (Particle), deltaTime, scrollY,
    // maskHeight, time, gid, rngState, newPos, p.vel, p.life, p.color, p.size
    std::string behaviorCode;

    // Custom collision response (optional).
    // Has access to: p, newPos, maskNorm (float2 doc-space normal), damping (float).
    // If empty, the default bounce is used: reflect + damping, newPos = p.pos + p.vel * deltaTime
    std::string collisionResponse;

    // Bounce damping factor used by the default collision response
    float defaultDamping = 0.3f;

    bool empty() const { return behaviorCode.empty(); }
};

// ────────────────────────────────────────────────────────────────────
// PostProcessSnippet - a single screen-space post-processing pass
// ────────────────────────────────────────────────────────────────────

struct PostProcessSnippet {
    enum class BlendMode { Replace, Additive, Alpha };

    std::string name;                 // For debugging / cache key
    ShaderSnippet fragment;           // Modifies `vec4 color`; has `vec2 uv`, `sampler2D uInputTex`
    BlendMode blendMode = BlendMode::Replace;
    float downsampleFactor = 1.0f;    // 0.5 = half resolution
    int iterations = 1;               // For multi-pass effects (e.g., blur)

    bool empty() const { return fragment.empty(); }
};

// SPHParams - per-fluid SPH parameters for effects that expose fluid behavior
struct SPHParams {
    float smoothingRadius = 5.0f;
    float restDensity = 1.0f;
    float stiffness = 150.0f;
    float viscosity = 6.0f;
    float cohesion = 0.3f;
    float particleMass = 1.0f;
};

// ────────────────────────────────────────────────────────────────────
// Effect - abstract base class for all text effects
// ────────────────────────────────────────────────────────────────────

class Effect {
public:
    virtual ~Effect() = default;
    
    // ── Identity ──
    virtual const char* getName() const = 0;
    virtual uint32_t getBehaviorID() const = 0;  // Unique ID for particle filtering
    virtual std::unique_ptr<Effect> clone() const = 0;  // Deep copy for per-span overrides
    
    // ── Capabilities ──
    virtual EffectCapabilities getCapabilities() const = 0;
    
    // ══════════════════════════════════════════════════════════════
    // SNIPPET API (composable, all stages)
    // ══════════════════════════════════════════════════════════════

    // ── Glyph (text) rendering snippets ──
    virtual GlyphSnippets getGlyphSnippets() const { return {}; }
    virtual void uploadGlyphSnippetUniforms(GLuint shader, float time) const {}

    // ── Particle rendering snippets ──
    virtual ParticleSnippets getParticleSnippets() const { return {}; }
    virtual void uploadParticleSnippetUniforms(GLuint shader, float time) const {}

    // ── Particle physics kernel snippet ──
    virtual KernelSnippet getKernelSnippet() const { return {}; }

    // Bind effect-specific kernel args starting at firstArgIndex.
    // Standard args (particles, collision, dt, scrollY, maskH, time, count)
    // are already bound at indices 0-6.
    virtual void bindKernelSnippetParams(cl_kernel kernel, const KernelParams& params,
                                         int firstArgIndex) const {}

    // ── Post-processing snippet passes ──
    virtual std::vector<PostProcessSnippet> getPostProcessSnippets() const { return {}; }
    virtual void uploadPostProcessSnippetUniforms(GLuint shader, int passIndex, float time) const {}

    // ── Particle emission config ──
    virtual EffectEmissionConfig getEmissionConfig() const { return {}; }
    
    // ── SPH / Fluid API ──
    // Effects that represent fluids should override these to expose SPH params
    virtual bool isFluid() const { return false; }
    virtual SPHParams getSPHParams() const { return SPHParams(); }
    
    // ── Effect Parameters ──
    // Common parameters that can be set at runtime
    std::array<float, 4> color1 = {1, 1, 1, 1};
    std::array<float, 4> color2 = {1, 1, 1, 1};
    float speed = 1.0f;
    float intensity = 1.0f;
    float scale = 1.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    
    // ── Compiled Resources (set by PreviewEffectSystem) ──
    GLuint glyphShader = 0;
    GLuint particleShader = 0;
    std::vector<GLuint> postProcessShaders;
    cl_program clProgram = nullptr;
    cl_kernel clKernel = nullptr;
    
    // ── Hot-reload support ──
    std::string sourceFile;  // Path to definition file for hot-reload
};

// ────────────────────────────────────────────────────────────────────
// EffectRegistry - singleton for effect type registration
// ────────────────────────────────────────────────────────────────────

using EffectFactory = std::unique_ptr<Effect>(*)();

class EffectRegistry {
public:
    static EffectRegistry& get();
    
    void registerFactory(const std::string& name, EffectFactory factory);
    std::unique_ptr<Effect> create(const std::string& name) const;
    std::vector<std::string> getRegisteredNames() const;
    
private:
    EffectRegistry() = default;
    std::unordered_map<std::string, EffectFactory> m_factories;
};

// Helper macro for registering effect types
#define REGISTER_EFFECT(ClassName) \
    namespace { \
        static bool _##ClassName##_registered = []() { \
            EffectRegistry::get().registerFactory(#ClassName, \
                []() -> std::unique_ptr<Effect> { return std::make_unique<ClassName>(); }); \
            return true; \
        }(); \
    }

} // namespace Markdown
