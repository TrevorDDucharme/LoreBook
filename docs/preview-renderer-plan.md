# 2.5D FBO Markdown Preview Renderer Plan

Build a 2.5D rendering system within `MarkdownEditor` where glyphs are quads in 3D world space with a fixed perspective camera. The entire scene renders in 3D — no compositing needed. **Particle physics runs on the GPU via OpenCL** — particles use 2D physics but render as 3D quads or meshes in the same scene. Custom particle behaviors are defined as OpenCL kernels, enabling massive parallelism and hot-reloadable physics. Collision detection uses the **alpha map of glyphs** (not bounding boxes) for pixel-accurate particle interaction via GL/CL texture interop.

A new `TextEffectSystem` manages a smart effect stack that batches all text sharing the same effect shader together, minimizing shader switches. Effects support per-glyph shaders, per-region shaders, post-process passes, and **custom OpenCL particle physics kernels**. The stack lerps between compatible effect behaviors for smooth transitions.

AST parsing lives in a separate `MarkdownDocument` class. This is a **fresh implementation** — no code ported from `MarkdownText.cpp`.

---

## Architecture

```
┌─ ImGui Frame ──────────────────────────────────────────────┐
│  MarkdownEditor::drawPreview()                             │
│    ┌─ 2.5D GL Scene (perspective camera, Z=0 doc plane) ─┐ │
│    │                                                      │ │
│    │  1. Parse: md4c → MarkdownDocument AST              │ │
│    │  2. Layout: AST → glyph quads with effect tags      │ │
│    │  3. Batch: group glyphs by effect shader            │ │
│    │                                                      │ │
│    │  4. Render collision mask (glyph alpha + bbox)      │ │
│    │     - Glyphs: sample font atlas alpha → R8 mask     │ │
│    │     - Interactive elements: solid bounding boxes    │ │
│    │     - glReadPixels → CPU buffer for particle physics│ │
│    │                                                      │ │
│    │  5. Render scene (batched by effect):               │ │
│    │     for each unique effect shader:                  │ │
│    │       bind shader once                              │ │
│    │       draw ALL glyphs using that effect             │ │
│    │     ModelViewer → render in-place (3D in scene)     │ │
│    │     LuaCanvas → render to texture → quad in scene   │ │
│    │     Images → textured quads in scene                │ │
│    │                                                      │ │
│    │  6. Update particles (OpenCL GPU physics)           │ │
│    │     - CL kernel samples collision texture directly  │ │
│    │     - Custom kernels for fire/snow/spark behaviors  │ │
│    │     - Render as quads or 3D meshes in same scene    │ │
│    │                                                      │ │
│    │  7. Post-process pass (optional per-effect)         │ │
│    └──────────────────────────────────────────────────────┘ │
│                                                            │
│    Display FBO via ImGui::Image()                          │
│    Overlay ImGui widgets on interactive element positions  │
│  EndChild()                                                │
└────────────────────────────────────────────────────────────┘
```

---

## Step 1: Create `MarkdownDocument` class

New files: `include/Editors/Markdown/MarkdownDocument.hpp`, `src/Editors/Markdown/MarkdownDocument.cpp`.

Owns the parsed AST using existing node types from `ProgramStructure/`:

```cpp
class MarkdownDocument {
public:
    void parseString(const std::string& markdown);
    void clear();
    
    bool isDirty() const;
    size_t sourceHash() const;  // for incremental update detection
    
    Document& getAST();
    const Document& getAST() const;
    
    // Ordered block traversal
    const std::vector<MarkdownBlock*>& getBlocks() const;

private:
    Document m_ast;
    size_t m_sourceHash = 0;
    bool m_dirty = false;
    
    // md4c callback state
    struct ParseState;
    static int enterBlock(MD_BLOCKTYPE, void*, void*);
    static int leaveBlock(MD_BLOCKTYPE, void*, void*);
    static int enterSpan(MD_SPANTYPE, void*, void*);
    static int leaveSpan(MD_SPANTYPE, void*, void*);
    static int textCallback(MD_TEXTTYPE, const MD_CHAR*, MD_SIZE, void*);
};
```

### Missing AST node types to add

Add to `include/Editors/Markdown/ProgramStructure/`:

- `Blockquote.hpp` — for `MD_BLOCK_QUOTE`
- `Table.hpp`, `TableRow.hpp`, `TableCell.hpp` — for table blocks
- `HorizontalRule.hpp` — for `MD_BLOCK_HR`
- `EffectSpan.hpp` — for text effect HTML tags (`<fire>`, `<shake>`, etc.)

---

## Step 2: Create new `TextEffectSystem`

New files: `include/Editors/Markdown/TextEffectSystem.hpp`, `src/Editors/Markdown/TextEffectSystem.cpp`.

### Core types

```cpp
// Identifies which shader to use
enum class EffectShaderType {
    None,           // default glyph shader
    Fire,           // vertex wave + fragment glow/distortion
    Rainbow,        // fragment hue cycling
    Shake,          // vertex noise offset
    Dissolve,       // fragment noise discard
    Glow,           // fragment additive bloom
    // ... extensible
    Custom          // user-defined via Lua
};

// OpenCL particle physics kernel reference
struct ParticleKernel {
    cl_kernel kernel = nullptr;        // compiled OpenCL kernel
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
    
    // Custom params buffer for user-defined kernels
    std::vector<float> customParams;
};

// Particle emission configuration (runs on CPU, spawns into GPU buffer)
struct EmissionConfig {
    enum Shape { Point, Line, Box, GlyphAlpha };  // GlyphAlpha = emit from glyph shape
    Shape shape = GlyphAlpha;
    float rate = 50.0f;           // particles per second
    glm::vec2 velocity = {0, -50}; // base velocity
    glm::vec2 velocityVar = {10, 20}; // velocity variance
    float lifetime = 1.0f;
    float lifetimeVar = 0.5f;
    float size = 4.0f;
    float sizeVar = 2.0f;
    const Mesh* mesh = nullptr;   // nullptr = quad, else 3D mesh
};

// Single effect definition
struct EffectDef {
    EffectShaderType shaderType = EffectShaderType::None;
    GLuint customShaderProgram = 0;  // for Custom type
    
    // Shader parameters (uploaded as uniforms)
    glm::vec4 color1 = {1,1,1,1};
    glm::vec4 color2 = {1,1,1,1};
    float speed = 1.0f;
    float intensity = 1.0f;
    float scale = 1.0f;
    
    // OpenCL particle physics (optional)
    ParticleKernel* particleKernel = nullptr;  // GPU physics kernel
    EmissionConfig emission;                    // particle spawning config
    
    // Post-process pass (optional)
    GLuint postProcessShader = 0;
    
    // For data-driven hot-reload
    std::string sourceFile;  // Lua/JSON path
};

// Runtime effect instance (tracks active region)
struct ActiveEffect {
    EffectDef* def;
    size_t startOffset;  // source byte offset
    size_t endOffset;
    float weight = 1.0f;  // for lerp blending
};
```

### Smart Effect Stack

The effect stack batches rendering to minimize shader switches:

```cpp
class EffectStack {
public:
    void push(EffectDef* effect, size_t sourceOffset);
    void pop(size_t sourceOffset);
    
    // Get the combined/blended effect for current position
    EffectDef* currentEffect() const;
    float currentWeight() const;
    
    // Lerp between two effects (for transitions)
    static EffectDef lerp(const EffectDef& a, const EffectDef& b, float t);

private:
    std::vector<ActiveEffect> m_stack;
};

class TextEffectSystem {
public:
    // Register effect definitions
    void registerEffect(const std::string& name, EffectDef def);
    EffectDef* getEffect(const std::string& name);
    
    // Hot-reload from Lua/JSON
    void loadEffectsFromFile(const std::string& path);
    void reloadAll();
    
    // Batching: group glyphs by effect for efficient rendering
    struct EffectBatch {
        EffectDef* effect;
        std::vector<GlyphVertex> vertices;  // all glyphs using this effect
        std::vector<Particle> particles;
    };
    
    // Build batches from laid-out glyphs
    void buildBatches(const std::vector<LayoutGlyph>& glyphs,
                      std::vector<EffectBatch>& outBatches);
    
    // Compile/cache shaders
    GLuint getShaderProgram(EffectShaderType type);
    
    // OpenCL particle system
    ParticleKernel* loadParticleKernel(const std::string& clPath, const std::string& entry);
    void reloadKernel(ParticleKernel* kernel);  // hot-reload

private:
    std::unordered_map<std::string, EffectDef> m_effects;
    std::unordered_map<EffectShaderType, GLuint> m_shaderCache;
    std::unordered_map<std::string, std::unique_ptr<ParticleKernel>> m_kernelCache;
};
```

### Effect batching strategy

During layout, each glyph is tagged with its active effect. Before rendering:

```
1. Collect all (glyph, effect) pairs from layout
2. Sort/group by effect shader type
3. For each unique shader:
   a. Bind shader ONCE
   b. Upload all glyph vertices for that effect
   c. Draw in one call
4. Result: O(num_effects) shader switches instead of O(num_glyphs)
```

---

## Step 3: 2.5D Rendering Infrastructure in `MarkdownEditor`

All rendering lives in `MarkdownEditor`. Add these members:

```cpp
class MarkdownEditor {
    // ... existing members ...
    
    // AST
    MarkdownDocument m_document;
    
    // Effect system
    TextEffectSystem m_effectSystem;
    
    // 2.5D scene FBO
    GLuint m_fbo = 0;
    GLuint m_colorTex = 0;       // GL_RGBA8
    GLuint m_depthTex = 0;       // GL_DEPTH24_STENCIL8
    int m_fboWidth = 0, m_fboHeight = 0;
    
    // Collision mask (alpha-based)
    GLuint m_collisionFBO = 0;
    GLuint m_collisionTex = 0;   // GL_R8 — stores font atlas alpha
    std::vector<uint8_t> m_collisionBuffer;  // CPU readback
    
    // 2.5D camera (fixed perspective looking at Z=0 document plane)
    glm::mat4 m_projection;
    glm::mat4 m_view;
    float m_cameraZ = 500.0f;    // distance from document plane
    float m_fovY = 45.0f;        // affects perspective distortion
    
    // Layout output
    struct LayoutGlyph {
        glm::vec3 pos;           // world position (x, y, z=0 for text)
        glm::vec2 size;
        glm::vec2 uvMin, uvMax;
        glm::vec4 color;
        EffectDef* effect;       // which effect applies (nullptr = default)
        size_t sourceOffset;     // byte offset in source
    };
    std::vector<LayoutGlyph> m_layoutGlyphs;
    
    // Interactive elements (for ImGui overlay)
    struct OverlayWidget {
        enum Type { Checkbox, Link, ModelViewer, LuaCanvas, WorldMap };
        Type type;
        glm::vec2 docPos;
        glm::vec2 size;
        size_t sourceOffset;
        std::string data;  // URL, script path, etc.
        bool checked = false;  // for checkboxes
    };
    std::vector<OverlayWidget> m_overlayWidgets;
    
    // Particle system
    struct Particle {
        glm::vec2 pos;           // 2D physics position
        glm::vec2 vel;           // 2D velocity
        float z;                 // depth for 3D rendering
        float zVel;              // z velocity (sparks fly toward camera)
        float life;
        float maxLife;
        glm::vec4 color;
        float size;
        uint32_t meshID;         // 0 = quad, else mesh index
        glm::vec3 rotation;      // for mesh particles
        glm::vec3 rotVel;
        uint32_t behaviorID;     // which kernel updates this particle
    };
    // GPU particle buffer (OpenCL managed)
    static constexpr size_t MAX_PARTICLES = 1000000;  // 1M particles max
    
    // OpenCL context (shared with existing OpenCLContext)
    cl_context m_clContext = nullptr;
    cl_command_queue m_clQueue = nullptr;
    cl_mem m_clParticleBuffer = nullptr;     // GPU particle array
    cl_mem m_clCollisionImage = nullptr;     // GL/CL shared collision texture
    size_t m_particleCount = 0;
    
    // Shaders
    GLuint m_baseGlyphShader = 0;
    GLuint m_collisionShader = 0;
    GLuint m_embedShader = 0;
    GLuint m_particleShader = 0;
    GLuint m_meshParticleShader = 0;
    
    // VAO/VBO
    GLuint m_glyphVAO = 0, m_glyphVBO = 0;
    GLuint m_particleVAO = 0, m_particleVBO = 0;
};
```

### Vertex formats

**Glyph vertex** (48 bytes):
```cpp
struct GlyphVertex {
    glm::vec3 pos;      // 12 bytes — world position
    glm::vec2 uv;       // 8 bytes
    glm::vec4 color;    // 16 bytes
    uint32_t effectID;  // 4 bytes — for shader selection
    float _pad[2];      // 8 bytes — align to 48
};
```

**Particle vertex** (for quad particles, 32 bytes):
```cpp
struct ParticleVertex {
    glm::vec3 pos;      // 12 bytes
    glm::vec2 uv;       // 8 bytes
    glm::vec4 color;    // 16 bytes (packed into 12 if needed)
};
```

---

## Step 4: Collision Mask (Alpha-Based)

The collision mask uses **glyph alpha values** for pixel-accurate collision, not bounding boxes. Interactive elements (images, embeds) use solid bounding boxes.

### Collision shader

```glsl
// collision.vert
#version 330 core
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
uniform mat4 uMVP;
out vec2 v_uv;
void main() {
    gl_Position = uMVP * vec4(in_pos, 1.0);
    v_uv = in_uv;
}

// collision.frag — outputs alpha from font atlas
#version 330 core
uniform sampler2D uFontAtlas;
in vec2 v_uv;
out float fragAlpha;
void main() {
    fragAlpha = texture(uFontAtlas, v_uv).a;
}
```

### Collision for interactive elements

For images, models, canvases — render solid white quads (alpha = 1.0) covering the bounding box:

```glsl
// collision_bbox.frag
#version 330 core
out float fragAlpha;
void main() {
    fragAlpha = 1.0;
}
```

### CollisionMask class

```cpp
class CollisionMask {
public:
    void resize(int w, int h);
    void readback();  // glReadPixels into CPU buffer (for debug/Lua only)
    
    // GL/CL interop — share collision texture with OpenCL
    void acquireForCL(cl_command_queue queue);  // glFinish + clEnqueueAcquireGLObjects
    void releaseFromCL(cl_command_queue queue); // clEnqueueReleaseGLObjects
    cl_mem getCLImage() const { return m_clImage; }
    
    // CPU sampling (for emission, debug)
    float sample(float x, float y) const;
    bool solid(float x, float y, float threshold = 0.5f) const {
        return sample(x, y) > threshold;
    }
    glm::vec2 surfaceNormal(float x, float y) const;
    void getEmissionPoints(const glm::vec2& min, const glm::vec2& max,
                           float threshold, std::vector<glm::vec2>& outPoints);

    GLuint getTexture() const { return m_tex; }
    int getWidth() const { return m_width; }
    int getHeight() const { return m_height; }

private:
    GLuint m_fbo = 0, m_tex = 0;
    cl_mem m_clImage = nullptr;  // CL image created from GL texture
    int m_width = 0, m_height = 0;
    std::vector<uint8_t> m_cpuBuffer;  // optional CPU readback
};
```

### Particle emission (CPU → GPU buffer)

Emission runs on CPU (needs access to glyph layout), spawns particles into GPU buffer:

```cpp
void ParticleEmitter::emit(const LayoutGlyph& glyph, const CollisionMask& mask,
                           const EmissionConfig& config, float dt,
                           cl_mem particleBuffer, size_t& particleCount,
                           cl_command_queue queue) {
    // Calculate how many particles to emit this frame
    float toEmit = config.rate * dt + m_emitAccum;
    int emitCount = (int)toEmit;
    m_emitAccum = toEmit - emitCount;
    
    if (emitCount <= 0) return;
    
    // Get emission points from glyph alpha
    std::vector<glm::vec2> points;
    if (config.shape == EmissionConfig::GlyphAlpha) {
        glm::vec2 min = {glyph.pos.x, glyph.pos.y};
        glm::vec2 max = {glyph.pos.x + glyph.size.x, glyph.pos.y + glyph.size.y * 0.3f};
        mask.getEmissionPoints(min, max, 0.1f, points);
    }
    
    // Build particles on CPU
    std::vector<Particle> newParticles;
    for (int i = 0; i < emitCount && !points.empty(); ++i) {
        glm::vec2 p = points[rand() % points.size()];
        Particle part;
        part.pos = p;
        part.vel = config.velocity + glm::vec2(randRange(-config.velocityVar.x, config.velocityVar.x),
                                                randRange(-config.velocityVar.y, config.velocityVar.y));
        part.z = randRange(-5, 5);
        part.zVel = 0;
        part.life = config.lifetime + randRange(-config.lifetimeVar, config.lifetimeVar);
        part.maxLife = part.life;
        part.size = config.size + randRange(-config.sizeVar, config.sizeVar);
        part.meshID = config.mesh ? config.mesh->getID() : 0;
        part.behaviorID = m_behaviorID;
        newParticles.push_back(part);
    }
    
    // Append to GPU buffer
    if (!newParticles.empty()) {
        size_t offset = particleCount * sizeof(Particle);
        clEnqueueWriteBuffer(queue, particleBuffer, CL_FALSE, offset,
                             newParticles.size() * sizeof(Particle),
                             newParticles.data(), 0, nullptr, nullptr);
        particleCount += newParticles.size();
    }
}
```

---

## Step 5: OpenCL GPU Particle Physics

All particle physics runs on the GPU via OpenCL kernels. Each effect can define a custom kernel for unique behavior.

### OpenCL infrastructure in MarkdownEditor

```cpp
void MarkdownEditor::initOpenCL() {
    // Get context from existing OpenCLContext singleton
    m_clContext = OpenCLContext::get().getContext();
    m_clQueue = OpenCLContext::get().getQueue();
    
    // Create particle buffer (large enough for MAX_PARTICLES)
    m_clParticleBuffer = clCreateBuffer(m_clContext, CL_MEM_READ_WRITE,
                                         MAX_PARTICLES * sizeof(Particle),
                                         nullptr, nullptr);
    
    // Load built-in kernels
    m_effectSystem.loadParticleKernel("shaders/particles/fire.cl", "update_fire");
    m_effectSystem.loadParticleKernel("shaders/particles/snow.cl", "update_snow");
    m_effectSystem.loadParticleKernel("shaders/particles/spark.cl", "update_spark");
    m_effectSystem.loadParticleKernel("shaders/particles/drip.cl", "update_drip");
}

void MarkdownEditor::setupCollisionCLImage() {
    // Create CL image from GL collision texture (GL/CL interop)
    cl_int err;
    m_clCollisionImage = clCreateFromGLTexture(m_clContext, CL_MEM_READ_ONLY,
                                                GL_TEXTURE_2D, 0,
                                                m_collisionMask.getTexture(), &err);
}
```

### Built-in OpenCL particle kernel (fire.cl)

```opencl
// fire.cl - Fire particle physics
__kernel void update_fire(
    __global Particle* particles,
    __read_only image2d_t collisionMask,
    const uint particleCount,
    const float dt,
    const float gravity,
    const float drag,
    const float turbulence,
    const float heatRise
) {
    uint gid = get_global_id(0);
    if (gid >= particleCount) return;
    
    __global Particle* p = &particles[gid];
    if (p->life <= 0) return;
    
    // Save old position
    float2 oldPos = p->pos;
    
    // Apply forces
    p->vel.y -= heatRise * dt;  // fire rises
    p->vel.x += (noise1(p->pos.x * 0.1f + p->life) - 0.5f) * turbulence * dt;
    p->vel *= (1.0f - drag * dt);
    
    // Update position
    p->pos += p->vel * dt;
    p->z += p->zVel * dt;
    
    // Sample collision mask
    const sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP | CLK_FILTER_LINEAR;
    float alpha = read_imagef(collisionMask, sampler, (int2)(p->pos.x, p->pos.y)).x;
    
    if (alpha > 0.5f) {
        // Hit something! Deflect laterally
        // Sample neighbors to find surface normal
        float alphaL = read_imagef(collisionMask, sampler, (int2)(p->pos.x - 1, p->pos.y)).x;
        float alphaR = read_imagef(collisionMask, sampler, (int2)(p->pos.x + 1, p->pos.y)).x;
        float alphaU = read_imagef(collisionMask, sampler, (int2)(p->pos.x, p->pos.y - 1)).x;
        float alphaD = read_imagef(collisionMask, sampler, (int2)(p->pos.x, p->pos.y + 1)).x;
        
        float2 normal = normalize((float2)(alphaL - alphaR, alphaU - alphaD));
        
        // Reflect velocity with some damping
        p->vel = reflect(p->vel, normal) * 0.5f;
        p->pos = oldPos;  // revert position
        
        // Add some lateral turbulence
        p->vel.x += (noise1(p->life * 10.0f) - 0.5f) * 20.0f;
    }
    
    // Age and fade
    p->life -= dt;
    float t = 1.0f - (p->life / p->maxLife);
    p->color.w = 1.0f - t;  // fade out
    p->color.xyz = mix(p->color1.xyz, p->color2.xyz, t);  // color shift
}
```

### Snow particle kernel (snow.cl)

```opencl
__kernel void update_snow(
    __global Particle* particles,
    __read_only image2d_t collisionMask,
    const uint particleCount,
    const float dt,
    const float gravity,
    const float windX,
    const float flutter
) {
    uint gid = get_global_id(0);
    if (gid >= particleCount) return;
    
    __global Particle* p = &particles[gid];
    if (p->life <= 0) return;
    
    float2 oldPos = p->pos;
    
    // Gentle falling + flutter
    p->vel.y += gravity * dt;
    p->vel.x = windX + sin(p->life * 5.0f + p->pos.y * 0.1f) * flutter;
    p->pos += p->vel * dt;
    
    // Collision: snow accumulates on top surfaces
    const sampler_t sampler = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP | CLK_FILTER_LINEAR;
    float alpha = read_imagef(collisionMask, sampler, (int2)(p->pos.x, p->pos.y)).x;
    float alphaAbove = read_imagef(collisionMask, sampler, (int2)(p->pos.x, p->pos.y - 2)).x;
    
    if (alpha > 0.5f && alphaAbove < 0.3f) {
        // Landed on top of something — stick and die slowly
        p->pos = oldPos;
        p->vel = (float2)(0, 0);
        p->life -= dt * 0.1f;  // slow melt
    } else if (alpha > 0.5f) {
        // Hit side of something — slide down
        p->pos = oldPos;
        p->vel.x *= 0.5f;
    }
    
    p->life -= dt;
}
```

### Custom kernel loading

Users can provide custom `.cl` files for unique particle behaviors:

```cpp
ParticleKernel* TextEffectSystem::loadParticleKernel(const std::string& clPath,
                                                      const std::string& entry) {
    auto& kernel = m_kernelCache[clPath + ":" + entry];
    if (!kernel) {
        kernel = std::make_unique<ParticleKernel>();
        kernel->sourcePath = clPath;
        kernel->entryPoint = entry;
        
        // Load and compile .cl source
        std::string source = readFile(clPath);
        const char* src = source.c_str();
        size_t len = source.size();
        
        cl_int err;
        cl_program program = clCreateProgramWithSource(m_clContext, 1, &src, &len, &err);
        clBuildProgram(program, 0, nullptr, "-cl-fast-relaxed-math", nullptr, nullptr);
        kernel->kernel = clCreateKernel(program, entry.c_str(), &err);
        clReleaseProgram(program);
    }
    return kernel.get();
}

void TextEffectSystem::reloadKernel(ParticleKernel* kernel) {
    if (kernel->kernel) clReleaseKernel(kernel->kernel);
    // Recompile from sourcePath
    std::string source = readFile(kernel->sourcePath);
    // ... same as above
}
```

### GPU particle update dispatch

```cpp
void MarkdownEditor::updateParticlesGPU(float dt) {
    if (m_particleCount == 0) return;
    
    // Acquire collision texture for CL
    m_collisionMask.acquireForCL(m_clQueue);
    
    // Group particles by behavior and dispatch kernels
    // (In practice, you'd sort particles by behaviorID or use indirect dispatch)
    for (auto& [name, effect] : m_effectSystem.getEffects()) {
        if (!effect.particleKernel) continue;
        
        cl_kernel k = effect.particleKernel->kernel;
        
        // Set kernel arguments
        clSetKernelArg(k, 0, sizeof(cl_mem), &m_clParticleBuffer);
        clSetKernelArg(k, 1, sizeof(cl_mem), &m_clCollisionImage);
        clSetKernelArg(k, 2, sizeof(uint), &m_particleCount);
        clSetKernelArg(k, 3, sizeof(float), &dt);
        clSetKernelArg(k, 4, sizeof(float), &effect.particleKernel->gravity);
        clSetKernelArg(k, 5, sizeof(float), &effect.particleKernel->drag);
        // ... additional params
        
        // Dispatch
        size_t globalSize = m_particleCount;
        clEnqueueNDRangeKernel(m_clQueue, k, 1, nullptr, &globalSize, nullptr, 0, nullptr, nullptr);
    }
    
    // Release collision texture
    m_collisionMask.releaseFromCL(m_clQueue);
    
    // Wait for kernels to complete before rendering
    clFinish(m_clQueue);
}
```

The layout engine walks the `MarkdownDocument` AST and produces `LayoutGlyph` entries.

```cpp
class LayoutEngine {
public:
    void layout(const MarkdownDocument& doc, float wrapWidth,
                std::vector<LayoutGlyph>& outGlyphs,
                std::vector<OverlayWidget>& outWidgets);

private:
    // Cursor state
    float m_curX = 0, m_curY = 0;
    float m_indentX = 0;
    float m_wrapWidth = 0;
    
    // Font state
    ImFont* m_font = nullptr;
    float m_scale = 1.0f;
    glm::vec4 m_color = {1,1,1,1};
    
    // Effect state
    EffectStack m_effectStack;
    
    void layoutBlock(MarkdownBlock* block);
    void layoutHeading(Heading* h);
    void layoutParagraph(Paragraph* p);
    void layoutList(List* list);
    void layoutCodeBlock(CodeBlock* cb);
    void layoutImage(Image* img);
    // ... etc
    
    void layoutText(const std::string& text, size_t sourceOffset);
    void emitGlyph(uint32_t codepoint, size_t sourceOffset);
    void lineBreak();
};
```

### Block layout rules

| Block | Action |
|---|---|
| **Heading** | Push bold font, scale = `1.4 - (level-1)*0.1`, after: pop, add vertical space |
| **Paragraph** | Set wrap width, after: add vertical space |
| **List** | Increase `m_indentX` by `fontSize * 1.5` |
| **ListItem** | Emit bullet/number glyphs, then layout children |
| **CodeBlock** | Emit background quad, push mono font, no wrap |
| **Blockquote** | Increase indent, tint color grey |
| **Image** | Record `OverlayWidget`, advance `m_curY` past image height |
| **EffectSpan** | Push/pop effect on `m_effectStack` |

### Text layout with word wrap

```cpp
void LayoutEngine::layoutText(const std::string& text, size_t sourceOffset) {
    const char* p = text.c_str();
    const char* end = p + text.size();
    
    while (p < end) {
        // Find wrap break point
        const char* breakPos = m_font->CalcWordWrapPositionA(
            m_scale, p, end, m_wrapWidth - (m_curX - m_indentX));
        
        // Emit glyphs from p to breakPos
        const char* c = p;
        while (c < breakPos) {
            uint32_t codepoint;
            c += decodeUTF8(c, &codepoint);
            emitGlyph(codepoint, sourceOffset + (c - text.c_str()));
        }
        
        // Line break if we haven't reached the end
        if (breakPos < end) {
            lineBreak();
        }
        p = breakPos;
    }
}

void LayoutEngine::emitGlyph(uint32_t codepoint, size_t sourceOffset) {
    const ImFontGlyph* g = m_font->FindGlyph(codepoint);
    if (!g) return;
    
    LayoutGlyph lg;
    lg.pos = {m_curX + g->X0 * m_scale, m_curY + g->Y0 * m_scale, 0.0f};
    lg.size = {(g->X1 - g->X0) * m_scale, (g->Y1 - g->Y0) * m_scale};
    lg.uvMin = {g->U0, g->V0};
    lg.uvMax = {g->U1, g->V1};
    lg.color = m_color;
    lg.effect = m_effectStack.currentEffect();
    lg.sourceOffset = sourceOffset;
    
    m_layoutGlyphs.push_back(lg);
    m_curX += g->AdvanceX * m_scale;
}
```

---

## Step 6: Render Pipeline in `drawPreview()`

```cpp
ImVec2 MarkdownEditor::drawPreview() {
    float dt = ImGui::GetIO().DeltaTime;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    
    // 1. Parse if dirty
    if (m_document.isDirty()) {
        m_document.parseString(getCurrentContent());
    }
    
    // 2. Layout
    m_layoutGlyphs.clear();
    m_overlayWidgets.clear();
    LayoutEngine layout;
    layout.layout(m_document, avail.x, m_layoutGlyphs, m_overlayWidgets);
    
    // 3. Batch glyphs by effect
    std::vector<TextEffectSystem::EffectBatch> batches;
    m_effectSystem.buildBatches(m_layoutGlyphs, batches);
    
    // 4. Resize FBOs if needed
    ensureFBO((int)avail.x, (int)avail.y);
    
    // 5. Save GL state
    GLint prevFBO; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    // ... save viewport, blend, depth, etc.
    
    // 6. Render collision mask (alpha-based)
    renderCollisionMask(batches);
    m_collisionMask.readback();
    
    // 7. Setup 2.5D camera
    float aspect = avail.x / avail.y;
    m_projection = glm::perspective(glm::radians(m_fovY), aspect, 0.1f, 2000.0f);
    m_view = glm::lookAt(
        glm::vec3(avail.x/2, avail.y/2, m_cameraZ),  // eye
        glm::vec3(avail.x/2, avail.y/2, 0),          // center (doc plane)
        glm::vec3(0, -1, 0)                          // up (Y down in doc space)
    );
    glm::mat4 mvp = m_projection * m_view;
    
    // 8. Render scene to FBO
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_fboWidth, m_fboHeight);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // 9. Render batched glyphs (one shader bind per effect type)
    for (auto& batch : batches) {
        GLuint shader = m_effectSystem.getShaderProgram(batch.effect->shaderType);
        glUseProgram(shader);
        glUniformMatrix4fv(glGetUniformLocation(shader, "uMVP"), 1, GL_FALSE, &mvp[0][0]);
        glUniform1f(glGetUniformLocation(shader, "uTime"), (float)glfwGetTime());
        uploadEffectUniforms(shader, batch.effect);
        
        // Upload and draw all glyphs for this effect
        uploadGlyphBatch(batch.vertices);
        glBindVertexArray(m_glyphVAO);
        glDrawArrays(GL_TRIANGLES, 0, batch.vertices.size());
    }
    
    // 10. Render embedded content (images, models, canvases)
    renderEmbeddedContent(mvp);
    
    // 11. Emit new particles (CPU → GPU buffer)
    emitParticles(dt, batches);
    
    // 12. Update particles on GPU (OpenCL)
    updateParticlesGPU(dt);
    
    // 13. Render particles (3D in same scene)
    renderParticlesFromGPU(mvp);
    
    // 14. Post-process passes (per-effect, optional)
    for (auto& batch : batches) {
        if (batch.effect->postProcessShader) {
            applyPostProcess(batch.effect->postProcessShader);
        }
    }
    
    // 15. Restore GL state
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    // ... restore viewport, blend, depth, etc.
    
    // 16. Display via ImGui
    ImGui::Image((ImTextureID)(intptr_t)m_colorTex, avail,
                 ImVec2(0, 1), ImVec2(1, 0));  // flip Y
    
    // 17. Overlay ImGui widgets for interactive elements
    ImVec2 origin = ImGui::GetItemRectMin();
    float scrollY = ImGui::GetScrollY();
    for (auto& w : m_overlayWidgets) {
        ImVec2 screenPos(origin.x + w.docPos.x, origin.y + w.docPos.y - scrollY);
        renderOverlayWidget(w, screenPos);
    }
    
    return avail;
}
```

---

## Step 8: GPU Particle Rendering

Particles are stored in an OpenCL buffer and rendered using CL/GL interop — no CPU readback needed for rendering.

### CL/GL buffer sharing for rendering

```cpp
void MarkdownEditor::initParticleRendering() {
    // Create GL buffer for particle data
    glGenBuffers(1, &m_particleVBO);
    glBindBuffer(GL_ARRAY_BUFFER, m_particleVBO);
    glBufferData(GL_ARRAY_BUFFER, MAX_PARTICLES * sizeof(Particle), nullptr, GL_DYNAMIC_DRAW);
    
    // Create CL buffer from GL buffer (shared memory)
    cl_int err;
    m_clParticleBuffer = clCreateFromGLBuffer(m_clContext, CL_MEM_READ_WRITE,
                                               m_particleVBO, &err);
    
    // Setup VAO
    glGenVertexArrays(1, &m_particleVAO);
    glBindVertexArray(m_particleVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_particleVBO);
    
    // Particle struct layout for rendering
    // pos (vec2), vel (vec2), z, zVel, life, maxLife, color (vec4), size, meshID, rotation (vec3), rotVel (vec3), behaviorID
    glEnableVertexAttribArray(0);  // pos.xy
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, pos));
    glEnableVertexAttribArray(1);  // z
    glVertexAttribPointer(1, 1, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, z));
    glEnableVertexAttribArray(2);  // color
    glVertexAttribPointer(2, 4, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, color));
    glEnableVertexAttribArray(3);  // size
    glVertexAttribPointer(3, 1, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, size));
    glEnableVertexAttribArray(4);  // life (for alpha fade)
    glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, sizeof(Particle), (void*)offsetof(Particle, life));
}
```

### Rendering particles directly from GPU buffer

```cpp
void MarkdownEditor::renderParticlesFromGPU(const glm::mat4& mvp) {
    if (m_particleCount == 0) return;
    
    // Release CL's hold on the buffer so GL can read it
    clEnqueueReleaseGLObjects(m_clQueue, 1, &m_clParticleBuffer, 0, nullptr, nullptr);
    clFinish(m_clQueue);
    
    // Render quad particles using geometry shader to expand points to quads
    glUseProgram(m_particleShader);
    glUniformMatrix4fv(glGetUniformLocation(m_particleShader, "uMVP"), 1, GL_FALSE, &mvp[0][0]);
    glUniform1f(glGetUniformLocation(m_particleShader, "uTime"), (float)glfwGetTime());
    
    glBindVertexArray(m_particleVAO);
    glDrawArrays(GL_POINTS, 0, m_particleCount);  // geometry shader expands to quads
    
    // For mesh particles: use compute shader to build instance transforms,
    // then draw instanced meshes
    renderMeshParticles(mvp);
    
    // Re-acquire buffer for CL
    clEnqueueAcquireGLObjects(m_clQueue, 1, &m_clParticleBuffer, 0, nullptr, nullptr);
}
```

### Particle geometry shader (point → quad)

```glsl
// particle.geom
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
    if (v_life[0] <= 0) return;  // skip dead particles
    
    vec3 pos = vec3(gl_in[0].gl_Position.xy, v_z[0]);
    float size = v_size[0];
    vec4 color = v_color[0];
    
    // Billboard quad facing camera
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
```

### Mesh particle rendering with compute shader

For 3D mesh particles, use a compute shader to build instance transforms:

```glsl
// build_mesh_instances.comp
#version 430 core
layout(local_size_x = 256) in;

struct Particle {
    vec2 pos;
    vec2 vel;
    float z;
    float zVel;
    float life;
    float maxLife;
    vec4 color;
    float size;
    uint meshID;
    vec3 rotation;
    vec3 rotVel;
    uint behaviorID;
};

layout(std430, binding = 0) readonly buffer Particles { Particle particles[]; };
layout(std430, binding = 1) writeonly buffer Transforms { mat4 transforms[]; };
layout(std430, binding = 2) buffer Counter { uint count; };

uniform uint particleCount;
uniform uint targetMeshID;

void main() {
    uint gid = gl_GlobalInvocationID.x;
    if (gid >= particleCount) return;
    
    Particle p = particles[gid];
    if (p.life <= 0 || p.meshID != targetMeshID) return;
    
    // Build transform matrix
    mat4 t = mat4(1.0);
    t[3] = vec4(p.pos.x, p.pos.y, p.z, 1.0);
    // Apply rotation...
    // Apply scale...
    
    uint idx = atomicAdd(count, 1);
    transforms[idx] = t;
}
```

### Particle compaction (remove dead particles on GPU)

Periodically compact the particle buffer to remove dead particles:

```opencl
// compact_particles.cl
__kernel void compact(
    __global Particle* particles,
    __global Particle* compacted,
    __global uint* aliveCount,
    const uint particleCount
) {
    uint gid = get_global_id(0);
    if (gid >= particleCount) return;
    
    Particle p = particles[gid];
    if (p.life > 0) {
        uint idx = atomic_inc(aliveCount);
        compacted[idx] = p;
    }
}
```

---

## Step 9: Effect Shaders

### Base glyph shader (no effect)

```glsl
// base_glyph.vert
#version 330 core
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
uniform mat4 uMVP;
out vec2 v_uv;
out vec4 v_color;
void main() {
    gl_Position = uMVP * vec4(in_pos, 1.0);
    v_uv = in_uv;
    v_color = in_color;
}

// base_glyph.frag
#version 330 core
uniform sampler2D uFontAtlas;
in vec2 v_uv;
in vec4 v_color;
out vec4 fragColor;
void main() {
    float alpha = texture(uFontAtlas, v_uv).a;
    fragColor = vec4(v_color.rgb, v_color.a * alpha);
}
```

### Fire effect shader

```glsl
// fire.vert — vertex displacement
#version 330 core
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
uniform mat4 uMVP;
uniform float uTime;
uniform float uIntensity;
out vec2 v_uv;
out vec4 v_color;
out float v_heat;

void main() {
    vec3 pos = in_pos;
    
    // Wave displacement (stronger at top of glyph)
    float heightFactor = 1.0 - in_uv.y;  // 0 at bottom, 1 at top
    float wave = sin(in_pos.x * 0.1 + uTime * 5.0) * heightFactor * uIntensity * 3.0;
    pos.x += wave;
    pos.y += sin(uTime * 7.0 + in_pos.x * 0.2) * heightFactor * uIntensity;
    
    gl_Position = uMVP * vec4(pos, 1.0);
    v_uv = in_uv;
    v_color = in_color;
    v_heat = heightFactor;
}

// fire.frag — glow + heat distortion
#version 330 core
uniform sampler2D uFontAtlas;
uniform float uTime;
uniform vec4 uColor1;  // inner fire color
uniform vec4 uColor2;  // outer fire color
in vec2 v_uv;
in vec4 v_color;
in float v_heat;
out vec4 fragColor;

void main() {
    float alpha = texture(uFontAtlas, v_uv).a;
    
    // Color gradient based on heat
    vec3 fireColor = mix(uColor1.rgb, uColor2.rgb, v_heat);
    
    // Glow effect (expand alpha)
    float glow = smoothstep(0.0, 0.5, alpha) * (1.0 + v_heat * 0.5);
    
    fragColor = vec4(fireColor, alpha * v_color.a * glow);
}
```

### Rainbow effect shader

```glsl
// rainbow.frag
#version 330 core
uniform sampler2D uFontAtlas;
uniform float uTime;
uniform float uSpeed;
in vec2 v_uv;
in vec4 v_color;
in vec3 v_worldPos;  // need to pass world pos from vertex shader
out vec4 fragColor;

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    float alpha = texture(uFontAtlas, v_uv).a;
    
    // Hue cycles based on x position and time
    float hue = fract(v_worldPos.x * 0.01 + uTime * uSpeed);
    vec3 rainbow = hsv2rgb(vec3(hue, 0.8, 1.0));
    
    fragColor = vec4(rainbow, alpha * v_color.a);
}
```

---

## Step 10: Expose to Lua

In Lua bindings:

```lua
-- Define a new effect with GPU-accelerated particles
effect.define("my_fire", {
    shader = "fire",  -- built-in or path to custom shader
    color1 = {1.0, 0.3, 0.0, 1.0},
    color2 = {1.0, 0.8, 0.0, 1.0},
    speed = 1.5,
    intensity = 0.8,
    
    -- GPU particle system with OpenCL kernel
    particles = {
        kernel = "fire",               -- built-in kernel: "fire", "snow", "sparkle"
        -- kernel = "vault://Effects/custom.cl",  -- custom kernel from vault
        emitRate = 50,
        lifetime = {0.5, 1.5},
        size = {2, 8},
        gravity = {0, -200},           -- initial velocity modifier
        color = {1.0, 0.5, 0.1, 1.0},  -- base particle color
        -- mesh = "vault://Assets/flame.glb",  -- optional 3D mesh particles
    },
    
    postProcess = nil,  -- or path to post-process shader
})

-- Define effect with custom OpenCL particle kernel
effect.define("magic_sparkles", {
    shader = "sparkle",
    particles = {
        kernel = "vault://Effects/magic_sparkle.cl",  -- custom .cl file
        emitRate = 100,
        lifetime = {1.0, 3.0},
        size = {1, 4},
    },
})

-- Use in markdown: <my_fire>burning text</my_fire>
-- Or: <magic_sparkles>enchanted words</magic_sparkles>

-- Sample collision from Lua canvas (reads from GPU texture)
local alpha = collision.sample(x, y)  -- 0.0–1.0 (readback cached)
local solid = collision.solid(x, y)   -- bool (alpha > 0.5)
local nx, ny = collision.normal(x, y) -- surface normal

-- Get collision texture for GPU sampling (CL/GL interop)
local tex = collision.getTexture()    -- GL texture ID
local w, h = collision.getSize()

-- Hot-reload a particle kernel at runtime
effect.reloadKernel("my_fire")

-- Query available built-in kernels
local kernels = effect.listBuiltinKernels()  -- {"fire", "snow", "sparkle", ...}
```

### Writing custom OpenCL kernels

Custom kernels must follow this signature:

```opencl
// vault://Effects/custom.cl
__kernel void update(
    __global Particle* particles,
    __read_only image2d_t collisionMask,  // sample alpha for collision
    const float dt,
    const uint particleCount,
    const float time
) {
    uint gid = get_global_id(0);
    if (gid >= particleCount) return;
    
    Particle p = particles[gid];
    if (p.life <= 0) return;
    
    // Your custom physics here...
    
    // Sample collision mask
    sampler_t smp = CLK_NORMALIZED_COORDS_TRUE | CLK_FILTER_LINEAR;
    float2 uv = (float2)(p.pos.x / WIDTH, p.pos.y / HEIGHT);  // normalize
    float alpha = read_imagef(collisionMask, smp, uv).w;
    
    // Write back
    particles[gid] = p;
}
```

The `Particle` struct is automatically provided:

```opencl
typedef struct {
    float2 pos;     // XY position
    float2 vel;     // XY velocity
    float z;        // Z depth
    float zVel;     // Z velocity
    float life;     // remaining life
    float maxLife;  // initial life
    float4 color;   // RGBA
    float size;     // render size
    uint meshID;    // 0 = quad, >0 = mesh index
    float3 rotation;
    float3 rotVel;
    uint behaviorID;  // identifies which kernel owns this particle
} Particle;
```

---

## Advanced Effect Examples

The system is flexible enough to support vastly different approaches to the same visual goal. Here's how a "blood" dripping effect could be implemented two completely different ways:

### Approach A: Per-Pixel Fluid Simulation

A full GPU fluid sim with viscosity, surface tension, and proper dripping behavior:

```lua
-- blood_fluid.lua
effect.define("blood_fluid", {
    shader = "fluid_render",  -- renders fluid density field
    color1 = {0.5, 0.0, 0.0, 1.0},  -- dark blood
    color2 = {0.8, 0.1, 0.1, 1.0},  -- bright blood
    
    -- This effect uses a density field instead of particles
    fluidSim = {
        kernel = "vault://Effects/blood_fluid.cl",
        gridSize = {256, 256},       -- simulation grid resolution
        viscosity = 0.15,            -- thick, sticky blood
        surfaceTension = 0.8,
        gravity = {0, 300},
        emitFromGlyphs = true,       -- spawn fluid at glyph edges
        emitRate = 0.5,              -- density units per second per glyph
    },
})
```

```opencl
// vault://Effects/blood_fluid.cl
// Navier-Stokes fluid simulation with viscosity

__kernel void advect(
    __global float* density,
    __global float* densityNew,
    __global float2* velocity,
    __read_only image2d_t collisionMask,
    const float dt,
    const float viscosity
) {
    int2 pos = (int2)(get_global_id(0), get_global_id(1));
    int idx = pos.y * WIDTH + pos.x;
    
    // Sample collision - fluid can't enter solid glyphs
    sampler_t smp = CLK_NORMALIZED_COORDS_FALSE | CLK_FILTER_NEAREST;
    float solid = read_imagef(collisionMask, smp, pos).w;
    if (solid > 0.5f) {
        densityNew[idx] = 0;
        return;
    }
    
    // Semi-Lagrangian advection
    float2 vel = velocity[idx];
    float2 prevPos = (float2)(pos.x, pos.y) - vel * dt;
    
    // Bilinear sample density at previous position
    float d = bilinearSample(density, prevPos);
    
    // Apply viscosity (diffusion)
    float laplacian = density[idx-1] + density[idx+1] + 
                      density[idx-WIDTH] + density[idx+WIDTH] - 4*density[idx];
    d += viscosity * laplacian * dt;
    
    // Surface tension pulls fluid into droplets
    // ... tension calculations ...
    
    densityNew[idx] = d;
}

__kernel void applyGravity(
    __global float2* velocity,
    __global float* density,
    __read_only image2d_t collisionMask,
    const float2 gravity,
    const float dt
) {
    int2 pos = (int2)(get_global_id(0), get_global_id(1));
    int idx = pos.y * WIDTH + pos.x;
    
    if (density[idx] < 0.01f) return;  // no fluid here
    
    // Check if we're resting on a surface below
    sampler_t smp = CLK_NORMALIZED_COORDS_FALSE | CLK_FILTER_NEAREST;
    float solidBelow = read_imagef(collisionMask, smp, (int2)(pos.x, pos.y+1)).w;
    
    if (solidBelow > 0.5f) {
        // On surface - fluid slowly drips along edges
        float solidLeft = read_imagef(collisionMask, smp, (int2)(pos.x-1, pos.y)).w;
        float solidRight = read_imagef(collisionMask, smp, (int2)(pos.x+1, pos.y)).w;
        
        // Drip toward open edge
        if (solidLeft < 0.5f) velocity[idx].x -= 20 * dt;
        if (solidRight < 0.5f) velocity[idx].x += 20 * dt;
    } else {
        // Free fall
        velocity[idx] += gravity * dt;
    }
}
```

### Approach B: Spring-Connected Blob Particles

Circular meshes with spring physics, stickiness, and teardrop deformation:

```lua
-- blood_blobs.lua
effect.define("blood_blobs", {
    shader = "blob_render",  -- metaball-style rendering
    color = {0.6, 0.05, 0.05, 1.0},
    
    particles = {
        kernel = "vault://Effects/blood_blob.cl",
        emitRate = 8,
        lifetime = {3.0, 6.0},
        size = {4, 12},
        
        -- Blob-specific parameters
        springStiffness = 50.0,      -- inter-blob springs
        springDamping = 0.8,
        stickiness = 0.95,           -- surface adhesion
        tearThreshold = 25.0,        -- force to break spring
        deformStretch = 2.5,         -- max teardrop elongation
    },
    
    -- Render as 2D circle meshes that deform
    mesh = "builtin://circle_deformable",
    meshSegments = 16,
})
```

```opencl
// vault://Effects/blood_blob.cl
// Spring-connected blob particles with stickiness and teardrop deformation

typedef struct {
    uint neighborA;      // spring connection indices
    uint neighborB;
    float restLengthA;
    float restLengthB;
    float stuckTimer;    // how long stuck to surface
    float2 stuckPos;     // where we're stuck
    float stretchFactor; // 1.0 = circle, >1 = teardrop
} BlobData;

__kernel void updateBlobs(
    __global Particle* particles,
    __global BlobData* blobData,
    __read_only image2d_t collisionMask,
    const float dt,
    const uint particleCount,
    const float springK,
    const float damping,
    const float stickiness,
    const float tearThreshold
) {
    uint gid = get_global_id(0);
    if (gid >= particleCount) return;
    
    Particle p = particles[gid];
    BlobData b = blobData[gid];
    if (p.life <= 0) return;
    
    sampler_t smp = CLK_NORMALIZED_COORDS_TRUE | CLK_FILTER_LINEAR;
    float2 uv = p.pos / (float2)(WIDTH, HEIGHT);
    float alpha = read_imagef(collisionMask, smp, uv).w;
    
    // Surface stickiness
    if (alpha > 0.3f) {
        if (b.stuckTimer <= 0) {
            // Just hit surface - stick!
            b.stuckPos = p.pos;
            b.stuckTimer = stickiness * 2.0f;  // time before can detach
        }
        
        // Pull toward stuck position
        float2 toStuck = b.stuckPos - p.pos;
        p.vel += toStuck * stickiness * 100 * dt;
        p.vel *= 0.9f;  // damping while stuck
        
        b.stuckTimer -= dt;
    } else {
        b.stuckTimer = 0;
    }
    
    // Spring forces to neighbors
    if (b.neighborA < particleCount) {
        Particle na = particles[b.neighborA];
        float2 delta = na.pos - p.pos;
        float dist = length(delta);
        float2 dir = delta / max(dist, 0.001f);
        
        float stretch = dist - b.restLengthA;
        float force = springK * stretch;
        
        // Break spring if stretched too far
        if (fabs(stretch) > tearThreshold) {
            b.neighborA = UINT_MAX;  // disconnect
        } else {
            p.vel += dir * force * dt;
        }
    }
    
    // Same for neighborB...
    
    // Gravity (stronger when not stuck)
    float gravityMult = (b.stuckTimer > 0) ? 0.3f : 1.0f;
    p.vel.y += 200 * gravityMult * dt;
    
    // Teardrop deformation based on velocity
    float speed = length(p.vel);
    b.stretchFactor = 1.0f + clamp(speed * 0.01f, 0.0f, 1.5f);
    
    // Calculate deformed mesh orientation (points in velocity direction)
    if (speed > 5.0f) {
        float angle = atan2(p.vel.y, p.vel.x);
        p.rotation.z = angle - 1.5708f;  // -90 degrees (point downward)
    }
    
    // Update position
    p.pos += p.vel * dt;
    p.life -= dt;
    
    particles[gid] = p;
    blobData[gid] = b;
}
```

### Which approach to use?

| Aspect | Fluid Sim | Blob Particles |
|--------|-----------|----------------|
| **Visual quality** | Photorealistic drips, pooling | Stylized, cartoony blobs |
| **Performance** | Heavy (grid-based), fixed resolution | Lighter, scales with particle count |
| **Interaction** | Flows around any shape perfectly | Springs can stretch/break dynamically |
| **Memory** | O(grid²) | O(particles) |
| **Best for** | Realistic horror, visceral effects | Stylized games, comic aesthetics |

Both are valid implementations of a "blood" effect — the system supports either approach through custom OpenCL kernels and flexible rendering.

---

## Step 11: Integration with MarkdownEditor

Update `MarkdownEditor.hpp`:

```cpp
#pragma once
#include <imgui.h>
#include <glm/glm.hpp>
#include <Editors/Markdown/MarkdownDocument.hpp>
#include <Editors/Markdown/TextEffectSystem.hpp>
// Remove Java includes and ProgramStructure

class MarkdownEditor {
    // ... existing editor state ...
    
    // New: 2.5D preview renderer (all in this class)
    MarkdownDocument m_document;
    TextEffectSystem m_effectSystem;
    CollisionMask m_collisionMask;
    
    // FBO, camera, layout, particles as described above
    // ...
    
public:
    ImVec2 drawPreview();  // renders 2.5D FBO scene
    ImVec2 drawEditor();   // existing text editor
    
    // Access for Lua bindings
    CollisionMask& getCollisionMask() { return m_collisionMask; }
    TextEffectSystem& getEffectSystem() { return m_effectSystem; }
};
```

---

## Verification

1. **Visual test**: Render document with mixed content, verify glyphs appear in perspective (slight trapezoid when camera angle adjusted)
2. **Collision test**: Enable debug overlay showing collision mask alpha — character shapes should be visible, not just boxes
3. **Effect batching**: Add logging to confirm shader switches equal number of unique effects, not number of glyphs
4. **Fire emission**: Verify fire particles emit from glyph shapes (letter curves), not rectangular boxes
5. **Particle collision**: Fire should curl around text below, snow should accumulate on letter tops
6. **3D mesh particles**: Create mesh particle effect, verify particles render as 3D shapes with rotation
7. **Effect lerp**: Animate effect weight 0→1, verify smooth blend
8. **Interactive overlay**: Checkboxes toggle, links click, model viewers orbit
9. **OpenCL init**: Verify CL/GL context sharing works, log device name and compute units
10. **GPU particle update**: Compare CPU vs GPU update timing — GPU should handle 100k+ particles at 60fps
11. **Custom kernel loading**: Load a custom `.cl` from vault, verify compilation and execution
12. **Kernel hot-reload**: Modify `.cl` file while running, call `effect.reloadKernel()`, verify behavior changes
13. **Performance**: Target < 16ms for complex documents with 10k+ particles

---

## Key Decisions

- **No PreviewRenderer class** — all rendering in `MarkdownEditor`
- **Fresh implementation** — no code from `MarkdownText.cpp`
- **2.5D perspective** — fixed camera looking at Z=0 document plane, particles can move in Z
- **Alpha-based collision** — uses font atlas alpha for pixel-accurate interaction, not bounding boxes
- **Smart effect batching** — glyphs grouped by effect shader, O(effects) shader switches
- **Effect stack with lerp** — compatible effects blend parameters, incompatible layer
- **3D mesh particles** — particles can be quads or instanced 3D meshes
- **GPU particle physics** — OpenCL kernels update particles entirely on GPU with CL/GL interop
- **Custom particle kernels** — users can write `.cl` files for custom particle behaviors (fire, snow, etc.)
- **Data-driven effects** — shaders and CL kernels in files, parameters in Lua/JSON for hot-reload
