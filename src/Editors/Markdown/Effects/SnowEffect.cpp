#include <Editors/Markdown/Effects/SnowEffect.hpp>

namespace Markdown {

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

GlyphSnippets SnowEffect::getGlyphSnippets() const {
    GlyphSnippets gs;
    gs.fragment.helpers = R"(
float snow_random(vec2 st) {
    return fract(sin(dot(st, vec2(12.9898, 78.233))) * 43758.5453);
}
)";
    gs.fragment.code = R"({
    vec3 snowColor = mix(vec3(0.8, 0.9, 1.0), vec3(1.0), snow_random(v_worldPos.xy * 0.1 + uTime * 0.5));
    float snowSparkle = step(0.98, snow_random(v_worldPos.xy + floor(uTime * 3.0))) * 0.3;
    color.rgb = snowColor + vec3(snowSparkle);
})";
    return gs;
}

void SnowEffect::uploadGlyphSnippetUniforms(GLuint shader, float time) const {
    // No custom uniforms beyond uTime
}

ParticleSnippets SnowEffect::getParticleSnippets() const {
    ParticleSnippets ps;
    // Geometry: faster fade-in than default (0.2 vs 0.3)
    ps.geometry.code = R"({
    color.a *= smoothstep(0.0, 0.2, life);
})";
    // Fragment: standard soft circle (same as default)
    return ps;
}

void SnowEffect::uploadParticleSnippetUniforms(GLuint shader, float time) const {
    // No custom particle uniforms
}

KernelSnippet SnowEffect::getKernelSnippet() const {
    KernelSnippet ks;
    ks.argDecls = ",\n    const float2 gravity,\n    const float2 wind,\n    const float drift,\n    const float meltRate";
    ks.behaviorCode = R"(
    // Apply gravity
    p.vel += gravity * deltaTime;
    
    // Apply wind with drift variation
    float driftNoise = sin(time * 2.0f + p.pos.x * 0.1f) * drift;
    p.vel.x += (wind.x + driftNoise * 30.0f) * deltaTime;
    
    // Limit fall speed
    if (p.vel.y > 100.0f) p.vel.y = 100.0f;
    
    // Drag
    p.vel *= 0.97f;
    
    // Slowly rotate
    p.rotation.z += randRange(&rngState, -0.5f, 0.5f) * deltaTime;
    
    // Life decay
    p.life -= meltRate * deltaTime;
    
    // Fade as life decreases
    float lifeRatio = p.life / p.maxLife;
    p.color.w = lifeRatio;
)";
    // Custom collision: snow settles (stops and melts faster)
    ks.collisionResponse = R"(
    p.vel = (float2)(0.0f, 0.0f);
    newPos = p.pos;
    p.life -= meltRate * deltaTime * 2.0f;
)";
    ks.defaultDamping = 0.3f;
    return ks;
}

void SnowEffect::bindKernelSnippetParams(cl_kernel kernel, const KernelParams& params,
                                           int firstArgIndex) const {
    cl_float2 grav = {{gravity[0], gravity[1]}};
    cl_float2 w = {{wind[0], wind[1]}};
    clSetKernelArg(kernel, firstArgIndex + 0, sizeof(cl_float2), &grav);
    clSetKernelArg(kernel, firstArgIndex + 1, sizeof(cl_float2), &w);
    clSetKernelArg(kernel, firstArgIndex + 2, sizeof(float), &drift);
    clSetKernelArg(kernel, firstArgIndex + 3, sizeof(float), &meltRate);
}

REGISTER_EFFECT(SnowEffect)

} // namespace Markdown
