#pragma once
#include <imgui.h>
#include <vector>
#include <string>
#include <cstdint>
#include <utility>

// ────────────────────────────────────────────────────────────────────
// TextEffectSystem — Composable, parameter-driven text effects.
//
// Effects are built from small atomic "modifiers" that stack.
// Each modifier has a type and a parameter block.  A complete effect
// is an ordered list of modifiers applied during ImDrawList rendering.
//
// Tag syntax:
//   Single:  <wave speed=2 amplitude=5>text</wave>
//   Compose: <effect wave bounce glow speed=2 color=#FFD700>text</effect>
//   Preset:  <effect preset=neon>text</effect>
//   Procgen: <effect seed=42>text</effect>
// ────────────────────────────────────────────────────────────────────

// ── Modifier component types ────────────────────────────────────────

enum class FXMod : int
{
    // ── Position modifiers ──
    Shake,       // random per-character displacement
    Wave,        // sinusoidal vertical oscillation
    Bounce,      // per-character abs(sin) bounce
    Wobble,      // horizontal oscillation
    Drift,       // slow uniform drift (phase = angle)
    Jitter,      // high-freq tiny random
    Spiral,      // circular per-character motion

    // ── Color modifiers ──
    Rainbow,     // per-character hue cycling
    Gradient,    // top-to-bottom color blend
    ColorWave,   // sinusoidal hue shift across characters
    Tint,        // solid color multiply
    Neon,        // bright saturated + brightness pulse
    Chromatic,   // RGB channel separation (additive copies)

    // ── Alpha modifiers ──
    Pulse,       // uniform alpha oscillation
    FadeWave,    // per-character wave fade
    Flicker,     // random alpha jitter per character
    Typewriter,  // characters appear sequentially over time

    // ── Additive / decoration modifiers ──
    Glow,        // multi-ring soft halo
    Shadow,      // dark offset copy
    Outline,     // thin colored outline copies
    FireEmit,    // rising ember circles
    FairyDust,   // sparkle fairy particles
    Snow,        // falling white particles
    Bubbles,     // rising translucent circles
    Drip,        // slow downward slide then gravity fall (blood drips)

    Count
};

// ── Per-modifier parameters ─────────────────────────────────────────

struct FXParams
{
    float  speed     = 1.0f;  // animation speed multiplier
    float  amplitude = 2.0f;  // displacement / strength (px)
    float  frequency = 1.0f;  // spatial frequency (per-char phase step)
    float  phase     = 0.0f;  // initial phase offset / angle for Drift
    ImVec4 color     = ImVec4(1.0f, 0.84f, 0.0f, 1.0f); // primary color
    ImVec4 color2    = ImVec4(1.0f, 0.0f, 0.0f, 1.0f);  // secondary color (gradients)
    float  radius    = 2.0f;  // glow / outline radius
    float  density   = 3.0f;  // particle spawn density
    float  lifetime  = 1.0f;  // particle lifetime (s)
};

// ── One modifier layer ──────────────────────────────────────────────

struct FXModifier
{
    FXMod    type = FXMod::Count;
    FXParams params;
};

// ── Complete effect = ordered list of modifiers ─────────────────────

struct TextEffectDef
{
    std::vector<FXModifier> modifiers;
    std::string name;    // optional label (preset name, etc.)
};

// ── Public API ──────────────────────────────────────────────────────

namespace TextEffectSystem
{
    // Apply all modifiers in an effect to a draw list vertex range.
    // Ordering: position → color → alpha → additive (automatic).
    void applyEffect(ImDrawList *dl, int vtxStart, int vtxEnd,
                     ImTextureID atlasID, float time,
                     const TextEffectDef &effect);

    // Look up a named preset; returns nullptr if not found.
    const TextEffectDef *getPreset(const std::string &name);

    // List all available preset names.
    std::vector<std::string> listPresets();

    // Procedurally generate a unique effect from a seed.
    TextEffectDef generateProcedural(uint32_t seed);

    // Parse a modifier name string to FXMod enum; returns FXMod::Count if unknown.
    FXMod parseModName(const std::string &name);

    // Build a TextEffectDef from a parsed tag.
    // tagName: the HTML tag name (e.g. "wave", "effect", "glow")
    // attrs: key-value pairs  (e.g. {"speed","2"}, {"color","#FF0000"})
    // bareWords: modifier names without values (e.g. "wave", "bounce")
    TextEffectDef buildFromTag(
        const std::string &tagName,
        const std::vector<std::pair<std::string, std::string>> &attrs,
        const std::vector<std::string> &bareWords = {});
}
