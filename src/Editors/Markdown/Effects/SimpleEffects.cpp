#include <Editors/Markdown/Effects/SimpleEffects.hpp>

namespace Markdown {

// ════════════════════════════════════════════════════════════════════
// RainbowEffect
// ════════════════════════════════════════════════════════════════════

static const char* s_rainbowVert = R"(
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

static const char* s_rainbowFrag = R"(
#version 330 core
uniform sampler2D uFontAtlas;
uniform float uTime;
uniform float uSpeed;

in vec2 v_uv;
in vec4 v_color;
in vec3 v_worldPos;

out vec4 fragColor;

vec3 hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}

void main() {
    float alpha = texture(uFontAtlas, v_uv).a;
    float hue = fract(v_worldPos.x * 0.01 + uTime * uSpeed);
    vec3 rainbow = hsv2rgb(vec3(hue, 0.85, 1.0));
    fragColor = vec4(rainbow, alpha * v_color.a);
}
)";

RainbowEffect::RainbowEffect() {
    speed = 0.5f;
    intensity = 1.0f;
}

EffectCapabilities RainbowEffect::getCapabilities() const {
    return {false, true, false, false, false};
}

ShaderSources RainbowEffect::getGlyphShaderSources() const {
    return {s_rainbowVert, s_rainbowFrag, ""};
}

void RainbowEffect::uploadGlyphUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uTime"), time);
    glUniform1f(glGetUniformLocation(shader, "uSpeed"), speed);
}

EffectSnippet RainbowEffect::getSnippet() const {
    EffectSnippet s;
    s.uniformDecls = "uniform float uRainbow_Speed;\n";
    s.helpers = R"(
vec3 rainbow_hsv2rgb(vec3 c) {
    vec4 K = vec4(1.0, 2.0/3.0, 1.0/3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}
)";
    s.fragmentCode = R"({
    float hue = fract(v_worldPos.x * 0.01 + uTime * uRainbow_Speed);
    vec3 rainbow = rainbow_hsv2rgb(vec3(hue, 0.85, 1.0));
    color = vec4(rainbow, color.a);
})";
    return s;
}

void RainbowEffect::uploadSnippetUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uRainbow_Speed"), speed);
}

REGISTER_EFFECT(RainbowEffect)

// ════════════════════════════════════════════════════════════════════
// WaveEffect
// ════════════════════════════════════════════════════════════════════

static const char* s_waveVert = R"(
#version 330 core
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) in uint in_effectID;

uniform mat4 uMVP;
uniform float uTime;
uniform float uAmplitude;
uniform float uFrequency;
uniform float uSpeed;

out vec2 v_uv;
out vec4 v_color;
out vec3 v_worldPos;

void main() {
    vec3 pos = in_pos;
    float wave = sin(in_pos.x * uFrequency * 0.1 + uTime * uSpeed * 3.0) * uAmplitude;
    pos.y += wave;
    gl_Position = uMVP * vec4(pos, 1.0);
    v_uv = in_uv;
    v_color = in_color;
    v_worldPos = in_pos;
}
)";

static const char* s_baseFrag = R"(
#version 330 core
uniform sampler2D uFontAtlas;

in vec2 v_uv;
in vec4 v_color;
in vec3 v_worldPos;

out vec4 fragColor;

void main() {
    float alpha = texture(uFontAtlas, v_uv).a;
    fragColor = vec4(v_color.rgb, v_color.a * alpha);
}
)";

WaveEffect::WaveEffect() {
    speed = 1.0f;
    intensity = 1.0f;
}

EffectCapabilities WaveEffect::getCapabilities() const {
    return {false, true, false, false, false};
}

ShaderSources WaveEffect::getGlyphShaderSources() const {
    return {s_waveVert, s_baseFrag, ""};
}

void WaveEffect::uploadGlyphUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uTime"), time);
    glUniform1f(glGetUniformLocation(shader, "uAmplitude"), amplitude);
    glUniform1f(glGetUniformLocation(shader, "uFrequency"), frequency);
    glUniform1f(glGetUniformLocation(shader, "uSpeed"), speed);
}

EffectSnippet WaveEffect::getSnippet() const {
    EffectSnippet s;
    s.uniformDecls = "uniform float uWave_Amplitude;\nuniform float uWave_Frequency;\nuniform float uWave_Speed;\n";
    s.vertexCode = R"({
    float wave = sin(in_pos.x * uWave_Frequency * 0.1 + uTime * uWave_Speed * 3.0) * uWave_Amplitude;
    pos.y += wave;
})";
    return s;
}

void WaveEffect::uploadSnippetUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uWave_Amplitude"), amplitude);
    glUniform1f(glGetUniformLocation(shader, "uWave_Frequency"), frequency);
    glUniform1f(glGetUniformLocation(shader, "uWave_Speed"), speed);
}

REGISTER_EFFECT(WaveEffect)

// ════════════════════════════════════════════════════════════════════
// ShakeEffect
// ════════════════════════════════════════════════════════════════════

static const char* s_shakeVert = R"(
#version 330 core
layout(location = 0) in vec3 in_pos;
layout(location = 1) in vec2 in_uv;
layout(location = 2) in vec4 in_color;
layout(location = 3) in uint in_effectID;

uniform mat4 uMVP;
uniform float uTime;
uniform float uIntensity;
uniform float uSpeed;

out vec2 v_uv;
out vec4 v_color;
out vec3 v_worldPos;

float rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec3 pos = in_pos;
    float seed = in_pos.x * 0.1;
    float shakeX = (rand(vec2(seed, uTime * uSpeed * 10.0)) - 0.5) * uIntensity * 2.0;
    float shakeY = (rand(vec2(seed + 0.5, uTime * uSpeed * 10.0)) - 0.5) * uIntensity * 2.0;
    pos.x += shakeX;
    pos.y += shakeY;
    gl_Position = uMVP * vec4(pos, 1.0);
    v_uv = in_uv;
    v_color = in_color;
    v_worldPos = in_pos;
}
)";

ShakeEffect::ShakeEffect() {
    speed = 1.0f;
    intensity = 2.0f;
}

EffectCapabilities ShakeEffect::getCapabilities() const {
    return {false, true, false, false, false};
}

ShaderSources ShakeEffect::getGlyphShaderSources() const {
    return {s_shakeVert, s_baseFrag, ""};
}

void ShakeEffect::uploadGlyphUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uTime"), time);
    glUniform1f(glGetUniformLocation(shader, "uIntensity"), intensity);
    glUniform1f(glGetUniformLocation(shader, "uSpeed"), speed);
}

EffectSnippet ShakeEffect::getSnippet() const {
    EffectSnippet s;
    s.uniformDecls = "uniform float uShake_Intensity;\nuniform float uShake_Speed;\n";
    s.helpers = R"(
float shake_rand(vec2 co) {
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}
)";
    s.vertexCode = R"({
    float seed = in_pos.x * 0.1;
    float shakeX = (shake_rand(vec2(seed, uTime * uShake_Speed * 10.0)) - 0.5) * uShake_Intensity * 2.0;
    float shakeY = (shake_rand(vec2(seed + 0.5, uTime * uShake_Speed * 10.0)) - 0.5) * uShake_Intensity * 2.0;
    pos.x += shakeX;
    pos.y += shakeY;
})";
    return s;
}

void ShakeEffect::uploadSnippetUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uShake_Intensity"), intensity);
    glUniform1f(glGetUniformLocation(shader, "uShake_Speed"), speed);
}

REGISTER_EFFECT(ShakeEffect)

} // namespace Markdown
