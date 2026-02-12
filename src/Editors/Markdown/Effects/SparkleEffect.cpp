#include <Editors/Markdown/Effects/SparkleEffect.hpp>

namespace Markdown {

// ────────────────────────────────────────────────────────────────────
// Sparkle glyph shader - shimmering glitter  
// ────────────────────────────────────────────────────────────────────

static const char* s_sparkleGlyphVert = R"(
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

static const char* s_sparkleGlyphFrag = R"(
#version 330 core
uniform sampler2D uFontAtlas;
uniform float uTime;
uniform float uTwinkleSpeed;

in vec2 v_uv;
in vec4 v_color;
in vec3 v_worldPos;

out vec4 fragColor;

float random(vec2 st) {
    return fract(sin(dot(st, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    float alpha = texture(uFontAtlas, v_uv).a;
    
    // Base golden color
    vec3 baseColor = vec3(1.0, 0.9, 0.6);
    
    // Multiple layers of sparkles at different frequencies
    float sparkle1 = step(0.97, random(floor(v_worldPos.xy * 0.3) + floor(uTime * uTwinkleSpeed)));
    float sparkle2 = step(0.95, random(floor(v_worldPos.xy * 0.5) + floor(uTime * uTwinkleSpeed * 1.3)));
    float sparkle3 = step(0.92, random(floor(v_worldPos.xy * 0.2) + floor(uTime * uTwinkleSpeed * 0.7)));
    
    float sparkle = max(max(sparkle1, sparkle2 * 0.7), sparkle3 * 0.4);
    
    vec3 finalColor = baseColor + vec3(sparkle);
    
    fragColor = vec4(finalColor, alpha * v_color.a);
}
)";

// ────────────────────────────────────────────────────────────────────
// Sparkle particle shaders
// ────────────────────────────────────────────────────────────────────

static const char* s_sparkleParticleVert = R"(
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

static const char* s_sparkleParticleGeom = R"(
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
out float g_twinkle;

void main() {
    if (v_life[0] <= 0) return;
    
    vec3 pos = vec3(gl_in[0].gl_Position.xy, v_z[0]);
    float size = v_size[0];
    vec4 color = v_color[0];
    
    // Twinkle: size pulsates
    float twinkle = sin(uTime * 15.0 + pos.x * 0.1 + pos.y * 0.1) * 0.5 + 0.5;
    size *= 0.5 + twinkle;
    
    // Fade in/out based on life
    float lifeNorm = v_life[0];
    float fade = smoothstep(0.0, 0.3, lifeNorm) * smoothstep(1.0, 0.7, lifeNorm);
    color.a *= fade;
    
    vec3 right = vec3(1, 0, 0) * size;
    vec3 up = vec3(0, 1, 0) * size;
    
    g_color = color;
    g_twinkle = twinkle;
    
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

static const char* s_sparkleParticleFrag = R"(
#version 330 core
in vec2 g_uv;
in vec4 g_color;
in float g_twinkle;

out vec4 fragColor;

void main() {
    vec2 centered = g_uv - vec2(0.5);
    float dist = length(centered);
    
    // Star shape
    float angle = atan(centered.y, centered.x);
    float star = 0.3 + 0.2 * cos(angle * 4.0);
    float alpha = 1.0 - smoothstep(star * 0.3, star * 0.5, dist);
    
    // Bright center
    alpha += (1.0 - smoothstep(0.0, 0.15, dist)) * g_twinkle;
    
    fragColor = vec4(g_color.rgb, g_color.a * alpha);
}
)";

// ────────────────────────────────────────────────────────────────────
// Sparkle particle OpenCL kernel
// ────────────────────────────────────────────────────────────────────

static const char* s_sparkleKernel = R"(
// Sparkle particle simulation - slow drift with twinkle

#include "common.cl"

__kernel void updateSparkle(
    __global Particle* particles,
    __read_only image2d_t collision,
    const float deltaTime,
    const float scrollY,
    const float maskHeight,
    const float time,
    const uint count,
    const float twinkleSpeed,
    const float driftSpeed
) {
    uint gid = get_global_id(0);
    if (gid >= count) return;
    
    Particle p = particles[gid];
    
    if (p.life <= 0.0f) return;
    if (p.behaviorID != BEHAVIOR_SPARKLE) return;
    
    uint rngState = (uint)(p.pos.x * 1000.0f) ^ (uint)(p.pos.y * 1000.0f) ^ gid;
    
    // Gentle drift
    float nx = sin(time * 0.5f + p.pos.y * 0.02f) * driftSpeed;
    float ny = cos(time * 0.7f + p.pos.x * 0.02f) * driftSpeed;
    p.vel.x = mix(p.vel.x, nx, deltaTime * 2.0f);
    p.vel.y = mix(p.vel.y, ny, deltaTime * 2.0f);
    
    // Update position
    float2 newPos = p.pos + p.vel * deltaTime;
    
    // Collision - bounce gently
    float2 newMaskPos = docToMask(newPos, scrollY, maskHeight);
    float newCol = sampleCollision(collision, collisionSampler, newMaskPos);
    
    if (newCol > 0.5f) {
        float2 norm = surfaceNormal(collision, collisionSampler, newMaskPos);
        float2 docNorm = (float2)(norm.x, -norm.y);
        p.vel = reflect(p.vel, docNorm) * 0.5f;
        newPos = p.pos;
    }
    
    p.pos = newPos;
    
    // Twinkle: color brightness pulsates
    float twinkle = sin(time * twinkleSpeed + p.pos.x * 0.1f) * 0.5f + 0.5f;
    p.color = (float4)(1.0f, 0.9f + twinkle * 0.1f, 0.5f + twinkle * 0.3f, 1.0f);
    
    // Life decay
    p.life -= deltaTime * 0.5f;
    
    particles[gid] = p;
}
)";

// ────────────────────────────────────────────────────────────────────────────
// SparkleEffect implementation
// ────────────────────────────────────────────────────────────────────────────

SparkleEffect::SparkleEffect() {
    color1 = {1.0f, 0.9f, 0.5f, 1.0f};
    color2 = {1.0f, 1.0f, 0.8f, 1.0f};
    speed = 1.0f;
    intensity = 1.0f;
}

EffectCapabilities SparkleEffect::getCapabilities() const {
    EffectCapabilities caps;
    caps.hasParticles = true;
    caps.hasGlyphShader = true;
    caps.hasParticleShader = true;
    caps.hasPostProcess = false;
    caps.contributesToBloom = true;
    return caps;
}

ShaderSources SparkleEffect::getGlyphShaderSources() const {
    return {s_sparkleGlyphVert, s_sparkleGlyphFrag, ""};
}

ShaderSources SparkleEffect::getParticleShaderSources() const {
    return {s_sparkleParticleVert, s_sparkleParticleFrag, s_sparkleParticleGeom};
}

KernelSources SparkleEffect::getKernelSources() const {
    return {s_sparkleKernel, "updateSparkle", "Kernels/particles"};
}

EffectEmissionConfig SparkleEffect::getEmissionConfig() const {
    EffectEmissionConfig cfg;
    cfg.shape = EffectEmissionConfig::Shape::GlyphOutline;
    cfg.rate = 5.0f;
    cfg.velocity = {0, 0};
    cfg.velocityVar = {10, 10};
    cfg.lifetime = 2.0f;
    cfg.lifetimeVar = 0.5f;
    cfg.size = 4.0f;
    cfg.sizeVar = 2.0f;
    return cfg;
}

void SparkleEffect::uploadGlyphUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uTime"), time);
    glUniform1f(glGetUniformLocation(shader, "uTwinkleSpeed"), twinkleSpeed);
}

EffectSnippet SparkleEffect::getSnippet() const {
    EffectSnippet s;
    s.uniformDecls = "uniform float uSparkle_TwinkleSpeed;\n";
    s.helpers = R"(
float sparkle_random(vec2 st) {
    return fract(sin(dot(st, vec2(12.9898, 78.233))) * 43758.5453);
}
)";
    s.fragmentCode = R"({
    vec3 sparkleBase = vec3(1.0, 0.9, 0.6);
    float sparkle1 = step(0.97, sparkle_random(floor(v_worldPos.xy * 0.3) + floor(uTime * uSparkle_TwinkleSpeed)));
    float sparkle2 = step(0.95, sparkle_random(floor(v_worldPos.xy * 0.5) + floor(uTime * uSparkle_TwinkleSpeed * 1.3)));
    float sparkle3 = step(0.92, sparkle_random(floor(v_worldPos.xy * 0.2) + floor(uTime * uSparkle_TwinkleSpeed * 0.7)));
    float sparkle = max(max(sparkle1, sparkle2 * 0.7), sparkle3 * 0.4);
    color.rgb = sparkleBase + vec3(sparkle);
})";
    return s;
}

void SparkleEffect::uploadSnippetUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uSparkle_TwinkleSpeed"), twinkleSpeed);
}

void SparkleEffect::uploadParticleUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uTime"), time);
}

void SparkleEffect::bindKernelParams(cl_kernel kernel, const KernelParams& params) const {
    clSetKernelArg(kernel, 7, sizeof(float), &twinkleSpeed);
    clSetKernelArg(kernel, 8, sizeof(float), &driftSpeed);
}

REGISTER_EFFECT(SparkleEffect)

} // namespace Markdown
