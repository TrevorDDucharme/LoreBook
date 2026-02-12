#include <Editors/Markdown/Effects/ElectricEffect.hpp>

namespace Markdown {

ElectricEffect::ElectricEffect() {
    color1 = {0.5f, 0.7f, 1.0f, 1.0f};
    color2 = {1.0f, 1.0f, 1.0f, 1.0f};
    speed = 1.0f;
    intensity = 1.0f;
}

EffectCapabilities ElectricEffect::getCapabilities() const {
    EffectCapabilities caps;
    caps.hasParticles = true;
    caps.hasGlyphShader = true;
    caps.hasParticleShader = true;
    caps.hasPostProcess = true;
    caps.contributesToBloom = true;
    return caps;
}

EffectEmissionConfig ElectricEffect::getEmissionConfig() const {
    EffectEmissionConfig cfg;
    cfg.shape = EffectEmissionConfig::Shape::GlyphOutline;
    cfg.rate = 20.0f;
    cfg.velocity = {0, 0};
    cfg.velocityVar = {100, 100};
    cfg.lifetime = 0.3f;
    cfg.lifetimeVar = 0.1f;
    cfg.size = 3.0f;
    cfg.sizeVar = 2.0f;
    return cfg;
}

GlyphSnippets ElectricEffect::getGlyphSnippets() const {
    GlyphSnippets gs;
    gs.vertex.uniformDecls = "uniform float uElectric_JitterStrength;\n";
    gs.vertex.helpers = R"(
float electric_random(float x) {
    return fract(sin(x * 12.9898) * 43758.5453);
}
)";
    gs.vertex.code = R"({
    float jitterTime = floor(uTime * 20.0);
    float jitterX = (electric_random(jitterTime + in_pos.y * 0.1) - 0.5) * uElectric_JitterStrength * 0.1;
    float jitterY = (electric_random(jitterTime * 1.3 + in_pos.x * 0.1) - 0.5) * uElectric_JitterStrength * 0.05;
    float doJitter = step(0.85, electric_random(jitterTime));
    pos.x += jitterX * doJitter;
    pos.y += jitterY * doJitter;
})";
    gs.fragment.uniformDecls = "uniform float uElectric_ArcFrequency;\n";
    gs.fragment.helpers = R"(
float electric_random2(vec2 st) {
    return fract(sin(dot(st, vec2(12.9898, 78.233))) * 43758.5453);
}
)";
    gs.fragment.code = R"({
    vec3 elecBase = vec3(0.5, 0.7, 1.0);
    float elecFlicker = step(0.7, electric_random2(vec2(floor(uTime * 30.0), 0.0)));
    float elecBrightness = 1.0 + elecFlicker * 0.5;
    float elecArc = step(0.95, electric_random2(vec2(floor(v_worldPos.x * 0.2), floor(uTime * uElectric_ArcFrequency))));
    color.rgb = elecBase * elecBrightness + vec3(elecArc);
    float elecGlow = smoothstep(0.0, 0.5, alpha) * (1.0 + elecFlicker * 0.2);
    color.a *= elecGlow;
})";
    return gs;
}

void ElectricEffect::uploadGlyphSnippetUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uElectric_ArcFrequency"), arcFrequency);
    glUniform1f(glGetUniformLocation(shader, "uElectric_JitterStrength"), jitterStrength);
}

ParticleSnippets ElectricEffect::getParticleSnippets() const {
    ParticleSnippets ps;
    // Geometry: fastest fade-in (0.1), flicker size + brightness
    ps.geometry.uniformDecls = "uniform float uTime;\n";
    ps.geometry.helpers = R"(
float elec_geom_random(float x) {
    return fract(sin(x * 12.9898) * 43758.5453);
}
)";
    ps.geometry.code = R"({
    // Electric particles flicker size
    float flicker = step(0.6, elec_geom_random(floor(uTime * 60.0) + pos.x));
    size *= 0.8 + flicker * 0.8;
    // Intense flash
    color.rgb *= 1.0 + flicker * 2.0;
    color.a *= smoothstep(0.0, 0.1, life);
})";
    // Fragment: hard core + soft glow
    ps.fragment.code = R"({
    float core = 1.0 - step(0.15, dist);
    float glow = 1.0 - smoothstep(0.15, 0.5, dist);
    alpha = core + glow * 0.5;
})";
    return ps;
}

void ElectricEffect::uploadParticleSnippetUniforms(GLuint shader, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uTime"), time);
}

KernelSnippet ElectricEffect::getKernelSnippet() const {
    KernelSnippet ks;
    ks.argDecls = ",\n    const float arcFrequency,\n    const float jitterStrength,\n    const float chainChance";
    ks.behaviorCode = R"(
    // Erratic jitter movement
    if (randRange(&rngState, 0.0f, 1.0f) < 0.3f) {
        p.vel.x = randRange(&rngState, -1.0f, 1.0f) * jitterStrength;
        p.vel.y = randRange(&rngState, -1.0f, 1.0f) * jitterStrength;
    }
    
    // Quick decay
    p.vel *= 0.85f;
    
    // Flickering color
    float flicker = step(0.7f, randRange(&rngState, 0.0f, 1.0f));
    float elecIntensity = 0.8f + flicker * 0.5f;
    p.color = (float4)(0.5f * elecIntensity, 0.7f * elecIntensity, 1.0f * elecIntensity, 1.0f);
    
    // Fast decay
    p.life -= deltaTime * 3.0f;
)";
    ks.defaultDamping = 0.9f;
    return ks;
}

void ElectricEffect::bindKernelSnippetParams(cl_kernel kernel, const KernelParams& params,
                                               int firstArgIndex) const {
    clSetKernelArg(kernel, firstArgIndex + 0, sizeof(float), &arcFrequency);
    clSetKernelArg(kernel, firstArgIndex + 1, sizeof(float), &jitterStrength);
    clSetKernelArg(kernel, firstArgIndex + 2, sizeof(float), &chainChance);
}

std::vector<PostProcessSnippet> ElectricEffect::getPostProcessSnippets() const {
    PostProcessSnippet bloom;
    bloom.name = "ElectricBloom";
    bloom.fragment.uniformDecls = "uniform vec2 uTexelSize;\nuniform float uBloomIntensity;\n";
    bloom.fragment.code = R"({
    // Simple 5-tap blur
    vec4 blur = color * 0.4;
    blur += texture(uInputTex, uv + vec2(uTexelSize.x * 2.0, 0)) * 0.15;
    blur += texture(uInputTex, uv - vec2(uTexelSize.x * 2.0, 0)) * 0.15;
    blur += texture(uInputTex, uv + vec2(0, uTexelSize.y * 2.0)) * 0.15;
    blur += texture(uInputTex, uv - vec2(0, uTexelSize.y * 2.0)) * 0.15;
    color = color + blur * uBloomIntensity;
})";
    bloom.blendMode = PostProcessSnippet::BlendMode::Additive;
    return {bloom};
}

void ElectricEffect::uploadPostProcessSnippetUniforms(GLuint shader, int passIndex, float time) const {
    glUniform1f(glGetUniformLocation(shader, "uBloomIntensity"), 0.5f);
}

REGISTER_EFFECT(ElectricEffect)

} // namespace Markdown
