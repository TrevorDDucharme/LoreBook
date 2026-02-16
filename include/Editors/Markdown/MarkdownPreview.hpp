#pragma once
#include <Editors/Markdown/MarkdownDocument.hpp>
#include <Editors/Markdown/PreviewEffectSystem.hpp>
#include <Editors/Markdown/CollisionMask.hpp>
#include <Editors/Markdown/LayoutEngine.hpp>
#include <WorldMaps/World/World.hpp>
#include <WorldMaps/World/Projections/MercatorProjection.hpp>
#include <WorldMaps/World/Projections/SphereProjection.hpp>
#include <WorldMaps/Orbital/OrbitalSystem.hpp>
#include <WorldMaps/Orbital/OrbitalProjection.hpp>
#include <GL/glew.h>
#include <CL/cl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <chrono>
#include <memory>

class LuaScriptManager;
class LuaEngine;
class Vault;
struct ModelViewer;

namespace Markdown {

// ────────────────────────────────────────────────────────────────────
// MarkdownPreview - 2.5D FBO-based markdown preview renderer
// ────────────────────────────────────────────────────────────────────

class MarkdownPreview {
public:
    MarkdownPreview();
    ~MarkdownPreview();
    
    /// Initialize rendering resources (call after GL context is ready)
    bool init();
    
    /// Cleanup all resources
    void cleanup();
    
    /// Check if initialized
    bool isInitialized() const { return m_initialized; }
    
    /// Set the markdown source text
    void setSource(const std::string& markdown);
    
    /// Get the parsed document
    MarkdownDocument& getDocument() { return m_document; }
    const MarkdownDocument& getDocument() const { return m_document; }
    
    /// Render the preview and return the size used
    /// This renders to an FBO and displays via ImGui::Image
    ImVec2 render();
    
    /// Access systems for external use (e.g., Lua bindings)
    PreviewEffectSystem& getEffectSystem() { return m_effectSystem; }
    CollisionMask& getCollisionMask() { return m_collisionMask; }
    
    /// Set the script manager for Lua canvas rendering
    void setScriptManager(LuaScriptManager* mgr) { m_scriptManager = mgr; }

    /// Set the vault for image resolution
    void setVault(Vault* v) { m_vault = v; }
    
    // ── Camera controls ──
    void setCameraZ(float z) { m_cameraZ = z; }
    float getCameraZ() const { return m_cameraZ; }
    void setFOV(float fovDegrees) { m_fovY = fovDegrees; }
    float getFOV() const { return m_fovY; }

private:
    // Rendering setup
    void ensureFBO(int width, int height);
    void initShaders();
    void initVAOs();
    void initOpenCL();
    void initParticleSystem();
    
    // Render passes
    void renderCollisionMask(const std::vector<EffectBatch>& batches);
    void renderGlyphBatches(const std::vector<EffectBatch>& batches, const glm::mat4& mvp);
    void renderEmbeddedContent(const glm::mat4& mvp);
    void emitParticles(float dt, const std::vector<EffectBatch>& batches);
    void updateParticlesGPU(float dt);
    void renderParticlesFromGPU(const glm::mat4& mvp);
    void renderOverlayWidgets(const ImVec2& origin, float scrollY);
    void renderGlowBloom(const std::vector<EffectBatch>& batches, const glm::mat4& mvp);
    
    // OpenCL kernel management
    void updateCollisionCLImage();
    
    // Helper methods
    void uploadGlyphBatch(const std::vector<GlyphVertex>& vertices);
    
    // ── Document & Layout ──
    MarkdownDocument m_document;
    LayoutEngine m_layoutEngine;
    std::vector<LayoutGlyph> m_layoutGlyphs;
    std::vector<OverlayWidget> m_overlayWidgets;
    
    // ── Effect System ──
    PreviewEffectSystem m_effectSystem;
    CollisionMask m_collisionMask;
    
    // ── FBO ──
    GLuint m_fbo = 0;
    GLuint m_colorTex = 0;
    GLuint m_depthTex = 0;
    // 1x1 placeholder texture used while remote images download
    GLuint m_placeholderTex = 0;
    int m_fboWidth = 0;
    int m_fboHeight = 0;
    
    // ── Bloom FBOs (for glow post-process) ──
    GLuint m_bloomFBO[2] = {0, 0};      // ping-pong FBOs for blur passes
    GLuint m_bloomTex[2] = {0, 0};      // ping-pong color textures
    GLuint m_bloomSrcFBO = 0;           // FBO to render glow silhouettes into
    GLuint m_bloomSrcTex = 0;           // texture for glow silhouettes
    GLuint m_bloomBlurShader = 0;       // Gaussian blur shader
    GLuint m_bloomCompositeShader = 0;  // Additive compositing shader
    GLuint m_bloomGlowShader = 0;       // Shader to render glyph as bright silhouette
    GLuint m_quadVAO = 0;              // Full-screen quad for blur/composite
    GLuint m_quadVBO = 0;
    
    // ── SPH / Fluid (multi-fluid support) ─────────────────────────────────────
    cl_program m_sphProgram = nullptr;
    cl_kernel m_sphDensityKernel = nullptr;   // Pass 1: density + pressure
    cl_kernel m_sphForcesKernel = nullptr;    // Pass 2: pressure + viscosity + cohesion
    cl_mem m_clSPHDensity = nullptr;          // float[MAX_PARTICLES]
    cl_mem m_clSPHPressure = nullptr;         // float[MAX_PARTICLES]
    cl_mem m_clSPHGrid = nullptr;             // Spatial hash grid cell starts
    cl_mem m_clSPHGridEntries = nullptr;      // Spatial hash particle entries

    // Per-behavior SPH descriptor buffers (host → CL)
    cl_mem m_clSPHBehaviourFlags = nullptr;   // int[MAX_FLUID_BEHAVIORS] (isFluid)
    cl_mem m_clSPHParams = nullptr;           // SPHParams[MAX_FLUID_BEHAVIORS]

    // Per-behavior density render targets (R16F) — indexed by behaviorID
    static constexpr size_t MAX_FLUID_BEHAVIORS = 16;
    std::array<GLuint, MAX_FLUID_BEHAVIORS> m_fluidDensityFBO = {0};
    std::array<GLuint, MAX_FLUID_BEHAVIORS> m_fluidDensityTex = {0};

    // Whether different fluid behaviors may interact/mix in SPH kernels
    bool m_sphAllowMixing = true;

    // Backwards-compatible single 'blood' shader is reused for generic fluids
    GLuint m_bloodDensityFBO = 0;             // legacy: FBO for density accumulation (behaviorID==2)
    GLuint m_bloodDensityTex = 0;             // legacy: R16F density texture (behaviorID==2)
    GLuint m_bloodFluidShader = 0;            // Post-process: density → fluid surface (generic)

    void initSPH();
    void renderBloodFluid();

    // Ensure per-behavior density FBO exists (lazy-create)
    void ensureFluidDensityFBO(uint32_t behaviorID);
    
    // ── Camera (2.5D perspective) ──
    glm::mat4 m_projection;
    glm::mat4 m_view;
    float m_cameraZ = 500.0f;
    float m_fovY = 45.0f;
    
    // ── Scroll state ──
    float m_scrollY = 0.0f;
    
    // ── Shaders ──
    GLuint m_collisionShader = 0;
    static constexpr float COLLISION_SCALE = 5.0f;  // 2x supersampled collision mask
    GLuint m_particleShader = 0;
    GLuint m_embedShader = 0;              // Textured quad shader for embedded content
    
    // ── VAO/VBO ──
    GLuint m_glyphVAO = 0;
    GLuint m_glyphVBO = 0;
    GLuint m_particleVAO = 0;
    GLuint m_particleVBO = 0;
    GLuint m_particleEBO = 0;
    
    // ── OpenCL ──
    cl_mem m_clParticleBuffer = nullptr;
    cl_mem m_clDeadIndices = nullptr;     // Buffer of indices of dead particles
    cl_mem m_clDeadCount = nullptr;       // Atomic counter of dead particles
    cl_mem m_clCollisionImage = nullptr;  // CL image for collision sampling
    int m_clCollisionWidth = 0;           // Tracked collision image dimensions
    int m_clCollisionHeight = 0;
    size_t m_particleCount = 0;
    uint32_t m_deadCount = 0;
    static constexpr size_t MAX_PARTICLES = 10000;
    
    // CPU particle buffer (for CPU-side emission before GPU update)
    std::vector<Particle> m_cpuParticles;
    std::vector<uint32_t> m_cpuDeadIndices;
    float m_emitAccumulators[16] = {0};  // Per-effect emission accumulators (indexed by behaviorID)
    std::unordered_map<uint32_t, std::vector<uint32_t>> m_particleBehaviorGroups;
    
    // ── State ──
    bool m_initialized = false;
    float m_zoomLevel = 1.0f;  // Ctrl+Scroll zoom (0.25 – 4.0)
    glm::vec4 m_clearColor = {0.1f, 0.1f, 0.12f, 1.0f};  // FBO background (from frontmatter)
    LuaScriptManager* m_scriptManager = nullptr;
    Vault* m_vault = nullptr;
    int m_embedCounter = 0;  // Per-frame unique embed counter

    // Active canvas instances (populated by renderEmbeddedContent, consumed by renderOverlayWidgets)
    struct ActiveCanvas {
        LuaEngine* engine;
        std::string embedID;
        glm::vec2 docPos;
        glm::vec2 size;        // scaled display size
        glm::vec2 nativeSize;  // unscaled FBO resolution
    };
    std::vector<ActiveCanvas> m_activeCanvases;

    // Active model viewer instances (populated by renderEmbeddedContent, consumed by renderOverlayWidgets)
    struct ActiveModelViewer {
        ModelViewer* viewer = nullptr; // may be null until created by Vault
        std::string src;               // original src string (vault://..., http://, file://)
        glm::vec2 docPos;
        glm::vec2 size;                // scaled display size
        glm::vec2 nativeSize;          // unscaled FBO resolution
    };
    std::vector<ActiveModelViewer> m_activeModelViewers;

    // Active world map instances (populated by renderEmbeddedContent, consumed by renderOverlayWidgets)
    struct ActiveWorldMap {
        std::string worldKey;      // cache lookup key (worldName)
        std::string projection;    // "mercator" or "globe"
        glm::vec2 docPos;          // position in document space
        glm::vec2 size;            // scaled display size
        glm::vec2 nativeSize;      // unscaled native resolution
    };
    std::vector<ActiveWorldMap> m_activeWorldMaps;

    // Active orbital view instances
    struct ActiveOrbitalView {
        std::string systemKey;     // system name
        glm::vec2 docPos;
        glm::vec2 size;
        glm::vec2 nativeSize;
    };
    std::vector<ActiveOrbitalView> m_activeOrbitalViews;

    // Per-world cached state (World object + camera + persistent projections)
    struct CachedWorldState {
        World world;
        std::string config;
        std::chrono::steady_clock::time_point last_used;
        // Camera state for mercator
        float mercCenterLon = 0.0f;   // degrees
        float mercCenterLat = 0.0f;   // degrees
        float mercZoom = 1.0f;
        // Camera state for globe
        float globeCenterLon = 0.0f;  // degrees
        float globeCenterLat = 0.0f;  // degrees
        float globeZoom = 3.0f;
        float globeFovDeg = 45.0f;
        // GL textures owned by projections (managed by project() calls)
        GLuint mercTexture = 0;
        GLuint globeTexture = 0;
        int selectedLayer = 0;
        // Persistent projections (keep CL buffers across frames)
        MercatorProjection mercProj;
        SphericalProjection sphereProj;
        CachedWorldState(const std::string& cfg)
            : world(cfg), config(cfg),
              last_used(std::chrono::steady_clock::now()) {}
    };
    static std::unordered_map<std::string, CachedWorldState> s_worldCache;

    // Per-system cached orbital state
    struct CachedOrbitalState {
        Orbital::OrbitalSystem system;
        Orbital::OrbitalProjection projection;
        GLuint texture = 0;
        double time = 0.0;
        float timeSpeed = 1.0f;
        bool playing = false;
        std::chrono::steady_clock::time_point last_used;
        CachedOrbitalState() : last_used(std::chrono::steady_clock::now()) {}
    };
    static std::unordered_map<std::string, CachedOrbitalState> s_orbitalCache;

    std::string m_sourceText;
    GLuint m_fontAtlasTexture = 0;
};

} // namespace Markdown
