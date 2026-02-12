#include <Editors/Markdown/Effects/MagicEffect.hpp>

namespace Markdown {

static const char* s_magicGlyphVert = R"(
#version 330 core
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) in uint in_effectID;

uniform mat4 uMVP;

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

static const char* s_magicGlyphFrag = R"(
#version 330 core
uniform sampler2D uFontAtlas;
uniform float uTime;
uniform vec4 uColor1;
uniform float uIntensity;
uniform float uSpeed;

in vec2 v_uv;
in vec4 v_color;
in vec3 v_worldPos;

out vec4 fragColor;

void main() {
    float alpha = texture(uFontAtlas, v_uv).a;
    
    float pulse = 0.7 + 0.3 * sin(uTime * 2.5 * uSpeed + v_worldPos.x * 0.05);
    float strength = clamp(uIntensity * pulse, 0.0, 1.0);
    
    vec3 magicColor = mix(v_color.rgb, uColor1.rgb, strength * 0.8);
    float glowAlpha = smoothstep(0.0, 0.5, alpha) * (1.0 + strength * 0.3);
    
    fragColor = vec4(magicColor, alpha * v_color.a * glowAlpha);
}
)";

static const char* s_magicParticleVert = R"(
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

static const char* s_magicParticleGeom = R"(
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

void main() {
    if (v_life[0] <= 0) return;
    
    vec3 pos = vec3(gl_in[0].gl_Position.xy, v_z[0]);
    float size = v_size[0];
    vec4 color = v_color[0];
    
    float lifeNorm = v_life[0];
    float fade = smoothstep(0.0, 0.3, lifeNorm) * smoothstep(1.0, 0.7, lifeNorm);
    color.a *= fade;
    
    // Pulsing size
    size *= 0.8 + 0.3 * sin(uTime * 8.0 + pos.x * 0.2);
    
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

static const char* s_magicParticleFrag = R"(
#version 330 core
in vec2 g_uv;
in vec4 g_color;

out vec4 fragColor;

void main() {
    vec2 centered = g_uv - vec2(0.5);
    float dist = length(centered);
    
    // Soft glow with bright center
    float core = 1.0 - smoothstep(0.0, 0.2, dist);
    float glow = 1.0 - smoothstep(0.1, 0.5, dist);
    float alpha = core * 0.8 + glow * 0.4;
    
    fragColor = vec4(g_color.rgb * (1.0 + core * 0.5), g_color.a * alpha);
}
)";

static const char* s_magicKernel = R"(
#include "common.cl"

__kernel void updateMagic(
    __global Particle* particles,
    __read_only image2d_t collision,
    const float deltaTime,
    const float scrollY,
    const float maskHeight,
    const float time,
    const uint count,
    const float orbitSpeed,
    const float orbitRadius,
    const float riseSpeed
) {
    uint gid = get_global_id(0);
    if (gid >= count) return;
    
    Particle p = particles[gid];
    if (p.life <= 0.0f) return;
    if (p.behaviorID != BEHAVIOR_MAGIC) return;
    
    uint rngState = (uint)(p.pos.x * 1000.0f) ^ (uint)(p.pos.y * 1000.0f) ^ gid;
    
    // Orbital motion
    float phase = time * orbitSpeed + (float)gid * 0.7f;
    float targetVx = cos(phase) * orbitRadius;
    float targetVy = -riseSpeed + sin(phase) * orbitRadius * 0.5f;
    
    p.vel.x = mix(p.vel.x, targetVx, deltaTime * 3.0f);
    p.vel.y = mix(p.vel.y, targetVy, deltaTime * 3.0f);
    
    // Update position
    float2 newPos = p.pos + p.vel * deltaTime;
    
    // Collision
    float2 newMaskPos = docToMask(newPos, scrollY, maskHeight);
    float newCol = sampleCollision(collision, collisionSampler, newMaskPos);
    
    if (newCol > 0.5f) {
        float2 norm = surfaceNormal(collision, collisionSampler, newMaskPos);
        float2 docNorm = (float2)(norm.x, -norm.y);
        p.vel = reflect(p.vel, docNorm) * 0.4f;
        newPos = p.pos;
    }
    
    p.pos = newPos;
    
    // Life decay
    p.life -= deltaTime * 0.7f;
    
    // Color: purple with shimmering brightness
    float shimmer = sin(time * 8.0f + p.pos.x * 0.1f) * 0.5f + 0.5f;
    float lifeRatio = p.life / p.maxLife;
    p.color = (float4)(0.5f + shimmer * 0.3f, 0.2f + shimmer * 0.1f, 0.8f + shimmer * 0.2f, lifeRatio);
    
    // Slight size variation
    p.size = 2.5f + shimmer;
    
    particles[gid] = p;
}
)";

MagicEffect::MagicEffect() {
    color1 = {0.7f, 0.3f, 1.0f, 1.0f};
    color2 = {0.9f, 0.5f, 1.0f, 1.0f};
    speed = 1.0f;
    intensity = 0.9f;
}

EffectCapabilities MagicEffect::getCapabilities() const {
    return {true, true, true, false, true};
}

ShaderSources MagicEffect::getGlyphShaderSources() const {
    return {s_magicGlyphVert, s_magicGlyphFrag, ""};
}

ShaderSources MagicEffect::getParticleShaderSources() const {
    return {s_magicParticleVert, s_magicParticleFrag, s_magicParticleGeom};
}

KernelSources MagicEffect::getKernelSources() const {
    return {s_magicKernel, "updateMagic", "Kernels/particles"};
}

EffectEmissionConfig MagicEffect::getEmissionConfig() const {
    EffectEmissionConfig cfg;
    cfg.shape = EffectEmissionConfig::Shape::GlyphOutline;
    cfg.rate = 12.0f;
    cfg.velocity = {0, -25};
    cfg.velocityVar = {25, 15};
    cfg.lifetime = 1.5f;
    cfg.lifetimeVar = 0.3f;
    cfg.size = 3.0f;
    cfg.sizeVar = 1.5f;
    return cfg;
}

void MagicEffect::uploadGlyphUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uTime"), time);
    glUniform1f(glGetUniformLocation(shader, "uIntensity"), intensity);
    glUniform1f(glGetUniformLocation(shader, "uSpeed"), speed);
    glUniform4fv(glGetUniformLocation(shader, "uColor1"), 1, &color1[0]);
}

EffectSnippet MagicEffect::getSnippet() const {
    EffectSnippet s;
    s.uniformDecls = "uniform vec4 uMagic_Color1;\nuniform float uMagic_Intensity;\nuniform float uMagic_Speed;\n";
    s.fragmentCode = R"({
    float magicPulse = 0.7 + 0.3 * sin(uTime * 2.5 * uMagic_Speed + v_worldPos.x * 0.05);
    float magicStrength = clamp(uMagic_Intensity * magicPulse, 0.0, 1.0);
    color.rgb = mix(color.rgb, uMagic_Color1.rgb, magicStrength * 0.8);
    float magicGlow = smoothstep(0.0, 0.5, alpha) * (1.0 + magicStrength * 0.3);
    color.a *= magicGlow;
})";
    return s;
}

void MagicEffect::uploadSnippetUniforms(GLuint shader, float time) const {
    glUniform4fv(glGetUniformLocation(shader, "uMagic_Color1"), 1, &color1[0]);
    glUniform1f(glGetUniformLocation(shader, "uMagic_Intensity"), intensity);
    glUniform1f(glGetUniformLocation(shader, "uMagic_Speed"), speed);
}

void MagicEffect::uploadParticleUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uTime"), time);
}

void MagicEffect::bindKernelParams(cl_kernel kernel, const KernelParams& params) const {
    clSetKernelArg(kernel, 7, sizeof(float), &orbitSpeed);
    clSetKernelArg(kernel, 8, sizeof(float), &orbitRadius);
    clSetKernelArg(kernel, 9, sizeof(float), &riseSpeed);
}

REGISTER_EFFECT(MagicEffect)

} // namespace Markdown
