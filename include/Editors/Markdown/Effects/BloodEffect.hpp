#pragma once
#include <Editors/Markdown/Effect.hpp>

namespace Markdown {

/// Blood effect - red drip particles that fall with gravity and splatter
class BloodEffect : public Effect {
public:
    BloodEffect();
    
    const char* getName() const override { return "blood"; }
    uint32_t getBehaviorID() const override { return 2; }  // BEHAVIOR_BLOOD
    std::unique_ptr<Effect> clone() const override { return std::make_unique<BloodEffect>(*this); }
    
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
    
    // Blood-specific parameters
    std::array<float, 2> gravity = {0.0f, 80.0f};  // Falls down
    float drag = 0.98f;
    float splatSize = 1.5f;
    
    // SPH fluid simulation parameters
    float sphSmoothingRadius = 25.0f;   // Interaction range (pixels)
    float sphRestDensity = 1.0f;        // Target density
    float sphStiffness = 150.0f;        // Pressure response (higher = more incompressible)
    float sphViscosity = 6.0f;          // Velocity diffusion (higher = thicker/goopier)
    float sphCohesion = 0.3f;           // Surface tension (pulls surface inward)
    float sphParticleMass = 1.0f;       // Mass per particle
};

} // namespace Markdown
