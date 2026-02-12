#pragma once
#include <Editors/Markdown/Effect.hpp>

namespace Markdown {

/// Electric/Lightning effect - crackling particles with intense glow
class ElectricEffect : public Effect {
public:
    ElectricEffect();
    
    const char* getName() const override { return "Electric"; }
    uint32_t getBehaviorID() const override { return 5; }
    
    EffectCapabilities getCapabilities() const override;
    ShaderSources getGlyphShaderSources() const override;
    ShaderSources getParticleShaderSources() const override;
    KernelSources getKernelSources() const override;
    EffectEmissionConfig getEmissionConfig() const override;
    std::vector<PostProcessPass> getPostProcessPasses() const override;
    
    void uploadGlyphUniforms(GLuint shader, float time) const override;
    void uploadParticleUniforms(GLuint shader, float time) const override;
    void bindKernelParams(cl_kernel kernel, const KernelParams& params) const override;
    
    // Electric-specific parameters
    float arcFrequency = 3.0f;
    float jitterStrength = 50.0f;
    float chainChance = 0.3f;
};

} // namespace Markdown
