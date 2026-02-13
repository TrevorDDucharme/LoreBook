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
    cfg.lifetime = 8.0f;      // Long life — blood persists on surfaces
    cfg.lifetimeVar = 1.0f;
    cfg.size = 4.0f;          // Moderate — balances density coverage vs visual size
    cfg.sizeVar = 1.0f;
    return cfg;
}

ParticleSnippets BloodEffect::getParticleSnippets() const {
    ParticleSnippets ps;
    // Geometry: large round blob for density accumulation pass
    ps.geometry.code = R"({
    size *= 1.5;  // Slight scale-up for density coverage
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
    
    // Life decay (slow — blood lingers)
    p.life -= deltaTime * 0.3f;
    
    // Darken as life fades
    float lifeRatio = p.life / p.maxLife;
    p.color = (float4)(0.6f + 0.2f * lifeRatio, 0.0f, 0.0f, lifeRatio);
)";
    // Custom collision: stick to surface, slide with gravity, drip off overhangs
    ks.collisionResponse = R"(
    // Surface tangent in doc space (perpendicular to normal)
    float2 tangent = (float2)(-docNorm.y, docNorm.x);
    
    // Project velocity onto tangent — remove normal component (stick to surface)
    float vTan = dot(p.vel, tangent) * 0.92f;
    
    // Add gravity sliding along surface
    float gTan = dot(gravity, tangent);
    vTan += gTan * deltaTime;
    
    // How much gravity pushes into vs pulls away from surface
    float gravNormal = dot(gravity, docNorm);
    
    // On overhangs (gravity pulls away from surface), slowly detach and drip
    float drip = 0.0f;
    if (gravNormal < 0.0f) {
        drip = -gravNormal * deltaTime * 0.25f;
    }
    
    // Slide along surface + drip off overhangs
    p.vel = tangent * vTan * 0.6f - docNorm * drip;
    
    // Push out of collision surface
    newPos = p.pos + docNorm * 0.5f + p.vel * deltaTime;
    
    // Very slow life decay while stuck — blood persists on surfaces
    p.life -= deltaTime * 0.05f;
    
    // Slight spread on contact
    p.size = min(p.size * 1.002f, splatSize * 4.0f);
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
