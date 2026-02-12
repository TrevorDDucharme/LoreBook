#include <Editors/Markdown/PreviewEffectSystem.hpp>
#include <OpenCLContext.hpp>
#include <plog/Log.h>
#include <fstream>
#include <sstream>
#include <algorithm>

namespace Markdown {

// ────────────────────────────────────────────────────────────────────
// ParticleKernel destructor
// ────────────────────────────────────────────────────────────────────

ParticleKernel::~ParticleKernel() {
    if (kernel) {
        clReleaseKernel(kernel);
        kernel = nullptr;
    }
    if (program) {
        clReleaseProgram(program);
        program = nullptr;
    }
}

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
// Built-in shader sources
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

static const char* s_fireVert = R"(
#version 330 core
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) in uint in_effectID;

uniform mat4 uMVP;
uniform float uTime;
uniform float uIntensity;
uniform float uSpeed;

out vec2 v_uv;
out vec4 v_color;
out float v_heat;
out vec3 v_worldPos;

void main() {
    vec3 pos = in_pos;
    
    // Wave displacement (stronger at top of glyph)
    float heightFactor = 1.0 - in_uv.y;  // 0 at bottom, 1 at top
    float wave = sin(in_pos.x * 0.1 + uTime * 5.0 * uSpeed) * heightFactor * uIntensity * 3.0;
    pos.x += wave;
    pos.y += sin(uTime * 7.0 * uSpeed + in_pos.x * 0.2) * heightFactor * uIntensity;
    
    gl_Position = uMVP * vec4(pos, 1.0);
    v_uv = in_uv;
    v_color = in_color;
    v_heat = heightFactor;
    v_worldPos = in_pos;
}
)";

static const char* s_fireFrag = R"(
#version 330 core
uniform sampler2D uFontAtlas;
uniform float uTime;
uniform vec4 uColor1;
uniform vec4 uColor2;

in vec2 v_uv;
in vec4 v_color;
in float v_heat;
in vec3 v_worldPos;

out vec4 fragColor;

void main() {
    float alpha = texture(uFontAtlas, v_uv).a;
    
    // Color gradient based on heat
    vec3 fireColor = mix(uColor1.rgb, uColor2.rgb, v_heat);
    
    // Glow effect (expand alpha)
    float glow = smoothstep(0.0, 0.5, alpha) * (1.0 + v_heat * 0.5);
    
    fragColor = vec4(fireColor, alpha * v_color.a * glow);
}
)";

static const char* s_rainbowFrag = R"(
#version 330 core
uniform sampler2D uFontAtlas;
uniform float uTime;
uniform float uSpeed;

in vec2 v_uv;
in vec4 v_color;
in vec3 v_worldPos;

out vec4 fragColor;

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    float alpha = texture(uFontAtlas, v_uv).a;
    
    float hue = fract(v_worldPos.x * 0.01 + uTime * uSpeed);
    vec3 rainbow = hsv2rgb(vec3(hue, 0.8, 1.0));
    
    fragColor = vec4(rainbow, alpha * v_color.a);
}
)";

static const char* s_shakeVert = R"(
#version 330 core
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) in uint in_effectID;

uniform mat4 uMVP;
uniform float uTime;
uniform float uIntensity;
uniform float uSpeed;

out vec2 v_uv;
out vec4 v_color;
out vec3 v_worldPos;

float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec3 pos = in_pos;
    
    // Per-character shake
    float seed = in_pos.x * 0.1;
    float shakeX = (rand(vec2(seed, uTime * uSpeed * 10.0)) - 0.5) * uIntensity * 2.0;
    float shakeY = (rand(vec2(seed + 0.5, uTime * uSpeed * 10.0)) - 0.5) * uIntensity * 2.0;
    pos.x += shakeX;
    pos.y += shakeY;
    
    gl_Position = uMVP * vec4(pos, 1.0);
    v_uv = in_uv;
    v_color = in_color;
    v_worldPos = in_pos;
}
)";

static const char* s_waveVert = R"(
#version 330 core
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) in uint in_effectID;

uniform mat4 uMVP;
uniform float uTime;
uniform float uAmplitude;
uniform float uFrequency;
uniform float uSpeed;

out vec2 v_uv;
out vec4 v_color;
out vec3 v_worldPos;

void main() {
    vec3 pos = in_pos;
    
    // Sinusoidal wave
    float wave = sin(in_pos.x * uFrequency * 0.1 + uTime * uSpeed * 3.0) * uAmplitude;
    pos.y += wave;
    
    gl_Position = uMVP * vec4(pos, 1.0);
    v_uv = in_uv;
    v_color = in_color;
    v_worldPos = in_pos;
}
)";

static const char* s_glowFrag = R"(
#version 330 core
uniform sampler2D uFontAtlas;
uniform float uTime;
uniform vec4 uColor1;
uniform float uIntensity;

in vec2 v_uv;
in vec4 v_color;
in vec3 v_worldPos;

out vec4 fragColor;

void main() {
    float alpha = texture(uFontAtlas, v_uv).a;
    
    // Subtle pulse on the glow color
    float pulse = 0.8 + 0.2 * sin(uTime * 3.0);
    float strength = clamp(uIntensity * pulse, 0.0, 1.0);
    
    // Tint text toward glow color; actual glow halo is done in bloom post-process
    vec3 glowColor = mix(v_color.rgb, uColor1.rgb, strength * 0.7);
    
    // Just render the glyph — bloom pass creates the halo outside
    fragColor = vec4(glowColor, alpha * v_color.a);
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
    
    // Compile built-in shaders
    compileShader(EffectShaderType::None);
    compileShader(EffectShaderType::Fire);
    compileShader(EffectShaderType::Rainbow);
    compileShader(EffectShaderType::Shake);
    compileShader(EffectShaderType::Wave);
    compileShader(EffectShaderType::Glow);
    compileShader(EffectShaderType::Sparkle);
    compileShader(EffectShaderType::Snow);
    
    // Register built-in effects
    registerBuiltinEffects();
    
    m_initialized = true;
    PLOG_INFO << "PreviewEffectSystem initialized";
    return true;
}

void PreviewEffectSystem::cleanup() {
    // Release shaders
    for (auto& [type, program] : m_shaderCache) {
        if (program) {
            glDeleteProgram(program);
        }
    }
    m_shaderCache.clear();
    
    // Release combined shaders
    for (auto& [key, program] : m_combinedShaderCache) {
        if (program) {
            glDeleteProgram(program);
        }
    }
    m_combinedShaderCache.clear();
    
    // Release kernels
    m_kernelCache.clear();
    
    m_effects.clear();
    m_initialized = false;
}

void PreviewEffectSystem::registerEffect(const std::string& name, EffectDef def) {
    def.name = name;
    m_effects[name] = std::move(def);
}

EffectDef* PreviewEffectSystem::getEffect(const std::string& name) {
    auto it = m_effects.find(name);
    return it != m_effects.end() ? &it->second : nullptr;
}

void PreviewEffectSystem::registerBuiltinEffects() {
    // Fire effect
    {
        EffectDef fire;
        fire.shaderType = EffectShaderType::Fire;
        fire.color1 = {1.0f, 0.3f, 0.0f, 1.0f};
        fire.color2 = {1.0f, 0.8f, 0.0f, 1.0f};
        fire.speed = 1.0f;
        fire.intensity = 1.0f;
        fire.hasParticles = true;
        fire.emission.rate = 8.0f;
        fire.emission.velocity = {0, -80};
        fire.emission.lifetime = 1.2f;
        fire.emission.size = 4.0f;
        registerEffect("fire", fire);
    }
    
    // Rainbow effect
    {
        EffectDef rainbow;
        rainbow.shaderType = EffectShaderType::Rainbow;
        rainbow.speed = 0.5f;
        registerEffect("rainbow", rainbow);
    }
    
    // Shake effect
    {
        EffectDef shake;
        shake.shaderType = EffectShaderType::Shake;
        shake.speed = 1.0f;
        shake.intensity = 2.0f;
        registerEffect("shake", shake);
    }
    
    // Wave effect
    {
        EffectDef wave;
        wave.shaderType = EffectShaderType::Wave;
        wave.speed = 1.0f;
        wave.amplitude = 3.0f;
        wave.frequency = 1.0f;
        registerEffect("wave", wave);
    }
    
    // Glow effect
    {
        EffectDef glow;
        glow.shaderType = EffectShaderType::Glow;
        glow.color1 = {1.0f, 1.0f, 0.5f, 1.0f};
        glow.intensity = 0.8f;
        registerEffect("glow", glow);
    }
    
    // Neon effect (glow with bright colors)
    {
        EffectDef neon;
        neon.shaderType = EffectShaderType::Glow;
        neon.color1 = {0.0f, 1.0f, 1.0f, 1.0f};
        neon.intensity = 1.2f;
        registerEffect("neon", neon);
    }
    
    // Electric effect (yellow neon with shake)
    {
        EffectDef electric;
        electric.shaderType = EffectShaderType::Shake;
        electric.color1 = {1.0f, 1.0f, 0.0f, 1.0f};
        electric.speed = 2.0f;
        electric.intensity = 1.5f;
        electric.hasParticles = true;
        electric.emission.rate = 20.0f;
        electric.emission.velocity = {0, -30};
        electric.emission.velocityVar = {50, 30};
        electric.emission.lifetime = 0.3f;
        electric.emission.size = 2.0f;
        registerEffect("electric", electric);
    }
    
    // Sparkle effect
    {
        EffectDef sparkle;
        sparkle.shaderType = EffectShaderType::Sparkle;
        sparkle.color1 = {1.0f, 0.9f, 0.5f, 1.0f};
        sparkle.hasParticles = true;
        sparkle.emission.rate = 15.0f;
        sparkle.emission.velocity = {0, -20};
        sparkle.emission.velocityVar = {30, 20};
        sparkle.emission.lifetime = 1.5f;
        sparkle.emission.size = 3.0f;
        registerEffect("sparkle", sparkle);
    }
    
    // Snow effect
    {
        EffectDef snow;
        snow.shaderType = EffectShaderType::Snow;
        snow.color1 = {1.0f, 1.0f, 1.0f, 0.9f};
        snow.hasParticles = true;
        snow.emission.rate = 25.0f;
        snow.emission.velocity = {0, 50};
        snow.emission.velocityVar = {20, 10};
        snow.emission.lifetime = 3.0f;
        snow.emission.size = 2.5f;
        registerEffect("snow", snow);
    }
    
    // Pulse effect (alpha oscillation)
    {
        EffectDef pulse;
        pulse.shaderType = EffectShaderType::Glow;
        pulse.color1 = {1.0f, 1.0f, 1.0f, 1.0f};
        pulse.intensity = 0.5f;
        pulse.speed = 1.0f;
        registerEffect("pulse", pulse);
    }
    
    // Bounce effect (wave with abs bounce)
    {
        EffectDef bounce;
        bounce.shaderType = EffectShaderType::Wave;
        bounce.speed = 2.0f;
        bounce.amplitude = 4.0f;
        bounce.frequency = 2.0f;
        registerEffect("bounce", bounce);
    }
    
    // Blood effect (red drip particles)
    {
        EffectDef blood;
        blood.shaderType = EffectShaderType::None;
        blood.color1 = {0.8f, 0.0f, 0.0f, 1.0f};
        blood.color2 = {0.4f, 0.0f, 0.0f, 1.0f};
        blood.hasParticles = true;
        blood.emission.rate = 10.0f;
        blood.emission.velocity = {0, 60};
        blood.emission.velocityVar = {5, 15};
        blood.emission.lifetime = 2.0f;
        blood.emission.size = 3.0f;
        registerEffect("blood", blood);
    }
    
    // Ice/frost effect (blue glow with snow particles)
    {
        EffectDef ice;
        ice.shaderType = EffectShaderType::Glow;
        ice.color1 = {0.6f, 0.85f, 1.0f, 1.0f};
        ice.intensity = 0.6f;
        ice.hasParticles = true;
        ice.emission.rate = 12.0f;
        ice.emission.velocity = {0, 30};
        ice.emission.velocityVar = {15, 10};
        ice.emission.lifetime = 2.0f;
        ice.emission.size = 2.0f;
        registerEffect("ice", ice);
        registerEffect("frost", ice);
    }
    
    // Magic effect (purple glow with sparkle particles)
    {
        EffectDef magic;
        magic.shaderType = EffectShaderType::Glow;
        magic.color1 = {0.7f, 0.3f, 1.0f, 1.0f};
        magic.intensity = 0.9f;
        magic.hasParticles = true;
        magic.emission.rate = 20.0f;
        magic.emission.velocity = {0, -25};
        magic.emission.velocityVar = {25, 15};
        magic.emission.lifetime = 1.2f;
        magic.emission.size = 2.5f;
        registerEffect("magic", magic);
    }
    
    // Ghost effect (fading glow)
    {
        EffectDef ghost;
        ghost.shaderType = EffectShaderType::Glow;
        ghost.color1 = {0.7f, 0.8f, 0.9f, 0.6f};
        ghost.intensity = 0.4f;
        ghost.speed = 0.5f;
        registerEffect("ghost", ghost);
    }
    
    // Underwater/bubbles effect
    {
        EffectDef underwater;
        underwater.shaderType = EffectShaderType::Wave;
        underwater.color1 = {0.3f, 0.6f, 1.0f, 1.0f};
        underwater.speed = 0.5f;
        underwater.amplitude = 2.0f;
        underwater.frequency = 0.8f;
        underwater.hasParticles = true;
        underwater.emission.rate = 8.0f;
        underwater.emission.velocity = {0, -40};
        underwater.emission.velocityVar = {10, 10};
        underwater.emission.lifetime = 2.5f;
        underwater.emission.size = 3.5f;
        registerEffect("underwater", underwater);
    }
    
    // Golden glow effect
    {
        EffectDef golden;
        golden.shaderType = EffectShaderType::Glow;
        golden.color1 = {1.0f, 0.84f, 0.0f, 1.0f};
        golden.intensity = 1.0f;
        registerEffect("golden", golden);
    }
    
    // Toxic/poison effect (green shake)
    {
        EffectDef toxic;
        toxic.shaderType = EffectShaderType::Shake;
        toxic.color1 = {0.2f, 0.9f, 0.1f, 1.0f};
        toxic.speed = 0.8f;
        toxic.intensity = 1.0f;
        toxic.hasParticles = true;
        toxic.emission.rate = 10.0f;
        toxic.emission.velocity = {0, -20};
        toxic.emission.velocityVar = {15, 10};
        toxic.emission.lifetime = 1.5f;
        toxic.emission.size = 2.0f;
        registerEffect("toxic", toxic);
    }
    
    // Crystal shimmer effect
    {
        EffectDef crystal;
        crystal.shaderType = EffectShaderType::Rainbow;
        crystal.color1 = {0.8f, 0.9f, 1.0f, 1.0f};
        crystal.speed = 0.3f;
        registerEffect("crystal", crystal);
    }
    
    // Storm/lightning effect
    {
        EffectDef storm;
        storm.shaderType = EffectShaderType::Shake;
        storm.color1 = {0.8f, 0.85f, 1.0f, 1.0f};
        storm.speed = 3.0f;
        storm.intensity = 3.0f;
        storm.hasParticles = true;
        storm.emission.rate = 15.0f;
        storm.emission.velocity = {0, 50};
        storm.emission.velocityVar = {40, 20};
        storm.emission.lifetime = 0.5f;
        storm.emission.size = 2.0f;
        registerEffect("storm", storm);
    }
    
    // Ethereal glow
    {
        EffectDef ethereal;
        ethereal.shaderType = EffectShaderType::Glow;
        ethereal.color1 = {0.5f, 0.8f, 1.0f, 0.8f};
        ethereal.intensity = 0.7f;
        ethereal.speed = 0.3f;
        registerEffect("ethereal", ethereal);
    }
    
    // Lava effect (slow fire)
    {
        EffectDef lava;
        lava.shaderType = EffectShaderType::Fire;
        lava.color1 = {1.0f, 0.2f, 0.0f, 1.0f};
        lava.color2 = {1.0f, 0.5f, 0.0f, 1.0f};
        lava.speed = 0.4f;
        lava.intensity = 0.8f;
        lava.hasParticles = true;
        lava.emission.rate = 15.0f;
        lava.emission.velocity = {0, -30};
        lava.emission.lifetime = 1.5f;
        lava.emission.size = 5.0f;
        registerEffect("lava", lava);
    }
    
    // Void effect (dark with inverse glow)
    {
        EffectDef voidEffect;
        voidEffect.shaderType = EffectShaderType::Glow;
        voidEffect.color1 = {0.1f, 0.0f, 0.2f, 1.0f};
        voidEffect.intensity = 0.6f;
        voidEffect.speed = 0.4f;
        registerEffect("void", voidEffect);
    }
    
    // Holy light effect
    {
        EffectDef holy;
        holy.shaderType = EffectShaderType::Glow;
        holy.color1 = {1.0f, 1.0f, 0.8f, 1.0f};
        holy.intensity = 1.2f;
        holy.speed = 0.5f;
        holy.hasParticles = true;
        holy.emission.rate = 10.0f;
        holy.emission.velocity = {0, -15};
        holy.emission.velocityVar = {15, 10};
        holy.emission.lifetime = 2.0f;
        holy.emission.size = 2.0f;
        registerEffect("holy", holy);
    }
    
    // Matrix style (green rain + shake)
    {
        EffectDef matrix;
        matrix.shaderType = EffectShaderType::None;
        matrix.color1 = {0.0f, 1.0f, 0.0f, 1.0f};
        matrix.hasParticles = true;
        matrix.emission.rate = 30.0f;
        matrix.emission.velocity = {0, 80};
        matrix.emission.velocityVar = {5, 20};
        matrix.emission.lifetime = 1.5f;
        matrix.emission.size = 2.0f;
        registerEffect("matrix", matrix);
    }
    
    // Disco effect (fast rainbow)
    {
        EffectDef disco;
        disco.shaderType = EffectShaderType::Rainbow;
        disco.speed = 3.0f;
        registerEffect("disco", disco);
    }
    
    // Glitch effect (fast shake)
    {
        EffectDef glitch;
        glitch.shaderType = EffectShaderType::Shake;
        glitch.speed = 5.0f;
        glitch.intensity = 3.0f;
        registerEffect("glitch", glitch);
    }
    
    // Gradient effect (slow color wave)
    {
        EffectDef gradient;
        gradient.shaderType = EffectShaderType::Rainbow;
        gradient.speed = 0.1f;
        registerEffect("gradient", gradient);
    }
    
    // Shadow effect (dark glow)
    {
        EffectDef shadow;
        shadow.shaderType = EffectShaderType::Glow;
        shadow.color1 = {0.0f, 0.0f, 0.0f, 0.8f};
        shadow.intensity = 0.5f;
        shadow.speed = 0.0f;
        registerEffect("shadow", shadow);
    }
    
    // Outline effect (bright glow, high intensity)
    {
        EffectDef outline;
        outline.shaderType = EffectShaderType::Glow;
        outline.color1 = {1.0f, 1.0f, 1.0f, 1.0f};
        outline.intensity = 1.5f;
        outline.speed = 0.0f;
        registerEffect("outline", outline);
    }
    
    // Typewriter effect (slow wave reveal)
    {
        EffectDef typewriter;
        typewriter.shaderType = EffectShaderType::Wave;
        typewriter.speed = 0.3f;
        typewriter.amplitude = 1.0f;
        typewriter.frequency = 0.5f;
        registerEffect("typewriter", typewriter);
    }
    
    // Dissolve effect
    {
        EffectDef dissolve;
        dissolve.shaderType = EffectShaderType::Glow;
        dissolve.color1 = {1.0f, 0.5f, 0.0f, 0.7f};
        dissolve.intensity = 0.4f;
        dissolve.speed = 1.0f;
        dissolve.hasParticles = true;
        dissolve.emission.rate = 20.0f;
        dissolve.emission.velocity = {0, -15};
        dissolve.emission.velocityVar = {20, 15};
        dissolve.emission.lifetime = 1.0f;
        dissolve.emission.size = 1.5f;
        registerEffect("dissolve", dissolve);
    }
}

void PreviewEffectSystem::compileShader(EffectShaderType type) {
    if (m_shaderCache.count(type)) return;
    
    const char* vertSrc = s_baseGlyphVert;
    const char* fragSrc = s_baseGlyphFrag;
    
    switch (type) {
        case EffectShaderType::None:
            // Use base shaders
            break;
        case EffectShaderType::Fire:
            vertSrc = s_fireVert;
            fragSrc = s_fireFrag;
            break;
        case EffectShaderType::Rainbow:
            fragSrc = s_rainbowFrag;
            break;
        case EffectShaderType::Shake:
            vertSrc = s_shakeVert;
            break;
        case EffectShaderType::Wave:
            vertSrc = s_waveVert;
            break;
        case EffectShaderType::Glow:
            fragSrc = s_glowFrag;
            break;
        // These types use base shader + particles
        case EffectShaderType::Sparkle:
        case EffectShaderType::Snow:
        case EffectShaderType::Blood:
        case EffectShaderType::Dissolve:
        case EffectShaderType::Glitch:
        case EffectShaderType::Neon:
        case EffectShaderType::Custom:
            // Use base shaders - effects come from particles or will be added later
            break;
        default:
            PLOG_WARNING << "Unknown shader type: " << static_cast<int>(type);
            // Still compile with base shaders as fallback
            break;
    }
    
    GLuint program = compileShaderProgram(vertSrc, fragSrc);
    if (program) {
        m_shaderCache[type] = program;
    }
}

GLuint PreviewEffectSystem::compileShaderProgram(const char* vertSrc, const char* fragSrc, const char* geomSrc) {
    GLint success;
    char infoLog[512];
    
    // Vertex shader
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
    
    // Fragment shader
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
    
    // Geometry shader (optional)
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
    
    // Link program
    GLuint program = glCreateProgram();
    glAttachShader(program, vertShader);
    glAttachShader(program, fragShader);
    if (geomShader) {
        glAttachShader(program, geomShader);
    }
    glLinkProgram(program);
    
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        PLOG_ERROR << "Shader program linking failed: " << infoLog;
        glDeleteProgram(program);
        program = 0;
    }
    
    // Cleanup
    glDeleteShader(vertShader);
    glDeleteShader(fragShader);
    if (geomShader) {
        glDeleteShader(geomShader);
    }
    
    return program;
}

GLuint PreviewEffectSystem::getShaderProgram(EffectShaderType type) {
    auto it = m_shaderCache.find(type);
    if (it != m_shaderCache.end()) {
        return it->second;
    }
    
    // Try to compile on demand
    compileShader(type);
    it = m_shaderCache.find(type);
    return it != m_shaderCache.end() ? it->second : 0;
}

// Helpers for shader source selection by type
static const char* getVertexSource(EffectShaderType type) {
    switch (type) {
        case EffectShaderType::Fire:  return s_fireVert;
        case EffectShaderType::Shake: return s_shakeVert;
        case EffectShaderType::Wave:  return s_waveVert;
        default:                      return s_baseGlyphVert;
    }
}

static const char* getFragmentSource(EffectShaderType type) {
    switch (type) {
        case EffectShaderType::Fire:    return s_fireFrag;
        case EffectShaderType::Rainbow: return s_rainbowFrag;
        case EffectShaderType::Glow:    return s_glowFrag;
        default:                        return s_baseGlyphFrag;
    }
}

static bool isVertexEffect(EffectShaderType type) {
    return type == EffectShaderType::Fire || type == EffectShaderType::Shake || type == EffectShaderType::Wave;
}

static bool isFragmentEffect(EffectShaderType type) {
    return type == EffectShaderType::Fire || type == EffectShaderType::Rainbow || type == EffectShaderType::Glow;
}

GLuint PreviewEffectSystem::getCombinedShaderProgram(EffectShaderType vertType, EffectShaderType fragType) {
    // If both are the same, just return the standard program
    if (vertType == fragType) return getShaderProgram(vertType);
    
    int key = static_cast<int>(vertType) * 100 + static_cast<int>(fragType);
    auto it = m_combinedShaderCache.find(key);
    if (it != m_combinedShaderCache.end()) {
        return it->second;
    }
    
    // Compile combined shader
    const char* vertSrc = getVertexSource(vertType);
    const char* fragSrc = getFragmentSource(fragType);
    
    GLuint program = compileShaderProgram(vertSrc, fragSrc);
    if (program) {
        m_combinedShaderCache[key] = program;
    }
    return program;
}

void PreviewEffectSystem::buildBatches(const std::vector<LayoutGlyph>& glyphs,
                                        std::vector<EffectBatch>& outBatches) {
    outBatches.clear();
    
    // Group glyphs by effect
    std::unordered_map<EffectDef*, std::vector<size_t>> effectGroups;
    
    for (size_t i = 0; i < glyphs.size(); ++i) {
        effectGroups[glyphs[i].effect].push_back(i);
    }
    
    // Build batches
    for (auto& [effect, indices] : effectGroups) {
        EffectBatch batch;
        batch.effect = effect;
        
        for (size_t idx : indices) {
            const auto& g = glyphs[idx];
            uint32_t effectID = effect ? static_cast<uint32_t>(effect->shaderType) : 0;
            
            // Two triangles per glyph (6 vertices)
            // Triangle 1: TL, TR, BL
            // Triangle 2: TR, BR, BL
            
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

void PreviewEffectSystem::uploadEffectUniforms(GLuint shader, const EffectDef* effect, float time) {
    glUniform1f(glGetUniformLocation(shader, "uTime"), time);
    
    if (effect) {
        glUniform1f(glGetUniformLocation(shader, "uSpeed"), effect->speed);
        glUniform1f(glGetUniformLocation(shader, "uIntensity"), effect->intensity);
        glUniform1f(glGetUniformLocation(shader, "uAmplitude"), effect->amplitude);
        glUniform1f(glGetUniformLocation(shader, "uFrequency"), effect->frequency);
        glUniform4fv(glGetUniformLocation(shader, "uColor1"), 1, &effect->color1[0]);
        glUniform4fv(glGetUniformLocation(shader, "uColor2"), 1, &effect->color2[0]);
    }
}

ParticleKernel* PreviewEffectSystem::loadParticleKernel(const std::string& clPath, const std::string& entry) {
    std::string key = clPath + ":" + entry;
    auto it = m_kernelCache.find(key);
    if (it != m_kernelCache.end()) {
        return it->second.get();
    }
    
    if (!m_clContext) {
        PLOG_ERROR << "Cannot load particle kernel: OpenCL context not set";
        return nullptr;
    }
    
    try {
        // Read kernel source
        std::ifstream file(clPath);
        if (!file.is_open()) {
            PLOG_ERROR << "Failed to open kernel file: " << clPath;
            return nullptr;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string source = buffer.str();
        
        // Create program
        const char* src = source.c_str();
        size_t len = source.size();
        cl_int err;
        cl_program program = clCreateProgramWithSource(m_clContext, 1, &src, &len, &err);
        if (err != CL_SUCCESS) {
            PLOG_ERROR << "clCreateProgramWithSource failed: " << err;
            return nullptr;
        }
        
        // Build program
        err = clBuildProgram(program, 1, &m_clDevice, "-cl-fast-relaxed-math", nullptr, nullptr);
        if (err != CL_SUCCESS) {
            size_t logSize;
            clGetProgramBuildInfo(program, m_clDevice, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logSize);
            std::string log(logSize, '\0');
            clGetProgramBuildInfo(program, m_clDevice, CL_PROGRAM_BUILD_LOG, logSize, &log[0], nullptr);
            PLOG_ERROR << "Kernel build failed: " << log;
            clReleaseProgram(program);
            return nullptr;
        }
        
        // Create kernel
        cl_kernel kernel = clCreateKernel(program, entry.c_str(), &err);
        if (err != CL_SUCCESS) {
            PLOG_ERROR << "clCreateKernel failed: " << err;
            clReleaseProgram(program);
            return nullptr;
        }
        
        // Create ParticleKernel object
        auto pk = std::make_unique<ParticleKernel>();
        pk->kernel = kernel;
        pk->program = program;
        pk->sourcePath = clPath;
        pk->entryPoint = entry;
        
        ParticleKernel* ptr = pk.get();
        m_kernelCache[key] = std::move(pk);
        
        PLOG_INFO << "Loaded particle kernel: " << clPath << " : " << entry;
        return ptr;
        
    } catch (const std::exception& e) {
        PLOG_ERROR << "Exception loading kernel: " << e.what();
        return nullptr;
    }
}

void PreviewEffectSystem::reloadKernel(ParticleKernel* kernel) {
    if (!kernel || kernel->sourcePath.empty()) return;
    
    // Release old kernel
    if (kernel->kernel) {
        clReleaseKernel(kernel->kernel);
        kernel->kernel = nullptr;
    }
    if (kernel->program) {
        clReleaseProgram(kernel->program);
        kernel->program = nullptr;
    }
    
    // Reload
    try {
        std::ifstream file(kernel->sourcePath);
        if (!file.is_open()) {
            PLOG_ERROR << "Failed to open kernel file for reload: " << kernel->sourcePath;
            return;
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string source = buffer.str();
        
        const char* src = source.c_str();
        size_t len = source.size();
        cl_int err;
        kernel->program = clCreateProgramWithSource(m_clContext, 1, &src, &len, &err);
        if (err != CL_SUCCESS) return;
        
        err = clBuildProgram(kernel->program, 1, &m_clDevice, "-cl-fast-relaxed-math", nullptr, nullptr);
        if (err != CL_SUCCESS) {
            clReleaseProgram(kernel->program);
            kernel->program = nullptr;
            return;
        }
        
        kernel->kernel = clCreateKernel(kernel->program, kernel->entryPoint.c_str(), &err);
        if (err != CL_SUCCESS) {
            clReleaseProgram(kernel->program);
            kernel->program = nullptr;
            return;
        }
        
        PLOG_INFO << "Reloaded particle kernel: " << kernel->sourcePath;
        
    } catch (const std::exception& e) {
        PLOG_ERROR << "Exception reloading kernel: " << e.what();
    }
}

void PreviewEffectSystem::reloadKernel(const std::string& effectName) {
    auto* effect = getEffect(effectName);
    if (effect && effect->particleKernel) {
        reloadKernel(effect->particleKernel);
    }
}

std::vector<std::string> PreviewEffectSystem::listBuiltinKernels() const {
    return {"fire", "snow", "sparkle", "drip", "blood"};
}

void PreviewEffectSystem::loadEffectsFromFile(const std::string& path) {
    // TODO: Implement Lua/JSON loading
    PLOG_INFO << "loadEffectsFromFile not yet implemented: " << path;
}

void PreviewEffectSystem::reloadAll() {
    // Reload all kernels
    for (auto& [key, kernel] : m_kernelCache) {
        reloadKernel(kernel.get());
    }
    
    // TODO: Reload effect definitions from files
}

} // namespace Markdown
