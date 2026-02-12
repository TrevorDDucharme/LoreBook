#include <Editors/Markdown/Effects/SparkleEffect.hpp>

namespace Markdown {

SparkleEffect::SparkleEffect() {
    color1 = {1.0f, 0.9f, 0.5f, 1.0f};
    color2 = {1.0f, 1.0f, 0.8f, 1.0f};
    speed = 1.0f;
    intensity = 1.0f;
}

EffectCapabilities SparkleEffect::getCapabilities() const {
    EffectCapabilities caps;
    caps.hasParticles = true;
    caps.hasGlyphShader = true;
    caps.hasParticleShader = true;
    caps.hasPostProcess = false;
    caps.contributesToBloom = true;
    return caps;
}

EffectEmissionConfig SparkleEffect::getEmissionConfig() const {
    EffectEmissionConfig cfg;
    cfg.shape = EffectEmissionConfig::Shape::GlyphOutline;
    cfg.rate = 5.0f;
    cfg.velocity = {0, 0};
    cfg.velocityVar = {10, 10};
    cfg.lifetime = 2.0f;
    cfg.lifetimeVar = 0.5f;
    cfg.size = 4.0f;
    cfg.sizeVar = 2.0f;
    return cfg;
}

GlyphSnippets SparkleEffect::getGlyphSnippets() const {
    GlyphSnippets gs;
    gs.fragment.uniformDecls = "uniform float uSparkle_TwinkleSpeed;\n";
    gs.fragment.helpers = R"(
float sparkle_random(vec2 st) {
    return fract(sin(dot(st, vec2(12.9898, 78.233))) * 43758.5453);
}
)";
    gs.fragment.code = R"({
    vec3 sparkleBase = vec3(1.0, 0.9, 0.6);
    float sparkle1 = step(0.97, sparkle_random(floor(v_worldPos.xy * 0.3) + floor(uTime * uSparkle_TwinkleSpeed)));
    float sparkle2 = step(0.95, sparkle_random(floor(v_worldPos.xy * 0.5) + floor(uTime * uSparkle_TwinkleSpeed * 1.3)));
    float sparkle3 = step(0.92, sparkle_random(floor(v_worldPos.xy * 0.2) + floor(uTime * uSparkle_TwinkleSpeed * 0.7)));
    float sparkle = max(max(sparkle1, sparkle2 * 0.7), sparkle3 * 0.4);
    color.rgb = sparkleBase + vec3(sparkle);
})";
    return gs;
}

void SparkleEffect::uploadGlyphSnippetUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uSparkle_TwinkleSpeed"), twinkleSpeed);
}

ParticleSnippets SparkleEffect::getParticleSnippets() const {
    ParticleSnippets ps;
    // Geometry: bell-curve fade, twinkle-pulsing size, extra varying for twinkle
    // uTime is provided by the base geometry template
    ps.geometry.varyingDecls = "out float g_twinkle;\n";
    ps.geometry.helpers = R"(
float sparkle_geom_sin(float x) { return sin(x); }
)";
    ps.geometry.code = R"({
    // Twinkle: size pulsates
    float twinkle = sparkle_geom_sin(uTime * 15.0 + pos.x * 0.1 + pos.y * 0.1) * 0.5 + 0.5;
    size *= 0.5 + twinkle;
    
    // Fade in/out with bell curve
    float fade = smoothstep(0.0, 0.3, life) * smoothstep(1.0, 0.7, life);
    color.a *= fade;
    
    g_twinkle = twinkle;
})";
    // Fragment: star shape with twinkle center brightness
    ps.fragment.varyingDecls = "in float g_twinkle;\n";
    ps.fragment.code = R"({
    vec2 centered = uv - vec2(0.5);
    float angle = atan(centered.y, centered.x);
    float star = 0.3 + 0.2 * cos(angle * 4.0);
    alpha = 1.0 - smoothstep(star * 0.3, star * 0.5, dist);
    // Bright center
    alpha += (1.0 - smoothstep(0.0, 0.15, dist)) * g_twinkle;
})";
    return ps;
}

void SparkleEffect::uploadParticleSnippetUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uTime"), time);
}

KernelSnippet SparkleEffect::getKernelSnippet() const {
    KernelSnippet ks;
    ks.argDecls = ",\n    const float twinkleSpeed,\n    const float driftSpeed";
    ks.behaviorCode = R"(
    // Gentle sinusoidal drift
    float nx = sin(time * 0.5f + p.pos.y * 0.02f) * driftSpeed;
    float ny = cos(time * 0.7f + p.pos.x * 0.02f) * driftSpeed;
    p.vel.x = mix(p.vel.x, nx, deltaTime * 2.0f);
    p.vel.y = mix(p.vel.y, ny, deltaTime * 2.0f);
    
    // Twinkle: color brightness pulsates
    float twinkle = sin(time * twinkleSpeed + p.pos.x * 0.1f) * 0.5f + 0.5f;
    p.color = (float4)(1.0f, 0.9f + twinkle * 0.1f, 0.5f + twinkle * 0.3f, 1.0f);
    
    // Life decay
    p.life -= deltaTime * 0.5f;
)";
    ks.defaultDamping = 0.5f;
    return ks;
}

void SparkleEffect::bindKernelSnippetParams(cl_kernel kernel, const KernelParams& params,
                                              int firstArgIndex) const {
    clSetKernelArg(kernel, firstArgIndex + 0, sizeof(float), &twinkleSpeed);
    clSetKernelArg(kernel, firstArgIndex + 1, sizeof(float), &driftSpeed);
}

REGISTER_EFFECT(SparkleEffect)

} // namespace Markdown
