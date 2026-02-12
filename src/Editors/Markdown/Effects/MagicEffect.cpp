#include <Editors/Markdown/Effects/MagicEffect.hpp>

namespace Markdown {

MagicEffect::MagicEffect() {
    color1 = {0.7f, 0.3f, 1.0f, 1.0f};
    color2 = {0.9f, 0.5f, 1.0f, 1.0f};
    speed = 1.0f;
    intensity = 0.9f;
}

EffectCapabilities MagicEffect::getCapabilities() const {
    return {true, true, true, false, true};
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

GlyphSnippets MagicEffect::getGlyphSnippets() const {
    GlyphSnippets gs;
    gs.fragment.uniformDecls = "uniform vec4 uMagic_Color1;\nuniform float uMagic_Intensity;\nuniform float uMagic_Speed;\n";
    gs.fragment.code = R"({
    float magicPulse = 0.7 + 0.3 * sin(uTime * 2.5 * uMagic_Speed + v_worldPos.x * 0.05);
    float magicStrength = clamp(uMagic_Intensity * magicPulse, 0.0, 1.0);
    color.rgb = mix(color.rgb, uMagic_Color1.rgb, magicStrength * 0.8);
    float magicGlow = smoothstep(0.0, 0.5, alpha) * (1.0 + magicStrength * 0.3);
    color.a *= magicGlow;
})";
    return gs;
}

void MagicEffect::uploadGlyphSnippetUniforms(GLuint shader, float time) const {
    glUniform4fv(glGetUniformLocation(shader, "uMagic_Color1"), 1, &color1[0]);
    glUniform1f(glGetUniformLocation(shader, "uMagic_Intensity"), intensity);
    glUniform1f(glGetUniformLocation(shader, "uMagic_Speed"), speed);
}

ParticleSnippets MagicEffect::getParticleSnippets() const {
    ParticleSnippets ps;
    // Geometry: bell-curve fade + pulsing size
    ps.geometry.uniformDecls = "uniform float uTime;\n";
    ps.geometry.code = R"({
    float fade = smoothstep(0.0, 0.3, life) * smoothstep(1.0, 0.7, life);
    color.a *= fade;
    // Pulsing size
    size *= 0.8 + 0.3 * sin(uTime * 8.0 + pos.x * 0.2);
})";
    // Fragment: two-layer core + glow with center brightness
    ps.fragment.code = R"({
    float core = 1.0 - smoothstep(0.0, 0.2, dist);
    float glow = 1.0 - smoothstep(0.1, 0.5, dist);
    alpha = core * 0.8 + glow * 0.4;
    color.rgb *= (1.0 + core * 0.5);
})";
    return ps;
}

void MagicEffect::uploadParticleSnippetUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uTime"), time);
}

KernelSnippet MagicEffect::getKernelSnippet() const {
    KernelSnippet ks;
    ks.argDecls = ",\n    const float orbitSpeed,\n    const float orbitRadius,\n    const float riseSpeed";
    ks.behaviorCode = R"(
    // Orbital motion with per-particle phase
    float phase = time * orbitSpeed + (float)gid * 0.7f;
    float targetVx = cos(phase) * orbitRadius;
    float targetVy = -riseSpeed + sin(phase) * orbitRadius * 0.5f;
    
    p.vel.x = mix(p.vel.x, targetVx, deltaTime * 3.0f);
    p.vel.y = mix(p.vel.y, targetVy, deltaTime * 3.0f);
    
    // Life decay
    p.life -= deltaTime * 0.7f;
    
    // Color: purple with shimmering brightness
    float shimmer = sin(time * 8.0f + p.pos.x * 0.1f) * 0.5f + 0.5f;
    float lifeRatio = p.life / p.maxLife;
    p.color = (float4)(0.5f + shimmer * 0.3f, 0.2f + shimmer * 0.1f, 0.8f + shimmer * 0.2f, lifeRatio);
    
    // Slight size variation
    p.size = 2.5f + shimmer;
)";
    ks.defaultDamping = 0.4f;
    return ks;
}

void MagicEffect::bindKernelSnippetParams(cl_kernel kernel, const KernelParams& params,
                                            int firstArgIndex) const {
    clSetKernelArg(kernel, firstArgIndex + 0, sizeof(float), &orbitSpeed);
    clSetKernelArg(kernel, firstArgIndex + 1, sizeof(float), &orbitRadius);
    clSetKernelArg(kernel, firstArgIndex + 2, sizeof(float), &riseSpeed);
}

REGISTER_EFFECT(MagicEffect)

} // namespace Markdown
