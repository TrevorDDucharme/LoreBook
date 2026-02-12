#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <glm/glm.hpp>
#include <GL/glew.h>
#include <CL/cl.h>

namespace Markdown {

// ────────────────────────────────────────────────────────────────────
// Effect shader types
// ────────────────────────────────────────────────────────────────────

enum class EffectShaderType {
    None,           // default glyph shader
    Fire,           // vertex wave + fragment glow/distortion
    Rainbow,        // fragment hue cycling
    Shake,          // vertex noise offset
    Dissolve,       // fragment noise discard
    Glow,           // fragment additive bloom
    Wave,           // vertex sinusoidal displacement
    Glitch,         // fragment RGB split + scan lines
    Neon,           // fragment bright outline
    Blood,          // drip physics + viscous rendering
    Snow,           // particle accumulation
    Sparkle,        // particle emission
    Custom          // user-defined via Lua
};

// ────────────────────────────────────────────────────────────────────
// OpenCL Particle Kernel
// ────────────────────────────────────────────────────────────────────

struct ParticleKernel {
    cl_kernel kernel = nullptr;        // compiled OpenCL kernel
    cl_program program = nullptr;      // program owning the kernel
    std::string sourcePath;            // path to .cl file for hot-reload
    std::string entryPoint = "update"; // kernel function name
    
    // Kernel parameters (set as CL arguments)
    float gravity = 0.0f;
    float drag = 0.0f;
    float bounciness = 0.5f;
    float collisionThreshold = 0.5f;
    glm::vec4 color1 = {1,1,1,1};
    glm::vec4 color2 = {1,1,1,1};
    float speed = 1.0f;
    float turbulence = 0.0f;
    float heatRise = 0.0f;
    
    // Custom params buffer for user-defined kernels
    std::vector<float> customParams;
    
    ~ParticleKernel();
};

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
    EffectShaderType shaderType = EffectShaderType::None;
    GLuint customShaderProgram = 0;  // for Custom type
    
    // Shader parameters (uploaded as uniforms)
    glm::vec4 color1 = {1,1,1,1};
    glm::vec4 color2 = {1,1,1,1};
    float speed = 1.0f;
    float intensity = 1.0f;
    float scale = 1.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    
    // OpenCL particle physics (optional)
    ParticleKernel* particleKernel = nullptr;
    EmissionConfig emission;
    bool hasParticles = false;
    
    // Post-process pass (optional)
    GLuint postProcessShader = 0;
    
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
    
    // Register effect definitions
    void registerEffect(const std::string& name, EffectDef def);
    EffectDef* getEffect(const std::string& name);
    const std::unordered_map<std::string, EffectDef>& getEffects() const { return m_effects; }
    
    // Hot-reload from Lua/JSON
    void loadEffectsFromFile(const std::string& path);
    void reloadAll();
    
    // Batching: group glyphs by effect for efficient rendering
    void buildBatches(const std::vector<LayoutGlyph>& glyphs,
                      std::vector<EffectBatch>& outBatches);
    
    // Compile/cache shaders
    GLuint getShaderProgram(EffectShaderType type);
    
    // OpenCL particle kernel management
    ParticleKernel* loadParticleKernel(const std::string& clPath, const std::string& entry);
    void reloadKernel(ParticleKernel* kernel);
    void reloadKernel(const std::string& effectName);
    
    // Get built-in kernel names
    std::vector<std::string> listBuiltinKernels() const;
    
    // Upload effect uniforms to shader
    void uploadEffectUniforms(GLuint shader, const EffectDef* effect, float time);
    
    // Get combined shader program (vertex from one type, fragment from another)
    GLuint getCombinedShaderProgram(EffectShaderType vertType, EffectShaderType fragType);

private:
    void registerBuiltinEffects();
    void compileShader(EffectShaderType type);
    GLuint compileShaderProgram(const char* vertSrc, const char* fragSrc, const char* geomSrc = nullptr);
    
    // OpenCL context (borrowed from OpenCLContext singleton)
    cl_context m_clContext = nullptr;
    cl_device_id m_clDevice = nullptr;
    
    // Effect registry
    std::unordered_map<std::string, EffectDef> m_effects;
    
    // Shader cache (single type)
    std::unordered_map<EffectShaderType, GLuint> m_shaderCache;
    
    // Combined shader cache (vertex_type * 100 + frag_type)
    std::unordered_map<int, GLuint> m_combinedShaderCache;
    
    // Particle kernel cache
    std::unordered_map<std::string, std::unique_ptr<ParticleKernel>> m_kernelCache;
    
    bool m_initialized = false;
};

} // namespace Markdown
