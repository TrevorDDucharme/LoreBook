#pragma once
#include <Editors/Markdown/Effect.hpp>
#include <array>

namespace Markdown {

// ────────────────────────────────────────────────────────────────────
// FireEffect - Fire text with rising particle embers
// ────────────────────────────────────────────────────────────────────

class FireEffect : public Effect {
public:
    FireEffect();
    
    const char* getName() const override { return "fire"; }
    uint32_t getBehaviorID() const override { return 1; }  // BEHAVIOR_FIRE
    std::unique_ptr<Effect> clone() const override { return std::make_unique<FireEffect>(*this); }
    
    EffectCapabilities getCapabilities() const override;
    ShaderSources getGlyphShaderSources() const override;
    ShaderSources getParticleShaderSources() const override;
    KernelSources getKernelSources() const override;
    EffectEmissionConfig getEmissionConfig() const override;
    
    void uploadGlyphUniforms(GLuint shader, float time) const override;
    void uploadParticleUniforms(GLuint shader, float time) const override;
    void bindKernelParams(cl_kernel kernel, const KernelParams& params) const override;
    
    EffectSnippet getSnippet() const override;
    void uploadSnippetUniforms(GLuint shader, float time) const override;
    
    // Fire-specific parameters
    float turbulence = 100.0f;
    float heatDecay = 1.5f;
    std::array<float, 2> gravity = {0.0f, -80.0f};
};

} // namespace Markdown
