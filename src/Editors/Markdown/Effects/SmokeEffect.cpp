#include <Editors/Markdown/Effects/SmokeEffect.hpp>

namespace Markdown {

SmokeEffect::SmokeEffect() {
    color1 = {0.5f, 0.5f, 0.5f, 0.6f};
    color2 = {0.3f, 0.3f, 0.3f, 0.3f};
    speed = 1.0f;
    intensity = 1.0f;
}

EffectCapabilities SmokeEffect::getCapabilities() const {
    return {true, false, true, false, false};
}

EffectEmissionConfig SmokeEffect::getEmissionConfig() const {
    EffectEmissionConfig cfg;
    cfg.shape = EffectEmissionConfig::Shape::GlyphAlpha;
    cfg.rate = 10.0f;
    cfg.velocity = {0, -20};
    cfg.velocityVar = {15, 10};
    cfg.lifetime = 2.0f;
    cfg.lifetimeVar = 0.5f;
    cfg.size = 4.0f;
    cfg.sizeVar = 2.0f;
    return cfg;
}

ParticleSnippets SmokeEffect::getParticleSnippets() const {
    ParticleSnippets ps;
    // Fragment: largest soft region with opacity cap
    ps.fragment.code = R"({
    alpha = (1.0 - smoothstep(0.15, 0.5, dist)) * 0.6;
})";
    return ps;
}

void SmokeEffect::uploadParticleSnippetUniforms(GLuint shader, float time) const {
    // No custom particle uniforms
}

KernelSnippet SmokeEffect::getKernelSnippet() const {
    KernelSnippet ks;
    ks.argDecls = ",\n    const float2 wind,\n    const float riseSpeed,\n    const float expansion,\n    const float dissipation";
    ks.behaviorCode = R"(
    // Rise upward
    p.vel.y -= riseSpeed * deltaTime;
    
    // Wind
    p.vel.x += wind.x * deltaTime;
    p.vel.y += wind.y * deltaTime;
    
    // Turbulence
    p.vel.x += randRange(&rngState, -15.0f, 15.0f) * deltaTime;
    
    // Drag
    p.vel *= 0.96f;
    
    // Expand over time
    float lifeRatio = p.life / p.maxLife;
    p.size += expansion * deltaTime;
    
    // Dissipate
    p.life -= dissipation * deltaTime;
    p.color.w = lifeRatio * 0.5f;
    
    // Fade to gray
    float gray = 0.3f + 0.2f * lifeRatio;
    p.color.x = gray;
    p.color.y = gray;
    p.color.z = gray;
)";
    ks.defaultDamping = 0.3f;
    return ks;
}

void SmokeEffect::bindKernelSnippetParams(cl_kernel kernel, const KernelParams& params,
                                            int firstArgIndex) const {
    cl_float2 w = {{wind[0], wind[1]}};
    clSetKernelArg(kernel, firstArgIndex + 0, sizeof(cl_float2), &w);
    clSetKernelArg(kernel, firstArgIndex + 1, sizeof(float), &riseSpeed);
    clSetKernelArg(kernel, firstArgIndex + 2, sizeof(float), &expansion);
    clSetKernelArg(kernel, firstArgIndex + 3, sizeof(float), &dissipation);
}

REGISTER_EFFECT(SmokeEffect)

} // namespace Markdown
