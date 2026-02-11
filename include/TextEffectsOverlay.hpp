#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <GL/glew.h>
#include <CL/cl.h>
#include <CL/cl_gl.h>
#include <vector>
#include <string>
#include <cstdint>
#include <cmath>

// ────────────────────────────────────────────────────────────────────
// TextEffectsOverlay — GPU-accelerated text effects composited over
// the markdown preview via a transparent overlay texture.
//
// Architecture:
//   1. During md4c parse, effect HTML tags are intercepted and glyph
//      data is recorded into EffectRegion structs.
//   2. A GL shader renders those glyphs (from ImGui's font atlas) into
//      an alpha-mask FBO — capturing the text shape.
//   3. OpenCL kernels (via CL/GL interop) read the alpha mask to drive
//      particle emission, color effects, glow, etc., writing into a
//      shared output texture.
//   4. ImDrawList::AddImage() composites the effect texture on top of
//      the preview each frame.
// ────────────────────────────────────────────────────────────────────

// ── Effect types ────────────────────────────────────────────────────

enum class TextEffectType : int
{
    None = 0,
    Pulse,      // alpha modulation over time
    Glow,       // soft halo around text
    Shake,      // per-character positional displacement (CPU-side only)
    Fire,       // upward-drifting particles emitted from text alpha
    Sparkle,    // brief bright dots within text bounding box
    Rainbow,    // per-character hue cycling
    Count
};

// ── Per-glyph data recorded during md4c text rendering ──────────────

struct EffectGlyphInfo
{
    ImVec2 docPos;      // position in document-space (relative to preview content origin)
    ImVec2 uvMin;       // font atlas UV top-left
    ImVec2 uvMax;       // font atlas UV bottom-right
    float  advanceX;    // horizontal advance
    float  glyphW;      // glyph pixel width  (uvMax.x - uvMin.x) * atlasW
    float  glyphH;      // glyph pixel height (uvMax.y - uvMin.y) * atlasH
    int    charIndex;   // sequential character index within the effect span (for wave offsets)
};

// ── Effect parameters for one tagged region ─────────────────────────

struct EffectParams
{
    TextEffectType type = TextEffectType::None;
    float cycleSec   = 1.0f;    // pulse period
    float intensity  = 2.0f;    // shake amplitude (px)
    ImVec4 color     = ImVec4(1.0f, 0.84f, 0.0f, 1.0f); // glow color (gold default)
    float radius     = 2.0f;    // glow radius (px)
    float lifetimeSec = 1.0f;   // particle lifetime
    float speed      = 1.0f;    // rainbow hue rotation speed
    float density    = 3.0f;    // sparkle density (particles/char/s)
};

// ── One tagged region of text with an active effect ─────────────────

struct EffectRegion
{
    EffectParams params;
    ImVec2 boundsMin;   // document-space bounding box top-left
    ImVec2 boundsMax;   // document-space bounding box bottom-right
    std::vector<EffectGlyphInfo> glyphs;
};

// ── GPU-side particle (matches CL struct layout) ────────────────────

struct TextParticle
{
    float posX, posY;       // document-space position
    float velX, velY;       // velocity
    float life;             // remaining life (seconds)
    float maxLife;          // initial lifetime
    float r, g, b, a;      // color
    int   effectIdx;        // index into effect regions array
    int   _pad;             // alignment padding
};

// ── CL kernel parameter block uploaded per-frame ────────────────────

struct CLEffectParams
{
    int   type;
    float cycleSec;
    float intensity;
    float colorR, colorG, colorB, colorA;
    float radius;
    float lifetimeSec;
    float speed;
    float density;
    float boundsMinX, boundsMinY;
    float boundsMaxX, boundsMaxY;
    float _pad;
};

// ── Main overlay class ──────────────────────────────────────────────

class TextEffectsOverlay
{
public:
    TextEffectsOverlay();
    ~TextEffectsOverlay();

    // Call before md4c parse — clears regions, updates viewport info
    void beginFrame(float scrollY, float viewportW, float viewportH, float contentH);

    // Called during md4c parse when effect-tagged text is encountered
    void addEffectRegion(const EffectRegion &region);

    // Call after markdown renders — runs GPU effects pipeline
    void endFrame(float dt);

    // Returns true if there are any active effects to composite
    bool hasActiveEffects() const { return m_hadEffectsThisFrame; }

    // Get the overlay texture for ImDrawList::AddImage()
    ImTextureID getOverlayTexture() const { return (ImTextureID)(intptr_t)m_outputTex; }

    // Viewport-space UV coords for mapping the overlay
    ImVec2 getUV0() const { return ImVec2(0.0f, 0.0f); }
    ImVec2 getUV1() const { return ImVec2(1.0f, 1.0f); }

    // Is the system operational (GL + CL interop available)?
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool e) { m_enabled = e; }

    // Statistics
    int getParticleCount() const { return m_activeParticleCount; }

    static constexpr int MAX_PARTICLES = 65536;
    static constexpr int MAX_EFFECT_REGIONS = 64;

private:
    // ── Initialization ──
    bool initGL();
    bool initCL();
    void cleanupGL();
    void cleanupCL();

    // ── Per-frame pipeline stages ──
    void renderAlphaMask();     // GL: render glyphs to alpha mask FBO
    void runCLEffects(float dt); // CL: particle sim + effect compositing
    void uploadEffectParams();  // Upload region data to CL

    // ── GL resources ──
    GLuint m_alphaMaskFBO = 0;
    GLuint m_alphaMaskTex = 0;      // GL_R8 texture: text alpha mask
    GLuint m_outputFBO    = 0;
    GLuint m_outputTex    = 0;      // GL_RGBA8 texture: final effects output
    GLuint m_glyphVAO     = 0;
    GLuint m_glyphVBO     = 0;
    GLuint m_glyphShader  = 0;      // shader for rendering glyphs from font atlas

    // ── CL resources ──
    cl_mem   m_clAlphaMask   = nullptr; // CL image wrapping m_alphaMaskTex
    cl_mem   m_clOutputTex   = nullptr; // CL image wrapping m_outputTex
    cl_mem   m_clParticleBuf = nullptr; // particle buffer
    cl_mem   m_clParticleCount = nullptr; // atomic counter
    cl_mem   m_clEffectParams  = nullptr; // effect region parameters
    cl_mem   m_clRandSeeds     = nullptr; // per-particle RNG seeds

    cl_program m_clProgram       = nullptr;
    cl_kernel  m_clSpawnKernel   = nullptr;
    cl_kernel  m_clStepKernel    = nullptr;
    cl_kernel  m_clRenderKernel  = nullptr;
    cl_kernel  m_clClearKernel   = nullptr;

    // ── State ──
    bool  m_enabled = true;
    bool  m_glInitialized = false;
    bool  m_clInitialized = false;
    bool  m_hasParticles  = false;
    bool  m_hadEffectsThisFrame = false;
    int   m_activeParticleCount = 0;

    float m_scrollY    = 0.0f;
    float m_viewportW  = 0.0f;
    float m_viewportH  = 0.0f;
    float m_contentH   = 0.0f;
    int   m_texW       = 0;    // current texture width
    int   m_texH       = 0;    // current texture height (full document capped at GL max)
    float m_time       = 0.0f; // accumulated time for animation

    std::vector<EffectRegion> m_regions;         // regions for this frame
    std::vector<EffectRegion> m_prevRegions;      // regions from previous frame (used for rendering)

    // ── GL shader compilation ──
    static GLuint compileShader(GLenum type, const char *src);
    static GLuint linkProgram(GLuint vs, GLuint fs);
};

// ── Global accessor (follows project static convention) ─────────────
TextEffectsOverlay &GetTextEffectsOverlay();
