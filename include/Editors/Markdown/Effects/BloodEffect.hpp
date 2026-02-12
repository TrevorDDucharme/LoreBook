#pragma once
#include <Editors/Markdown/Effect.hpp>

namespace Markdown {

/// Blood effect - red drip particles that fall with gravity and splatter
class BloodEffect : public Effect {
public:
    BloodEffect();
    
    const char* getName() const override { return "blood"; }
    uint32_t getBehaviorID() const override { return 2; }  // BEHAVIOR_BLOOD
    
    EffectCapabilities getCapabilities() const override;
    KernelSources getKernelSources() const override;
    ShaderSources getParticleShaderSources() const override;
    EffectEmissionConfig getEmissionConfig() const override;
    
    void uploadParticleUniforms(GLuint shader, float time) const override;
    void bindKernelParams(cl_kernel kernel, const KernelParams& params) const override;
    
    // Blood-specific parameters
    std::array<float, 2> gravity = {0.0f, 80.0f};  // Falls down
    float drag = 0.98f;
    float splatSize = 1.5f;
};

} // namespace Markdown
