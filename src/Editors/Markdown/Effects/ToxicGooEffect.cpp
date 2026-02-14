#include <Editors/Markdown/Effects/ToxicGooEffect.hpp>

namespace Markdown {

ToxicGooEffect::ToxicGooEffect() {
    color1 = {0.2f, 1.0f, 0.1f, 1.0f};
    color2 = {0.05f, 0.6f, 0.02f, 1.0f};
    speed = 0.9f;
    intensity = 1.0f;
}

EffectCapabilities ToxicGooEffect::getCapabilities() const {
    return {true, true, true, false, false};
}

GlyphSnippets ToxicGooEffect::getGlyphSnippets() const {
    GlyphSnippets gs;
    gs.fragment.uniformDecls = "uniform vec4 uToxic_Color1;\nuniform vec4 uToxic_Color2;\n";
    gs.fragment.code = R"({
    float drip = smoothstep(0.0, 1.0, v_uv.y);
    vec3 c = mix(uToxic_Color1.rgb, uToxic_Color2.rgb, drip);
    color.rgb = c * 0.95;
})";
    return gs;
}

void ToxicGooEffect::uploadGlyphSnippetUniforms(GLuint shader, float time) const {
    glUniform4fv(glGetUniformLocation(shader, "uToxic_Color1"), 1, &color1[0]);
    glUniform4fv(glGetUniformLocation(shader, "uToxic_Color2"), 1, &color2[0]);
}

EffectEmissionConfig ToxicGooEffect::getEmissionConfig() const {
    EffectEmissionConfig cfg;
    cfg.shape = EffectEmissionConfig::Shape::GlyphAlpha;
    cfg.rate = 22.0f;
    cfg.velocity = {0, 45};
    cfg.velocityVar = {6, 12};
    cfg.lifetime = 6.0f;
    cfg.lifetimeVar = 1.5f;
    cfg.size = 4.0f;
    cfg.sizeVar = 1.2f;
    return cfg;
}

ParticleSnippets ToxicGooEffect::getParticleSnippets() const {
    ParticleSnippets ps;
    ps.geometry.code = R"({
    size *= 1.4;
    color.a *= smoothstep(0.0, 0.2, life) * 0.7;
})";
    ps.fragment.code = R"({
    float r = dist * 2.0;
    alpha = exp(-r * r * 2.2);
})";
    return ps;
}

void ToxicGooEffect::uploadParticleSnippetUniforms(GLuint shader, float time) const {
}

KernelSnippet ToxicGooEffect::getKernelSnippet() const {
    KernelSnippet ks;
    ks.argDecls = ",\n    const float2 gravity,\n    const float drag,\n    const float splatSize";
    ks.behaviorCode = R"(
    // Slightly buoyant/viscous goo with jitter
    p.vel += gravity * deltaTime;
    p.vel.x += randRange(&rngState, -4.0f, 4.0f) * deltaTime;
    // Small pulsing wobble
    p.vel.y += sin(time * 2.0f + p.pos.x * 0.01f) * 2.0f * deltaTime;
    p.vel *= drag;
    p.life -= deltaTime * 0.25f;
    float lifeRatio = p.life / p.maxLife;
    p.color = (float4)(0.2f + 0.6f * lifeRatio, 1.0f * lifeRatio, 0.1f, lifeRatio);
)";
    ks.collisionResponse = R"(
    // Stick + slow slide, occasional detach
    float2 tangent = (float2)(-docNorm.y, docNorm.x);
    float vTan = dot(p.vel, tangent) * 0.85f;
    float gTan = dot(gravity, tangent);
    vTan += gTan * deltaTime;
    float gravNormal = dot(gravity, docNorm);
    float drip = 0.0f;
    if (gravNormal < 0.0f) drip = -gravNormal * deltaTime * 0.2f;
    p.vel = tangent * vTan * 0.7f - docNorm * drip;
    newPos = p.pos + docNorm * 0.5f + p.vel * deltaTime;
    p.life -= deltaTime * 0.03f;
    p.size = min(p.size * 1.0015f, splatSize * 3.0f);
)";
    ks.defaultDamping = 0.12f;
    return ks;
}

void ToxicGooEffect::bindKernelSnippetParams(cl_kernel kernel, const KernelParams& params, int firstArgIndex) const {
    cl_float2 grav = {{gravity[0], gravity[1]}};
    clSetKernelArg(kernel, firstArgIndex + 0, sizeof(cl_float2), &grav);
    clSetKernelArg(kernel, firstArgIndex + 1, sizeof(float), &drag);
    clSetKernelArg(kernel, firstArgIndex + 2, sizeof(float), &splatSize);
}

SPHParams ToxicGooEffect::getSPHParams() const {
    SPHParams p;
    p.smoothingRadius = sphSmoothingRadius;
    p.restDensity = sphRestDensity;
    p.stiffness = sphStiffness;
    p.viscosity = sphViscosity;
    p.cohesion = sphCohesion;
    p.particleMass = sphParticleMass;
    return p;
}

REGISTER_EFFECT(ToxicGooEffect)

} // namespace Markdown