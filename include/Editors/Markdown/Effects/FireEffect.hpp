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
    EffectEmissionConfig getEmissionConfig() const override;
    
    // Snippet API
    GlyphSnippets getGlyphSnippets() const override;
    void uploadGlyphSnippetUniforms(GLuint shader, float time) const override;
    ParticleSnippets getParticleSnippets() const override;
    void uploadParticleSnippetUniforms(GLuint shader, float time) const override;
    KernelSnippet getKernelSnippet() const override;
    void bindKernelSnippetParams(cl_kernel kernel, const KernelParams& params,
                                  int firstArgIndex) const override;
    
    // Fire-specific parameters
    float turbulence = 100.0f;
    float heatDecay = 1.5f;
    std::array<float, 2> gravity = {0.0f, -80.0f};
};

} // namespace Markdown
