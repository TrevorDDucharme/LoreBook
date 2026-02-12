#pragma once
#include <Editors/Markdown/MarkdownDocument.hpp>
#include <Editors/Markdown/PreviewEffectSystem.hpp>
#include <Editors/Markdown/CollisionMask.hpp>
#include <Editors/Markdown/LayoutEngine.hpp>
#include <GL/glew.h>
#include <CL/cl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <imgui.h>
#include <vector>
#include <string>

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
    int m_fboWidth = 0;
    int m_fboHeight = 0;
    
    // ── Camera (2.5D perspective) ──
    glm::mat4 m_projection;
    glm::mat4 m_view;
    float m_cameraZ = 500.0f;
    float m_fovY = 45.0f;
    
    // ── Scroll state ──
    float m_scrollY = 0.0f;
    
    // ── Shaders ──
    GLuint m_collisionShader = 0;
    GLuint m_particleShader = 0;
    
    // ── VAO/VBO ──
    GLuint m_glyphVAO = 0;
    GLuint m_glyphVBO = 0;
    GLuint m_particleVAO = 0;
    GLuint m_particleVBO = 0;
    
    // ── OpenCL ──
    cl_mem m_clParticleBuffer = nullptr;
    cl_mem m_clDeadIndices = nullptr;     // Buffer of indices of dead particles
    cl_mem m_clDeadCount = nullptr;       // Atomic counter of dead particles
    cl_mem m_clCollisionImage = nullptr;  // CL image for collision sampling
    size_t m_particleCount = 0;
    uint32_t m_deadCount = 0;
    static constexpr size_t MAX_PARTICLES = 100000;
    
    // CPU particle buffer (for CPU-side emission before GPU update)
    std::vector<Particle> m_cpuParticles;
    std::vector<uint32_t> m_cpuDeadIndices;
    float m_emitAccumulators[16] = {0};  // Per-effect emission accumulators
    
    // ── State ──
    bool m_initialized = false;
    std::string m_sourceText;
    GLuint m_fontAtlasTexture = 0;
};

} // namespace Markdown
