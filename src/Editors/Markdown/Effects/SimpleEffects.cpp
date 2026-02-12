#include <Editors/Markdown/Effects/SimpleEffects.hpp>

namespace Markdown {

// ════════════════════════════════════════════════════════════════════
// RainbowEffect
// ════════════════════════════════════════════════════════════════════

RainbowEffect::RainbowEffect() {
    speed = 0.5f;
    intensity = 1.0f;
}

EffectCapabilities RainbowEffect::getCapabilities() const {
    return {false, true, false, false, false};
}

GlyphSnippets RainbowEffect::getGlyphSnippets() const {
    GlyphSnippets gs;
    gs.fragment.uniformDecls = "uniform float uRainbow_Speed;\n";
    gs.fragment.helpers = R"(
vec3 rainbow_hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}
)";
    gs.fragment.code = R"({
    float hue = fract(v_worldPos.x * 0.01 + uTime * uRainbow_Speed);
    vec3 rainbow = rainbow_hsv2rgb(vec3(hue, 0.85, 1.0));
    color = vec4(rainbow, color.a);
})";
    return gs;
}

void RainbowEffect::uploadGlyphSnippetUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uRainbow_Speed"), speed);
}

REGISTER_EFFECT(RainbowEffect)

// ════════════════════════════════════════════════════════════════════
// WaveEffect
// ════════════════════════════════════════════════════════════════════

WaveEffect::WaveEffect() {
    speed = 1.0f;
    intensity = 1.0f;
}

EffectCapabilities WaveEffect::getCapabilities() const {
    return {false, true, false, false, false};
}

GlyphSnippets WaveEffect::getGlyphSnippets() const {
    GlyphSnippets gs;
    gs.vertex.uniformDecls = "uniform float uWave_Amplitude;\nuniform float uWave_Frequency;\nuniform float uWave_Speed;\n";
    gs.vertex.code = R"({
    float wave = sin(in_pos.x * uWave_Frequency * 0.1 + uTime * uWave_Speed * 3.0) * uWave_Amplitude;
    pos.y += wave;
})";
    return gs;
}

void WaveEffect::uploadGlyphSnippetUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uWave_Amplitude"), amplitude);
    glUniform1f(glGetUniformLocation(shader, "uWave_Frequency"), frequency);
    glUniform1f(glGetUniformLocation(shader, "uWave_Speed"), speed);
}

REGISTER_EFFECT(WaveEffect)

// ════════════════════════════════════════════════════════════════════
// ShakeEffect
// ════════════════════════════════════════════════════════════════════

ShakeEffect::ShakeEffect() {
    speed = 1.0f;
    intensity = 2.0f;
}

EffectCapabilities ShakeEffect::getCapabilities() const {
    return {false, true, false, false, false};
}

GlyphSnippets ShakeEffect::getGlyphSnippets() const {
    GlyphSnippets gs;
    gs.vertex.uniformDecls = "uniform float uShake_Intensity;\nuniform float uShake_Speed;\n";
    gs.vertex.helpers = R"(
float shake_rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}
)";
    gs.vertex.code = R"({
    float seed = in_pos.x * 0.1;
    float shakeX = (shake_rand(vec2(seed, uTime * uShake_Speed * 10.0)) - 0.5) * uShake_Intensity * 2.0;
    float shakeY = (shake_rand(vec2(seed + 0.5, uTime * uShake_Speed * 10.0)) - 0.5) * uShake_Intensity * 2.0;
    pos.x += shakeX;
    pos.y += shakeY;
})";
    return gs;
}

void ShakeEffect::uploadGlyphSnippetUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uShake_Intensity"), intensity);
    glUniform1f(glGetUniformLocation(shader, "uShake_Speed"), speed);
}

REGISTER_EFFECT(ShakeEffect)

} // namespace Markdown
