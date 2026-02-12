#pragma once
#include <Editors/Markdown/Effect.hpp>

namespace Markdown {

/// Rainbow effect - hue cycling text (shader only)
class RainbowEffect : public Effect {
public:
    RainbowEffect();
    
    const char* getName() const override { return "Rainbow"; }
    uint32_t getBehaviorID() const override { return 0; }
    std::unique_ptr<Effect> clone() const override { return std::make_unique<RainbowEffect>(*this); }
    
    EffectCapabilities getCapabilities() const override;
    
    GlyphSnippets getGlyphSnippets() const override;
    void uploadGlyphSnippetUniforms(GLuint shader, float time) const override;
};

/// Wave effect - sinusoidal displacement (shader only)
class WaveEffect : public Effect {
public:
    WaveEffect();
    
    const char* getName() const override { return "Wave"; }
    uint32_t getBehaviorID() const override { return 0; }
    std::unique_ptr<Effect> clone() const override { return std::make_unique<WaveEffect>(*this); }
    
    EffectCapabilities getCapabilities() const override;
    
    GlyphSnippets getGlyphSnippets() const override;
    void uploadGlyphSnippetUniforms(GLuint shader, float time) const override;
    
    float amplitude = 3.0f;
    float frequency = 1.0f;
};

/// Shake effect - per-character noise displacement (shader only)
class ShakeEffect : public Effect {
public:
    ShakeEffect();
    
    const char* getName() const override { return "Shake"; }
    uint32_t getBehaviorID() const override { return 0; }
    std::unique_ptr<Effect> clone() const override { return std::make_unique<ShakeEffect>(*this); }
    
    EffectCapabilities getCapabilities() const override;
    
    GlyphSnippets getGlyphSnippets() const override;
    void uploadGlyphSnippetUniforms(GLuint shader, float time) const override;
};

} // namespace Markdown
