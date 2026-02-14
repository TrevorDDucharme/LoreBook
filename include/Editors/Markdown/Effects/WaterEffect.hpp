#pragma once
#include <Editors/Markdown/Effect.hpp>

namespace Markdown {

/// Water effect - low-viscosity fluid (blue)
class WaterEffect : public Effect {
public:
    WaterEffect();

    const char* getName() const override { return "water"; }
    uint32_t getBehaviorID() const override { return 8; }
    std::unique_ptr<Effect> clone() const override { return std::make_unique<WaterEffect>(*this); }

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

    // Water-specific parameters
    std::array<float, 2> gravity = {0.0f, 60.0f};
    float drag = 0.995f;
    float splatSize = 0.35f;

    // SPH fluid simulation parameters (defaults tuned for water)
    float sphSmoothingRadius = 4.5f;
    float sphRestDensity = 1.0f;
    float sphStiffness = 80.0f;
    float sphViscosity = 1.5f;
    float sphCohesion = 0.05f;
    float sphParticleMass = 1.0f;
};

} // namespace Markdown