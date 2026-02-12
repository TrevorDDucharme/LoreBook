#pragma once
#include <Editors/Markdown/Effect.hpp>
#include <array>

namespace Markdown {

/// Magic effect - glowing text with ascending mystical particles
class MagicEffect : public Effect {
public:
    MagicEffect();
    
    const char* getName() const override { return "magic"; }
    uint32_t getBehaviorID() const override { return 7; }  // BEHAVIOR_MAGIC
    std::unique_ptr<Effect> clone() const override { return std::make_unique<MagicEffect>(*this); }
    
    EffectCapabilities getCapabilities() const override;
    ShaderSources getGlyphShaderSources() const override;
    ShaderSources getParticleShaderSources() const override;
    KernelSources getKernelSources() const override;
    EffectEmissionConfig getEmissionConfig() const override;
    
    void uploadGlyphUniforms(GLuint shader, float time) const override;
    void uploadParticleUniforms(GLuint shader, float time) const override;
    void bindKernelParams(cl_kernel kernel, const KernelParams& params) const override;
    
    float orbitSpeed = 2.0f;
    float orbitRadius = 20.0f;
    float riseSpeed = 25.0f;
};

} // namespace Markdown
