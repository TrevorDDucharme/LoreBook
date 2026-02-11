#include <Editors/Markdown/MarkdownPreview.hpp>
#include <OpenCLContext.hpp>
#include <plog/Log.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <cstdlib>
#include <cmath>

namespace Markdown {

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

out float fragAlpha;

void main() {
    fragAlpha = texture(uFontAtlas, v_uv).a;
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
    if (m_clParticleBuffer) {
        OpenCLContext::get().releaseMem(m_clParticleBuffer);
        m_clParticleBuffer = nullptr;
    }
    
    // Clear CPU particle buffers
    m_cpuParticles.clear();
    m_cpuDeadIndices.clear();
    m_particleCount = 0;
    m_deadCount = 0;
    
    m_collisionMask.cleanup();
    m_effectSystem.cleanup();
    
    m_initialized = false;
}

void MarkdownPreview::setSource(const std::string& markdown) {
    if (markdown == m_sourceText) return;
    
    m_sourceText = markdown;
    m_document.markDirty();
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
}

void MarkdownPreview::initParticleSystem() {
    // Initialize CPU buffers regardless of OpenCL availability
    m_cpuParticles.resize(MAX_PARTICLES);
    m_cpuDeadIndices.resize(MAX_PARTICLES);
    
    // Initially, all particles are dead
    for (size_t i = 0; i < MAX_PARTICLES; ++i) {
        m_cpuParticles[i].life = 0.0f;
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
    
    // Resize collision mask to allocated size
    m_collisionMask.resize(allocWidth, allocHeight);
}

ImVec2 MarkdownPreview::render() {
    if (!m_initialized) {
        init();
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
    
    // 3. Batch glyphs by effect
    std::vector<EffectBatch> batches;
    m_effectSystem.buildBatches(m_layoutGlyphs, batches);
    
    // 4. Resize FBOs if needed
    ensureFBO(static_cast<int>(avail.x), static_cast<int>(avail.y));
    
    // 5. Save GL state
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    
    // 6. Render collision mask
    renderCollisionMask(batches);
    m_collisionMask.readback();
    
    // 7. Setup 2.5D camera
    float aspect = avail.x / avail.y;
    m_projection = glm::perspective(glm::radians(m_fovY), aspect, 0.1f, 2000.0f);
    m_view = glm::lookAt(
        glm::vec3(avail.x / 2, avail.y / 2, m_cameraZ),
        glm::vec3(avail.x / 2, avail.y / 2, 0),
        glm::vec3(0, -1, 0)
    );
    glm::mat4 mvp = m_projection * m_view;
    
    // 8. Render scene to FBO
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_fboWidth, m_fboHeight);
    glClearColor(0.1f, 0.1f, 0.12f, 1.0f);
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
    
    // 14. Restore GL state
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    
    // 15. Display via ImGui
    ImGui::Image((ImTextureID)(intptr_t)m_colorTex, avail, ImVec2(1, 1), ImVec2(0, 0));
    
    // 16. Overlay ImGui widgets
    ImVec2 origin = ImGui::GetItemRectMin();
    float scrollY = ImGui::GetScrollY();
    renderOverlayWidgets(origin, scrollY);
    
    return avail;
}

void MarkdownPreview::renderCollisionMask(const std::vector<EffectBatch>& batches) {
    if (!m_collisionShader || !m_glyphVAO || !m_glyphVBO) return;
    
    m_collisionMask.bindForRendering();
    m_collisionMask.clear();
    
    glUseProgram(m_collisionShader);
    
    // Simple orthographic projection for collision mask
    glm::mat4 ortho = glm::ortho(0.0f, (float)m_fboWidth, (float)m_fboHeight, 0.0f, -1.0f, 1.0f);
    glUniformMatrix4fv(glGetUniformLocation(m_collisionShader, "uMVP"), 1, GL_FALSE, &ortho[0][0]);
    
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fontAtlasTexture);
    glUniform1i(glGetUniformLocation(m_collisionShader, "uFontAtlas"), 0);
    
    // Render all glyphs (we just need their alpha)
    for (const auto& batch : batches) {
        if (batch.vertices.empty()) continue;
        uploadGlyphBatch(batch.vertices);
        glBindVertexArray(m_glyphVAO);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.vertices.size()));
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
        
        EffectShaderType shaderType = batch.effect ? batch.effect->shaderType : EffectShaderType::None;
        GLuint shader = m_effectSystem.getShaderProgram(shaderType);
        
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

void MarkdownPreview::renderEmbeddedContent(const glm::mat4& mvp) {
    // TODO: Render images, model viewers, etc.
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
        
        // Accumulate emission time
        if (batchIdx < 16) {
            m_emitAccumulators[batchIdx] += dt * emission.rate;
        }
        
        // Emit particles based on accumulated time
        int toEmit = static_cast<int>(m_emitAccumulators[batchIdx]);
        if (toEmit <= 0) continue;
        if (batchIdx < 16) {
            m_emitAccumulators[batchIdx] -= toEmit;
        }
        
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
            
            // Color from effect
            p.color = batch.effect->color1;
            
            // Size
            float sizeVar = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f;
            p.size = emission.size + sizeVar * emission.sizeVar;
            
            p.meshID = emission.meshID;
            p.rotation = glm::vec3(0, 0, static_cast<float>(rand()) / RAND_MAX * 6.28f);
            p.rotVel = glm::vec3(0, 0, (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f);
            p.behaviorID = static_cast<uint32_t>(batch.effect->shaderType);
            
            m_particleCount = std::max(m_particleCount, static_cast<size_t>(idx + 1));
        }
    }
}

void MarkdownPreview::updateParticlesGPU(float dt) {
    // Safety check - ensure particle buffers are initialized
    if (m_cpuParticles.empty() || m_cpuDeadIndices.empty()) {
        return;
    }
    
    // CPU-based particle update for now
    // TODO: Move to OpenCL kernels for better performance
    
    for (size_t i = 0; i < m_particleCount && i < m_cpuParticles.size(); ++i) {
        Particle& p = m_cpuParticles[i];
        
        if (p.life <= 0.0f) continue;
        
        // Apply physics based on behavior ID
        switch (static_cast<EffectShaderType>(p.behaviorID)) {
            case EffectShaderType::Fire:
                // Fire rises with turbulence
                p.vel.y -= 80.0f * dt;  // Rise
                p.vel.x += (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 100.0f * dt;
                p.vel *= 0.98f;  // Drag
                p.life -= dt * 1.5f;
                
                // Color fades from white-yellow to red to black
                {
                    float heat = p.life / p.maxLife;
                    if (heat > 0.7f) {
                        float t = (heat - 0.7f) / 0.3f;
                        p.color = glm::vec4(1.0f, 1.0f, t, 1.0f);
                    } else if (heat > 0.4f) {
                        float t = (heat - 0.4f) / 0.3f;
                        p.color = glm::vec4(1.0f, 0.5f + 0.5f * t, 0.0f, 1.0f);
                    } else {
                        float t = heat / 0.4f;
                        p.color = glm::vec4(1.0f * t, 0.1f * t, 0.0f, 0.8f * t);
                    }
                }
                break;
                
            case EffectShaderType::Snow:
                // Snow drifts down with sway
                p.vel.y += 30.0f * dt;  // Fall
                p.vel.x += sin(static_cast<float>(glfwGetTime()) * 2.0f + p.pos.x * 0.05f) * 20.0f * dt;
                p.vel.y = std::min(p.vel.y, 80.0f);  // Terminal velocity
                p.life -= dt * 0.3f;
                p.color.a = p.life / p.maxLife;
                break;
                
            case EffectShaderType::Sparkle:
                // Sparkle stays in place and twinkles
                p.vel *= 0.9f;
                p.life -= dt * 0.5f;
                {
                    float sinVal = static_cast<float>(sin(static_cast<float>(glfwGetTime()) * 10.0f + static_cast<float>(i) * 2.718f));
                    float twinkle = pow(std::max(0.0f, sinVal), 8.0f);
                    float brightness = (p.life / p.maxLife) * (0.3f + twinkle * 0.7f);
                    p.color = glm::vec4(brightness, brightness * 0.95f, brightness * 0.7f, brightness);
                }
                break;
                
            default:
                // Generic particle - just gravity and decay
                p.vel.y += 100.0f * dt;  // Gravity
                p.vel *= 0.99f;
                p.life -= dt;
                p.color.a = std::min(1.0f, p.life * 2.0f);
                break;
        }
        
        // Update position
        glm::vec2 newPos = p.pos + p.vel * dt;
        
        // Collision check against collision mask
        if (m_collisionMask.sample(newPos.x, newPos.y) > 0.5f) {
            // Hit something - bounce
            glm::vec2 normal = m_collisionMask.surfaceNormal(newPos.x, newPos.y);
            p.vel = glm::reflect(p.vel, normal) * 0.5f;
        } else {
            p.pos = newPos;
        }
        
        // Update Z
        p.z += p.zVel * dt;
        
        // Update rotation
        p.rotation += p.rotVel * dt;
        
        // Mark dead particles for recycling
        if (p.life <= 0.0f && m_deadCount < m_cpuDeadIndices.size()) {
            m_cpuDeadIndices[m_deadCount++] = static_cast<uint32_t>(i);
        }
    }
    
    // Upload to GPU for rendering
    if (m_particleCount > 0 && m_particleCount <= m_cpuParticles.size()) {
        glBindBuffer(GL_ARRAY_BUFFER, m_particleVBO);
        glBufferData(GL_ARRAY_BUFFER, m_particleCount * sizeof(Particle),
                     m_cpuParticles.data(), GL_DYNAMIC_DRAW);
    }
}

void MarkdownPreview::renderParticlesFromGPU(const glm::mat4& mvp) {
    if (m_particleCount == 0) return;
    
    glUseProgram(m_particleShader);
    glUniformMatrix4fv(glGetUniformLocation(m_particleShader, "uMVP"), 1, GL_FALSE, &mvp[0][0]);
    
    glBindVertexArray(m_particleVAO);
    glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(m_particleCount));
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
                // TODO: Load and display image
                break;
                
            default:
                break;
        }
    }
}

} // namespace Markdown
