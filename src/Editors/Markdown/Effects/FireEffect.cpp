#include <Editors/Markdown/Effects/FireEffect.hpp>
#include <plog/Log.h>

namespace Markdown {

// ────────────────────────────────────────────────────────────────────
// Fire glyph vertex shader - wave displacement
// ────────────────────────────────────────────────────────────────────

static const char* s_fireGlyphVert = R"(
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

// ────────────────────────────────────────────────────────────────────
// Fire glyph fragment shader - heat gradient coloring
// ────────────────────────────────────────────────────────────────────

static const char* s_fireGlyphFrag = R"(
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

// ────────────────────────────────────────────────────────────────────
// Fire particle vertex shader
// ────────────────────────────────────────────────────────────────────

static const char* s_fireParticleVert = R"(
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

// ────────────────────────────────────────────────────────────────────
// Fire particle geometry shader - expand points to quads
// ────────────────────────────────────────────────────────────────────

static const char* s_fireParticleGeom = R"(
#version 330 core
layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

uniform mat4 uMVP;
uniform uint uBehaviorID;

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
    
    // Fire particles glow brighter, fade slowly
    color.a *= smoothstep(0.0, 0.3, v_life[0]);
    
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

// ────────────────────────────────────────────────────────────────────
// Fire particle fragment shader - soft glowing circle
// ────────────────────────────────────────────────────────────────────

static const char* s_fireParticleFrag = R"(
#version 330 core
in vec2 g_uv;
in vec4 g_color;

out vec4 fragColor;

void main() {
    // Soft glowing circle
    float dist = length(g_uv - vec2(0.5));
    float alpha = 1.0 - smoothstep(0.2, 0.5, dist);
    
    // Fire particles are additive, so multiply color by alpha for proper blending
    fragColor = vec4(g_color.rgb * alpha, g_color.a * alpha);
}
)";

// ────────────────────────────────────────────────────────────────────
// Fire particle OpenCL kernel
// ────────────────────────────────────────────────────────────────────

static const char* s_fireKernel = R"(
// Fire particle simulation kernel
// Particles rise with turbulence, fade from yellow to red to black

#include "common.cl"

__kernel void updateFire(
    __global Particle* particles,
    __read_only image2d_t collision,
    const float deltaTime,
    const float scrollY,
    const float maskHeight,
    const float time,
    const uint count,
    const float2 gravity,
    const float turbulence,
    const float heatDecay
) {
    uint gid = get_global_id(0);
    if (gid >= count) return;
    
    Particle p = particles[gid];
    
    // Skip dead particles and particles not belonging to this kernel
    if (p.life <= 0.0f) return;
    if (p.behaviorID != BEHAVIOR_FIRE) return;
    
    // Initialize random state from particle position and time
    uint rngState = (uint)(p.pos.x * 1000.0f) ^ (uint)(p.pos.y * 1000.0f) ^ gid;
    
    // Apply gravity (upward for fire)
    p.vel += gravity * deltaTime;
    
    // Add turbulent horizontal motion
    float noise = randRange(&rngState, -1.0f, 1.0f);
    p.vel.x += noise * turbulence * deltaTime;
    
    // Apply drag
    p.vel *= 0.98f;
    
    // Update position
    float2 newPos = p.pos + p.vel * deltaTime;
    
    // Two-point collision check
    float2 newMaskPos = docToMask(newPos, scrollY, maskHeight);
    float2 curMaskPos = docToMask(p.pos, scrollY, maskHeight);
    float newCol = sampleCollision(collision, collisionSampler, newMaskPos);
    float curCol = sampleCollision(collision, collisionSampler, curMaskPos);
    
    if (newCol > 0.5f && curCol <= 0.5f) {
        float2 maskNorm = surfaceNormal(collision, collisionSampler, newMaskPos);
        float2 docNorm = (float2)(maskNorm.x, -maskNorm.y);
        p.vel = reflect(p.vel, docNorm) * 0.3f;
        newPos = p.pos;
    }
    
    p.pos = newPos;
    
    // Decay life
    p.life -= heatDecay * deltaTime;
    
    // Update color based on temperature (life ratio)
    float heat = p.life / p.maxLife;
    if (heat > 0.7f) {
        float t = (heat - 0.7f) / 0.3f;
        p.color = (float4)(1.0f, 1.0f, t, 1.0f);
    } else if (heat > 0.4f) {
        float t = (heat - 0.4f) / 0.3f;
        p.color = (float4)(1.0f, 0.5f + 0.5f * t, 0.0f, 1.0f);
    } else if (heat > 0.1f) {
        float t = (heat - 0.1f) / 0.3f;
        p.color = (float4)(1.0f, 0.1f + 0.4f * t, 0.0f, 0.8f);
    } else {
        float t = heat / 0.1f;
        p.color = (float4)(t * 0.5f, 0.0f, 0.0f, t * 0.5f);
    }
    
    // Size grows as heat decreases (smoke expansion)
    p.size = mix(p.size, p.size * 1.5f, (1.0f - heat) * 0.02f);
    
    // Spin
    p.rotation.z += randRange(&rngState, -2.0f, 2.0f) * deltaTime;
    
    particles[gid] = p;
}
)";

// ────────────────────────────────────────────────────────────────────
// FireEffect implementation
// ────────────────────────────────────────────────────────────────────

FireEffect::FireEffect() {
    // Set default fire colors
    color1 = {1.0f, 0.3f, 0.0f, 1.0f};  // Orange
    color2 = {1.0f, 0.8f, 0.0f, 1.0f};  // Yellow
    speed = 1.0f;
    intensity = 1.0f;
}

EffectCapabilities FireEffect::getCapabilities() const {
    EffectCapabilities caps;
    caps.hasParticles = true;
    caps.hasGlyphShader = true;
    caps.hasParticleShader = true;
    caps.hasPostProcess = false;
    caps.contributesToBloom = false;
    return caps;
}

ShaderSources FireEffect::getGlyphShaderSources() const {
    return {s_fireGlyphVert, s_fireGlyphFrag, ""};
}

ShaderSources FireEffect::getParticleShaderSources() const {
    return {s_fireParticleVert, s_fireParticleFrag, s_fireParticleGeom};
}

KernelSources FireEffect::getKernelSources() const {
    return {s_fireKernel, "updateFire", "Kernels/particles"};
}

EffectEmissionConfig FireEffect::getEmissionConfig() const {
    EffectEmissionConfig cfg;
    cfg.shape = EffectEmissionConfig::Shape::GlyphAlpha;
    cfg.rate = 8.0f;
    cfg.velocity = {0, -80};
    cfg.velocityVar = {20, 30};
    cfg.lifetime = 1.2f;
    cfg.lifetimeVar = 0.3f;
    cfg.size = 4.0f;
    cfg.sizeVar = 2.0f;
    return cfg;
}

void FireEffect::uploadGlyphUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uTime"), time);
    glUniform1f(glGetUniformLocation(shader, "uSpeed"), speed);
    glUniform1f(glGetUniformLocation(shader, "uIntensity"), intensity);
    glUniform4fv(glGetUniformLocation(shader, "uColor1"), 1, &color1[0]);
    glUniform4fv(glGetUniformLocation(shader, "uColor2"), 1, &color2[0]);
}

void FireEffect::uploadParticleUniforms(GLuint shader, float time) const {
    glUniform1ui(glGetUniformLocation(shader, "uBehaviorID"), getBehaviorID());
}

void FireEffect::bindKernelParams(cl_kernel kernel, const KernelParams& params) const {
    // Standard params are bound at indices 0-6
    // Fire-specific params start at index 7
    cl_float2 grav = {{gravity[0], gravity[1]}};
    clSetKernelArg(kernel, 7, sizeof(cl_float2), &grav);
    clSetKernelArg(kernel, 8, sizeof(float), &turbulence);
    clSetKernelArg(kernel, 9, sizeof(float), &heatDecay);
}

// Auto-register the effect
REGISTER_EFFECT(FireEffect)

} // namespace Markdown
