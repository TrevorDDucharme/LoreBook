#pragma once
#include <Editors/Markdown/Effect.hpp>

namespace Markdown {

/// Electric/Lightning effect - crackling particles with intense glow
class ElectricEffect : public Effect {
public:
    ElectricEffect();
    
    const char* getName() const override { return "Electric"; }
    uint32_t getBehaviorID() const override { return 5; }
    std::unique_ptr<Effect> clone() const override { return std::make_unique<ElectricEffect>(*this); }
    
    EffectCapabilities getCapabilities() const override;
    EffectEmissionConfig getEmissionConfig() const override;
    
    // Snippet API
    GlyphSnippets getGlyphSnippets() const override;
    void uploadGlyphSnippetUniforms(GLuint shader, float time) const override;
    ParticleSnippets getParticleSnippets() const override;
    void uploadParticleSnippetUniforms(GLuint shader, float time) const override;
    KernelSnippet getKernelSnippet() const override;
    void bindKernelSnippetParams(cl_kernel kernel, const KernelParams& params,
                                  int firstArgIndex) const override;
    std::vector<PostProcessSnippet> getPostProcessSnippets() const override;
    void uploadPostProcessSnippetUniforms(GLuint shader, int passIndex, float time) const override;
    
    // Electric-specific parameters
    float arcFrequency = 3.0f;
    float jitterStrength = 50.0f;
    float chainChance = 0.3f;
};

} // namespace Markdown
