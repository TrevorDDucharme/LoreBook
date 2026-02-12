#pragma once
#include <Editors/Markdown/Effect.hpp>

namespace Markdown {

/// Glow effect - pulsating colored halo (no particles, shader only)
class GlowEffect : public Effect {
public:
    GlowEffect();
    
    const char* getName() const override { return "Glow"; }
    uint32_t getBehaviorID() const override { return 0; } // No particles
    
    EffectCapabilities getCapabilities() const override;
    ShaderSources getGlyphShaderSources() const override;
    ShaderSources getParticleShaderSources() const override { return {}; }
    KernelSources getKernelSources() const override { return {}; }
    EffectEmissionConfig getEmissionConfig() const override { return {}; }
    
    void uploadGlyphUniforms(GLuint shader, float time) const override;
    void uploadParticleUniforms(GLuint, float) const override {}
    void bindKernelParams(cl_kernel, const KernelParams&) const override {}
};

} // namespace Markdown
