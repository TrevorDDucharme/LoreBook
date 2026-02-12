#include <Editors/Markdown/Effects/ElectricEffect.hpp>

namespace Markdown {

// ────────────────────────────────────────────────────────────────────
// Electric glyph shaders - flickering with static displacement
// ────────────────────────────────────────────────────────────────────

static const char* s_electricGlyphVert = R"(
#version 330 core
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) in uint in_effectID;

uniform mat4 uMVP;
uniform float uTime;
uniform float uJitterStrength;

out vec2 v_uv;
out vec4 v_color;
out vec3 v_worldPos;

float random(float x) {
    return fract(sin(x * 12.9898) * 43758.5453);
}

void main() {
    vec3 pos = in_pos;
    
    // Random jitter displacement
    float jitterTime = floor(uTime * 20.0);
    float jitterX = (random(jitterTime + in_pos.y * 0.1) - 0.5) * uJitterStrength * 0.1;
    float jitterY = (random(jitterTime * 1.3 + in_pos.x * 0.1) - 0.5) * uJitterStrength * 0.05;
    
    // Only jitter occasionally
    float doJitter = step(0.85, random(jitterTime));
    pos.x += jitterX * doJitter;
    pos.y += jitterY * doJitter;
    
    gl_Position = uMVP * vec4(pos, 1.0);
    v_uv = in_uv;
    v_color = in_color;
    v_worldPos = in_pos;
}
)";

static const char* s_electricGlyphFrag = R"(
#version 330 core
uniform sampler2D uFontAtlas;
uniform float uTime;
uniform float uArcFrequency;

in vec2 v_uv;
in vec4 v_color;
in vec3 v_worldPos;

out vec4 fragColor;

float random(vec2 st) {
    return fract(sin(dot(st, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    float alpha = texture(uFontAtlas, v_uv).a;
    
    // Electric blue-white base
    vec3 baseColor = vec3(0.5, 0.7, 1.0);
    
    // Flickering brightness
    float flicker = step(0.7, random(vec2(floor(uTime * 30.0), 0.0)));
    float brightness = 1.0 + flicker * 0.5;
    
    // Arc highlights
    float arc = step(0.95, random(vec2(floor(v_worldPos.x * 0.2), floor(uTime * uArcFrequency))));
    vec3 arcColor = vec3(1.0, 1.0, 1.0) * arc;
    
    vec3 finalColor = baseColor * brightness + arcColor;
    
    // Glow expansion
    float glow = smoothstep(0.0, 0.5, alpha) * (1.0 + flicker * 0.2);
    
    fragColor = vec4(finalColor, alpha * v_color.a * glow);
}
)";

// ────────────────────────────────────────────────────────────────────
// Electric particle shaders
// ────────────────────────────────────────────────────────────────────

static const char* s_electricParticleVert = R"(
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

static const char* s_electricParticleGeom = R"(
#version 330 core
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

uniform mat4 uMVP;
uniform float uTime;

in float v_z[];
in vec4 v_color[];
in float v_size[];
in float v_life[];

out vec2 g_uv;
out vec4 g_color;

float random(float x) {
    return fract(sin(x * 12.9898) * 43758.5453);
}

void main() {
    if (v_life[0] <= 0) return;
    
    vec3 pos = vec3(gl_in[0].gl_Position.xy, v_z[0]);
    float size = v_size[0];
    vec4 color = v_color[0];
    
    // Electric particles flicker size
    float flicker = step(0.6, random(floor(uTime * 60.0) + pos.x));
    size *= 0.8 + flicker * 0.8;
    
    // Intense flash
    color.rgb *= 1.0 + flicker * 2.0;
    color.a *= smoothstep(0.0, 0.1, v_life[0]);
    
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

static const char* s_electricParticleFrag = R"(
#version 330 core
in vec2 g_uv;
in vec4 g_color;

out vec4 fragColor;

void main() {
    float dist = length(g_uv - vec2(0.5));
    
    // Hard bright center, soft glow
    float core = 1.0 - step(0.15, dist);
    float glow = 1.0 - smoothstep(0.15, 0.5, dist);
    float alpha = core + glow * 0.5;
    
    fragColor = vec4(g_color.rgb, g_color.a * alpha);
}
)";

// ────────────────────────────────────────────────────────────────────
// Electric particle OpenCL kernel
// ────────────────────────────────────────────────────────────────────

static const char* s_electricKernel = R"(
// Electric particle simulation - erratic movement with arcing

#include "common.cl"

__kernel void updateElectric(
    __global Particle* particles,
    __read_only image2d_t collision,
    const float deltaTime,
    const float scrollY,
    const float maskHeight,
    const float time,
    const uint count,
    const float arcFrequency,
    const float jitterStrength,
    const float chainChance
) {
    uint gid = get_global_id(0);
    if (gid >= count) return;
    
    Particle p = particles[gid];
    
    if (p.life <= 0.0f) return;
    if (p.behaviorID != BEHAVIOR_ELECTRIC) return;
    
    uint rngState = (uint)(p.pos.x * 1000.0f) ^ (uint)(time * 10000.0f) ^ gid;
    
    // Erratic jitter movement
    if (randRange(&rngState, 0.0f, 1.0f) < 0.3f) {
        p.vel.x = randRange(&rngState, -1.0f, 1.0f) * jitterStrength;
        p.vel.y = randRange(&rngState, -1.0f, 1.0f) * jitterStrength;
    }
    
    // Quick decay
    p.vel *= 0.85f;
    
    // Update position
    float2 newPos = p.pos + p.vel * deltaTime;
    
    // Collision - electric bounces rapidly
    float2 newMaskPos = docToMask(newPos, scrollY, maskHeight);
    float2 curMaskPos = docToMask(p.pos, scrollY, maskHeight);
    float newCol = sampleCollision(collision, collisionSampler, newMaskPos);
    float curCol = sampleCollision(collision, collisionSampler, curMaskPos);
    
    if (newCol > 0.5f && curCol <= 0.5f) {
        float2 norm = surfaceNormal(collision, collisionSampler, newMaskPos);
        float2 docNorm = (float2)(norm.x, -norm.y);
        p.vel = reflect(p.vel, docNorm) * 0.9f;
        newPos = p.pos;
    }
    
    p.pos = newPos;
    
    // Flickering color
    float flicker = step(0.7f, randRange(&rngState, 0.0f, 1.0f));
    float intensity = 0.8f + flicker * 0.5f;
    p.color = (float4)(0.5f * intensity, 0.7f * intensity, 1.0f * intensity, 1.0f);
    
    // Fast decay
    p.life -= deltaTime * 3.0f;
    
    particles[gid] = p;
}
)";

// ────────────────────────────────────────────────────────────────────
// Post-process bloom pass for electric glow
// ────────────────────────────────────────────────────────────────────

static const char* s_electricBloomFrag = R"(
#version 330 core
uniform sampler2D uInputTex;
uniform vec2 uTexelSize;
uniform float uIntensity;

in vec2 v_uv;
out vec4 fragColor;

void main() {
    vec4 color = texture(uInputTex, v_uv);
    
    // Simple 5-tap blur
    vec4 blur = color * 0.4;
    blur += texture(uInputTex, v_uv + vec2(uTexelSize.x * 2.0, 0)) * 0.15;
    blur += texture(uInputTex, v_uv - vec2(uTexelSize.x * 2.0, 0)) * 0.15;
    blur += texture(uInputTex, v_uv + vec2(0, uTexelSize.y * 2.0)) * 0.15;
    blur += texture(uInputTex, v_uv - vec2(0, uTexelSize.y * 2.0)) * 0.15;
    
    // Add bloom
    fragColor = color + blur * uIntensity;
}
)";

// ────────────────────────────────────────────────────────────────────────────
// ElectricEffect implementation
// ────────────────────────────────────────────────────────────────────────────

ElectricEffect::ElectricEffect() {
    color1 = {0.5f, 0.7f, 1.0f, 1.0f};
    color2 = {1.0f, 1.0f, 1.0f, 1.0f};
    speed = 1.0f;
    intensity = 1.0f;
}

EffectCapabilities ElectricEffect::getCapabilities() const {
    EffectCapabilities caps;
    caps.hasParticles = true;
    caps.hasGlyphShader = true;
    caps.hasParticleShader = true;
    caps.hasPostProcess = true;
    caps.contributesToBloom = true;
    return caps;
}

ShaderSources ElectricEffect::getGlyphShaderSources() const {
    return {s_electricGlyphVert, s_electricGlyphFrag, ""};
}

ShaderSources ElectricEffect::getParticleShaderSources() const {
    return {s_electricParticleVert, s_electricParticleFrag, s_electricParticleGeom};
}

KernelSources ElectricEffect::getKernelSources() const {
    return {s_electricKernel, "updateElectric", "Kernels/particles"};
}

EffectEmissionConfig ElectricEffect::getEmissionConfig() const {
    EffectEmissionConfig cfg;
    cfg.shape = EffectEmissionConfig::Shape::GlyphOutline;
    cfg.rate = 20.0f;
    cfg.velocity = {0, 0};
    cfg.velocityVar = {100, 100};
    cfg.lifetime = 0.3f;
    cfg.lifetimeVar = 0.1f;
    cfg.size = 3.0f;
    cfg.sizeVar = 2.0f;
    return cfg;
}

std::vector<PostProcessPass> ElectricEffect::getPostProcessPasses() const {
    PostProcessPass bloom;
    bloom.name = "ElectricBloom";
    bloom.fragmentShader = s_electricBloomFrag;
    bloom.uniforms["uIntensity"] = 0.5f;
    return {bloom};
}

void ElectricEffect::uploadGlyphUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uTime"), time);
    glUniform1f(glGetUniformLocation(shader, "uArcFrequency"), arcFrequency);
    glUniform1f(glGetUniformLocation(shader, "uJitterStrength"), jitterStrength);
}

void ElectricEffect::uploadParticleUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uTime"), time);
}

void ElectricEffect::bindKernelParams(cl_kernel kernel, const KernelParams& params) const {
    clSetKernelArg(kernel, 7, sizeof(float), &arcFrequency);
    clSetKernelArg(kernel, 8, sizeof(float), &jitterStrength);
    clSetKernelArg(kernel, 9, sizeof(float), &chainChance);
}

REGISTER_EFFECT(ElectricEffect)

} // namespace Markdown
