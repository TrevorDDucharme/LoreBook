#pragma once
#include <Editors/Markdown/Effect.hpp>

namespace Markdown {

/// Sparkle effect - glittering particles that pop in and out
class SparkleEffect : public Effect {
public:
    SparkleEffect();
    
    const char* getName() const override { return "Sparkle"; }
    uint32_t getBehaviorID() const override { return 4; }
    std::unique_ptr<Effect> clone() const override { return std::make_unique<SparkleEffect>(*this); }
    
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
    
    // Sparkle-specific parameters
    float twinkleSpeed = 8.0f;
    float driftSpeed = 10.0f;
};

} // namespace Markdown
