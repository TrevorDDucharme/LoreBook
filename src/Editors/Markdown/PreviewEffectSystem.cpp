#include <Editors/Markdown/PreviewEffectSystem.hpp>
#include <Editors/Markdown/Effect.hpp>
#include <Editors/Markdown/Effects/AllEffects.hpp>
#include <OpenCLContext.hpp>
#include <LoreBook_Resources/LoreBook_ResourcesEmbeddedVFS.hpp>
#include <plog/Log.h>
#include <algorithm>
#include <sstream>

namespace Markdown {

// ────────────────────────────────────────────────────────────────────
// EffectStack
// ────────────────────────────────────────────────────────────────────

void EffectStack::push(EffectDef* effect, size_t sourceOffset) {
    ActiveEffect ae;
    ae.def = effect;
    ae.startOffset = sourceOffset;
    ae.weight = 1.0f;
    m_stack.push_back(ae);
}

void EffectStack::pop(size_t sourceOffset) {
    if (!m_stack.empty()) {
        m_stack.back().endOffset = sourceOffset;
        m_stack.pop_back();
    }
}

EffectDef* EffectStack::currentEffect() const {
    return m_stack.empty() ? nullptr : m_stack.back().def;
}

float EffectStack::currentWeight() const {
    return m_stack.empty() ? 1.0f : m_stack.back().weight;
}

void EffectStack::clear() {
    m_stack.clear();
}

// ────────────────────────────────────────────────────────────────────
// Base glyph shader (used when an Effect has no custom glyph shader)
// ────────────────────────────────────────────────────────────────────

static const char* s_baseGlyphVert = R"(
#version 330 core
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) in uint in_effectID;

uniform mat4 uMVP;

out vec2 v_uv;
out vec4 v_color;
out vec3 v_worldPos;
flat out uint v_effectID;

void main() {
    gl_Position = uMVP * vec4(in_pos, 1.0);
    v_uv = in_uv;
    v_color = in_color;
    v_worldPos = in_pos;
    v_effectID = in_effectID;
}
)";

static const char* s_baseGlyphFrag = R"(
#version 330 core
uniform sampler2D uFontAtlas;

in vec2 v_uv;
in vec4 v_color;
in vec3 v_worldPos;
flat in uint v_effectID;

out vec4 fragColor;

void main() {
    float alpha = texture(uFontAtlas, v_uv).a;
    fragColor = vec4(v_color.rgb, v_color.a * alpha);
}
)";

// ────────────────────────────────────────────────────────────────────
// PreviewEffectSystem implementation
// ────────────────────────────────────────────────────────────────────

PreviewEffectSystem::PreviewEffectSystem() = default;

PreviewEffectSystem::~PreviewEffectSystem() {
    cleanup();
}

bool PreviewEffectSystem::init(cl_context clContext, cl_device_id clDevice) {
    if (m_initialized) return true;
    
    m_clContext = clContext;
    m_clDevice = clDevice;
    
    // Compile the base glyph shader
    m_baseGlyphShader = compileShaderProgram(s_baseGlyphVert, s_baseGlyphFrag);
    
    // Register all effects from the EffectRegistry (auto-registered via REGISTER_EFFECT macro)
    registerBuiltinEffects();
    
    m_initialized = true;
    PLOG_INFO << "PreviewEffectSystem initialized with " << m_effects.size() << " effects";
    return true;
}

void PreviewEffectSystem::cleanup() {
    // Release base glyph shader
    if (m_baseGlyphShader) {
        glDeleteProgram(m_baseGlyphShader);
        m_baseGlyphShader = 0;
    }
    
    // Release compiled Effect resources
    for (auto& [name, def] : m_effects) {
        if (def.effectGlyphShader) {
            glDeleteProgram(def.effectGlyphShader);
            def.effectGlyphShader = 0;
        }
        if (def.effectParticleShader) {
            glDeleteProgram(def.effectParticleShader);
            def.effectParticleShader = 0;
        }
        if (def.effectKernel) {
            clReleaseKernel(def.effectKernel);
            def.effectKernel = nullptr;
        }
        if (def.effectProgram) {
            clReleaseProgram(def.effectProgram);
            def.effectProgram = nullptr;
        }
    }
    
    m_effects.clear();
    m_ownedEffects.clear();
    
    // Release composite shader cache
    for (auto& [sig, shader] : m_compositeShaderCache) {
        if (shader) glDeleteProgram(shader);
    }
    m_compositeShaderCache.clear();
    
    m_initialized = false;
}

void PreviewEffectSystem::registerBuiltinEffects() {
    // Create all effects from the EffectRegistry (FireEffect, SnowEffect, etc.)
    auto& registry = EffectRegistry::get();
    for (const auto& name : registry.getRegisteredNames()) {
        auto effect = registry.create(name);
        if (effect) {
            Effect* ptr = effect.get();
            m_ownedEffects.push_back(std::move(effect));
            registerEffect(ptr);
        }
    }
    
    // ── Parameter Variants ──
    // Instances of existing Effect classes with modified parameters.
    
    auto variant = [&](const std::string& name, std::unique_ptr<Effect> e) {
        Effect* ptr = e.get();
        m_ownedEffects.push_back(std::move(e));
        registerEffectAs(name, ptr);
    };
    
    // Glow variants
    { auto e = std::make_unique<GlowEffect>(); e->color1 = {0.0f,1.0f,1.0f,1.0f}; e->intensity = 1.2f; variant("neon", std::move(e)); }
    { auto e = std::make_unique<GlowEffect>(); e->color1 = {1,1,1,1}; e->intensity = 0.5f; variant("pulse", std::move(e)); }
    { auto e = std::make_unique<GlowEffect>(); e->color1 = {1.0f,0.84f,0,1}; e->intensity = 1.0f; variant("golden", std::move(e)); }
    { auto e = std::make_unique<GlowEffect>(); e->color1 = {0.7f,0.8f,0.9f,0.6f}; e->intensity = 0.4f; e->speed = 0.5f; variant("ghost", std::move(e)); }
    { auto e = std::make_unique<GlowEffect>(); e->color1 = {0.1f,0,0.2f,1}; e->intensity = 0.6f; e->speed = 0.4f; variant("void", std::move(e)); }
    { auto e = std::make_unique<GlowEffect>(); e->color1 = {0,0,0,0.8f}; e->intensity = 0.5f; e->speed = 0; variant("shadow", std::move(e)); }
    { auto e = std::make_unique<GlowEffect>(); e->color1 = {1,1,1,1}; e->intensity = 1.5f; e->speed = 0; variant("outline", std::move(e)); }
    { auto e = std::make_unique<GlowEffect>(); e->color1 = {0.5f,0.8f,1,0.8f}; e->intensity = 0.7f; e->speed = 0.3f; variant("ethereal", std::move(e)); }
    
    // Rainbow variants
    { auto e = std::make_unique<RainbowEffect>(); e->speed = 3.0f; variant("disco", std::move(e)); }
    { auto e = std::make_unique<RainbowEffect>(); e->speed = 0.3f; variant("crystal", std::move(e)); }
    { auto e = std::make_unique<RainbowEffect>(); e->speed = 0.1f; variant("gradient", std::move(e)); }
    
    // Shake variants
    { auto e = std::make_unique<ShakeEffect>(); e->speed = 5.0f; e->intensity = 3.0f; variant("glitch", std::move(e)); }
    
    // Wave variants
    { auto e = std::make_unique<WaveEffect>(); e->speed = 2.0f; e->amplitude = 4.0f; e->frequency = 2.0f; variant("bounce", std::move(e)); }
    { auto e = std::make_unique<WaveEffect>(); e->speed = 0.3f; e->amplitude = 1.0f; e->frequency = 0.5f; variant("typewriter", std::move(e)); }
    
    // Fire variant
    { auto e = std::make_unique<FireEffect>(); e->color1 = {1,0.2f,0,1}; e->color2 = {1,0.5f,0,1}; e->speed = 0.4f; e->intensity = 0.8f; variant("lava", std::move(e)); }
    
    // Snow variants
    { auto e = std::make_unique<SnowEffect>(); e->color1 = {0.6f,0.85f,1,1}; e->color2 = {0.8f,0.9f,1,1}; variant("ice", std::move(e)); }
    { auto e = std::make_unique<SnowEffect>(); e->color1 = {0.6f,0.85f,1,1}; e->color2 = {0.8f,0.9f,1,1}; variant("frost", std::move(e)); }
    
    // Electric variant
    { auto e = std::make_unique<ElectricEffect>(); e->color1 = {0.8f,0.85f,1,1}; e->jitterStrength = 80.0f; e->arcFrequency = 5.0f; variant("storm", std::move(e)); }
    
    // Smoke variants
    { auto e = std::make_unique<SmokeEffect>(); e->color1 = {0.2f,0.9f,0.1f,0.8f}; variant("toxic", std::move(e)); }
    { auto e = std::make_unique<SmokeEffect>(); e->color1 = {1,0.5f,0,0.7f}; e->expansion = 3.0f; e->dissipation = 1.5f; variant("dissolve", std::move(e)); }
    
    // Magic variants
    { auto e = std::make_unique<MagicEffect>(); e->color1 = {1,1,0.8f,1}; e->intensity = 1.2f; variant("holy", std::move(e)); }
    { auto e = std::make_unique<MagicEffect>(); e->color1 = {0.3f,0.6f,1,1}; e->riseSpeed = 40.0f; variant("underwater", std::move(e)); }
    
    // Blood variant
    { auto e = std::make_unique<BloodEffect>(); e->color1 = {0,1,0,1}; variant("matrix", std::move(e)); }
}

// ────────────────────────────────────────────────────────────────────
// Registration
// ────────────────────────────────────────────────────────────────────

void PreviewEffectSystem::registerEffect(Effect* effect) {
    if (!effect) return;
    
    std::string name = effect->getName();
    std::transform(name.begin(), name.end(), name.begin(), ::tolower);
    registerEffectAs(name, effect);
}

void PreviewEffectSystem::registerEffectAs(const std::string& name, Effect* effect) {
    if (!effect) return;
    
    EffectDef def;
    def.name = name;
    def.effect = effect;
    
    auto caps = effect->getCapabilities();
    def.hasParticles = caps.hasParticles;
    if (caps.hasParticles) {
        auto emCfg = effect->getEmissionConfig();
        def.emission.rate = emCfg.rate;
        def.emission.velocity = {emCfg.velocity[0], emCfg.velocity[1]};
        def.emission.velocityVar = {emCfg.velocityVar[0], emCfg.velocityVar[1]};
        def.emission.lifetime = emCfg.lifetime;
        def.emission.lifetimeVar = emCfg.lifetimeVar;
        def.emission.size = emCfg.size;
        def.emission.sizeVar = emCfg.sizeVar;
        
        switch (emCfg.shape) {
            case EffectEmissionConfig::Shape::Point:        def.emission.shape = EmissionConfig::Point; break;
            case EffectEmissionConfig::Shape::Line:         def.emission.shape = EmissionConfig::Line; break;
            case EffectEmissionConfig::Shape::GlyphAlpha:   def.emission.shape = EmissionConfig::GlyphAlpha; break;
            case EffectEmissionConfig::Shape::GlyphOutline: def.emission.shape = EmissionConfig::GlyphAlpha; break;
            case EffectEmissionConfig::Shape::ScreenTop:    def.emission.shape = EmissionConfig::Box; break;
            case EffectEmissionConfig::Shape::Box:          def.emission.shape = EmissionConfig::Box; break;
        }
    }
    
    compileEffectResources(def);
    
    std::string lowerName = name;
    std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
    m_effects[lowerName] = std::move(def);
    
    PLOG_INFO << "Registered effect: " << name
              << " (behaviorID=" << effect->getBehaviorID()
              << ", hasParticles=" << caps.hasParticles << ")";
}

EffectDef* PreviewEffectSystem::getEffect(const std::string& name) {
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    auto it = m_effects.find(lower);
    return it != m_effects.end() ? &it->second : nullptr;
}

// ────────────────────────────────────────────────────────────────────
// Shader compilation
// ────────────────────────────────────────────────────────────────────

GLuint PreviewEffectSystem::compileShaderProgram(const char* vertSrc, const char* fragSrc, const char* geomSrc) {
    GLint success;
    char infoLog[512];
    
    GLuint vertShader = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vertShader, 1, &vertSrc, nullptr);
    glCompileShader(vertShader);
    glGetShaderiv(vertShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(vertShader, 512, nullptr, infoLog);
        PLOG_ERROR << "Vertex shader compilation failed: " << infoLog;
        glDeleteShader(vertShader);
        return 0;
    }
    
    GLuint fragShader = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fragShader, 1, &fragSrc, nullptr);
    glCompileShader(fragShader);
    glGetShaderiv(fragShader, GL_COMPILE_STATUS, &success);
    if (!success) {
        glGetShaderInfoLog(fragShader, 512, nullptr, infoLog);
        PLOG_ERROR << "Fragment shader compilation failed: " << infoLog;
        glDeleteShader(vertShader);
        glDeleteShader(fragShader);
        return 0;
    }
    
    GLuint geomShader = 0;
    if (geomSrc) {
        geomShader = glCreateShader(GL_GEOMETRY_SHADER);
        glShaderSource(geomShader, 1, &geomSrc, nullptr);
        glCompileShader(geomShader);
        glGetShaderiv(geomShader, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(geomShader, 512, nullptr, infoLog);
            PLOG_ERROR << "Geometry shader compilation failed: " << infoLog;
            glDeleteShader(vertShader);
            glDeleteShader(fragShader);
            glDeleteShader(geomShader);
            return 0;
        }
    }
    
    GLuint program = glCreateProgram();
    glAttachShader(program, vertShader);
    glAttachShader(program, fragShader);
    if (geomShader) glAttachShader(program, geomShader);
    glLinkProgram(program);
    
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        PLOG_ERROR << "Shader program linking failed: " << infoLog;
        glDeleteProgram(program);
        program = 0;
    }
    
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
    if (geomShader) glDeleteShader(geomShader);
    
    return program;
}

// ────────────────────────────────────────────────────────────────────
// Effect resource compilation
// ────────────────────────────────────────────────────────────────────

void PreviewEffectSystem::compileEffectResources(EffectDef& def) {
    if (!def.effect) return;
    
    auto caps = def.effect->getCapabilities();
    
    if (caps.hasGlyphShader) {
        auto src = def.effect->getGlyphShaderSources();
        if (src.isValid()) {
            const char* geom = src.hasGeometry() ? src.geometry.c_str() : nullptr;
            def.effectGlyphShader = compileShaderProgram(src.vertex.c_str(), src.fragment.c_str(), geom);
            if (def.effectGlyphShader) {
                PLOG_DEBUG << "Compiled glyph shader for " << def.name;
            }
        }
    }
    
    if (caps.hasParticleShader) {
        auto src = def.effect->getParticleShaderSources();
        if (src.isValid()) {
            const char* geom = src.hasGeometry() ? src.geometry.c_str() : nullptr;
            def.effectParticleShader = compileShaderProgram(src.vertex.c_str(), src.fragment.c_str(), geom);
            if (def.effectParticleShader) {
                PLOG_DEBUG << "Compiled particle shader for " << def.name;
            }
        }
    }
    
    if (caps.hasParticles && m_clContext) {
        auto ksrc = def.effect->getKernelSources();
        if (ksrc.isValid()) {
            std::string commonCL;
            if (existsLoreBook_ResourcesEmbeddedFile("Kernels/particles/common.cl")) {
                commonCL = loadLoreBook_ResourcesEmbeddedFileAsString("Kernels/particles/common.cl");
            }
            
            std::string fullSource = ksrc.source;
            size_t includePos = fullSource.find("#include \"common.cl\"");
            if (includePos != std::string::npos) {
                fullSource.replace(includePos, 20, commonCL);
            }
            
            const char* src = fullSource.c_str();
            size_t len = fullSource.size();
            cl_int err;
            
            def.effectProgram = clCreateProgramWithSource(m_clContext, 1, &src, &len, &err);
            if (err == CL_SUCCESS) {
                err = clBuildProgram(def.effectProgram, 1, &m_clDevice, "-cl-fast-relaxed-math", nullptr, nullptr);
                if (err == CL_SUCCESS) {
                    def.effectKernel = clCreateKernel(def.effectProgram, ksrc.entryPoint.c_str(), &err);
                    if (err == CL_SUCCESS) {
                        PLOG_DEBUG << "Compiled kernel '" << ksrc.entryPoint << "' for " << def.name;
                    } else {
                        PLOG_ERROR << "clCreateKernel failed for " << def.name << ": " << err;
                    }
                } else {
                    size_t logSize;
                    clGetProgramBuildInfo(def.effectProgram, m_clDevice, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
                    std::string log(logSize, '\0');
                    clGetProgramBuildInfo(def.effectProgram, m_clDevice, CL_PROGRAM_BUILD_LOG, logSize, &log[0], nullptr);
                    PLOG_ERROR << "Kernel build failed for " << def.name << ": " << log;
                    clReleaseProgram(def.effectProgram);
                    def.effectProgram = nullptr;
                }
            } else {
                PLOG_ERROR << "clCreateProgramWithSource failed for " << def.name << ": " << err;
            }
        }
    }
}

// ────────────────────────────────────────────────────────────────────
// Composite Shader Generation
// ────────────────────────────────────────────────────────────────────

std::string PreviewEffectSystem::generateCompositeVertexShader(const std::vector<Effect*>& stack) {
    std::ostringstream vs;
    vs << "#version 330 core\n"
       << "layout(location = 0) in vec3 in_pos;\n"
       << "layout(location = 1) in vec2 in_uv;\n"
       << "layout(location = 2) in vec4 in_color;\n"
       << "layout(location = 3) in uint in_effectID;\n\n"
       << "uniform mat4 uMVP;\n"
       << "uniform float uTime;\n";

    // Collect uniform declarations from all effects
    for (const auto* fx : stack) {
        auto snippet = fx->getSnippet();
        if (!snippet.uniformDecls.empty())
            vs << snippet.uniformDecls;
    }

    vs << "\nout vec2 v_uv;\n"
       << "out vec4 v_color;\n"
       << "out vec3 v_worldPos;\n"
       << "flat out uint v_effectID;\n\n";

    // Collect helper functions (vertex-relevant)
    for (const auto* fx : stack) {
        auto snippet = fx->getSnippet();
        if (snippet.hasVertex() && !snippet.helpers.empty())
            vs << snippet.helpers << "\n";
    }

    vs << "void main() {\n"
       << "    vec3 pos = in_pos;\n";

    // Apply vertex modifications in stack order (outer→inner)
    for (const auto* fx : stack) {
        auto snippet = fx->getSnippet();
        if (snippet.hasVertex())
            vs << "    " << snippet.vertexCode << "\n";
    }

    vs << "    gl_Position = uMVP * vec4(pos, 1.0);\n"
       << "    v_uv = in_uv;\n"
       << "    v_color = in_color;\n"
       << "    v_worldPos = in_pos;\n"
       << "    v_effectID = in_effectID;\n"
       << "}\n";

    return vs.str();
}

std::string PreviewEffectSystem::generateCompositeFragmentShader(const std::vector<Effect*>& stack) {
    std::ostringstream fs;
    fs << "#version 330 core\n"
       << "uniform sampler2D uFontAtlas;\n"
       << "uniform float uTime;\n";

    // Collect uniform declarations
    for (const auto* fx : stack) {
        auto snippet = fx->getSnippet();
        if (!snippet.uniformDecls.empty())
            fs << snippet.uniformDecls;
    }

    fs << "\nin vec2 v_uv;\n"
       << "in vec4 v_color;\n"
       << "in vec3 v_worldPos;\n"
       << "flat in uint v_effectID;\n\n"
       << "out vec4 fragColor;\n\n";

    // Collect helper functions (fragment-relevant)
    for (const auto* fx : stack) {
        auto snippet = fx->getSnippet();
        if (snippet.hasFragment() && !snippet.helpers.empty())
            fs << snippet.helpers << "\n";
    }

    fs << "void main() {\n"
       << "    float alpha = texture(uFontAtlas, v_uv).a;\n"
       << "    vec4 color = vec4(v_color.rgb, v_color.a * alpha);\n";

    // Apply fragment modifications in reverse stack order (inner first, outer last)
    for (int i = static_cast<int>(stack.size()) - 1; i >= 0; --i) {
        auto snippet = stack[i]->getSnippet();
        if (snippet.hasFragment())
            fs << "    " << snippet.fragmentCode << "\n";
    }

    fs << "    fragColor = color;\n"
       << "}\n";

    return fs.str();
}

GLuint PreviewEffectSystem::getOrCompileCompositeShader(const std::vector<Effect*>& stack, const std::string& signature) {
    // Check cache
    auto it = m_compositeShaderCache.find(signature);
    if (it != m_compositeShaderCache.end()) {
        return it->second;
    }

    // Generate and compile
    std::string vertSrc = generateCompositeVertexShader(stack);
    std::string fragSrc = generateCompositeFragmentShader(stack);

    GLuint shader = compileShaderProgram(vertSrc.c_str(), fragSrc.c_str());
    if (shader) {
        PLOG_INFO << "Compiled composite shader for stack: " << signature;
    } else {
        PLOG_ERROR << "Failed to compile composite shader for stack: " << signature;
        // Log sources for debugging
        PLOG_ERROR << "Vertex:\n" << vertSrc;
        PLOG_ERROR << "Fragment:\n" << fragSrc;
    }

    m_compositeShaderCache[signature] = shader;
    return shader;
}

// ────────────────────────────────────────────────────────────────────
// Shader / Kernel access
// ────────────────────────────────────────────────────────────────────

GLuint PreviewEffectSystem::getGlyphShader(const EffectDef* def) {
    if (!def) return m_baseGlyphShader;
    
    // If this is a composite EffectDef with a stacked signature, use composite shader
    if (def->effectStack.size() > 1 && !def->stackSignature.empty()) {
        GLuint composite = getOrCompileCompositeShader(def->effectStack, def->stackSignature);
        if (composite) return composite;
    }
    
    if (def->effectGlyphShader) {
        return def->effectGlyphShader;
    }
    return m_baseGlyphShader;
}

GLuint PreviewEffectSystem::getParticleShader(const EffectDef* def) {
    if (!def) return 0;
    return def->effectParticleShader;
}

cl_kernel PreviewEffectSystem::getEffectKernel(const EffectDef* def) {
    if (!def) return nullptr;
    return def->effectKernel;
}

void PreviewEffectSystem::uploadEffectUniforms(GLuint shader, const EffectDef* effect, float time) {
    if (effect && effect->effect) {
        // If composite stack, upload snippet uniforms for each effect in stack
        if (effect->effectStack.size() > 1) {
            uploadCompositeUniforms(shader, effect, time);
            return;
        }
        effect->effect->uploadGlyphUniforms(shader, time);
    } else {
        glUniform1f(glGetUniformLocation(shader, "uTime"), time);
    }
}

void PreviewEffectSystem::uploadCompositeUniforms(GLuint shader, const EffectDef* effect, float time) {
    // Upload shared uTime
    glUniform1f(glGetUniformLocation(shader, "uTime"), time);
    
    // Upload each effect's namespaced snippet uniforms
    for (const auto* fx : effect->effectStack) {
        if (fx) {
            fx->uploadSnippetUniforms(shader, time);
        }
    }
}

// ────────────────────────────────────────────────────────────────────
// Batching
// ────────────────────────────────────────────────────────────────────

void PreviewEffectSystem::buildBatches(const std::vector<LayoutGlyph>& glyphs,
                                        std::vector<EffectBatch>& outBatches) {
    outBatches.clear();
    
    // Group glyphs by EffectDef pointer (each composite gets its own unique EffectDef)
    std::unordered_map<EffectDef*, std::vector<size_t>> effectGroups;
    
    for (size_t i = 0; i < glyphs.size(); ++i) {
        effectGroups[glyphs[i].effect].push_back(i);
    }
    
    for (auto& [effect, indices] : effectGroups) {
        EffectBatch batch;
        batch.effect = effect;
        
        uint32_t effectID = 0;
        if (effect && effect->effect) {
            effectID = effect->effect->getBehaviorID();
        }
        
        for (size_t idx : indices) {
            const auto& g = glyphs[idx];
            
            GlyphVertex tl, tr, bl, br;
            
            tl.pos = g.pos;
            tl.uv = g.uvMin;
            tl.color = g.color;
            tl.effectID = effectID;
            
            tr.pos = g.pos + glm::vec3(g.size.x, 0, 0);
            tr.uv = {g.uvMax.x, g.uvMin.y};
            tr.color = g.color;
            tr.effectID = effectID;
            
            bl.pos = g.pos + glm::vec3(0, g.size.y, 0);
            bl.uv = {g.uvMin.x, g.uvMax.y};
            bl.color = g.color;
            bl.effectID = effectID;
            
            br.pos = g.pos + glm::vec3(g.size.x, g.size.y, 0);
            br.uv = g.uvMax;
            br.color = g.color;
            br.effectID = effectID;
            
            batch.vertices.push_back(tl);
            batch.vertices.push_back(tr);
            batch.vertices.push_back(bl);
            batch.vertices.push_back(tr);
            batch.vertices.push_back(br);
            batch.vertices.push_back(bl);
        }
        
        outBatches.push_back(std::move(batch));
    }
}

// ────────────────────────────────────────────────────────────────────
// Query
// ────────────────────────────────────────────────────────────────────

std::vector<EffectDef*> PreviewEffectSystem::getParticleEffects() {
    std::vector<EffectDef*> result;
    for (auto& [name, def] : m_effects) {
        if (def.hasParticles && def.effectKernel) {
            result.push_back(&def);
        }
    }
    return result;
}

void PreviewEffectSystem::loadEffectsFromFile(const std::string& path) {
    PLOG_INFO << "loadEffectsFromFile not yet implemented: " << path;
}

void PreviewEffectSystem::reloadAll() {
    PLOG_INFO << "reloadAll not yet implemented";
}

} // namespace Markdown
