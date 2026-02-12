#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <glm/glm.hpp>
#include <GL/glew.h>
#include <CL/cl.h>

namespace Markdown {

// Forward declaration of abstract Effect
class Effect;

// ────────────────────────────────────────────────────────────────────
// Particle Emission Configuration
// ────────────────────────────────────────────────────────────────────

struct EmissionConfig {
    enum Shape { Point, Line, Box, GlyphAlpha };
    Shape shape = GlyphAlpha;    // GlyphAlpha = emit from glyph shape
    float rate = 5.0f;           // particles per second
    glm::vec2 velocity = {0, -50};
    glm::vec2 velocityVar = {10, 20};
    float lifetime = 1.5f;
    float lifetimeVar = 0.5f;
    float size = 4.0f;
    float sizeVar = 2.0f;
    uint32_t meshID = 0;         // 0 = quad, else mesh index
};

// ────────────────────────────────────────────────────────────────────
// Effect Definition
// ────────────────────────────────────────────────────────────────────

struct EffectDef {
    std::string name;
    
    // Abstract Effect instance (owns shaders, kernels, params)
    Effect* effect = nullptr;
    GLuint effectGlyphShader = 0;      // compiled from Effect::getGlyphShaderSources()
    GLuint effectParticleShader = 0;   // compiled from Effect::getParticleShaderSources()
    cl_kernel effectKernel = nullptr;  // compiled from Effect::getKernelSources()
    cl_program effectProgram = nullptr;
    
    // Emission config (copied from Effect at registration)
    EmissionConfig emission;
    bool hasParticles = false;
    
    // Bloom effect from stack compositing (may differ from primary effect)
    Effect* bloomEffect = nullptr;
    
    // Composited effect stack (populated by LayoutEngine for stacked effects)
    std::vector<Effect*> effectStack;     // All effects in the stack (outer→inner)
    std::string stackSignature;            // Cache key, e.g. "glow+shake"
    
    // For data-driven hot-reload
    std::string sourceFile;
};

// ────────────────────────────────────────────────────────────────────
// Active Effect Instance
// ────────────────────────────────────────────────────────────────────

struct ActiveEffect {
    EffectDef* def = nullptr;
    size_t startOffset = 0;
    size_t endOffset = 0;
    float weight = 1.0f;  // for lerp blending
};

// ────────────────────────────────────────────────────────────────────
// Layout Glyph (output from layout engine)
// ────────────────────────────────────────────────────────────────────

struct LayoutGlyph {
    glm::vec3 pos;           // world position (x, y, z=0 for text)
    glm::vec2 size;
    glm::vec2 uvMin, uvMax;
    glm::vec4 color;
    EffectDef* effect = nullptr;
    size_t sourceOffset = 0;
};

// ────────────────────────────────────────────────────────────────────
// Glyph Vertex (GPU format)
// ────────────────────────────────────────────────────────────────────

struct GlyphVertex {
    glm::vec3 pos;      // 12 bytes
    glm::vec2 uv;       // 8 bytes
    glm::vec4 color;    // 16 bytes
    uint32_t effectID;  // 4 bytes
    float pad[3];       // 12 bytes — align to 52
};

// ────────────────────────────────────────────────────────────────────
// GPU Particle (must match OpenCL kernel struct layout)
// OpenCL float3 occupies 16 bytes (same as float4), so explicit
// padding is required after meshID and after each vec3 field.
// Total: 112 bytes, matching OpenCL struct alignment.
// ────────────────────────────────────────────────────────────────────

struct alignas(16) Particle {
    glm::vec2 pos;       // 8 bytes  — offset  0
    glm::vec2 vel;       // 8 bytes  — offset  8
    float z;             // 4 bytes  — offset 16
    float zVel;          // 4 bytes  — offset 20
    float life;          // 4 bytes  — offset 24
    float maxLife;       // 4 bytes  — offset 28
    glm::vec4 color;     // 16 bytes — offset 32
    float size;          // 4 bytes  — offset 48
    uint32_t meshID;     // 4 bytes  — offset 52
    float _pad0[2] = {}; // 8 bytes  — offset 56  (align rotation to 64)
    glm::vec3 rotation;  // 12 bytes — offset 64
    float _padRot = 0;   // 4 bytes  — offset 76  (CL float3 = 16 bytes)
    glm::vec3 rotVel;    // 12 bytes — offset 80
    float _padRotVel = 0;// 4 bytes  — offset 92  (CL float3 = 16 bytes)
    uint32_t behaviorID; // 4 bytes  — offset 96
    float _pad1[3] = {}; // 12 bytes — offset 100 (pad struct to 112)
};

// ────────────────────────────────────────────────────────────────────
// Effect Batch (for batched rendering)
// ────────────────────────────────────────────────────────────────────

struct EffectBatch {
    EffectDef* effect = nullptr;
    std::vector<GlyphVertex> vertices;
};

// ────────────────────────────────────────────────────────────────────
// Effect Stack (runtime state)
// ────────────────────────────────────────────────────────────────────

class EffectStack {
public:
    void push(EffectDef* effect, size_t sourceOffset);
    void pop(size_t sourceOffset);
    
    EffectDef* currentEffect() const;
    float currentWeight() const;
    
    /// Get the full active effect stack (bottom to top)
    const std::vector<ActiveEffect>& getStack() const { return m_stack; }
    size_t size() const { return m_stack.size(); }
    
    void clear();
    bool empty() const { return m_stack.empty(); }

private:
    std::vector<ActiveEffect> m_stack;
};

// ────────────────────────────────────────────────────────────────────
// PreviewEffectSystem - manages effects for 2.5D rendering
// ────────────────────────────────────────────────────────────────────

class PreviewEffectSystem {
public:
    PreviewEffectSystem();
    ~PreviewEffectSystem();
    
    // Initialize OpenCL and shaders
    bool init(cl_context clContext, cl_device_id clDevice);
    void cleanup();
    
    // Register Effect instances
    void registerEffect(Effect* effect);
    void registerEffectAs(const std::string& name, Effect* effect);
    
    // Look up effect by name
    EffectDef* getEffect(const std::string& name);
    const std::unordered_map<std::string, EffectDef>& getEffects() const { return m_effects; }
    
    // Hot-reload from Lua/JSON
    void loadEffectsFromFile(const std::string& path);
    void reloadAll();
    
    // Batching: group glyphs by effect for efficient rendering
    void buildBatches(const std::vector<LayoutGlyph>& glyphs,
                      std::vector<EffectBatch>& outBatches);
    
    // Get shader program for an Effect's glyph rendering (falls back to base)
    GLuint getGlyphShader(const EffectDef* def);
    
    // Get shader program for an Effect's particle rendering
    GLuint getParticleShader(const EffectDef* def);
    
    // Get OpenCL kernel for an Effect's particle physics
    cl_kernel getEffectKernel(const EffectDef* def);
    
    // Upload effect uniforms to shader (delegates to Effect::uploadGlyphUniforms)
    void uploadEffectUniforms(GLuint shader, const EffectDef* effect, float time);
    
    // Upload composite snippet uniforms for a stacked effect
    void uploadCompositeUniforms(GLuint shader, const EffectDef* effect, float time);
    
    // Get or compile a composite shader for a stack of effects
    GLuint getOrCompileCompositeShader(const std::vector<Effect*>& stack, const std::string& signature);
    
    // Get all active Effects that have particles
    std::vector<EffectDef*> getParticleEffects();

private:
    void registerBuiltinEffects();
    GLuint compileShaderProgram(const char* vertSrc, const char* fragSrc, const char* geomSrc = nullptr);
    
    /// Compile shaders and kernel for an Effect instance
    void compileEffectResources(EffectDef& def);
    
    /// Generate composite GLSL from a stack of effect snippets
    std::string generateCompositeVertexShader(const std::vector<Effect*>& stack);
    std::string generateCompositeFragmentShader(const std::vector<Effect*>& stack);
    
    // OpenCL context (borrowed from OpenCLContext singleton)
    cl_context m_clContext = nullptr;
    cl_device_id m_clDevice = nullptr;
    
    // Effect registry
    std::unordered_map<std::string, EffectDef> m_effects;
    
    // Owned Effect instances
    std::vector<std::unique_ptr<Effect>> m_ownedEffects;
    
    // Base glyph shader (for effects with no custom glyph shader)
    GLuint m_baseGlyphShader = 0;
    
    // Composite shader cache: stackSignature → compiled shader program
    std::unordered_map<std::string, GLuint> m_compositeShaderCache;
    
    bool m_initialized = false;
};

} // namespace Markdown
