#include <Editors/Markdown/Effects/SnowEffect.hpp>

namespace Markdown {

// ────────────────────────────────────────────────────────────────────
// Snow glyph vertex shader - subtle shimmer
// ────────────────────────────────────────────────────────────────────

static const char* s_snowGlyphVert = R"(
#version 330 core
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) in uint in_effectID;

uniform mat4 uMVP;
uniform float uTime;

out vec2 v_uv;
out vec4 v_color;
out vec3 v_worldPos;

void main() {
    gl_Position = uMVP * vec4(in_pos, 1.0);
    v_uv = in_uv;
    v_color = in_color;
    v_worldPos = in_pos;
}
)";

// ────────────────────────────────────────────────────────────────────
// Snow glyph fragment shader - icy blue tint with sparkle
// ────────────────────────────────────────────────────────────────────

static const char* s_snowGlyphFrag = R"(
#version 330 core
uniform sampler2D uFontAtlas;
uniform float uTime;

in vec2 v_uv;
in vec4 v_color;
in vec3 v_worldPos;

out vec4 fragColor;

float random(vec2 st) {
    return fract(sin(dot(st, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    float alpha = texture(uFontAtlas, v_uv).a;
    
    // Icy blue-white tint
    vec3 snowColor = mix(vec3(0.8, 0.9, 1.0), vec3(1.0, 1.0, 1.0), random(v_worldPos.xy * 0.1 + uTime * 0.5));
    
    // Sparkle effect
    float sparkle = step(0.98, random(v_worldPos.xy + floor(uTime * 3.0))) * 0.3;
    snowColor += sparkle;
    
    fragColor = vec4(snowColor, alpha * v_color.a);
}
)";

// ────────────────────────────────────────────────────────────────────
// Snow particle vertex shader
// ────────────────────────────────────────────────────────────────────

static const char* s_snowParticleVert = R"(
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
// Snow particle geometry shader
// ────────────────────────────────────────────────────────────────────

static const char* s_snowParticleGeom = R"(
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
    
    color.a *= smoothstep(0.0, 0.2, v_life[0]);
    
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
// Snow particle fragment shader
// ────────────────────────────────────────────────────────────────────

static const char* s_snowParticleFrag = R"(
#version 330 core
in vec2 g_uv;
in vec4 g_color;

out vec4 fragColor;

void main() {
    float dist = length(g_uv - vec2(0.5));
    float alpha = 1.0 - smoothstep(0.3, 0.5, dist);
    fragColor = vec4(g_color.rgb, g_color.a * alpha);
}
)";

// ────────────────────────────────────────────────────────────────────
// Snow particle OpenCL kernel
// ────────────────────────────────────────────────────────────────────

static const char* s_snowKernel = R"(
// Snow particle simulation kernel
// Particles fall with wind drift and settle on surfaces

#include "common.cl"

__kernel void updateSnow(
    __global Particle* particles,
    __read_only image2d_t collision,
    const float deltaTime,
    const float scrollY,
    const float maskHeight,
    const float time,
    const uint count,
    const float2 gravity,
    const float2 wind,
    const float drift,
    const float meltRate
) {
    uint gid = get_global_id(0);
    if (gid >= count) return;
    
    Particle p = particles[gid];
    
    if (p.life <= 0.0f) return;
    if (p.behaviorID != BEHAVIOR_SNOW) return;
    
    uint rngState = (uint)(p.pos.x * 1000.0f) ^ (uint)(p.pos.y * 1000.0f) ^ gid;
    
    // Apply gravity
    p.vel += gravity * deltaTime;
    
    // Apply wind with drift variation
    float driftNoise = sin(time * 2.0f + p.pos.x * 0.1f) * drift;
    p.vel.x += (wind.x + driftNoise * 30.0f) * deltaTime;
    
    // Limit fall speed
    if (p.vel.y > 100.0f) p.vel.y = 100.0f;
    
    // Drag
    p.vel *= 0.97f;
    
    // Update position
    float2 newPos = p.pos + p.vel * deltaTime;
    
    // Collision check
    float2 newMaskPos = docToMask(newPos, scrollY, maskHeight);
    float2 curMaskPos = docToMask(p.pos, scrollY, maskHeight);
    float newCol = sampleCollision(collision, collisionSampler, newMaskPos);
    float curCol = sampleCollision(collision, collisionSampler, curMaskPos);
    
    if (newCol > 0.5f && curCol <= 0.5f) {
        // Snow settles - stop and start melting
        p.vel = (float2)(0.0f, 0.0f);
        newPos = p.pos;
        p.life -= meltRate * deltaTime * 2.0f;  // Faster melt when settled
    } else {
        p.life -= meltRate * deltaTime;
    }
    
    p.pos = newPos;
    
    // Slowly rotate
    p.rotation.z += randRange(&rngState, -0.5f, 0.5f) * deltaTime;
    
    // Fade as life decreases
    float lifeRatio = p.life / p.maxLife;
    p.color.w = lifeRatio;
    
    particles[gid] = p;
}
)";

// ────────────────────────────────────────────────────────────────────────────
// SnowEffect implementation
// ────────────────────────────────────────────────────────────────────────────

SnowEffect::SnowEffect() {
    color1 = {1.0f, 1.0f, 1.0f, 1.0f};
    color2 = {0.9f, 0.95f, 1.0f, 1.0f};
    speed = 1.0f;
    intensity = 1.0f;
}

EffectCapabilities SnowEffect::getCapabilities() const {
    EffectCapabilities caps;
    caps.hasParticles = true;
    caps.hasGlyphShader = true;
    caps.hasParticleShader = true;
    caps.hasPostProcess = false;
    caps.contributesToBloom = false;
    return caps;
}

ShaderSources SnowEffect::getGlyphShaderSources() const {
    return {s_snowGlyphVert, s_snowGlyphFrag, ""};
}

ShaderSources SnowEffect::getParticleShaderSources() const {
    return {s_snowParticleVert, s_snowParticleFrag, s_snowParticleGeom};
}

KernelSources SnowEffect::getKernelSources() const {
    return {s_snowKernel, "updateSnow", "Kernels/particles"};
}

EffectEmissionConfig SnowEffect::getEmissionConfig() const {
    EffectEmissionConfig cfg;
    cfg.shape = EffectEmissionConfig::Shape::ScreenTop;
    cfg.rate = 15.0f;
    cfg.velocity = {0, 30};
    cfg.velocityVar = {15, 10};
    cfg.lifetime = 5.0f;
    cfg.lifetimeVar = 1.0f;
    cfg.size = 3.0f;
    cfg.sizeVar = 1.5f;
    return cfg;
}

void SnowEffect::uploadGlyphUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uTime"), time);
}

void SnowEffect::uploadParticleUniforms(GLuint shader, float time) const {
    // No custom uniforms needed
}

void SnowEffect::bindKernelParams(cl_kernel kernel, const KernelParams& params) const {
    cl_float2 grav = {{gravity[0], gravity[1]}};
    cl_float2 w = {{wind[0], wind[1]}};
    clSetKernelArg(kernel, 7, sizeof(cl_float2), &grav);
    clSetKernelArg(kernel, 8, sizeof(cl_float2), &w);
    clSetKernelArg(kernel, 9, sizeof(float), &drift);
    clSetKernelArg(kernel, 10, sizeof(float), &meltRate);
}

REGISTER_EFFECT(SnowEffect)

} // namespace Markdown
