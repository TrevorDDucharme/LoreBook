#pragma once
#include <Editors/Markdown/Effect.hpp>

namespace Markdown {

/// Toxic goo - slightly viscous, neon-green poisonous fluid
class ToxicGooEffect : public Effect {
public:
    ToxicGooEffect();

    const char* getName() const override { return "toxic_goo"; }
    uint32_t getBehaviorID() const override { return 10; }
    std::unique_ptr<Effect> clone() const override { return std::make_unique<ToxicGooEffect>(*this); }

    // Fluid API
    bool isFluid() const override { return true; }
    SPHParams getSPHParams() const override;

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

    // Toxic goo-specific parameters
    std::array<float, 2> gravity = {0.0f, 50.0f};
    float drag = 0.99f;
    float splatSize = 0.7f;

    // SPH fluid simulation parameters (defaults tuned for toxic goo)
    float sphSmoothingRadius = 5.0f;
    float sphRestDensity = 0.95f;
    float sphStiffness = 120.0f;
    float sphViscosity = 8.0f;
    float sphCohesion = 0.25f;
    float sphParticleMass = 1.0f;
};

} // namespace Markdown