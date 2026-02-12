#include <Editors/Markdown/Effects/GlowEffect.hpp>

namespace Markdown {

static const char* s_glowVert = R"(
#version 330 core
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) in uint in_effectID;

uniform mat4 uMVP;

out vec2 v_uv;
out vec4 v_color;
out vec3 v_worldPos;

void main() {
    gl_Position = uMVP * vec4(in_pos, 1.0);
    v_uv = in_uv;
    v_color = in_color;
    v_worldPos = in_pos;
}
)";

static const char* s_glowFrag = R"(
#version 330 core
uniform sampler2D uFontAtlas;
uniform float uTime;
uniform vec4 uColor1;
uniform float uIntensity;
uniform float uSpeed;

in vec2 v_uv;
in vec4 v_color;
in vec3 v_worldPos;

out vec4 fragColor;

void main() {
    float alpha = texture(uFontAtlas, v_uv).a;
    
    // Pulsating glow intensity
    float pulse = 0.7 + 0.3 * sin(uTime * 3.0 * uSpeed);
    float strength = clamp(uIntensity * pulse, 0.0, 1.0);
    
    // Blend text color toward glow color
    vec3 glowColor = mix(v_color.rgb, uColor1.rgb, strength * 0.8);
    
    // Expanded alpha for halo effect
    float glowAlpha = smoothstep(0.0, 0.5, alpha) * (1.0 + strength * 0.3);
    
    fragColor = vec4(glowColor, alpha * v_color.a * glowAlpha);
}
)";

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

ShaderSources GlowEffect::getGlyphShaderSources() const {
    return {s_glowVert, s_glowFrag, ""};
}

void GlowEffect::uploadGlyphUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uTime"), time);
    glUniform1f(glGetUniformLocation(shader, "uIntensity"), intensity);
    glUniform1f(glGetUniformLocation(shader, "uSpeed"), speed);
    glUniform4fv(glGetUniformLocation(shader, "uColor1"), 1, &color1[0]);
}

EffectSnippet GlowEffect::getSnippet() const {
    EffectSnippet s;
    s.uniformDecls = "uniform vec4 uGlow_Color1;\nuniform float uGlow_Intensity;\nuniform float uGlow_Speed;\n";
    s.fragmentCode = R"({
    float glowPulse = 0.7 + 0.3 * sin(uTime * 3.0 * uGlow_Speed);
    float glowStrength = clamp(uGlow_Intensity * glowPulse, 0.0, 1.0);
    color.rgb = mix(color.rgb, uGlow_Color1.rgb, glowStrength * 0.8);
    float glowAlpha = smoothstep(0.0, 0.5, alpha) * (1.0 + glowStrength * 0.3);
    color.a *= glowAlpha;
})";
    return s;
}

void GlowEffect::uploadSnippetUniforms(GLuint shader, float time) const {
    glUniform4fv(glGetUniformLocation(shader, "uGlow_Color1"), 1, &color1[0]);
    glUniform1f(glGetUniformLocation(shader, "uGlow_Intensity"), intensity);
    glUniform1f(glGetUniformLocation(shader, "uGlow_Speed"), speed);
}

REGISTER_EFFECT(GlowEffect)

} // namespace Markdown
