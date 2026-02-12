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
    EffectEmissionConfig getEmissionConfig() const override;
    
    // Snippet API
    GlyphSnippets getGlyphSnippets() const override;
    void uploadGlyphSnippetUniforms(GLuint shader, float time) const override;
    ParticleSnippets getParticleSnippets() const override;
    void uploadParticleSnippetUniforms(GLuint shader, float time) const override;
    KernelSnippet getKernelSnippet() const override;
    void bindKernelSnippetParams(cl_kernel kernel, const KernelParams& params,
                                  int firstArgIndex) const override;
    
    float orbitSpeed = 2.0f;
    float orbitRadius = 20.0f;
    float riseSpeed = 25.0f;
};

} // namespace Markdown
