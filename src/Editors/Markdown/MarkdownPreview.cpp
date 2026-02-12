#include <Editors/Markdown/MarkdownPreview.hpp>
#include <Editors/Markdown/Effect.hpp>
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

layout(location = 0) out vec4 fragColor;

void main() {
    float a = texture(uFontAtlas, v_uv).a;
    fragColor = vec4(a, 0.0, 0.0, 0.0);
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
// Bloom / Glow post-processing shaders
// ────────────────────────────────────────────────────────────────────

// Renders glyph as a bright white silhouette tinted with glow color
static const char* s_bloomGlowVert = R"(
#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec2 aUV;
layout(location = 2) in vec4 aColor;

uniform mat4 uMVP;

out vec2 v_uv;
out vec4 v_color;

void main() {
    gl_Position = uMVP * vec4(aPos, 1.0);
    v_uv = aUV;
    v_color = aColor;
}
)";

static const char* s_bloomGlowFrag = R"(
#version 330 core
uniform sampler2D uFontAtlas;
uniform vec4 uColor1;
uniform float uIntensity;

in vec2 v_uv;
in vec4 v_color;

out vec4 fragColor;

void main() {
    float alpha = texture(uFontAtlas, v_uv).a;
    // Create a bright, saturated glow source using white + tint
    vec3 brightColor = mix(vec3(1.0), uColor1.rgb, 0.6);
    float glowAlpha = alpha * clamp(uIntensity * 1.5, 0.0, 3.0);
    fragColor = vec4(brightColor, glowAlpha);
}
)";

// Gaussian blur shader (horizontal or vertical pass)
static const char* s_bloomBlurVert = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

out vec2 v_uv;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    v_uv = aUV;
}
)";

static const char* s_bloomBlurFrag = R"(
#version 330 core
uniform sampler2D uTexture;
uniform vec2 uDirection;  // (1/w, 0) for horizontal, (0, 1/h) for vertical
uniform float uRadius;

in vec2 v_uv;
out vec4 fragColor;

void main() {
    // 9-tap Gaussian blur with weights for sigma ~= 4
    float weights[5] = float[](0.227027, 0.1945946, 0.1216216, 0.054054, 0.016216);
    
    vec4 result = texture(uTexture, v_uv) * weights[0];
    
    for (int i = 1; i < 5; ++i) {
        vec2 offset = uDirection * float(i) * uRadius;
        result += texture(uTexture, v_uv + offset) * weights[i];
        result += texture(uTexture, v_uv - offset) * weights[i];
    }
    
    fragColor = result;
}
)";

// Additive composite shader (blends bloom texture onto scene)
static const char* s_bloomCompositeVert = R"(
#version 330 core
layout(location = 0) in vec2 aPos;
layout(location = 1) in vec2 aUV;

out vec2 v_uv;

void main() {
    gl_Position = vec4(aPos, 0.0, 1.0);
    v_uv = aUV;
}
)";

static const char* s_bloomCompositeFrag = R"(
#version 330 core
uniform sampler2D uBloom;
uniform float uBloomStrength;

in vec2 v_uv;
out vec4 fragColor;

void main() {
    vec4 bloom = texture(uBloom, v_uv);
    // Output only the bloom contribution; additive blending adds it to the scene
    fragColor = vec4(bloom.rgb * uBloomStrength, bloom.a * uBloomStrength);
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
    if (m_particleEBO) {
        glDeleteBuffers(1, &m_particleEBO);
        m_particleEBO = 0;
    }
    if (m_clParticleBuffer) {
        OpenCLContext::get().releaseMem(m_clParticleBuffer);
        m_clParticleBuffer = nullptr;
    }
    if (m_clCollisionImage) {
        clReleaseMemObject(m_clCollisionImage);
        m_clCollisionImage = nullptr;
        m_clCollisionWidth = 0;
        m_clCollisionHeight = 0;
    }
    
    // Cleanup bloom resources
    for (int i = 0; i < 2; ++i) {
        if (m_bloomFBO[i]) { glDeleteFramebuffers(1, &m_bloomFBO[i]); m_bloomFBO[i] = 0; }
        if (m_bloomTex[i]) { glDeleteTextures(1, &m_bloomTex[i]); m_bloomTex[i] = 0; }
    }
    if (m_bloomSrcFBO) { glDeleteFramebuffers(1, &m_bloomSrcFBO); m_bloomSrcFBO = 0; }
    if (m_bloomSrcTex) { glDeleteTextures(1, &m_bloomSrcTex); m_bloomSrcTex = 0; }
    if (m_bloomBlurShader) { glDeleteProgram(m_bloomBlurShader); m_bloomBlurShader = 0; }
    if (m_bloomCompositeShader) { glDeleteProgram(m_bloomCompositeShader); m_bloomCompositeShader = 0; }
    if (m_bloomGlowShader) { glDeleteProgram(m_bloomGlowShader); m_bloomGlowShader = 0; }
    if (m_quadVAO) { glDeleteVertexArrays(1, &m_quadVAO); m_quadVAO = 0; }
    if (m_quadVBO) { glDeleteBuffers(1, &m_quadVBO); m_quadVBO = 0; }
    
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
    
    // Compile bloom glow shader (renders glyph silhouettes for bloom source)
    {
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &s_bloomGlowVert, nullptr);
        glCompileShader(vs);
        
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &s_bloomGlowFrag, nullptr);
        glCompileShader(fs);
        
        m_bloomGlowShader = glCreateProgram();
        glAttachShader(m_bloomGlowShader, vs);
        glAttachShader(m_bloomGlowShader, fs);
        glLinkProgram(m_bloomGlowShader);
        
        glDeleteShader(vs);
        glDeleteShader(fs);
    }
    
    // Compile bloom blur shader
    {
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &s_bloomBlurVert, nullptr);
        glCompileShader(vs);
        
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &s_bloomBlurFrag, nullptr);
        glCompileShader(fs);
        
        m_bloomBlurShader = glCreateProgram();
        glAttachShader(m_bloomBlurShader, vs);
        glAttachShader(m_bloomBlurShader, fs);
        glLinkProgram(m_bloomBlurShader);
        
        glDeleteShader(vs);
        glDeleteShader(fs);
    }
    
    // Compile bloom composite shader
    {
        GLuint vs = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vs, 1, &s_bloomCompositeVert, nullptr);
        glCompileShader(vs);
        
        GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fs, 1, &s_bloomCompositeFrag, nullptr);
        glCompileShader(fs);
        
        m_bloomCompositeShader = glCreateProgram();
        glAttachShader(m_bloomCompositeShader, vs);
        glAttachShader(m_bloomCompositeShader, fs);
        glLinkProgram(m_bloomCompositeShader);
        
        glDeleteShader(vs);
        glDeleteShader(fs);
    }
    
    // Full-screen quad for bloom blur/composite
    {
        float quadVerts[] = {
            // pos       uv
            -1, -1,    0, 0,
             1, -1,    1, 0,
             1,  1,    1, 1,
            -1, -1,    0, 0,
             1,  1,    1, 1,
            -1,  1,    0, 1,
        };
        glGenVertexArrays(1, &m_quadVAO);
        glGenBuffers(1, &m_quadVBO);
        glBindVertexArray(m_quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVerts), quadVerts, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
        glBindVertexArray(0);
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
    glGenBuffers(1, &m_particleEBO);
    
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
    // Particle kernels are now compiled by the Effect system during init
}

void MarkdownPreview::updateCollisionCLImage() {
    if (!OpenCLContext::get().isReady()) return;
    if (!m_collisionMask.hasCPUData() || m_collisionMask.getWidth() <= 0) return;
    
    int w = m_collisionMask.getWidth();
    int h = m_collisionMask.getHeight();
    
    // Recreate if size changed
    if (m_clCollisionImage && (m_clCollisionWidth != w || m_clCollisionHeight != h)) {
        clReleaseMemObject(m_clCollisionImage);
        m_clCollisionImage = nullptr;
    }
    
    cl_image_format fmt;
    fmt.image_channel_order = CL_R;
    fmt.image_channel_data_type = CL_UNORM_INT8;
    
    cl_command_queue q = OpenCLContext::get().getQueue();
    const uint8_t* data = m_collisionMask.getCPUData();
    
    if (!m_clCollisionImage) {
        // Create new CL image from CPU data
        cl_image_desc desc = {};
        desc.image_type = CL_MEM_OBJECT_IMAGE2D;
        desc.image_width = static_cast<size_t>(w);
        desc.image_height = static_cast<size_t>(h);
        desc.image_row_pitch = static_cast<size_t>(w);  // R8: 1 byte per pixel
        
        cl_int err;
        m_clCollisionImage = clCreateImage(
            OpenCLContext::get().getContext(),
            CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
            &fmt, &desc,
            const_cast<uint8_t*>(data),
            &err
        );
        
        if (err != CL_SUCCESS || !m_clCollisionImage) {
            PLOG_WARNING << "Failed to create CL collision image: " << err;
            m_clCollisionImage = nullptr;
            return;
        }
        
        m_clCollisionWidth = w;
        m_clCollisionHeight = h;
    } else {
        // Update existing image with new CPU data
        size_t origin[3] = {0, 0, 0};
        size_t region[3] = {static_cast<size_t>(w), static_cast<size_t>(h), 1};
        clEnqueueWriteImage(q, m_clCollisionImage, CL_FALSE,
                            origin, region, static_cast<size_t>(w), 0,
                            data, 0, nullptr, nullptr);
    }
}

void MarkdownPreview::initParticleSystem() {
    // Initialize CPU buffers regardless of OpenCL availability
    m_cpuParticles.resize(MAX_PARTICLES);
    m_cpuDeadIndices.resize(MAX_PARTICLES);
    
    // Initially, all particles are dead
    for (size_t i = 0; i < MAX_PARTICLES; ++i) {
        m_cpuParticles[i].life = 0.0f;
        m_cpuParticles[i].maxLife = -1.0f;  // sentinel: already in dead list
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
    
    // Bloom FBOs at half resolution for performance
    int bloomW = allocWidth / 2;
    int bloomH = allocHeight / 2;
    
    // Cleanup old bloom resources
    for (int i = 0; i < 2; ++i) {
        if (m_bloomFBO[i]) { glDeleteFramebuffers(1, &m_bloomFBO[i]); m_bloomFBO[i] = 0; }
        if (m_bloomTex[i]) { glDeleteTextures(1, &m_bloomTex[i]); m_bloomTex[i] = 0; }
    }
    if (m_bloomSrcFBO) { glDeleteFramebuffers(1, &m_bloomSrcFBO); m_bloomSrcFBO = 0; }
    if (m_bloomSrcTex) { glDeleteTextures(1, &m_bloomSrcTex); m_bloomSrcTex = 0; }
    
    // Create bloom source FBO (full res — glow silhouettes rendered here)
    glGenTextures(1, &m_bloomSrcTex);
    glBindTexture(GL_TEXTURE_2D, m_bloomSrcTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, allocWidth, allocHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    glGenFramebuffers(1, &m_bloomSrcFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_bloomSrcFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bloomSrcTex, 0);
    // Share depth buffer from main FBO for correct depth testing
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D, m_depthTex, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Create ping-pong FBOs for blur (half res)
    for (int i = 0; i < 2; ++i) {
        glGenTextures(1, &m_bloomTex[i]);
        glBindTexture(GL_TEXTURE_2D, m_bloomTex[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, bloomW, bloomH, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        
        glGenFramebuffers(1, &m_bloomFBO[i]);
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFBO[i]);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_bloomTex[i], 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
    
    // Resize collision mask to allocated size
    m_collisionMask.resize(allocWidth, allocHeight);
}

ImVec2 MarkdownPreview::render() {
    if (!m_initialized) {
        // Save GL state before init (which creates shaders, VAOs, textures)
        GLint preInitProgram, preInitVAO, preInitVBO, preInitTex, preInitActiveTex;
        GLint preInitFBO;
        glGetIntegerv(GL_CURRENT_PROGRAM, &preInitProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &preInitVAO);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &preInitVBO);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &preInitTex);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &preInitActiveTex);
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &preInitFBO);
        
        init();
        
        // Restore GL state after init
        glUseProgram(preInitProgram);
        glBindVertexArray(preInitVAO);
        glBindBuffer(GL_ARRAY_BUFFER, preInitVBO);
        glActiveTexture(preInitActiveTex);
        glBindTexture(GL_TEXTURE_2D, preInitTex);
        glBindFramebuffer(GL_FRAMEBUFFER, preInitFBO);
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
    
    // Clamp scroll to content bounds
    float contentHeight = m_layoutEngine.getContentHeight();
    float maxScroll = std::max(0.0f, contentHeight - avail.y);
    m_scrollY = std::clamp(m_scrollY, 0.0f, maxScroll);
    
    // 3. Batch glyphs by effect
    std::vector<EffectBatch> batches;
    m_effectSystem.buildBatches(m_layoutGlyphs, batches);
    
    // 4. Resize FBOs if needed
    ensureFBO(static_cast<int>(avail.x), static_cast<int>(avail.y));
    
    // 5. Save ALL relevant GL state to prevent corruption of other renderers
    GLint prevFBO;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevProgram;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    GLint prevVAO;
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVAO);
    GLint prevVBO;
    glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevVBO);
    GLint prevTex;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    GLint prevActiveTex;
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTex);
    GLboolean prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
    GLboolean prevBlend = glIsEnabled(GL_BLEND);
    GLint prevBlendSrc, prevBlendDst;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrc);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDst);
    GLint prevBlendSrcRGB, prevBlendDstRGB;
    glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRGB);
    glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRGB);
    
    // 6. Render collision mask
    renderCollisionMask(batches);
    m_collisionMask.readback();
    
    // 7. Setup 2.5D camera – compute Z so viewport dimensions match layout at Z=0
    float aspect = avail.x / avail.y;
    float halfFovRad = glm::radians(m_fovY) * 0.5f;
    float viewCameraZ = (avail.y * 0.5f) / std::tan(halfFovRad);
    m_projection = glm::perspective(glm::radians(m_fovY), aspect, 0.1f, viewCameraZ * 4.0f);
    m_view = glm::lookAt(
        glm::vec3(avail.x / 2, avail.y / 2 + m_scrollY, viewCameraZ),
        glm::vec3(avail.x / 2, avail.y / 2 + m_scrollY, 0),
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
    
    // 14. Glow bloom post-process
    renderGlowBloom(batches, mvp);
    
    // 15. Restore ALL GL state
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glUseProgram(prevProgram);
    glBindVertexArray(prevVAO);
    glBindBuffer(GL_ARRAY_BUFFER, prevVBO);
    glActiveTexture(prevActiveTex);
    glBindTexture(GL_TEXTURE_2D, prevTex);
    if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
    glBlendFuncSeparate(prevBlendSrcRGB, prevBlendDstRGB, prevBlendSrc, prevBlendDst);
    
    // 15. Display via ImGui
    ImGui::Image((ImTextureID)(intptr_t)m_colorTex, avail, ImVec2(1, 1), ImVec2(0, 0));
    
    // Handle mouse wheel scrolling on preview
    if (ImGui::IsItemHovered()) {
        float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f) {
            m_scrollY -= wheel * 40.0f;
            m_scrollY = std::clamp(m_scrollY, 0.0f, maxScroll);
        }
    }
    
    // Draw scrollbar indicator when content exceeds viewport
    if (contentHeight > avail.y && contentHeight > 0.0f) {
        ImVec2 imgMin = ImGui::GetItemRectMin();
        ImVec2 imgMax = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float barW = 6.0f;
        float trackH = imgMax.y - imgMin.y;
        float thumbH = std::max(20.0f, trackH * (avail.y / contentHeight));
        float thumbY = imgMin.y + (m_scrollY / contentHeight) * trackH;
        dl->AddRectFilled(ImVec2(imgMax.x - barW, imgMin.y), imgMax,
                          IM_COL32(40, 40, 40, 100), barW * 0.5f);
        dl->AddRectFilled(ImVec2(imgMax.x - barW, thumbY),
                          ImVec2(imgMax.x, thumbY + thumbH),
                          IM_COL32(180, 180, 180, 150), barW * 0.5f);
    }
    
    // 16. Overlay ImGui widgets
    ImVec2 origin = ImGui::GetItemRectMin();
    renderOverlayWidgets(origin, m_scrollY);
    
    return avail;
}

void MarkdownPreview::renderCollisionMask(const std::vector<EffectBatch>& batches) {
    if (!m_collisionShader || !m_glyphVAO || !m_glyphVBO) return;
    
    m_collisionMask.bindForRendering();
    m_collisionMask.clear();
    
    // Collision mask is a simple alpha stamp — no depth test needed, max blending
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_SCISSOR_TEST);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE);  // Additive so overlapping glyphs accumulate
    
    glUseProgram(m_collisionShader);
    
    // Orthographic projection offset by scroll position
    glm::mat4 ortho = glm::ortho(0.0f, (float)m_fboWidth,
                                 m_scrollY + (float)m_fboHeight, m_scrollY,
                                 -1.0f, 1.0f);
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
        
        GLuint shader = m_effectSystem.getGlyphShader(batch.effect);
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

void MarkdownPreview::renderGlowBloom(const std::vector<EffectBatch>& batches, const glm::mat4& mvp) {
    if (!m_bloomGlowShader || !m_bloomBlurShader || !m_bloomCompositeShader ||
        !m_bloomSrcFBO || !m_quadVAO || m_fboWidth <= 0 || m_fboHeight <= 0) {
        return;
    }
    
    // Check if any batches contribute to bloom
    bool hasGlow = false;
    for (const auto& batch : batches) {
        if (!batch.effect) continue;
        // Check bloomEffect pointer
        Effect* fx = batch.effect->bloomEffect ? batch.effect->bloomEffect : batch.effect->effect;
        if (fx && fx->getCapabilities().contributesToBloom) {
            hasGlow = true;
            break;
        }
        // Also check any effect in the composite stack
        for (const auto* stackFx : batch.effect->effectStack) {
            if (stackFx && stackFx->getCapabilities().contributesToBloom) {
                hasGlow = true;
                break;
            }
        }
        if (hasGlow) break;
    }
    if (!hasGlow) return;
    
    float time = static_cast<float>(glfwGetTime());
    
    // === Pass 1: Render glow glyph silhouettes to bloom source FBO ===
    glBindFramebuffer(GL_FRAMEBUFFER, m_bloomSrcFBO);
    glViewport(0, 0, m_fboWidth, m_fboHeight);
    glClearColor(0, 0, 0, 0);
    glClear(GL_COLOR_BUFFER_BIT);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    
    glUseProgram(m_bloomGlowShader);
    glUniformMatrix4fv(glGetUniformLocation(m_bloomGlowShader, "uMVP"), 1, GL_FALSE, &mvp[0][0]);
    glUniform1i(glGetUniformLocation(m_bloomGlowShader, "uFontAtlas"), 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_fontAtlasTexture);
    
    for (const auto& batch : batches) {
        if (!batch.effect || batch.vertices.empty()) continue;
        
        // Find the bloom-contributing effect: check bloomEffect, then stack, then primary
        Effect* bloomFx = batch.effect->bloomEffect;
        if (!bloomFx || !bloomFx->getCapabilities().contributesToBloom) {
            bloomFx = nullptr;
            for (const auto* stackFx : batch.effect->effectStack) {
                if (stackFx && stackFx->getCapabilities().contributesToBloom) {
                    bloomFx = const_cast<Effect*>(stackFx);
                    break;
                }
            }
        }
        if (!bloomFx && batch.effect->effect && batch.effect->effect->getCapabilities().contributesToBloom) {
            bloomFx = batch.effect->effect;
        }
        if (!bloomFx) continue;
        
        glUniform4fv(glGetUniformLocation(m_bloomGlowShader, "uColor1"), 1, &bloomFx->color1[0]);
        glUniform1f(glGetUniformLocation(m_bloomGlowShader, "uIntensity"), bloomFx->intensity);
        
        uploadGlyphBatch(batch.vertices);
        glBindVertexArray(m_glyphVAO);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.vertices.size()));
    }
    
    // === Pass 2: Downsample bloom source to half-res ping texture ===
    int bloomW = m_fboWidth / 2;
    int bloomH = m_fboHeight / 2;
    
    // Blit/downsample bloom source -> bloomTex[0] via blur shader with zero radius
    glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFBO[0]);
    glViewport(0, 0, bloomW, bloomH);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND);
    
    glUseProgram(m_bloomBlurShader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_bloomSrcTex);
    glUniform1i(glGetUniformLocation(m_bloomBlurShader, "uTexture"), 0);
    glUniform2f(glGetUniformLocation(m_bloomBlurShader, "uDirection"), 0, 0);
    glUniform1f(glGetUniformLocation(m_bloomBlurShader, "uRadius"), 0);
    
    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    // === Pass 3 & 4: Two-pass Gaussian blur (horizontal + vertical), repeated for wider glow ===
    int blurPasses = 3; // Multiple passes for a very wide, soft glow
    for (int pass = 0; pass < blurPasses; ++pass) {
        // Horizontal blur: bloomTex[0] -> bloomFBO[1]
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFBO[1]);
        glViewport(0, 0, bloomW, bloomH);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_bloomTex[0]);
        glUniform1i(glGetUniformLocation(m_bloomBlurShader, "uTexture"), 0);
        glUniform2f(glGetUniformLocation(m_bloomBlurShader, "uDirection"), 1.0f / bloomW, 0);
        glUniform1f(glGetUniformLocation(m_bloomBlurShader, "uRadius"), 2.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        
        // Vertical blur: bloomTex[1] -> bloomFBO[0]
        glBindFramebuffer(GL_FRAMEBUFFER, m_bloomFBO[0]);
        glViewport(0, 0, bloomW, bloomH);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, m_bloomTex[1]);
        glUniform2f(glGetUniformLocation(m_bloomBlurShader, "uDirection"), 0, 1.0f / bloomH);
        glUniform1f(glGetUniformLocation(m_bloomBlurShader, "uRadius"), 2.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }
    
    // === Pass 5: Composite blurred bloom onto main scene FBO ===
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_fboWidth, m_fboHeight);
    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE); // Additive blend
    glDisable(GL_DEPTH_TEST);
    
    glUseProgram(m_bloomCompositeShader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_bloomTex[0]);
    glUniform1i(glGetUniformLocation(m_bloomCompositeShader, "uBloom"), 0);
    glUniform1f(glGetUniformLocation(m_bloomCompositeShader, "uBloomStrength"), 1.0f);
    
    // Draw fullscreen quad — additive blend adds bloom glow halo onto the scene
    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    
    // Restore blend mode
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glActiveTexture(GL_TEXTURE0); // Reset active texture unit
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
        
        // Use behaviorID for accumulator index (stable across frames,
        // unlike batchIdx which depends on unordered_map iteration order)
        uint32_t bid = batch.effect->effect ? batch.effect->effect->getBehaviorID() : 0;
        if (bid >= 16) bid = 0;
        
        // Accumulate emission time
        m_emitAccumulators[bid] += dt * emission.rate;
        
        // Emit particles based on accumulated time
        int toEmit = static_cast<int>(m_emitAccumulators[bid]);
        if (toEmit <= 0) continue;
        // Cap particles emitted per batch per frame
        toEmit = std::min(toEmit, 4);
        m_emitAccumulators[bid] -= toEmit;
        
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
            
            // Size
            float sizeVar = (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 2.0f;
            p.size = emission.size + sizeVar * emission.sizeVar;
            
            p.meshID = emission.meshID;
            p.rotation = glm::vec3(0, 0, static_cast<float>(rand()) / RAND_MAX * 6.28f);
            p.rotVel = glm::vec3(0, 0, (static_cast<float>(rand()) / RAND_MAX - 0.5f) * 4.0f);
            p.behaviorID = batch.effect->effect ? batch.effect->effect->getBehaviorID() : 0;
            
            // Color from effect
            if (batch.effect->effect) {
                const auto& c = batch.effect->effect->color1;
                p.color = glm::vec4(c[0], c[1], c[2], c[3]);
            }
            
            m_particleCount = std::max(m_particleCount, static_cast<size_t>(idx + 1));
        }
    }
}

void MarkdownPreview::updateParticlesGPU(float dt) {
    if (m_cpuParticles.empty() || m_cpuDeadIndices.empty() || m_particleCount == 0) {
        return;
    }
    
    auto& cl = OpenCLContext::get();
    if (!cl.isReady() || !m_clParticleBuffer) {
        return;
    }
    
    // Get all effects that have compiled particle kernels
    auto particleEffects = m_effectSystem.getParticleEffects();
    if (particleEffects.empty()) {
        return;
    }
    
    // Deduplicate by behaviorID — only dispatch one kernel per behavior type
    // (variants like "lava" share behaviorID with "fire", dispatching both
    //  would double-process the same particles)
    {
        uint32_t seenBitmask = 0;
        auto it = particleEffects.begin();
        while (it != particleEffects.end()) {
            uint32_t bid = (*it)->effect ? (*it)->effect->getBehaviorID() : 0;
            uint32_t bit = 1u << bid;
            if (bid == 0 || (seenBitmask & bit)) {
                it = particleEffects.erase(it);
            } else {
                seenBitmask |= bit;
                ++it;
            }
        }
    }
    
    cl_command_queue q = cl.getQueue();
    uint32_t count = static_cast<uint32_t>(m_particleCount);
    size_t globalSize = m_particleCount;
    float scrollY = m_scrollY;
    float maskH = static_cast<float>(m_collisionMask.getHeight());
    float time = static_cast<float>(glfwGetTime());
    
    // 1. Upload CPU particles to CL buffer
    cl_int err = clEnqueueWriteBuffer(q, m_clParticleBuffer, CL_FALSE, 0,
                                       m_particleCount * sizeof(Particle),
                                       m_cpuParticles.data(), 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        PLOG_ERROR << "Failed to upload particles to CL: " << err;
        return;
    }
    
    // 2. Create/update CL collision image from CPU readback data
    updateCollisionCLImage();
    
    // If collision image failed, create a tiny dummy (kernels require a valid image arg)
    cl_mem collisionImg = m_clCollisionImage;
    cl_mem dummyImg = nullptr;
    if (!collisionImg) {
        cl_image_format fmt;
        fmt.image_channel_order = CL_R;
        fmt.image_channel_data_type = CL_UNORM_INT8;
        cl_image_desc desc = {};
        desc.image_type = CL_MEM_OBJECT_IMAGE2D;
        desc.image_width = 1;
        desc.image_height = 1;
        desc.image_row_pitch = 1;
        uint8_t zero = 0;
        dummyImg = clCreateImage(cl.getContext(), CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR,
                                  &fmt, &desc, &zero, &err);
        collisionImg = dummyImg;
        maskH = 1.0f;
    }
    
    // 3. Dispatch each Effect's particle kernel
    // Standard arg order: (particles, collision, dt, scrollY, maskH, time, count)
    // Effect-specific args start at index 7 via bindKernelParams()
    for (EffectDef* def : particleEffects) {
        cl_kernel kernel = def->effectKernel;
        if (!kernel || !def->effect) continue;
        
        // Set standard kernel args 0-6
        clSetKernelArg(kernel, 0, sizeof(cl_mem), &m_clParticleBuffer);
        clSetKernelArg(kernel, 1, sizeof(cl_mem), &collisionImg);
        clSetKernelArg(kernel, 2, sizeof(float), &dt);
        clSetKernelArg(kernel, 3, sizeof(float), &scrollY);
        clSetKernelArg(kernel, 4, sizeof(float), &maskH);
        clSetKernelArg(kernel, 5, sizeof(float), &time);
        clSetKernelArg(kernel, 6, sizeof(uint32_t), &count);
        
        // Let the Effect bind its specific params from arg 7 onward
        KernelParams params;
        params.particleBuffer = m_clParticleBuffer;
        params.collisionImage = collisionImg;
        params.deltaTime = dt;
        params.scrollY = scrollY;
        params.maskHeight = maskH;
        params.time = time;
        params.particleCount = count;
        def->effect->bindKernelParams(kernel, params);
        
        err = clEnqueueNDRangeKernel(q, kernel, 1, nullptr, &globalSize, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            PLOG_ERROR << def->name << " kernel dispatch failed: " << err;
        }
    }
    
    // 4. Read back particles from CL buffer (blocking read)
    err = clEnqueueReadBuffer(q, m_clParticleBuffer, CL_TRUE, 0,
                               m_particleCount * sizeof(Particle),
                               m_cpuParticles.data(), 0, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        PLOG_ERROR << "Failed to read back particles from CL: " << err;
    }
    
    // Release dummy collision image if we created one
    if (dummyImg) {
        clReleaseMemObject(dummyImg);
    }
    
    // 5. Mark dead particles for recycling (only newly-dead, using maxLife sentinel)
    for (size_t i = 0; i < m_particleCount && i < m_cpuParticles.size(); ++i) {
        if (m_cpuParticles[i].life <= 0.0f && m_cpuParticles[i].maxLife > 0.0f
            && m_deadCount < m_cpuDeadIndices.size()) {
            m_cpuDeadIndices[m_deadCount++] = static_cast<uint32_t>(i);
            m_cpuParticles[i].maxLife = -1.0f;  // sentinel: already recycled
        }
    }
    
    // 5b. Build per-behaviorID index groups for per-effect rendering
    m_particleBehaviorGroups.clear();
    for (size_t i = 0; i < m_particleCount && i < m_cpuParticles.size(); ++i) {
        if (m_cpuParticles[i].life > 0.0f) {
            m_particleBehaviorGroups[m_cpuParticles[i].behaviorID].push_back(
                static_cast<uint32_t>(i));
        }
    }
    
    // 6. Upload to GPU for rendering
    if (m_particleCount > 0 && m_particleCount <= m_cpuParticles.size()) {
        glBindBuffer(GL_ARRAY_BUFFER, m_particleVBO);
        glBufferData(GL_ARRAY_BUFFER, m_particleCount * sizeof(Particle),
                     m_cpuParticles.data(), GL_DYNAMIC_DRAW);
    }
}

void MarkdownPreview::renderParticlesFromGPU(const glm::mat4& mvp) {
    if (m_particleCount == 0 || m_particleBehaviorGroups.empty()) return;
    
    // Particles should always render on top of glyphs (no depth test)
    // and use additive blending for fire/glow effects
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE);  // Additive blend
    
    glBindVertexArray(m_particleVAO);
    
    float time = static_cast<float>(glfwGetTime());
    
    // Build behaviorID → EffectDef* map for per-effect shader lookup
    auto particleEffects = m_effectSystem.getParticleEffects();
    std::unordered_map<uint32_t, EffectDef*> effectByBehavior;
    for (auto* def : particleEffects) {
        if (def->effect && def->effectParticleShader) {
            uint32_t bid = def->effect->getBehaviorID();
            if (effectByBehavior.find(bid) == effectByBehavior.end()) {
                effectByBehavior[bid] = def;
            }
        }
    }
    
    // Render each behavior group with its effect's particle shader
    for (auto& [bid, indices] : m_particleBehaviorGroups) {
        if (indices.empty()) continue;
        
        GLuint shader = m_particleShader;  // fallback generic shader
        EffectDef* def = nullptr;
        auto it = effectByBehavior.find(bid);
        if (it != effectByBehavior.end()) {
            shader = it->second->effectParticleShader;
            def = it->second;
        }
        
        glUseProgram(shader);
        glUniformMatrix4fv(glGetUniformLocation(shader, "uMVP"), 1, GL_FALSE, &mvp[0][0]);
        
        if (def && def->effect) {
            def->effect->uploadParticleUniforms(shader, time);
        }
        
        // Upload index buffer and draw this behavior group
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_particleEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     indices.size() * sizeof(uint32_t),
                     indices.data(), GL_DYNAMIC_DRAW);
        glDrawElements(GL_POINTS, static_cast<GLsizei>(indices.size()),
                       GL_UNSIGNED_INT, nullptr);
    }
    
    // Restore state
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
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
