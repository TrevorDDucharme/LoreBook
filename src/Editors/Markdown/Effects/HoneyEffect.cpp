#include <Editors/Markdown/Effects/HoneyEffect.hpp>

namespace Markdown {

HoneyEffect::HoneyEffect() {
    color1 = {0.9f, 0.7f, 0.2f, 1.0f};
    color2 = {0.6f, 0.4f, 0.05f, 1.0f};
    speed = 0.6f;
    intensity = 1.0f;
}

EffectCapabilities HoneyEffect::getCapabilities() const {
    return {true, true, true, false, false};
}

GlyphSnippets HoneyEffect::getGlyphSnippets() const {
    GlyphSnippets gs;
    gs.fragment.uniformDecls = "uniform vec4 uHoney_Color1;\nuniform vec4 uHoney_Color2;\n";
    gs.fragment.code = R"({
    float drip = smoothstep(0.0, 1.0, v_uv.y);
    vec3 c = mix(uHoney_Color1.rgb, uHoney_Color2.rgb, drip);
    color.rgb = c * 0.9;
})";
    return gs;
}

void HoneyEffect::uploadGlyphSnippetUniforms(GLuint shader, float time) const {
    glUniform4fv(glGetUniformLocation(shader, "uHoney_Color1"), 1, &color1[0]);
    glUniform4fv(glGetUniformLocation(shader, "uHoney_Color2"), 1, &color2[0]);
}

EffectEmissionConfig HoneyEffect::getEmissionConfig() const {
    EffectEmissionConfig cfg;
    cfg.shape = EffectEmissionConfig::Shape::GlyphAlpha;
    cfg.rate = 18.0f;
    cfg.velocity = {0, 18};
    cfg.velocityVar = {3, 6};
    cfg.lifetime = 10.0f;
    cfg.lifetimeVar = 2.0f;
    cfg.size = 2.5f; // reduced default size — was 5.0f
    cfg.sizeVar = 1.0f;
    return cfg;
}

ParticleSnippets HoneyEffect::getParticleSnippets() const {
    ParticleSnippets ps;
    ps.geometry.code = R"({
    size *= 1.2; // smaller footprint for thick goo (was 1.8)
    color.a *= smoothstep(0.0, 0.25, life) * 0.6;
})";
    ps.fragment.code = R"({
    float r = dist * 1.6;
    alpha = exp(-r * r * 1.8);
})";
    return ps;
}

void HoneyEffect::uploadParticleSnippetUniforms(GLuint shader, float time) const {
}

KernelSnippet HoneyEffect::getKernelSnippet() const {
    KernelSnippet ks;
    ks.argDecls = ",\n    const float2 gravity,\n    const float drag,\n    const float splatSize";
    ks.behaviorCode = R"(
    // Heavy, slow-moving fluid
    p.vel += gravity * deltaTime * 0.6f;
    p.vel.x += randRange(&rngState, -1.5f, 1.5f) * deltaTime;
    p.vel *= drag;
    p.life -= deltaTime * 0.15f;
    float lifeRatio = p.life / p.maxLife;
    p.color = (float4)(0.9f * lifeRatio + 0.3f, 0.7f * lifeRatio + 0.2f, 0.05f, lifeRatio);
)";
    ks.collisionResponse = R"(
    // Strongly stick to surface and slowly creep
    float2 tangent = (float2)(-docNorm.y, docNorm.x);
    float vTan = dot(p.vel, tangent) * 0.6f;
    float gTan = dot(gravity, tangent);
    vTan += gTan * deltaTime * 0.5f;
    float gravNormal = dot(gravity, docNorm);
    float drip = 0.0f;
    if (gravNormal < 0.0f) drip = -gravNormal * deltaTime * 0.05f;
    p.vel = tangent * vTan * 0.5f - docNorm * drip;
    newPos = p.pos + docNorm * 0.6f + p.vel * deltaTime;
    p.life -= deltaTime * 0.01f; // persist on surface
    p.size = min(p.size * 1.0005f, splatSize * 3.0f);
)";
    ks.defaultDamping = 0.05f;
    return ks;
}

void HoneyEffect::bindKernelSnippetParams(cl_kernel kernel, const KernelParams& params, int firstArgIndex) const {
    cl_float2 grav = {{gravity[0], gravity[1]}};
    clSetKernelArg(kernel, firstArgIndex + 0, sizeof(cl_float2), &grav);
    clSetKernelArg(kernel, firstArgIndex + 1, sizeof(float), &drag);
    clSetKernelArg(kernel, firstArgIndex + 2, sizeof(float), &splatSize);
}

SPHParams HoneyEffect::getSPHParams() const {
    SPHParams p;
    p.smoothingRadius = sphSmoothingRadius;
    p.restDensity = sphRestDensity;
    p.stiffness = sphStiffness;
    p.viscosity = sphViscosity;
    p.cohesion = sphCohesion;
    p.particleMass = sphParticleMass;
    return p;
}

REGISTER_EFFECT(HoneyEffect)

} // namespace Markdown