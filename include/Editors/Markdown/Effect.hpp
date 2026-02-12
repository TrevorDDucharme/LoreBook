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
// Shader Sources - GLSL source code for an effect's shaders
// ────────────────────────────────────────────────────────────────────

struct ShaderSources {
    std::string vertex;
    std::string fragment;
    std::string geometry;  // Optional
    
    bool hasGeometry() const { return !geometry.empty(); }
    bool isValid() const { return !vertex.empty() && !fragment.empty(); }
};

// ────────────────────────────────────────────────────────────────────
// Kernel Sources - OpenCL source code for particle physics
// ────────────────────────────────────────────────────────────────────

struct KernelSources {
    std::string source;           // OpenCL source code
    std::string entryPoint;       // Kernel function name (e.g., "updateFire")
    std::string includePath;      // Path to common.cl include directory
    
    bool isValid() const { return !source.empty() && !entryPoint.empty(); }
};

// ────────────────────────────────────────────────────────────────────
// Post Process Pass - single post-processing pass definition
// ────────────────────────────────────────────────────────────────────

struct PostProcessPass {
    enum class BlendMode { Replace, Additive, Alpha };
    
    std::string name;             // For debugging
    std::string fragmentShader;   // Fragment shader source
    std::string vertexShader;     // Vertex shader source (optional, uses fullscreen quad)
    BlendMode blendMode = BlendMode::Replace;
    float downsampleFactor = 1.0f;  // 0.5 = half resolution
    int iterations = 1;             // For multi-pass blur
    std::unordered_map<std::string, float> uniforms;  // Uniform values
    
    bool isValid() const { return !fragmentShader.empty(); }
};

// ────────────────────────────────────────────────────────────────────
// Emission Config - particle emission parameters (for Effect interface)
// Named differently to avoid conflict with PreviewEffectSystem::EmissionConfig
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
    
    // ── Shader Sources ──
    // Return empty ShaderSources if not applicable
    virtual ShaderSources getGlyphShaderSources() const { return {}; }
    virtual ShaderSources getParticleShaderSources() const { return {}; }
    
    // ── Post-Processing ──
    virtual std::vector<PostProcessPass> getPostProcessPasses() const { return {}; }
    
    // ── Particle Physics ──
    virtual KernelSources getKernelSources() const { return {}; }
    virtual EffectEmissionConfig getEmissionConfig() const { return {}; }
    
    // ── Parameter Binding ──
    // Called before rendering glyphs with this effect
    virtual void uploadGlyphUniforms(GLuint shader, float time) const {}
    
    // Called before rendering particles for this effect
    virtual void uploadParticleUniforms(GLuint shader, float time) const {}
    
    // Called before dispatching particle kernel - set effect-specific args
    // Args 0-6 are already set (particles, collision, dt, scrollY, maskH, time, count)
    // Start binding at argIndex 7
    virtual void bindKernelParams(cl_kernel kernel, const KernelParams& params) const {}
    
    // Called for post-process passes
    virtual void uploadPostProcessUniforms(GLuint shader, int passIndex, float time) const {}
    
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
