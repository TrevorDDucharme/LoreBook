#pragma once
#include <Editors/Markdown/Effect.hpp>
#include <array>

namespace Markdown {

/// Snow effect - particles fall down with wind drift, accumulate on glyphs
class SnowEffect : public Effect {
public:
    SnowEffect();
    
    const char* getName() const override { return "Snow"; }
    uint32_t getBehaviorID() const override { return 3; }
    std::unique_ptr<Effect> clone() const override { return std::make_unique<SnowEffect>(*this); }
    
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
    
    // Snow-specific parameters
    std::array<float, 2> gravity = {0.0f, 50.0f};
    std::array<float, 2> wind = {20.0f, 0.0f};
    float drift = 0.5f;
    float meltRate = 0.3f;
};

} // namespace Markdown
