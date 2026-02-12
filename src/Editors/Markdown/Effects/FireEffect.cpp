#include <Editors/Markdown/Effects/FireEffect.hpp>
#include <plog/Log.h>

namespace Markdown {

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

GlyphSnippets FireEffect::getGlyphSnippets() const {
    GlyphSnippets gs;
    gs.vertex.uniformDecls = "uniform float uFire_Speed;\nuniform float uFire_Intensity;\n";
    gs.vertex.code = R"({
    float fireHeight = 1.0 - in_uv.y;
    float fireWave = sin(in_pos.x * 0.1 + uTime * 5.0 * uFire_Speed) * fireHeight * uFire_Intensity * 3.0;
    pos.x += fireWave;
    pos.y += sin(uTime * 7.0 * uFire_Speed + in_pos.x * 0.2) * fireHeight * uFire_Intensity;
})";
    gs.fragment.uniformDecls = "uniform vec4 uFire_Color1;\nuniform vec4 uFire_Color2;\n";
    gs.fragment.helpers = R"(
float fireHash(vec2 p) {
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453);
}
float fireNoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = fireHash(i);
    float b = fireHash(i + vec2(1.0, 0.0));
    float c = fireHash(i + vec2(0.0, 1.0));
    float d = fireHash(i + vec2(1.0, 1.0));
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}
)";
    gs.fragment.code = R"({
    float heat = 1.0 - v_uv.y;
    vec3 fireColor = mix(uFire_Color1.rgb, uFire_Color2.rgb, heat);
    // Dark blobs via scrolling noise
    float n = fireNoise(v_worldPos.xy * 0.08 + vec2(0.0, uTime * 2.5));
    float blob = smoothstep(0.35, 0.65, n);
    fireColor *= mix(0.3, 1.0, blob);
    color.rgb = fireColor;
    float fireGlow = smoothstep(0.0, 0.5, alpha) * (1.0 + heat * 0.5);
    color.a *= fireGlow;
})";
    return gs;
}

void FireEffect::uploadGlyphSnippetUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uFire_Speed"), speed);
    glUniform1f(glGetUniformLocation(shader, "uFire_Intensity"), intensity);
    glUniform4fv(glGetUniformLocation(shader, "uFire_Color1"), 1, &color1[0]);
    glUniform4fv(glGetUniformLocation(shader, "uFire_Color2"), 1, &color2[0]);
}

ParticleSnippets FireEffect::getParticleSnippets() const {
    ParticleSnippets ps;
    // Geometry: fire uses smoothstep(0.0, 0.3, life) for alpha — this is the template default
    // No custom geometry code needed for fire (default alpha fade is fine)
    
    // Fragment: soft glowing circle with tighter core, pre-multiplied alpha
    ps.fragment.code = R"({
    // Tighter glowing core
    alpha = 1.0 - smoothstep(0.2, 0.5, dist);
})";
    return ps;
}

void FireEffect::uploadParticleSnippetUniforms(GLuint shader, float time) const {
    // Fire particles don't need extra uniforms beyond uMVP/uTime
}

KernelSnippet FireEffect::getKernelSnippet() const {
    KernelSnippet ks;
    ks.argDecls = ",\n    const float2 gravity,\n    const float turbulence,\n    const float heatDecay";
    ks.behaviorCode = R"(
    // Apply gravity (upward for fire)
    p.vel += gravity * deltaTime;
    
    // Add turbulent horizontal motion
    float noise = randRange(&rngState, -1.0f, 1.0f);
    p.vel.x += noise * turbulence * deltaTime;
    
    // Apply drag
    p.vel *= 0.98f;
    
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
)";
    ks.defaultDamping = 0.3f;
    return ks;
}

void FireEffect::bindKernelSnippetParams(cl_kernel kernel, const KernelParams& params,
                                          int firstArgIndex) const {
    cl_float2 grav = {{gravity[0], gravity[1]}};
    clSetKernelArg(kernel, firstArgIndex + 0, sizeof(cl_float2), &grav);
    clSetKernelArg(kernel, firstArgIndex + 1, sizeof(float), &turbulence);
    clSetKernelArg(kernel, firstArgIndex + 2, sizeof(float), &heatDecay);
}

// Auto-register the effect
REGISTER_EFFECT(FireEffect)

} // namespace Markdown
