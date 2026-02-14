#pragma once
#include <Editors/Markdown/Effect.hpp>

namespace Markdown {

/// Honey effect - high-viscosity, high-cohesion goo (golden)
class HoneyEffect : public Effect {
public:
    HoneyEffect();

    const char* getName() const override { return "honey"; }
    uint32_t getBehaviorID() const override { return 9; }
    std::unique_ptr<Effect> clone() const override { return std::make_unique<HoneyEffect>(*this); }

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

    // Honey-specific parameters
    std::array<float, 2> gravity = {0.0f, 40.0f};
    float drag = 0.985f;
    float splatSize = 0.9f; // reduced default splat size

    // SPH fluid simulation parameters (defaults tuned for honey)
    float sphSmoothingRadius = 6.0f;
    float sphRestDensity = 1.0f;
    float sphStiffness = 200.0f;
    float sphViscosity = 28.0f;
    float sphCohesion = 0.9f;
    float sphParticleMass = 1.2f;
};

} // namespace Markdown