#include "TextEffectsOverlay.hpp"
#include "OpenCLContext.hpp"
#include <plog/Log.h>
#include <cstring>
#include <algorithm>
#include <random>

// ── Global accessor ─────────────────────────────────────────────────
TextEffectsOverlay &GetTextEffectsOverlay()
{
    static TextEffectsOverlay instance;
    return instance;
}

// ── Glyph shader sources (GLSL 330) ────────────────────────────────

static const char *s_glyphVS = R"(
#version 330 core
layout(location = 0) in vec2 in_pos;    // document-space position
layout(location = 1) in vec2 in_uv;     // font atlas UV
uniform vec2 uDocSize;                  // document (texture) dimensions
void main() {
    // Map document coords to NDC [-1,1]
    vec2 ndc = (in_pos / uDocSize) * 2.0 - 1.0;
    ndc.y = -ndc.y; // flip Y (document Y increases downward)
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

static const char *s_glyphFS = R"(
#version 330 core
uniform sampler2D uFontAtlas;
in vec2 v_uv;
out float fragAlpha;
void main() {
    // Sample font atlas alpha and write to R channel (GL_R8 target)
    fragAlpha = texture(uFontAtlas, v_uv).a;
}
)";

// Vertex shader with UV passthrough
static const char *s_glyphVS_full = R"(
#version 330 core
layout(location = 0) in vec2 in_pos;
layout(location = 1) in vec2 in_uv;
uniform vec2 uDocSize;
out vec2 v_uv;
void main() {
    vec2 ndc = (in_pos / uDocSize) * 2.0 - 1.0;
    ndc.y = -ndc.y;
    gl_Position = vec4(ndc, 0.0, 1.0);
    v_uv = in_uv;
}
)";

// ── Constructor / Destructor ────────────────────────────────────────

TextEffectsOverlay::TextEffectsOverlay()
{
}

TextEffectsOverlay::~TextEffectsOverlay()
{
    cleanupCL();
    cleanupGL();
}

// ── GL initialization ───────────────────────────────────────────────

GLuint TextEffectsOverlay::compileShader(GLenum type, const char *src)
{
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        PLOGE << "TextEffectsOverlay shader compile error: " << log;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint TextEffectsOverlay::linkProgram(GLuint vs, GLuint fs)
{
    GLuint p = glCreateProgram();
    glAttachShader(p, vs);
    glAttachShader(p, fs);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok)
    {
        char log[1024];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        PLOGE << "TextEffectsOverlay shader link error: " << log;
        glDeleteProgram(p);
        return 0;
    }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return p;
}

bool TextEffectsOverlay::initGL()
{
    if (m_glInitialized)
        return true;

    // Compile glyph rendering shader
    GLuint vs = compileShader(GL_VERTEX_SHADER, s_glyphVS_full);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, s_glyphFS);
    if (!vs || !fs)
        return false;
    m_glyphShader = linkProgram(vs, fs);
    if (!m_glyphShader)
        return false;

    // Create VAO/VBO for glyph quads
    glGenVertexArrays(1, &m_glyphVAO);
    glGenBuffers(1, &m_glyphVBO);
    glBindVertexArray(m_glyphVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_glyphVBO);
    // Format: vec2 pos, vec2 uv (4 floats per vertex, 6 vertices per quad)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void *)(2 * sizeof(float)));
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    m_glInitialized = true;
    PLOGI << "TextEffectsOverlay GL initialized";
    return true;
}

// Ensure FBO textures match the required dimensions
static void ensureTexture(GLuint &tex, GLuint &fbo, int w, int h, GLenum internalFormat, GLenum format, int &curW, int &curH)
{
    if (tex && curW == w && curH == h)
        return;

    if (tex)
        glDeleteTextures(1, &tex);
    if (fbo)
        glDeleteFramebuffers(1, &fbo);

    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format,
                 internalFormat == GL_R8 ? GL_UNSIGNED_BYTE : GL_UNSIGNED_BYTE, nullptr);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);

    GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        PLOGW << "TextEffectsOverlay FBO incomplete, status=" << status;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);

    curW = w;
    curH = h;
}

void TextEffectsOverlay::cleanupGL()
{
    if (m_glyphShader)
    {
        glDeleteProgram(m_glyphShader);
        m_glyphShader = 0;
    }
    if (m_glyphVAO)
    {
        glDeleteVertexArrays(1, &m_glyphVAO);
        m_glyphVAO = 0;
    }
    if (m_glyphVBO)
    {
        glDeleteBuffers(1, &m_glyphVBO);
        m_glyphVBO = 0;
    }
    if (m_alphaMaskTex)
    {
        glDeleteTextures(1, &m_alphaMaskTex);
        m_alphaMaskTex = 0;
    }
    if (m_alphaMaskFBO)
    {
        glDeleteFramebuffers(1, &m_alphaMaskFBO);
        m_alphaMaskFBO = 0;
    }
    if (m_outputTex)
    {
        glDeleteTextures(1, &m_outputTex);
        m_outputTex = 0;
    }
    if (m_outputFBO)
    {
        glDeleteFramebuffers(1, &m_outputFBO);
        m_outputFBO = 0;
    }
    m_glInitialized = false;
}

// ── CL initialization ───────────────────────────────────────────────

bool TextEffectsOverlay::initCL()
{
    if (m_clInitialized)
        return true;

    auto &cl = OpenCLContext::get();
    if (!cl.isReady() || !cl.hasGLInterop())
    {
        PLOGW << "TextEffectsOverlay: CL/GL interop not available, effects disabled";
        return false;
    }

    // Build CL program from embedded resource
    try
    {
        cl.createProgram(m_clProgram, "Kernels/TextEffects.cl");
        cl.createKernelFromProgram(m_clSpawnKernel, m_clProgram, "particle_spawn");
        cl.createKernelFromProgram(m_clStepKernel, m_clProgram, "particle_step");
        cl.createKernelFromProgram(m_clRenderKernel, m_clProgram, "render_effects");
        cl.createKernelFromProgram(m_clClearKernel, m_clProgram, "clear_output");
    }
    catch (const std::exception &ex)
    {
        PLOGE << "TextEffectsOverlay CL program build failed: " << ex.what();
        return false;
    }

    // Allocate particle buffer
    cl_int err = CL_SUCCESS;
    m_clParticleBuf = cl.createBuffer(CL_MEM_READ_WRITE,
                                       sizeof(TextParticle) * MAX_PARTICLES,
                                       nullptr, &err, "textfx_particles");
    if (err != CL_SUCCESS)
        return false;

    // Particle count (atomic int)
    int zero = 0;
    m_clParticleCount = cl.createBuffer(CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                                         sizeof(int), &zero, &err, "textfx_pcount");
    if (err != CL_SUCCESS)
        return false;

    // Effect params buffer
    m_clEffectParams = cl.createBuffer(CL_MEM_READ_ONLY,
                                        sizeof(CLEffectParams) * MAX_EFFECT_REGIONS,
                                        nullptr, &err, "textfx_params");
    if (err != CL_SUCCESS)
        return false;

    // Random seeds buffer
    std::vector<uint32_t> seeds(MAX_PARTICLES);
    std::mt19937 rng(42);
    for (auto &s : seeds)
        s = rng();
    m_clRandSeeds = cl.createBuffer(CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR,
                                     sizeof(uint32_t) * MAX_PARTICLES,
                                     seeds.data(), &err, "textfx_seeds");
    if (err != CL_SUCCESS)
        return false;

    m_clInitialized = true;
    PLOGI << "TextEffectsOverlay CL initialized";
    return true;
}

void TextEffectsOverlay::cleanupCL()
{
    auto &cl = OpenCLContext::get();
    if (m_clAlphaMask)
    {
        cl.releaseMem(m_clAlphaMask);
        m_clAlphaMask = nullptr;
    }
    if (m_clOutputTex)
    {
        cl.releaseMem(m_clOutputTex);
        m_clOutputTex = nullptr;
    }
    if (m_clParticleBuf)
    {
        cl.releaseMem(m_clParticleBuf);
        m_clParticleBuf = nullptr;
    }
    if (m_clParticleCount)
    {
        cl.releaseMem(m_clParticleCount);
        m_clParticleCount = nullptr;
    }
    if (m_clEffectParams)
    {
        cl.releaseMem(m_clEffectParams);
        m_clEffectParams = nullptr;
    }
    if (m_clRandSeeds)
    {
        cl.releaseMem(m_clRandSeeds);
        m_clRandSeeds = nullptr;
    }
    // Kernels and program — static pattern in codebase: don't release
    // (they are cached across frames like other CL programs)
    m_clInitialized = false;
}

// ── Frame lifecycle ─────────────────────────────────────────────────

void TextEffectsOverlay::beginFrame(float scrollY, float viewportW, float viewportH, float contentH)
{
    m_scrollY = scrollY;
    m_viewportW = viewportW;
    m_viewportH = viewportH;
    m_contentH = contentH;
    m_regions.clear();
}

void TextEffectsOverlay::addEffectRegion(const EffectRegion &region)
{
    if (m_regions.size() < (size_t)MAX_EFFECT_REGIONS)
        m_regions.push_back(region);
}

void TextEffectsOverlay::endFrame(float dt)
{
    m_hadEffectsThisFrame = false;

    if (!m_enabled)
        return;

    // Skip if no effects this frame or last frame and no particles
    if (m_prevRegions.empty() && m_regions.empty() && !m_hasParticles)
        return;

    m_hadEffectsThisFrame = true;

    // Lazy init
    if (!m_glInitialized && !initGL())
    {
        PLOGW << "TextEffectsOverlay: GL init failed — effects overlay disabled";
        m_prevRegions = std::move(m_regions);
        return;
    }
    if (!m_clInitialized && !initCL())
    {
        PLOGW << "TextEffectsOverlay: CL init failed (CL/GL interop may not be available) — GPU overlay disabled, ImDrawList fallback active";
        // Still mark effects active so the flag is correct, but skip GPU rendering
        m_prevRegions = std::move(m_regions);
        return;
    }

    m_time += dt;

    // Determine texture dimensions
    int vpW = std::max(1, (int)m_viewportW);
    int vpH = std::max(1, (int)m_viewportH);

    // Alpha mask covers full document height (capped at GL max texture size)
    GLint maxTexSize = 0;
    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTexSize);
    int docH = std::max(1, std::min((int)m_contentH, maxTexSize));
    int docW = vpW;

    // Track old texture dimensions for CL object recreation
    int oldMaskW = 0, oldMaskH = 0;
    static int s_maskW = 0, s_maskH = 0;
    static int s_outW = 0, s_outH = 0;
    oldMaskW = s_maskW;
    oldMaskH = s_maskH;

    // Ensure alpha mask texture (document-sized, GL_R8)
    ensureTexture(m_alphaMaskTex, m_alphaMaskFBO, docW, docH, GL_R8, GL_RED, s_maskW, s_maskH);

    // Ensure output texture (viewport-sized, GL_RGBA8)
    ensureTexture(m_outputTex, m_outputFBO, vpW, vpH, GL_RGBA8, GL_RGBA, s_outW, s_outH);

    m_texW = vpW;
    m_texH = vpH;

    // If textures were recreated, need to recreate CL wrappers
    auto &cl = OpenCLContext::get();
    bool texturesChanged = (s_maskW != oldMaskW || s_maskH != oldMaskH);
    if (texturesChanged || !m_clAlphaMask)
    {
        if (m_clAlphaMask)
        {
            cl.releaseMem(m_clAlphaMask);
            m_clAlphaMask = nullptr;
        }
        if (m_clOutputTex)
        {
            cl.releaseMem(m_clOutputTex);
            m_clOutputTex = nullptr;
        }
    }

    // Use previous frame's regions for rendering (1-frame latency)
    // This frame's regions will be used next frame
    auto &activeRegions = m_prevRegions;

    // ── Stage 1: Render glyphs to alpha mask ──
    renderAlphaMask();

    // ── Stage 2: Create CL image wrappers if needed ──
    if (!m_clAlphaMask)
    {
        m_clAlphaMask = cl.createFromGLTexture(m_alphaMaskTex, CL_MEM_READ_ONLY);
        if (!m_clAlphaMask)
        {
            PLOGW << "TextEffectsOverlay: failed to create CL image from alpha mask";
            m_prevRegions = std::move(m_regions);
            return;
        }
    }
    if (!m_clOutputTex)
    {
        m_clOutputTex = cl.createFromGLTexture(m_outputTex, CL_MEM_WRITE_ONLY);
        if (!m_clOutputTex)
        {
            PLOGW << "TextEffectsOverlay: failed to create CL image from output tex";
            m_prevRegions = std::move(m_regions);
            return;
        }
    }

    // ── Stage 3: Run CL effects ──
    runCLEffects(dt);

    // Swap regions for next frame
    m_prevRegions = std::move(m_regions);
}

// ── Alpha mask rendering (GL) ───────────────────────────────────────

void TextEffectsOverlay::renderAlphaMask()
{
    auto &activeRegions = m_prevRegions;
    if (activeRegions.empty())
        return;

    // Build vertex data for all glyph quads
    // Format per vertex: posX, posY, uvX, uvY — 6 verts per quad
    std::vector<float> vertices;
    for (const auto &region : activeRegions)
    {
        for (const auto &g : region.glyphs)
        {
            float x0 = g.docPos.x;
            float y0 = g.docPos.y;
            float x1 = x0 + g.glyphW;
            float y1 = y0 + g.glyphH;
            float u0 = g.uvMin.x;
            float v0 = g.uvMin.y;
            float u1 = g.uvMax.x;
            float v1 = g.uvMax.y;

            // Triangle 1
            vertices.insert(vertices.end(), {x0, y0, u0, v0});
            vertices.insert(vertices.end(), {x1, y0, u1, v0});
            vertices.insert(vertices.end(), {x1, y1, u1, v1});
            // Triangle 2
            vertices.insert(vertices.end(), {x0, y0, u0, v0});
            vertices.insert(vertices.end(), {x1, y1, u1, v1});
            vertices.insert(vertices.end(), {x0, y1, u0, v1});
        }
    }

    if (vertices.empty())
        return;

    // Save GL state
    GLint lastFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &lastFBO);
    GLint lastViewport[4];
    glGetIntegerv(GL_VIEWPORT, lastViewport);
    GLboolean wasBlend = glIsEnabled(GL_BLEND);
    GLboolean wasDepth = glIsEnabled(GL_DEPTH_TEST);
    GLboolean wasScissor = glIsEnabled(GL_SCISSOR_TEST);

    // Bind alpha mask FBO
    glBindFramebuffer(GL_FRAMEBUFFER, m_alphaMaskFBO);
    glViewport(0, 0, m_texW, (int)m_contentH);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Use glyph shader
    glUseProgram(m_glyphShader);

    // Set uniforms
    GLint locDocSize = glGetUniformLocation(m_glyphShader, "uDocSize");
    glUniform2f(locDocSize, (float)m_texW, m_contentH);

    // Bind font atlas texture
    GLint locAtlas = glGetUniformLocation(m_glyphShader, "uFontAtlas");
    glActiveTexture(GL_TEXTURE0);
    ImTextureID atlasTexID = ImGui::GetIO().Fonts->TexID;
    glBindTexture(GL_TEXTURE_2D, (GLuint)(intptr_t)atlasTexID);
    glUniform1i(locAtlas, 0);

    // Upload and draw glyph quads
    glBindVertexArray(m_glyphVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_glyphVBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(vertices.size() / 4));

    // Restore GL state
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, lastFBO);
    glViewport(lastViewport[0], lastViewport[1], lastViewport[2], lastViewport[3]);
    if (!wasBlend)
        glDisable(GL_BLEND);
    if (wasDepth)
        glEnable(GL_DEPTH_TEST);
    if (wasScissor)
        glEnable(GL_SCISSOR_TEST);
}

// ── Upload effect parameters to CL ─────────────────────────────────

void TextEffectsOverlay::uploadEffectParams()
{
    auto &activeRegions = m_prevRegions;
    if (activeRegions.empty())
        return;

    std::vector<CLEffectParams> params;
    params.reserve(activeRegions.size());

    for (const auto &r : activeRegions)
    {
        CLEffectParams p;
        p.type = (int)r.params.type;
        p.cycleSec = r.params.cycleSec;
        p.intensity = r.params.intensity;
        p.colorR = r.params.color.x;
        p.colorG = r.params.color.y;
        p.colorB = r.params.color.z;
        p.colorA = r.params.color.w;
        p.radius = r.params.radius;
        p.lifetimeSec = r.params.lifetimeSec;
        p.speed = r.params.speed;
        p.density = r.params.density;
        p.boundsMinX = r.boundsMin.x;
        p.boundsMinY = r.boundsMin.y;
        p.boundsMaxX = r.boundsMax.x;
        p.boundsMaxY = r.boundsMax.y;
        p._pad = 0;
        params.push_back(p);
    }

    auto &cl = OpenCLContext::get();
    clEnqueueWriteBuffer(cl.getQueue(), m_clEffectParams, CL_TRUE, 0,
                         sizeof(CLEffectParams) * params.size(),
                         params.data(), 0, nullptr, nullptr);
}

// ── Run CL effects pipeline ─────────────────────────────────────────

void TextEffectsOverlay::runCLEffects(float dt)
{
    auto &cl = OpenCLContext::get();
    auto &activeRegions = m_prevRegions;

    // Acquire GL objects for CL
    cl_mem glObjects[] = {m_clAlphaMask, m_clOutputTex};
    cl.acquireGLObjects(glObjects, 2);

    // Upload effect parameters
    uploadEffectParams();

    int numEffects = (int)activeRegions.size();
    int maskW = m_texW;
    int maskH = std::max(1, (int)m_contentH);
    int outW = m_texW;
    int outH = m_texH;

    // ── Clear output texture ──
    clSetKernelArg(m_clClearKernel, 0, sizeof(cl_mem), &m_clOutputTex);
    clSetKernelArg(m_clClearKernel, 1, sizeof(int), &outW);
    clSetKernelArg(m_clClearKernel, 2, sizeof(int), &outH);
    size_t clearGlobal[2] = {(size_t)outW, (size_t)outH};
    clEnqueueNDRangeKernel(cl.getQueue(), m_clClearKernel, 2,
                           nullptr, clearGlobal, nullptr, 0, nullptr, nullptr);

    if (numEffects > 0)
    {
        // ── Spawn particles ──
        clSetKernelArg(m_clSpawnKernel, 0, sizeof(cl_mem), &m_clAlphaMask);
        clSetKernelArg(m_clSpawnKernel, 1, sizeof(cl_mem), &m_clParticleBuf);
        clSetKernelArg(m_clSpawnKernel, 2, sizeof(cl_mem), &m_clParticleCount);
        clSetKernelArg(m_clSpawnKernel, 3, sizeof(cl_mem), &m_clEffectParams);
        clSetKernelArg(m_clSpawnKernel, 4, sizeof(int), &numEffects);
        int maxP = MAX_PARTICLES;
        clSetKernelArg(m_clSpawnKernel, 5, sizeof(int), &maxP);
        clSetKernelArg(m_clSpawnKernel, 6, sizeof(float), &m_time);
        clSetKernelArg(m_clSpawnKernel, 7, sizeof(float), &dt);
        clSetKernelArg(m_clSpawnKernel, 8, sizeof(int), &maskW);
        clSetKernelArg(m_clSpawnKernel, 9, sizeof(int), &maskH);
        clSetKernelArg(m_clSpawnKernel, 10, sizeof(cl_mem), &m_clRandSeeds);
        size_t spawnGlobal = (size_t)numEffects;
        clEnqueueNDRangeKernel(cl.getQueue(), m_clSpawnKernel, 1,
                               nullptr, &spawnGlobal, nullptr, 0, nullptr, nullptr);

        // ── Step particles ──
        clSetKernelArg(m_clStepKernel, 0, sizeof(cl_mem), &m_clParticleBuf);
        clSetKernelArg(m_clStepKernel, 1, sizeof(cl_mem), &m_clParticleCount);
        clSetKernelArg(m_clStepKernel, 2, sizeof(int), &maxP);
        clSetKernelArg(m_clStepKernel, 3, sizeof(float), &dt);
        size_t stepGlobal = (size_t)MAX_PARTICLES;
        clEnqueueNDRangeKernel(cl.getQueue(), m_clStepKernel, 1,
                               nullptr, &stepGlobal, nullptr, 0, nullptr, nullptr);
        m_hasParticles = true;

        // ── Render effects ──
        clSetKernelArg(m_clRenderKernel, 0, sizeof(cl_mem), &m_clAlphaMask);
        clSetKernelArg(m_clRenderKernel, 1, sizeof(cl_mem), &m_clOutputTex);
        clSetKernelArg(m_clRenderKernel, 2, sizeof(cl_mem), &m_clParticleBuf);
        clSetKernelArg(m_clRenderKernel, 3, sizeof(cl_mem), &m_clParticleCount);
        clSetKernelArg(m_clRenderKernel, 4, sizeof(cl_mem), &m_clEffectParams);
        clSetKernelArg(m_clRenderKernel, 5, sizeof(int), &numEffects);
        clSetKernelArg(m_clRenderKernel, 6, sizeof(int), &outW);
        clSetKernelArg(m_clRenderKernel, 7, sizeof(int), &outH);
        clSetKernelArg(m_clRenderKernel, 8, sizeof(int), &maskW);
        clSetKernelArg(m_clRenderKernel, 9, sizeof(int), &maskH);
        clSetKernelArg(m_clRenderKernel, 10, sizeof(float), &m_scrollY);
        clSetKernelArg(m_clRenderKernel, 11, sizeof(float), &m_time);
        size_t renderGlobal[2] = {(size_t)outW, (size_t)outH};
        clEnqueueNDRangeKernel(cl.getQueue(), m_clRenderKernel, 2,
                               nullptr, renderGlobal, nullptr, 0, nullptr, nullptr);
    }

    // Read back particle count for stats
    clEnqueueReadBuffer(cl.getQueue(), m_clParticleCount, CL_TRUE, 0,
                        sizeof(int), &m_activeParticleCount, 0, nullptr, nullptr);

    // Release GL objects
    cl.releaseGLObjects(glObjects, 2);

    // Finish CL before GL uses the textures
    clFinish(cl.getQueue());
}
