#include <Editors/Markdown/Effects/BloodEffect.hpp>

namespace Markdown {

BloodEffect::BloodEffect() {
    color1 = {0.8f, 0.0f, 0.0f, 1.0f};
    color2 = {0.4f, 0.0f, 0.0f, 1.0f};
    speed = 1.0f;
    intensity = 1.0f;
}

EffectCapabilities BloodEffect::getCapabilities() const {
    return {true, true, true, false, false};
}

GlyphSnippets BloodEffect::getGlyphSnippets() const {
    GlyphSnippets gs;
    gs.fragment.uniformDecls = "uniform vec4 uBlood_Color1;\nuniform vec4 uBlood_Color2;\n";
    gs.fragment.code = R"({
    // Tint text blood red with subtle darkening toward bottom
    float drip = smoothstep(0.0, 1.0, v_uv.y);
    vec3 bloodColor = mix(uBlood_Color1.rgb, uBlood_Color2.rgb, drip);
    color.rgb = bloodColor;
})";
    return gs;
}

void BloodEffect::uploadGlyphSnippetUniforms(GLuint shader, float time) const {
    glUniform4fv(glGetUniformLocation(shader, "uBlood_Color1"), 1, &color1[0]);
    glUniform4fv(glGetUniformLocation(shader, "uBlood_Color2"), 1, &color2[0]);
}

EffectEmissionConfig BloodEffect::getEmissionConfig() const {
    EffectEmissionConfig cfg;
    cfg.shape = EffectEmissionConfig::Shape::GlyphAlpha;
    cfg.rate = 30.0f;         // Higher rate for fluid density
    cfg.velocity = {0, 50};   // Slightly slower initial (SPH handles flow)
    cfg.velocityVar = {8, 12};
    cfg.lifetime = 3.0f;      // Longer life — fluid persists
    cfg.lifetimeVar = 0.5f;
    cfg.size = 10.0f;         // Larger — SPH smoothing radius is 25px
    cfg.sizeVar = 3.0f;
    return cfg;
}

ParticleSnippets BloodEffect::getParticleSnippets() const {
    ParticleSnippets ps;
    // Geometry: large round blob for density accumulation pass
    ps.geometry.code = R"({
    size *= 2.0;  // Scale up for density coverage
    color.a *= smoothstep(0.0, 0.2, life) * 0.5;
})";
    // Fragment: gaussian falloff for smooth density blending
    ps.fragment.code = R"({
    float r = dist * 2.0;
    alpha = exp(-r * r * 2.5);
})";
    return ps;
}

void BloodEffect::uploadParticleSnippetUniforms(GLuint shader, float time) const {
    // No custom particle uniforms
}

KernelSnippet BloodEffect::getKernelSnippet() const {
    KernelSnippet ks;
    ks.argDecls = ",\n    const float2 gravity,\n    const float drag,\n    const float splatSize";
    ks.behaviorCode = R"(
    // Gravity
    p.vel += gravity * deltaTime;
    
    // Slight horizontal wobble
    p.vel.x += randRange(&rngState, -5.0f, 5.0f) * deltaTime;
    
    // Drag
    p.vel *= drag;
    
    // Life decay
    p.life -= deltaTime * 0.8f;
    
    // Darken as life fades
    float lifeRatio = p.life / p.maxLife;
    p.color = (float4)(0.6f + 0.2f * lifeRatio, 0.0f, 0.0f, lifeRatio);
)";
    // Custom collision: splat (expand, slow down, die faster)
    ks.collisionResponse = R"(
    p.vel = reflect_f2(p.vel, maskNorm) * 0.15f;
    newPos = p.pos + p.vel * deltaTime;
    p.size = min(p.size * splatSize, 8.0f);
    p.life -= deltaTime * 3.0f;
)";
    ks.defaultDamping = 0.15f;
    return ks;
}

void BloodEffect::bindKernelSnippetParams(cl_kernel kernel, const KernelParams& params,
                                            int firstArgIndex) const {
    cl_float2 grav = {{gravity[0], gravity[1]}};
    clSetKernelArg(kernel, firstArgIndex + 0, sizeof(cl_float2), &grav);
    clSetKernelArg(kernel, firstArgIndex + 1, sizeof(float), &drag);
    clSetKernelArg(kernel, firstArgIndex + 2, sizeof(float), &splatSize);
}

REGISTER_EFFECT(BloodEffect)

} // namespace Markdown
