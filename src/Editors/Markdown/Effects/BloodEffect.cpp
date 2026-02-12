#include <Editors/Markdown/Effects/BloodEffect.hpp>

namespace Markdown {

static const char* s_bloodParticleVert = R"(
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

static const char* s_bloodParticleGeom = R"(
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
    
    // Drip shape: taller than wide when moving fast
    float aspect = 1.0 + abs(v_life[0]) * 0.3;
    color.a *= smoothstep(0.0, 0.3, v_life[0]);
    
    vec3 right = vec3(1, 0, 0) * size;
    vec3 up = vec3(0, aspect, 0) * size;
    
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

static const char* s_bloodParticleFrag = R"(
#version 330 core
in vec2 g_uv;
in vec4 g_color;

out vec4 fragColor;

void main() {
    float dist = length(g_uv - vec2(0.5));
    float alpha = 1.0 - smoothstep(0.25, 0.5, dist);
    fragColor = vec4(g_color.rgb, g_color.a * alpha);
}
)";

static const char* s_bloodKernel = R"(
#include "common.cl"

__kernel void updateBlood(
    __global Particle* particles,
    __read_only image2d_t collision,
    const float deltaTime,
    const float scrollY,
    const float maskHeight,
    const float time,
    const uint count,
    const float2 gravity,
    const float drag,
    const float splatSize
) {
    uint gid = get_global_id(0);
    if (gid >= count) return;
    
    Particle p = particles[gid];
    if (p.life <= 0.0f) return;
    if (p.behaviorID != BEHAVIOR_BLOOD) return;
    
    uint rngState = (uint)(p.pos.x * 1000.0f) ^ (uint)(p.pos.y * 1000.0f) ^ gid;
    
    // Gravity
    p.vel += gravity * deltaTime;
    
    // Slight horizontal wobble
    p.vel.x += randRange(&rngState, -5.0f, 5.0f) * deltaTime;
    
    // Drag
    p.vel *= drag;
    
    // Update position
    float2 newPos = p.pos + p.vel * deltaTime;
    
    // Collision
    float2 newMaskPos = docToMask(newPos, scrollY, maskHeight);
    float2 curMaskPos = docToMask(p.pos, scrollY, maskHeight);
    float newCol = sampleCollision(collision, collisionSampler, newMaskPos);
    float curCol = sampleCollision(collision, collisionSampler, curMaskPos);
    
    if (newCol > 0.5f && curCol <= 0.5f) {
        // Splat: slow down dramatically and expand
        float2 norm = surfaceNormal(collision, collisionSampler, newMaskPos);
        float2 docNorm = (float2)(norm.x, -norm.y);
        p.vel = reflect(p.vel, docNorm) * 0.15f;
        newPos = p.pos;
        p.size = min(p.size * splatSize, 8.0f);
        p.life -= deltaTime * 3.0f;  // Die faster on collision
    }
    
    p.pos = newPos;
    p.life -= deltaTime * 0.8f;
    
    // Darken as life fades
    float lifeRatio = p.life / p.maxLife;
    p.color = (float4)(0.6f + 0.2f * lifeRatio, 0.0f, 0.0f, lifeRatio);
    
    particles[gid] = p;
}
)";

BloodEffect::BloodEffect() {
    color1 = {0.8f, 0.0f, 0.0f, 1.0f};
    color2 = {0.4f, 0.0f, 0.0f, 1.0f};
    speed = 1.0f;
    intensity = 1.0f;
}

EffectCapabilities BloodEffect::getCapabilities() const {
    return {true, false, true, false, false};
}

ShaderSources BloodEffect::getParticleShaderSources() const {
    return {s_bloodParticleVert, s_bloodParticleFrag, s_bloodParticleGeom};
}

KernelSources BloodEffect::getKernelSources() const {
    return {s_bloodKernel, "updateBlood", "Kernels/particles"};
}

EffectEmissionConfig BloodEffect::getEmissionConfig() const {
    EffectEmissionConfig cfg;
    cfg.shape = EffectEmissionConfig::Shape::GlyphAlpha;
    cfg.rate = 10.0f;
    cfg.velocity = {0, 60};
    cfg.velocityVar = {5, 15};
    cfg.lifetime = 2.0f;
    cfg.lifetimeVar = 0.5f;
    cfg.size = 3.0f;
    cfg.sizeVar = 1.0f;
    return cfg;
}

void BloodEffect::uploadParticleUniforms(GLuint shader, float time) const {
    // No custom uniforms
}

void BloodEffect::bindKernelParams(cl_kernel kernel, const KernelParams& params) const {
    cl_float2 grav = {{gravity[0], gravity[1]}};
    clSetKernelArg(kernel, 7, sizeof(cl_float2), &grav);
    clSetKernelArg(kernel, 8, sizeof(float), &drag);
    clSetKernelArg(kernel, 9, sizeof(float), &splatSize);
}

REGISTER_EFFECT(BloodEffect)

} // namespace Markdown
