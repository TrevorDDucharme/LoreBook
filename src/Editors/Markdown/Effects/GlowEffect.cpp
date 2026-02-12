#include <Editors/Markdown/Effects/GlowEffect.hpp>

namespace Markdown {

GlowEffect::GlowEffect() {
    color1 = {1.0f, 1.0f, 0.5f, 1.0f}; // Warm yellow glow
    color2 = {1.0f, 1.0f, 1.0f, 1.0f};
    speed = 1.0f;
    intensity = 0.8f;
}

EffectCapabilities GlowEffect::getCapabilities() const {
    EffectCapabilities caps;
    caps.hasParticles = false;
    caps.hasGlyphShader = true;
    caps.hasParticleShader = false;
    caps.hasPostProcess = false;
    caps.contributesToBloom = true;
    return caps;
}

GlyphSnippets GlowEffect::getGlyphSnippets() const {
    GlyphSnippets gs;
    gs.fragment.uniformDecls = "uniform vec4 uGlow_Color1;\nuniform float uGlow_Intensity;\nuniform float uGlow_Speed;\n";
    gs.fragment.code = R"({
    float glowPulse = 0.7 + 0.3 * sin(uTime * 3.0 * uGlow_Speed);
    float glowStrength = clamp(uGlow_Intensity * glowPulse, 0.0, 1.0);
    color.rgb = mix(color.rgb, uGlow_Color1.rgb, glowStrength * 0.8);
    float glowAlpha = smoothstep(0.0, 0.5, alpha) * (1.0 + glowStrength * 0.3);
    color.a *= glowAlpha;
})";
    return gs;
}

void GlowEffect::uploadGlyphSnippetUniforms(GLuint shader, float time) const {
    glUniform4fv(glGetUniformLocation(shader, "uGlow_Color1"), 1, &color1[0]);
    glUniform1f(glGetUniformLocation(shader, "uGlow_Intensity"), intensity);
    glUniform1f(glGetUniformLocation(shader, "uGlow_Speed"), speed);
}

REGISTER_EFFECT(GlowEffect)

} // namespace Markdown
