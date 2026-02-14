#include <Editors/Markdown/Effects/WaterEffect.hpp>

namespace Markdown {

WaterEffect::WaterEffect() {
    color1 = {0.2f, 0.55f, 0.9f, 1.0f};
    color2 = {0.05f, 0.25f, 0.6f, 1.0f};
    speed = 1.0f;
    intensity = 1.0f;
}

EffectCapabilities WaterEffect::getCapabilities() const {
    return {true, true, true, false, false};
}

GlyphSnippets WaterEffect::getGlyphSnippets() const {
    GlyphSnippets gs;
    gs.fragment.uniformDecls = "uniform vec4 uWater_Color1;\nuniform vec4 uWater_Color2;\n";
    gs.fragment.code = R"({
    // Slight bluish tint with subtle wave toward bottom
    float t = smoothstep(0.0, 1.0, v_uv.y);
    vec3 c = mix(uWater_Color1.rgb, uWater_Color2.rgb, t);
    color.rgb = c;
})";
    return gs;
}

void WaterEffect::uploadGlyphSnippetUniforms(GLuint shader, float time) const {
    glUniform4fv(glGetUniformLocation(shader, "uWater_Color1"), 1, &color1[0]);
    glUniform4fv(glGetUniformLocation(shader, "uWater_Color2"), 1, &color2[0]);
}

EffectEmissionConfig WaterEffect::getEmissionConfig() const {
    EffectEmissionConfig cfg;
    cfg.shape = EffectEmissionConfig::Shape::GlyphAlpha;
    cfg.rate = 20.0f;
    cfg.velocity = {0, 40};
    cfg.velocityVar = {6, 10};
    cfg.lifetime = 4.0f;
    cfg.lifetimeVar = 1.0f;
    cfg.size = 3.0f;
    cfg.sizeVar = 1.0f;
    return cfg;
}

ParticleSnippets WaterEffect::getParticleSnippets() const {
    ParticleSnippets ps;
    ps.geometry.code = R"({
    size *= 1.2; // slightly larger density footprint
    color.a *= smoothstep(0.0, 0.2, life) * 0.6;
})";
    ps.fragment.code = R"({
    float r = dist * 2.0;
    alpha = exp(-r * r * 2.0);
})";
    return ps;
}

void WaterEffect::uploadParticleSnippetUniforms(GLuint shader, float time) const {
    // no extra uniforms
}

KernelSnippet WaterEffect::getKernelSnippet() const {
    KernelSnippet ks;
    ks.argDecls = ",\n    const float2 gravity,\n    const float drag,\n    const float splatSize";
    ks.behaviorCode = R"(
    // Simple gravity + gentle wobble
    p.vel += gravity * deltaTime;
    p.vel.x += randRange(&rngState, -3.0f, 3.0f) * deltaTime;
    p.vel *= drag;
    p.life -= deltaTime * 0.4f;
    float lifeRatio = p.life / p.maxLife;
    p.color = (float4)(0.4f + 0.6f * lifeRatio, 0.7f + 0.2f * lifeRatio, 1.0f, lifeRatio);
)";
    ks.collisionResponse = R"(
    // Bounce + slide along surface
    float2 tangent = (float2)(-docNorm.y, docNorm.x);
    float vTan = dot(p.vel, tangent) * 0.9f;
    float gTan = dot(gravity, tangent);
    vTan += gTan * deltaTime;
    float gravNormal = dot(gravity, docNorm);
    float detach = 0.0f;
    if (gravNormal < 0.0f) detach = -gravNormal * deltaTime * 0.1f;
    p.vel = tangent * vTan - docNorm * detach;
    newPos = p.pos + docNorm * 0.5f + p.vel * deltaTime;
    p.life -= deltaTime * 0.02f;
    p.size = min(p.size * 1.001f, splatSize * 2.0f);
)";
    ks.defaultDamping = 0.35f;
    return ks;
}

void WaterEffect::bindKernelSnippetParams(cl_kernel kernel, const KernelParams& params, int firstArgIndex) const {
    cl_float2 grav = {{gravity[0], gravity[1]}};
    clSetKernelArg(kernel, firstArgIndex + 0, sizeof(cl_float2), &grav);
    clSetKernelArg(kernel, firstArgIndex + 1, sizeof(float), &drag);
    clSetKernelArg(kernel, firstArgIndex + 2, sizeof(float), &splatSize);
}

SPHParams WaterEffect::getSPHParams() const {
    SPHParams p;
    p.smoothingRadius = sphSmoothingRadius;
    p.restDensity = sphRestDensity;
    p.stiffness = sphStiffness;
    p.viscosity = sphViscosity;
    p.cohesion = sphCohesion;
    p.particleMass = sphParticleMass;
    return p;
}

REGISTER_EFFECT(WaterEffect)

} // namespace Markdown