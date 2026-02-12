#pragma once
#include <Editors/Markdown/Effect.hpp>
#include <array>

namespace Markdown {

/// Smoke effect - rising, expanding, dissipating particles
class SmokeEffect : public Effect {
public:
    SmokeEffect();
    
    const char* getName() const override { return "smoke"; }
    uint32_t getBehaviorID() const override { return 6; }  // BEHAVIOR_SMOKE
    std::unique_ptr<Effect> clone() const override { return std::make_unique<SmokeEffect>(*this); }
    
    EffectCapabilities getCapabilities() const override;
    KernelSources getKernelSources() const override;
    ShaderSources getParticleShaderSources() const override;
    EffectEmissionConfig getEmissionConfig() const override;
    
    void uploadParticleUniforms(GLuint shader, float time) const override;
    void bindKernelParams(cl_kernel kernel, const KernelParams& params) const override;
    
    std::array<float, 2> wind = {5.0f, 0.0f};
    float riseSpeed = 30.0f;
    float expansion = 2.0f;
    float dissipation = 1.0f;
};

} // namespace Markdown
