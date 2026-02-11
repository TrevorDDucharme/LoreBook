#include "TextEffectSystem.hpp"
#include <imgui_internal.h>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <unordered_map>

using namespace ImGui;

// ════════════════════════════════════════════════════════════════════
//  Helpers
// ════════════════════════════════════════════════════════════════════

static float fxHash(int seed)
{
    seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
    return (float)seed / (float)0x7FFFFFFF;
}

struct GlyphQuad { ImVec2 p0, p1, uv0, uv1; };

static void collectQuads(ImDrawList *dl, int s, int e, std::vector<GlyphQuad> &out)
{
    for (int v = s; v + 3 < e; v += 4)
        out.push_back({dl->VtxBuffer[v].pos, dl->VtxBuffer[v + 2].pos,
                       dl->VtxBuffer[v].uv, dl->VtxBuffer[v + 2].uv});
}

static bool isPositionMod(FXMod t)
{
    return t == FXMod::Shake || t == FXMod::Wave || t == FXMod::Bounce ||
           t == FXMod::Wobble || t == FXMod::Drift || t == FXMod::Jitter ||
           t == FXMod::Spiral;
}

static bool isColorMod(FXMod t)
{
    return t == FXMod::Rainbow || t == FXMod::Gradient || t == FXMod::ColorWave ||
           t == FXMod::Tint || t == FXMod::Neon;
}

static bool isAlphaMod(FXMod t)
{
    return t == FXMod::Pulse || t == FXMod::FadeWave || t == FXMod::Flicker ||
           t == FXMod::Typewriter;
}

// Everything else is additive (Glow, Shadow, Outline, particles, Chromatic)

// ════════════════════════════════════════════════════════════════════
//  Particle systems
// ════════════════════════════════════════════════════════════════════

// ── Fairy dust ──────────────────────────────────────────────────────

namespace {

struct FairyParticle
{
    float x, y, vx, vy, life, maxLife, size, hue, twinklePhase, trailX, trailY;
};

static std::vector<FairyParticle> s_fairy;
static float  s_fairyTime = 0.0f;
static uint32_t s_fairySeed = 12345;

static float fairyRand()
{
    s_fairySeed = s_fairySeed * 1103515245u + 12345u;
    return (float)(s_fairySeed >> 16 & 0x7FFF) / 32767.0f;
}

static void fairySpawn(float x, float y, float density)
{
    int count = (int)(density * 0.3f + 0.5f);
    if (count < 1 && fairyRand() < density * 0.3f) count = 1;
    for (int i = 0; i < count; i++)
    {
        FairyParticle p;
        p.x = x + (fairyRand() - 0.5f) * 8.0f;
        p.y = y + (fairyRand() - 0.5f) * 4.0f;
        float angle = fairyRand() * 6.2832f;
        float spd = 8.0f + fairyRand() * 20.0f;
        p.vx = cosf(angle) * spd;
        p.vy = -5.0f - fairyRand() * 15.0f;
        p.maxLife = 0.6f + fairyRand() * 1.2f;
        p.life = p.maxLife;
        p.size = 1.0f + fairyRand() * 2.5f;
        p.hue = fairyRand();
        p.twinklePhase = fairyRand() * 6.28f;
        p.trailX = p.x;
        p.trailY = p.y;
        s_fairy.push_back(p);
    }
}

static void fairyUpdate(float dt)
{
    for (int i = (int)s_fairy.size() - 1; i >= 0; i--)
    {
        auto &p = s_fairy[i];
        p.trailX = p.x;
        p.trailY = p.y;
        float t = p.maxLife - p.life;
        p.vx += sinf(t * 3.0f + p.twinklePhase) * 30.0f * dt;
        p.vy += cosf(t * 2.5f + p.twinklePhase * 1.3f) * 15.0f * dt;
        p.vx *= (1.0f - 1.5f * dt);
        p.vy *= (1.0f - 1.0f * dt);
        p.x += p.vx * dt;
        p.y += p.vy * dt;
        p.life -= dt;
        if (p.life <= 0.0f)
        {
            s_fairy[i] = s_fairy.back();
            s_fairy.pop_back();
        }
    }
}

static void fairyDraw(ImDrawList *dl)
{
    for (auto &p : s_fairy)
    {
        float t = 1.0f - (p.life / p.maxLife);
        float alpha = (t < 0.1f) ? (t / 0.1f) : (1.0f - (t - 0.1f) / 0.9f);
        alpha = alpha * alpha;
        float twinkle = sinf(p.life * 12.0f + p.twinklePhase) * 0.5f + 0.5f;
        alpha *= 0.5f + 0.5f * twinkle;
        float sz = p.size * (0.3f + 0.7f * (1.0f - t));

        float r, g, b;
        ColorConvertHSVtoRGB(p.hue, 0.4f + 0.3f * twinkle, 1.0f, r, g, b);

        float trailAlpha = alpha * 0.3f;
        if (trailAlpha > 0.01f)
        {
            ImU32 tc = ColorConvertFloat4ToU32(ImVec4(r, g, b, trailAlpha));
            dl->AddLine(ImVec2(p.trailX, p.trailY), ImVec2(p.x, p.y), tc, sz * 0.5f);
        }
        if (alpha > 0.01f)
        {
            ImU32 gc = ColorConvertFloat4ToU32(ImVec4(r, g, b, alpha * 0.25f));
            dl->AddCircleFilled(ImVec2(p.x, p.y), sz * 2.5f, gc, 8);
            ImU32 cc = ColorConvertFloat4ToU32(ImVec4(r * 0.5f + 0.5f, g * 0.5f + 0.5f, b * 0.5f + 0.5f, alpha * 0.9f));
            dl->AddCircleFilled(ImVec2(p.x, p.y), sz * 0.6f, cc, 6);
            ImU32 dc = ColorConvertFloat4ToU32(ImVec4(1, 1, 1, alpha * twinkle));
            dl->AddCircleFilled(ImVec2(p.x, p.y), sz * 0.25f, dc, 4);
        }
        if (t < 0.15f)
        {
            float sa = (1.0f - t / 0.15f) * 0.7f;
            float ss = sz * 3.0f * (1.0f - t / 0.15f);
            ImU32 sc = ColorConvertFloat4ToU32(ImVec4(1, 1, 1, sa));
            dl->AddLine(ImVec2(p.x - ss, p.y), ImVec2(p.x + ss, p.y), sc, 1.0f);
            dl->AddLine(ImVec2(p.x, p.y - ss), ImVec2(p.x, p.y + ss), sc, 1.0f);
            dl->AddLine(ImVec2(p.x - ss * 0.7f, p.y - ss * 0.7f),
                        ImVec2(p.x + ss * 0.7f, p.y + ss * 0.7f), sc, 0.7f);
            dl->AddLine(ImVec2(p.x + ss * 0.7f, p.y - ss * 0.7f),
                        ImVec2(p.x - ss * 0.7f, p.y + ss * 0.7f), sc, 0.7f);
        }
    }
}

// ── Snow particles ──────────────────────────────────────────────────

struct SnowParticle
{
    float x, y, vy, life, maxLife, size, drift;
};
static std::vector<SnowParticle> s_snow;
static float s_snowTime = 0.0f;
static uint32_t s_snowSeed = 77777;

static float snowRand()
{
    s_snowSeed = s_snowSeed * 1103515245u + 12345u;
    return (float)(s_snowSeed >> 16 & 0x7FFF) / 32767.0f;
}

static void snowSpawn(float x, float y)
{
    SnowParticle p;
    p.x = x + (snowRand() - 0.5f) * 20.0f;
    p.y = y - snowRand() * 6.0f;
    p.vy = 15.0f + snowRand() * 25.0f;
    p.maxLife = 1.0f + snowRand() * 2.0f;
    p.life = p.maxLife;
    p.size = 1.0f + snowRand() * 2.0f;
    p.drift = snowRand() * 6.28f;
    s_snow.push_back(p);
}

static void snowUpdate(float dt)
{
    for (int i = (int)s_snow.size() - 1; i >= 0; i--)
    {
        auto &p = s_snow[i];
        p.y += p.vy * dt;
        p.x += sinf(p.life * 2.0f + p.drift) * 15.0f * dt;
        p.life -= dt;
        if (p.life <= 0.0f)
        {
            s_snow[i] = s_snow.back();
            s_snow.pop_back();
        }
    }
}

static void snowDraw(ImDrawList *dl, ImVec4 col)
{
    for (auto &p : s_snow)
    {
        float t = 1.0f - p.life / p.maxLife;
        float a = (t < 0.1f) ? t / 0.1f : (1.0f - t);
        a *= 0.8f;
        ImU32 c = ColorConvertFloat4ToU32(ImVec4(col.x, col.y, col.z, a));
        dl->AddCircleFilled(ImVec2(p.x, p.y), p.size * (1.0f - t * 0.3f), c, 6);
        // Soft glow around snowflake
        ImU32 gc = ColorConvertFloat4ToU32(ImVec4(col.x, col.y, col.z, a * 0.2f));
        dl->AddCircleFilled(ImVec2(p.x, p.y), p.size * 2.5f, gc, 6);
    }
}

// ── Bubble particles ────────────────────────────────────────────────

struct BubbleParticle
{
    float x, y, vy, life, maxLife, size, wobble;
};
static std::vector<BubbleParticle> s_bubbles;
static float s_bubbleTime = 0.0f;
static uint32_t s_bubbleSeed = 54321;

static float bubbleRand()
{
    s_bubbleSeed = s_bubbleSeed * 1103515245u + 12345u;
    return (float)(s_bubbleSeed >> 16 & 0x7FFF) / 32767.0f;
}

static void bubbleSpawn(float x, float y)
{
    BubbleParticle p;
    p.x = x + (bubbleRand() - 0.5f) * 12.0f;
    p.y = y + bubbleRand() * 4.0f;
    p.vy = 20.0f + bubbleRand() * 30.0f;
    p.maxLife = 1.2f + bubbleRand() * 1.5f;
    p.life = p.maxLife;
    p.size = 2.0f + bubbleRand() * 3.0f;
    p.wobble = bubbleRand() * 6.28f;
    s_bubbles.push_back(p);
}

static void bubbleUpdate(float dt)
{
    for (int i = (int)s_bubbles.size() - 1; i >= 0; i--)
    {
        auto &p = s_bubbles[i];
        p.y -= p.vy * dt;
        p.x += sinf(p.life * 3.0f + p.wobble) * 12.0f * dt;
        p.size += 0.3f * dt; // slowly grow
        p.life -= dt;
        if (p.life <= 0.0f)
        {
            s_bubbles[i] = s_bubbles.back();
            s_bubbles.pop_back();
        }
    }
}

static void bubbleDraw(ImDrawList *dl, ImVec4 col)
{
    for (auto &p : s_bubbles)
    {
        float t = 1.0f - p.life / p.maxLife;
        float a = (t < 0.1f) ? t / 0.1f : (t > 0.85f) ? (1.0f - t) / 0.15f : 0.6f;
        // Translucent bubble
        ImU32 bc = ColorConvertFloat4ToU32(ImVec4(col.x, col.y, col.z, a * 0.15f));
        dl->AddCircleFilled(ImVec2(p.x, p.y), p.size, bc, 12);
        // Rim
        ImU32 rc = ColorConvertFloat4ToU32(ImVec4(col.x, col.y, col.z, a * 0.5f));
        dl->AddCircle(ImVec2(p.x, p.y), p.size, rc, 12, 0.8f);
        // Highlight
        ImU32 hc = ColorConvertFloat4ToU32(ImVec4(1, 1, 1, a * 0.6f));
        dl->AddCircleFilled(ImVec2(p.x - p.size * 0.3f, p.y - p.size * 0.3f),
                            p.size * 0.25f, hc, 6);
    }
}

} // anonymous namespace

// ── Blood drip particles ────────────────────────────────────────────
// Lifecycle: spawn at bottom of glyph → cling (slow slide) → drip (gravity fall)
namespace {

struct DripParticle
{
    float x, y;          // position
    float vy;            // vertical velocity
    float life, maxLife;
    float size;
    float clingTime;     // how long the drop clings before falling
    float clingDuration; // total cling phase length
    float wobblePhase;   // per-drop wobble seed
    ImVec4 color;
};

static std::vector<DripParticle> s_drips;
static float s_dripTime = 0.0f;
static uint32_t s_dripSeed = 31337;

static float dripRand()
{
    s_dripSeed = s_dripSeed * 1103515245u + 12345u;
    return (float)(s_dripSeed >> 16 & 0x7FFF) / 32767.0f;
}

static void dripSpawn(float x, float y, ImVec4 col)
{
    DripParticle p;
    p.x = x + (dripRand() - 0.5f) * 4.0f;
    p.y = y;
    p.vy = 0.0f;
    p.maxLife = 2.5f + dripRand() * 2.0f;
    p.life = p.maxLife;
    // Cling for 40-70% of lifetime before dripping
    p.clingDuration = p.maxLife * (0.4f + dripRand() * 0.3f);
    p.clingTime = 0.0f;
    p.size = 1.5f + dripRand() * 1.5f;
    p.wobblePhase = dripRand() * 6.28f;
    // Vary the red shade slightly
    float shade = 0.7f + dripRand() * 0.3f;
    p.color = ImVec4(col.x * shade, col.y * shade * 0.3f, col.z * shade * 0.2f, col.w);
    s_drips.push_back(p);
}

static void dripUpdate(float dt)
{
    for (int i = (int)s_drips.size() - 1; i >= 0; i--)
    {
        auto &p = s_drips[i];
        p.clingTime += dt;

        if (p.clingTime < p.clingDuration)
        {
            // Cling phase: very slow creep downward, slight wobble
            float clingProgress = p.clingTime / p.clingDuration;
            // Accelerate slightly as the drop gets heavier
            float creepSpeed = 3.0f + clingProgress * 8.0f;
            p.y += creepSpeed * dt;
            p.x += sinf(p.clingTime * 2.0f + p.wobblePhase) * 0.5f * dt;
            // Drop grows slightly as it accumulates
            p.size += 0.3f * dt;
        }
        else
        {
            // Drip phase: gravity acceleration (fast fall)
            p.vy += 180.0f * dt; // strong gravity
            p.y += p.vy * dt;
            p.x += sinf(p.clingTime * 1.5f + p.wobblePhase) * 0.3f * dt;
            // Elongate while falling
            p.size *= (1.0f - 0.4f * dt);
            if (p.size < 0.5f) p.size = 0.5f;
        }

        p.life -= dt;
        if (p.life <= 0.0f)
        {
            s_drips[i] = s_drips.back();
            s_drips.pop_back();
        }
    }
}

static void dripDraw(ImDrawList *dl)
{
    for (auto &p : s_drips)
    {
        float t = 1.0f - p.life / p.maxLife; // 0=born, 1=dead
        float alpha = (t < 0.05f) ? (t / 0.05f) : 1.0f;
        // Fade out only in the last 15% of life
        if (t > 0.85f) alpha *= (1.0f - t) / 0.15f;

        bool dripping = (p.clingTime >= p.clingDuration);

        if (dripping)
        {
            // Elongated drop shape: teardrop = circle + stretched tail above
            float tailLen = std::min(p.vy * 0.04f, 12.0f);
            float sz = p.size;

            // Trail/tail (line from above to drop center)
            ImU32 tailCol = ColorConvertFloat4ToU32(
                ImVec4(p.color.x, p.color.y, p.color.z, alpha * 0.5f));
            dl->AddLine(ImVec2(p.x, p.y - tailLen), ImVec2(p.x, p.y),
                        tailCol, sz * 0.6f);

            // Main drop body
            ImU32 bodyCol = ColorConvertFloat4ToU32(
                ImVec4(p.color.x, p.color.y, p.color.z, alpha * 0.85f));
            dl->AddCircleFilled(ImVec2(p.x, p.y), sz, bodyCol, 8);

            // Specular highlight
            ImU32 specCol = ColorConvertFloat4ToU32(
                ImVec4(1.0f, 0.3f, 0.3f, alpha * 0.4f));
            dl->AddCircleFilled(ImVec2(p.x - sz * 0.25f, p.y - sz * 0.25f),
                                sz * 0.3f, specCol, 6);
        }
        else
        {
            // Clinging phase: round bead growing at bottom of letter
            float sz = p.size;
            // Dark drop body
            ImU32 bodyCol = ColorConvertFloat4ToU32(
                ImVec4(p.color.x, p.color.y, p.color.z, alpha * 0.9f));
            dl->AddCircleFilled(ImVec2(p.x, p.y), sz, bodyCol, 8);

            // Wet smear trail above (thin line going up)
            float smearLen = p.clingTime / p.clingDuration * 6.0f;
            if (smearLen > 1.0f)
            {
                ImU32 smearCol = ColorConvertFloat4ToU32(
                    ImVec4(p.color.x, p.color.y, p.color.z, alpha * 0.25f));
                dl->AddLine(ImVec2(p.x, p.y - smearLen), ImVec2(p.x, p.y - sz * 0.5f),
                            smearCol, sz * 0.4f);
            }

            // Specular
            ImU32 specCol = ColorConvertFloat4ToU32(
                ImVec4(1.0f, 0.3f, 0.3f, alpha * 0.3f));
            dl->AddCircleFilled(ImVec2(p.x - sz * 0.2f, p.y - sz * 0.2f),
                                sz * 0.25f, specCol, 6);
        }
    }
}

} // anon drip namespace

static void applyDrip(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    float dt = t - s_dripTime;
    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;

    // Tint text dark red with a wet look
    int ci = 0;
    for (int v = s; v + 3 < e; v += 4, ci++)
    {
        // Slight per-char color variation for wet/bloody look
        float wetPulse = sinf(t * 1.5f + ci * 0.9f) * 0.1f + 0.9f;
        float r = p.color.x * 0.8f * wetPulse;
        float g = p.color.y * 0.15f * wetPulse;
        float b = p.color.z * 0.1f * wetPulse;
        ImU32 topCol = ColorConvertFloat4ToU32(ImVec4(r, g, b, 1.0f));
        // Darker at bottom
        ImU32 botCol = ColorConvertFloat4ToU32(ImVec4(r * 0.5f, g * 0.3f, b * 0.2f, 1.0f));
        dl->VtxBuffer[v].col = topCol;
        dl->VtxBuffer[v + 1].col = topCol;
        dl->VtxBuffer[v + 2].col = botCol;
        dl->VtxBuffer[v + 3].col = botCol;

        // Spawn drip particles from bottom edge of characters
        ImVec2 cMin = dl->VtxBuffer[v].pos;
        ImVec2 cMax = dl->VtxBuffer[v + 2].pos;
        float cx = cMin.x + (cMax.x - cMin.x) * dripRand();
        float cy = cMax.y; // bottom of character

        if (dripRand() < p.density * dt * 0.15f)
            dripSpawn(cx, cy, p.color);
    }

    dripUpdate(dt);
    dripDraw(dl);
    s_dripTime = t;
}

// ════════════════════════════════════════════════════════════════════
//  Individual modifier apply functions
// ════════════════════════════════════════════════════════════════════

// ── Position modifiers ──────────────────────────────────────────────

static void applyShake(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    int ci = 0;
    for (int v = s; v + 3 < e; v += 4, ci++)
    {
        float ox = sinf(t * 15.0f * p.speed + ci * 2.1f) * p.amplitude;
        float oy = cosf(t * 13.0f * p.speed + ci * 3.3f) * p.amplitude;
        for (int k = 0; k < 4; k++)
        {
            dl->VtxBuffer[v + k].pos.x += ox;
            dl->VtxBuffer[v + k].pos.y += oy;
        }
    }
}

static void applyWave(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    int ci = 0;
    for (int v = s; v + 3 < e; v += 4, ci++)
    {
        float oy = sinf(t * 3.0f * p.speed + ci * 0.5f * p.frequency + p.phase) * p.amplitude;
        for (int k = 0; k < 4; k++)
            dl->VtxBuffer[v + k].pos.y += oy;
    }
}

static void applyBounce(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    int ci = 0;
    for (int v = s; v + 3 < e; v += 4, ci++)
    {
        float bounce = fabsf(sinf(t * 4.0f * p.speed + ci * 0.7f * p.frequency)) * p.amplitude;
        for (int k = 0; k < 4; k++)
            dl->VtxBuffer[v + k].pos.y -= bounce;
    }
}

static void applyWobble(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    int ci = 0;
    for (int v = s; v + 3 < e; v += 4, ci++)
    {
        float ox = sinf(t * 5.0f * p.speed + ci * 0.9f * p.frequency) * p.amplitude * 0.6f;
        for (int k = 0; k < 4; k++)
            dl->VtxBuffer[v + k].pos.x += ox;
    }
}

static void applyDrift(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    // phase = angle in radians (0=right, PI/2=up)
    float dx = cosf(p.phase) * p.speed * 5.0f * sinf(t * 0.5f);
    float dy = -sinf(p.phase) * p.speed * 5.0f * sinf(t * 0.5f);
    for (int v = s; v < e; v++)
    {
        dl->VtxBuffer[v].pos.x += dx;
        dl->VtxBuffer[v].pos.y += dy;
    }
}

static void applyJitter(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    int ci = 0;
    for (int v = s; v + 3 < e; v += 4, ci++)
    {
        float ox = fxHash(ci * 7 + (int)(t * 60.0f * p.speed)) * 2.0f - 1.0f;
        float oy = fxHash(ci * 13 + (int)(t * 60.0f * p.speed) + 999) * 2.0f - 1.0f;
        ox *= p.amplitude * 0.5f;
        oy *= p.amplitude * 0.5f;
        for (int k = 0; k < 4; k++)
        {
            dl->VtxBuffer[v + k].pos.x += ox;
            dl->VtxBuffer[v + k].pos.y += oy;
        }
    }
}

static void applySpiral(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    int ci = 0;
    for (int v = s; v + 3 < e; v += 4, ci++)
    {
        float angle = t * 3.0f * p.speed + ci * 0.8f * p.frequency;
        float ox = cosf(angle) * p.amplitude;
        float oy = sinf(angle) * p.amplitude;
        for (int k = 0; k < 4; k++)
        {
            dl->VtxBuffer[v + k].pos.x += ox;
            dl->VtxBuffer[v + k].pos.y += oy;
        }
    }
}

// ── Color modifiers ─────────────────────────────────────────────────

static void applyRainbow(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    int ci = 0;
    for (int v = s; v + 3 < e; v += 4, ci++)
    {
        float hue = fmodf(ci * 0.08f * p.frequency + t * p.speed, 1.0f);
        float r, g, b;
        ColorConvertHSVtoRGB(hue, 0.9f, 1.0f, r, g, b);
        ImU32 col = ColorConvertFloat4ToU32(ImVec4(r, g, b, 1.0f));
        for (int k = 0; k < 4; k++)
            dl->VtxBuffer[v + k].col = col;
    }
}

static void applyGradient(ImDrawList *dl, int s, int e, float /*t*/, const FXParams &p)
{
    ImU32 topCol = ColorConvertFloat4ToU32(p.color);
    ImU32 botCol = ColorConvertFloat4ToU32(p.color2);
    for (int v = s; v + 3 < e; v += 4)
    {
        dl->VtxBuffer[v].col = topCol;     // top-left
        dl->VtxBuffer[v + 1].col = topCol; // top-right
        dl->VtxBuffer[v + 2].col = botCol; // bottom-right
        dl->VtxBuffer[v + 3].col = botCol; // bottom-left
    }
}

static void applyColorWave(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    int ci = 0;
    for (int v = s; v + 3 < e; v += 4, ci++)
    {
        float blend = sinf(t * 2.0f * p.speed + ci * 0.4f * p.frequency) * 0.5f + 0.5f;
        float r = p.color.x * (1.0f - blend) + p.color2.x * blend;
        float g = p.color.y * (1.0f - blend) + p.color2.y * blend;
        float b = p.color.z * (1.0f - blend) + p.color2.z * blend;
        ImU32 col = ColorConvertFloat4ToU32(ImVec4(r, g, b, 1.0f));
        for (int k = 0; k < 4; k++)
            dl->VtxBuffer[v + k].col = col;
    }
}

static void applyTint(ImDrawList *dl, int s, int e, float /*t*/, const FXParams &p)
{
    for (int v = s; v < e; v++)
    {
        ImVec4 c = ColorConvertU32ToFloat4(dl->VtxBuffer[v].col);
        c.x *= p.color.x;
        c.y *= p.color.y;
        c.z *= p.color.z;
        c.w *= p.color.w;
        dl->VtxBuffer[v].col = ColorConvertFloat4ToU32(c);
    }
}

static void applyNeon(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    float pulse = sinf(t * 4.0f * p.speed) * 0.15f + 0.85f;
    float bx = p.color.x * 0.5f + 0.5f;
    float by = p.color.y * 0.5f + 0.5f;
    float bz = p.color.z * 0.5f + 0.5f;
    ImU32 col = ColorConvertFloat4ToU32(ImVec4(bx * pulse, by * pulse, bz * pulse, 1.0f));
    for (int v = s; v < e; v++)
        dl->VtxBuffer[v].col = col;
}

// ── Alpha modifiers ─────────────────────────────────────────────────

static void applyPulse(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    float phase = sinf(t * 2.0f * 3.14159f * p.speed);
    float alpha = 0.3f + 0.7f * (phase * 0.5f + 0.5f);
    for (int v = s; v < e; v++)
    {
        ImVec4 c = ColorConvertU32ToFloat4(dl->VtxBuffer[v].col);
        c.w *= alpha;
        dl->VtxBuffer[v].col = ColorConvertFloat4ToU32(c);
    }
}

static void applyFadeWave(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    int ci = 0;
    for (int v = s; v + 3 < e; v += 4, ci++)
    {
        float alpha = sinf(t * 3.0f * p.speed + ci * 0.6f * p.frequency) * 0.5f + 0.5f;
        alpha = 0.2f + 0.8f * alpha;
        for (int k = 0; k < 4; k++)
        {
            ImVec4 c = ColorConvertU32ToFloat4(dl->VtxBuffer[v + k].col);
            c.w *= alpha;
            dl->VtxBuffer[v + k].col = ColorConvertFloat4ToU32(c);
        }
    }
}

static void applyFlicker(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    int ci = 0;
    for (int v = s; v + 3 < e; v += 4, ci++)
    {
        float flick = fxHash(ci * 17 + (int)(t * 30.0f * p.speed));
        float alpha = 0.4f + 0.6f * flick;
        for (int k = 0; k < 4; k++)
        {
            ImVec4 c = ColorConvertU32ToFloat4(dl->VtxBuffer[v + k].col);
            c.w *= alpha;
            dl->VtxBuffer[v + k].col = ColorConvertFloat4ToU32(c);
        }
    }
}

static void applyTypewriter(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    int totalChars = 0;
    for (int v = s; v + 3 < e; v += 4) totalChars++;
    int visibleChars = (int)(t * p.speed * 8.0f) % (totalChars + totalChars / 2 + 1);
    // After all chars shown, hold for a moment before looping
    if (visibleChars > totalChars) visibleChars = totalChars;
    int ci = 0;
    for (int v = s; v + 3 < e; v += 4, ci++)
    {
        if (ci >= visibleChars)
        {
            for (int k = 0; k < 4; k++)
            {
                ImVec4 c = ColorConvertU32ToFloat4(dl->VtxBuffer[v + k].col);
                c.w = 0.0f;
                dl->VtxBuffer[v + k].col = ColorConvertFloat4ToU32(c);
            }
        }
    }
}

// ── Additive modifiers ──────────────────────────────────────────────

static void applyGlow(ImDrawList *dl, int s, int e, ImTextureID atlas, float t, const FXParams &p)
{
    float pulse = sinf(t * 2.0f) * 0.5f + 0.5f;
    float breathe = sinf(t * 1.2f) * 0.3f + 0.7f;
    float rad = p.radius;
    float baseAlpha = 0.35f + 0.15f * pulse;

    std::vector<GlyphQuad> quads;
    collectQuads(dl, s, e, quads);

    const int NUM_RINGS = 4;
    float ringRadii[NUM_RINGS] = {rad * 0.5f, rad, rad * 1.8f, rad * 3.0f};
    float ringAlphas[NUM_RINGS] = {baseAlpha * 0.7f, baseAlpha * 0.45f,
                                    baseAlpha * 0.2f, baseAlpha * 0.08f};
    float ringScale[NUM_RINGS] = {1.0f, 1.02f, 1.05f, 1.1f};

    for (int ring = NUM_RINGS - 1; ring >= 0; ring--)
    {
        float r = ringRadii[ring] * breathe;
        float a = ringAlphas[ring];
        ImU32 col = ColorConvertFloat4ToU32(ImVec4(p.color.x, p.color.y, p.color.z, a));
        float sc = ringScale[ring];
        for (auto &q : quads)
        {
            float cx = (q.p0.x + q.p1.x) * 0.5f;
            float cy = (q.p0.y + q.p1.y) * 0.5f;
            float hw = (q.p1.x - q.p0.x) * 0.5f * sc;
            float hh = (q.p1.y - q.p0.y) * 0.5f * sc;
            ImVec2 sp0(cx - hw, cy - hh);
            ImVec2 sp1(cx + hw, cy + hh);
            for (int d = 0; d < 12; d++)
            {
                float angle = d * (3.14159f * 2.0f / 12.0f);
                float ox = cosf(angle) * r;
                float oy = sinf(angle) * r;
                dl->AddImage(atlas, ImVec2(sp0.x + ox, sp0.y + oy),
                             ImVec2(sp1.x + ox, sp1.y + oy), q.uv0, q.uv1, col);
            }
        }
    }

    // Bright tinted text
    float bx = p.color.x * 0.6f + 0.4f;
    float by = p.color.y * 0.6f + 0.4f;
    float bz = p.color.z * 0.6f + 0.4f;
    ImU32 tintCol = ColorConvertFloat4ToU32(ImVec4(bx, by, bz, 1.0f));
    for (int v = s; v < e; v++)
        dl->VtxBuffer[v].col = tintCol;
}

static void applyShadow(ImDrawList *dl, int s, int e, ImTextureID atlas, float /*t*/, const FXParams &p)
{
    std::vector<GlyphQuad> quads;
    collectQuads(dl, s, e, quads);

    float ox = p.amplitude;
    float oy = p.amplitude;
    float a = 0.5f;
    ImU32 col = ColorConvertFloat4ToU32(ImVec4(p.color.x * 0.1f, p.color.y * 0.1f, p.color.z * 0.1f, a));

    for (auto &q : quads)
    {
        dl->AddImage(atlas, ImVec2(q.p0.x + ox, q.p0.y + oy),
                     ImVec2(q.p1.x + ox, q.p1.y + oy), q.uv0, q.uv1, col);
    }
}

static void applyOutline(ImDrawList *dl, int s, int e, ImTextureID atlas, float /*t*/, const FXParams &p)
{
    std::vector<GlyphQuad> quads;
    collectQuads(dl, s, e, quads);

    float rad = p.radius * 0.5f;
    ImU32 col = ColorConvertFloat4ToU32(ImVec4(p.color.x, p.color.y, p.color.z, 0.9f));

    for (auto &q : quads)
    {
        for (int d = 0; d < 8; d++)
        {
            float angle = d * 0.7854f;
            float ox = cosf(angle) * rad;
            float oy = sinf(angle) * rad;
            dl->AddImage(atlas, ImVec2(q.p0.x + ox, q.p0.y + oy),
                         ImVec2(q.p1.x + ox, q.p1.y + oy), q.uv0, q.uv1, col);
        }
    }
}

static void applyChromatic(ImDrawList *dl, int s, int e, ImTextureID atlas, float t, const FXParams &p)
{
    std::vector<GlyphQuad> quads;
    collectQuads(dl, s, e, quads);

    float spread = p.amplitude * 0.5f;
    float anim = sinf(t * p.speed) * 0.3f + 0.7f;
    float ox = spread * anim;

    for (auto &q : quads)
    {
        // Red channel offset left
        dl->AddImage(atlas, ImVec2(q.p0.x - ox, q.p0.y),
                     ImVec2(q.p1.x - ox, q.p1.y), q.uv0, q.uv1,
                     IM_COL32(255, 0, 0, 100));
        // Blue channel offset right
        dl->AddImage(atlas, ImVec2(q.p0.x + ox, q.p0.y),
                     ImVec2(q.p1.x + ox, q.p1.y), q.uv0, q.uv1,
                     IM_COL32(0, 0, 255, 100));
    }
    // Tint original green-ish
    for (int v = s; v < e; v++)
        dl->VtxBuffer[v].col = IM_COL32(0, 255, 0, 200);
}

static void applyFireEmit(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    int ci = 0;
    for (int v = s; v + 3 < e; v += 4, ci++)
    {
        // Tint chars with fire gradient
        float flicker = sinf(t * 8.0f * p.speed + ci * 1.7f) * 0.5f + 0.5f;
        float r = 1.0f, g = 0.3f + 0.4f * flicker, b = 0.0f + 0.1f * flicker;
        ImU32 topCol = ColorConvertFloat4ToU32(ImVec4(r, g, b, 1.0f));
        ImU32 botCol = ColorConvertFloat4ToU32(ImVec4(0.8f, 0.15f, 0.0f, 1.0f));
        dl->VtxBuffer[v].col = topCol;
        dl->VtxBuffer[v + 1].col = topCol;
        dl->VtxBuffer[v + 2].col = botCol;
        dl->VtxBuffer[v + 3].col = botCol;

        // Spawn ember
        ImVec2 cMin = dl->VtxBuffer[v].pos;
        ImVec2 cMax = dl->VtxBuffer[v + 2].pos;
        float cx = (cMin.x + cMax.x) * 0.5f;
        float cy = cMin.y;
        float sc = sinf(t * 13.7f * p.speed + ci * 3.1f);
        if (sc > 0.7f)
        {
            float age = fmodf(t * 2.0f * p.speed + ci * 0.4f, 1.0f);
            float py = cy - age * 14.0f * p.lifetime;
            float px = cx + sinf(age * 6.0f + ci) * 3.0f;
            float pA = (1.0f - age) * 0.8f;
            float pSz = (1.0f - age) * 2.5f;
            ImU32 pCol = ColorConvertFloat4ToU32(ImVec4(1.0f, 0.6f * (1.0f - age), 0.0f, pA));
            dl->AddCircleFilled(ImVec2(px, py), pSz, pCol, 8);
        }
    }
}

static void applyFairyDust(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    float dt = t - s_fairyTime;
    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;

    int ci = 0;
    for (int v = s; v + 3 < e; v += 4, ci++)
    {
        // Shimmer tint
        float shimmer = sinf(t * 3.0f + ci * 1.1f) * 0.5f + 0.5f;
        float hue = fmodf(ci * 0.07f + t * 0.15f, 1.0f);
        float r, g, b;
        ColorConvertHSVtoRGB(hue, 0.2f + 0.15f * shimmer, 1.0f, r, g, b);
        ImU32 col = ColorConvertFloat4ToU32(ImVec4(r, g, b, 1.0f));
        for (int k = 0; k < 4; k++)
            dl->VtxBuffer[v + k].col = col;

        // Spawn
        ImVec2 cMin = dl->VtxBuffer[v].pos;
        ImVec2 cMax = dl->VtxBuffer[v + 2].pos;
        float cx = cMin.x + (cMax.x - cMin.x) * fairyRand();
        float cy = cMin.y + (cMax.y - cMin.y) * fairyRand();
        if (fairyRand() < p.density * dt * 0.5f)
            fairySpawn(cx, cy, 1.0f);
    }

    fairyUpdate(dt);
    fairyDraw(dl);
    s_fairyTime = t;
}

static void applySnow(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    float dt = t - s_snowTime;
    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;

    int ci = 0;
    for (int v = s; v + 3 < e; v += 4, ci++)
    {
        ImVec2 cMin = dl->VtxBuffer[v].pos;
        ImVec2 cMax = dl->VtxBuffer[v + 2].pos;
        float cx = cMin.x + (cMax.x - cMin.x) * snowRand();
        float cy = cMin.y;
        if (snowRand() < p.density * dt * 0.3f)
            snowSpawn(cx, cy);
    }

    snowUpdate(dt);
    snowDraw(dl, p.color);
    s_snowTime = t;
}

static void applyBubbles(ImDrawList *dl, int s, int e, float t, const FXParams &p)
{
    float dt = t - s_bubbleTime;
    if (dt <= 0.0f || dt > 0.1f) dt = 0.016f;

    int ci = 0;
    for (int v = s; v + 3 < e; v += 4, ci++)
    {
        ImVec2 cMin = dl->VtxBuffer[v].pos;
        ImVec2 cMax = dl->VtxBuffer[v + 2].pos;
        float cx = cMin.x + (cMax.x - cMin.x) * bubbleRand();
        float cy = cMax.y;
        if (bubbleRand() < p.density * dt * 0.2f)
            bubbleSpawn(cx, cy);
    }

    bubbleUpdate(dt);
    bubbleDraw(dl, p.color);
    s_bubbleTime = t;
}

// ════════════════════════════════════════════════════════════════════
//  Main apply function
// ════════════════════════════════════════════════════════════════════

namespace TextEffectSystem
{

void applyEffect(ImDrawList *dl, int vtxStart, int vtxEnd,
                 ImTextureID atlasID, float time,
                 const TextEffectDef &effect)
{
    if (vtxStart >= vtxEnd || effect.modifiers.empty())
        return;

    int origStart = vtxStart;
    int origEnd = vtxEnd;

    // Phase 1: Position modifiers
    for (const auto &mod : effect.modifiers)
    {
        if (!isPositionMod(mod.type)) continue;
        switch (mod.type)
        {
        case FXMod::Shake:   applyShake(dl, origStart, origEnd, time, mod.params); break;
        case FXMod::Wave:    applyWave(dl, origStart, origEnd, time, mod.params); break;
        case FXMod::Bounce:  applyBounce(dl, origStart, origEnd, time, mod.params); break;
        case FXMod::Wobble:  applyWobble(dl, origStart, origEnd, time, mod.params); break;
        case FXMod::Drift:   applyDrift(dl, origStart, origEnd, time, mod.params); break;
        case FXMod::Jitter:  applyJitter(dl, origStart, origEnd, time, mod.params); break;
        case FXMod::Spiral:  applySpiral(dl, origStart, origEnd, time, mod.params); break;
        default: break;
        }
    }

    // Phase 2: Color modifiers
    for (const auto &mod : effect.modifiers)
    {
        if (!isColorMod(mod.type)) continue;
        switch (mod.type)
        {
        case FXMod::Rainbow:   applyRainbow(dl, origStart, origEnd, time, mod.params); break;
        case FXMod::Gradient:  applyGradient(dl, origStart, origEnd, time, mod.params); break;
        case FXMod::ColorWave: applyColorWave(dl, origStart, origEnd, time, mod.params); break;
        case FXMod::Tint:      applyTint(dl, origStart, origEnd, time, mod.params); break;
        case FXMod::Neon:      applyNeon(dl, origStart, origEnd, time, mod.params); break;
        default: break;
        }
    }

    // Phase 3: Alpha modifiers
    for (const auto &mod : effect.modifiers)
    {
        if (!isAlphaMod(mod.type)) continue;
        switch (mod.type)
        {
        case FXMod::Pulse:      applyPulse(dl, origStart, origEnd, time, mod.params); break;
        case FXMod::FadeWave:   applyFadeWave(dl, origStart, origEnd, time, mod.params); break;
        case FXMod::Flicker:    applyFlicker(dl, origStart, origEnd, time, mod.params); break;
        case FXMod::Typewriter: applyTypewriter(dl, origStart, origEnd, time, mod.params); break;
        default: break;
        }
    }

    // Phase 4: Additive modifiers (may add geometry; always read original range)
    for (const auto &mod : effect.modifiers)
    {
        if (isPositionMod(mod.type) || isColorMod(mod.type) || isAlphaMod(mod.type))
            continue;
        switch (mod.type)
        {
        case FXMod::Glow:      applyGlow(dl, origStart, origEnd, atlasID, time, mod.params); break;
        case FXMod::Shadow:    applyShadow(dl, origStart, origEnd, atlasID, time, mod.params); break;
        case FXMod::Outline:   applyOutline(dl, origStart, origEnd, atlasID, time, mod.params); break;
        case FXMod::Chromatic: applyChromatic(dl, origStart, origEnd, atlasID, time, mod.params); break;
        case FXMod::FireEmit:  applyFireEmit(dl, origStart, origEnd, time, mod.params); break;
        case FXMod::FairyDust: applyFairyDust(dl, origStart, origEnd, time, mod.params); break;
        case FXMod::Snow:      applySnow(dl, origStart, origEnd, time, mod.params); break;
        case FXMod::Bubbles:   applyBubbles(dl, origStart, origEnd, time, mod.params); break;
        case FXMod::Drip:      applyDrip(dl, origStart, origEnd, time, mod.params); break;
        default: break;
        }
    }
}

// ════════════════════════════════════════════════════════════════════
//  Modifier name parsing
// ════════════════════════════════════════════════════════════════════

static const struct { const char *name; FXMod mod; } s_modNames[] = {
    {"shake",     FXMod::Shake},
    {"wave",      FXMod::Wave},
    {"bounce",    FXMod::Bounce},
    {"wobble",    FXMod::Wobble},
    {"drift",     FXMod::Drift},
    {"jitter",    FXMod::Jitter},
    {"spiral",    FXMod::Spiral},
    {"rainbow",   FXMod::Rainbow},
    {"gradient",  FXMod::Gradient},
    {"colorwave", FXMod::ColorWave},
    {"tint",      FXMod::Tint},
    {"neon",      FXMod::Neon},
    {"chromatic", FXMod::Chromatic},
    {"pulse",     FXMod::Pulse},
    {"fadewave",  FXMod::FadeWave},
    {"flicker",   FXMod::Flicker},
    {"typewriter",FXMod::Typewriter},
    {"glow",      FXMod::Glow},
    {"shadow",    FXMod::Shadow},
    {"outline",   FXMod::Outline},
    {"fire",      FXMod::FireEmit},
    {"fireemit",  FXMod::FireEmit},
    {"fairydust", FXMod::FairyDust},
    {"sparkle",   FXMod::FairyDust},
    {"snow",      FXMod::Snow},
    {"bubbles",   FXMod::Bubbles},
    {"drip",      FXMod::Drip},
    {"blood",     FXMod::Drip},     // <blood> tag = drip modifier
    {"particle",  FXMod::FireEmit},  // legacy: <particle> defaults to fire
};

FXMod parseModName(const std::string &name)
{
    for (auto &m : s_modNames)
        if (name == m.name) return m.mod;
    return FXMod::Count;
}

// ════════════════════════════════════════════════════════════════════
//  Presets
// ════════════════════════════════════════════════════════════════════

static FXModifier makeMod(FXMod type)
{
    FXModifier m;
    m.type = type;
    return m;
}

static FXModifier makeMod(FXMod type, ImVec4 col, float speed = 1.0f, float amp = 2.0f,
                           float freq = 1.0f, float rad = 2.0f, float dens = 3.0f, float life = 1.0f)
{
    FXModifier m;
    m.type = type;
    m.params.color = col;
    m.params.speed = speed;
    m.params.amplitude = amp;
    m.params.frequency = freq;
    m.params.radius = rad;
    m.params.density = dens;
    m.params.lifetime = life;
    return m;
}

static std::unordered_map<std::string, TextEffectDef> initPresets()
{
    std::unordered_map<std::string, TextEffectDef> m;
    auto W = ImVec4(1,1,1,1);
    auto G = ImVec4(1.0f,0.84f,0.0f,1.0f);  // gold
    auto R = ImVec4(1,0,0,1);
    auto B = ImVec4(0.2f,0.4f,1.0f,1.0f);   // blue
    auto C = ImVec4(0,1,1,1);                // cyan
    auto Gn = ImVec4(0,1,0,1);               // green
    auto P = ImVec4(0.6f,0,1,1);             // purple
    auto O = ImVec4(1.0f,0.5f,0,1);          // orange

    // neon — bright glow + pulse
    { TextEffectDef d; d.name = "neon";
      d.modifiers.push_back(makeMod(FXMod::Glow, C, 2.0f, 2.0f, 1.0f, 3.0f));
      d.modifiers.push_back(makeMod(FXMod::Pulse, W, 2.0f));
      m["neon"] = std::move(d); }

    // fire — gradient + fire particles + subtle wave
    { TextEffectDef d; d.name = "fire";
      auto gm = makeMod(FXMod::Gradient, O); gm.params.color2 = R;
      d.modifiers.push_back(gm);
      d.modifiers.push_back(makeMod(FXMod::FireEmit, O, 1.0f, 2.0f, 1.0f, 2.0f, 3.0f, 1.0f));
      d.modifiers.push_back(makeMod(FXMod::Wave, W, 0.5f, 1.0f));
      m["fire"] = std::move(d); }

    // ice — blue tint + snow + glow
    { TextEffectDef d; d.name = "ice";
      d.modifiers.push_back(makeMod(FXMod::Tint, C));
      d.modifiers.push_back(makeMod(FXMod::Snow, W, 1.0f, 2.0f, 1.0f, 2.0f, 2.0f, 2.0f));
      d.modifiers.push_back(makeMod(FXMod::Glow, B, 1.0f, 2.0f, 1.0f, 2.0f));
      m["ice"] = std::move(d); }

    // magic — rainbow + fairy dust + glow
    { TextEffectDef d; d.name = "magic";
      d.modifiers.push_back(makeMod(FXMod::Rainbow, W, 0.8f));
      d.modifiers.push_back(makeMod(FXMod::FairyDust, W, 1.0f, 2.0f, 1.0f, 2.0f, 4.0f));
      d.modifiers.push_back(makeMod(FXMod::Glow, G, 1.0f, 2.0f, 1.0f, 2.5f));
      m["magic"] = std::move(d); }

    // ghost — pulse + fade wave + drift up
    { TextEffectDef d; d.name = "ghost";
      d.modifiers.push_back(makeMod(FXMod::Pulse, W, 0.5f));
      d.modifiers.push_back(makeMod(FXMod::FadeWave, W, 0.7f, 2.0f, 0.8f));
      auto dr = makeMod(FXMod::Drift, W, 0.3f, 3.0f); dr.params.phase = 1.5708f; // up
      d.modifiers.push_back(dr);
      d.modifiers.push_back(makeMod(FXMod::Tint, ImVec4(0.6f,0.7f,0.9f,0.7f)));
      m["ghost"] = std::move(d); }

    // electric — neon yellow + shake + flicker
    { TextEffectDef d; d.name = "electric";
      d.modifiers.push_back(makeMod(FXMod::Neon, ImVec4(1,1,0,1), 3.0f));
      d.modifiers.push_back(makeMod(FXMod::Shake, W, 2.0f, 1.0f));
      d.modifiers.push_back(makeMod(FXMod::Flicker, W, 2.0f));
      m["electric"] = std::move(d); }

    // underwater — blue tint + wave + bubbles
    { TextEffectDef d; d.name = "underwater";
      d.modifiers.push_back(makeMod(FXMod::Tint, B));
      d.modifiers.push_back(makeMod(FXMod::Wave, W, 0.6f, 3.0f, 0.5f));
      d.modifiers.push_back(makeMod(FXMod::Bubbles, ImVec4(0.5f,0.7f,1,1), 1.0f, 2.0f, 1.0f, 2.0f, 2.0f, 2.0f));
      m["underwater"] = std::move(d); }

    // golden — gold glow + fairy dust
    { TextEffectDef d; d.name = "golden";
      d.modifiers.push_back(makeMod(FXMod::Glow, G, 1.0f, 2.0f, 1.0f, 3.0f));
      d.modifiers.push_back(makeMod(FXMod::FairyDust, G, 1.0f, 2.0f, 1.0f, 2.0f, 2.0f));
      m["golden"] = std::move(d); }

    // toxic — green neon + bubbles + shake
    { TextEffectDef d; d.name = "toxic";
      d.modifiers.push_back(makeMod(FXMod::Neon, Gn, 2.0f));
      d.modifiers.push_back(makeMod(FXMod::Bubbles, Gn, 1.0f, 2.0f, 1.0f, 2.0f, 2.0f, 1.5f));
      d.modifiers.push_back(makeMod(FXMod::Shake, W, 1.5f, 0.8f));
      m["toxic"] = std::move(d); }

    // shadow — dark shadow + flicker
    { TextEffectDef d; d.name = "shadow";
      d.modifiers.push_back(makeMod(FXMod::Shadow, W, 1.0f, 3.0f));
      d.modifiers.push_back(makeMod(FXMod::Flicker, W, 0.8f));
      m["shadow"] = std::move(d); }

    // crystal — rainbow glow + fairy dust
    { TextEffectDef d; d.name = "crystal";
      d.modifiers.push_back(makeMod(FXMod::Rainbow, W, 0.5f, 2.0f, 0.5f));
      d.modifiers.push_back(makeMod(FXMod::Glow, W, 1.0f, 2.0f, 1.0f, 2.0f));
      d.modifiers.push_back(makeMod(FXMod::FairyDust, W, 1.0f, 2.0f, 1.0f, 2.0f, 2.0f));
      m["crystal"] = std::move(d); }

    // storm — shake + flicker + chromatic
    { TextEffectDef d; d.name = "storm";
      d.modifiers.push_back(makeMod(FXMod::Shake, W, 3.0f, 3.0f));
      d.modifiers.push_back(makeMod(FXMod::Flicker, W, 3.0f));
      d.modifiers.push_back(makeMod(FXMod::Chromatic, W, 2.0f, 2.0f));
      m["storm"] = std::move(d); }

    // ethereal — soft pulse + drift + fairy dust
    { TextEffectDef d; d.name = "ethereal";
      d.modifiers.push_back(makeMod(FXMod::Pulse, W, 0.4f));
      auto dr = makeMod(FXMod::Drift, W, 0.2f, 2.0f); dr.params.phase = 1.5708f;
      d.modifiers.push_back(dr);
      d.modifiers.push_back(makeMod(FXMod::FairyDust, ImVec4(0.8f,0.8f,1,1), 0.5f, 2.0f, 1.0f, 2.0f, 1.5f));
      m["ethereal"] = std::move(d); }

    // lava — fire gradient + glow + slow wave
    { TextEffectDef d; d.name = "lava";
      auto gm = makeMod(FXMod::Gradient, R); gm.params.color2 = ImVec4(1,0.8f,0,1);
      d.modifiers.push_back(gm);
      d.modifiers.push_back(makeMod(FXMod::Glow, O, 0.8f, 2.0f, 1.0f, 3.0f));
      d.modifiers.push_back(makeMod(FXMod::Wave, W, 0.3f, 2.0f, 0.3f));
      m["lava"] = std::move(d); }

    // frost — white/cyan gradient + snow + glow
    { TextEffectDef d; d.name = "frost";
      auto gm = makeMod(FXMod::Gradient, W); gm.params.color2 = C;
      d.modifiers.push_back(gm);
      d.modifiers.push_back(makeMod(FXMod::Snow, W, 1.0f, 2.0f, 1.0f, 2.0f, 3.0f, 2.0f));
      d.modifiers.push_back(makeMod(FXMod::Glow, ImVec4(0.5f,0.8f,1,1), 1.0f, 2.0f, 1.0f, 2.0f));
      m["frost"] = std::move(d); }

    // void — purple tint + pulse + jitter
    { TextEffectDef d; d.name = "void";
      d.modifiers.push_back(makeMod(FXMod::Tint, P));
      d.modifiers.push_back(makeMod(FXMod::Pulse, W, 1.5f));
      d.modifiers.push_back(makeMod(FXMod::Jitter, W, 2.0f, 1.5f));
      m["void"] = std::move(d); }

    // holy — gold glow (big) + fairy dust + gentle pulse
    { TextEffectDef d; d.name = "holy";
      d.modifiers.push_back(makeMod(FXMod::Glow, G, 0.8f, 2.0f, 1.0f, 4.0f));
      d.modifiers.push_back(makeMod(FXMod::FairyDust, G, 0.6f, 2.0f, 1.0f, 2.0f, 2.0f));
      d.modifiers.push_back(makeMod(FXMod::Pulse, W, 0.3f));
      m["holy"] = std::move(d); }

    // matrix — green typewriter + neon
    { TextEffectDef d; d.name = "matrix";
      d.modifiers.push_back(makeMod(FXMod::Typewriter, Gn, 3.0f));
      d.modifiers.push_back(makeMod(FXMod::Neon, Gn, 1.0f));
      m["matrix"] = std::move(d); }

    // blood — dark red dripping effect: drops cling to letters then fall with gravity
    { TextEffectDef d; d.name = "blood";
      auto drip = makeMod(FXMod::Drip, R, 0.8f, 2.0f, 1.0f, 2.0f, 2.5f, 3.0f);
      d.modifiers.push_back(drip);
      // Subtle slow pulse for a throbbing wet look
      d.modifiers.push_back(makeMod(FXMod::Pulse, W, 0.25f));
      m["blood"] = std::move(d); }

    // disco — fast rainbow + bounce + glow
    { TextEffectDef d; d.name = "disco";
      d.modifiers.push_back(makeMod(FXMod::Rainbow, W, 3.0f, 2.0f, 2.0f));
      d.modifiers.push_back(makeMod(FXMod::Bounce, W, 2.0f, 4.0f));
      d.modifiers.push_back(makeMod(FXMod::Glow, W, 2.0f, 2.0f, 1.0f, 2.0f));
      m["disco"] = std::move(d); }

    // glitch — chromatic + jitter + flicker
    { TextEffectDef d; d.name = "glitch";
      d.modifiers.push_back(makeMod(FXMod::Chromatic, W, 3.0f, 3.0f));
      d.modifiers.push_back(makeMod(FXMod::Jitter, W, 4.0f, 2.0f));
      d.modifiers.push_back(makeMod(FXMod::Flicker, W, 4.0f));
      m["glitch"] = std::move(d); }

    return m;
}

static const std::unordered_map<std::string, TextEffectDef> &presets()
{
    static auto s = initPresets();
    return s;
}

const TextEffectDef *getPreset(const std::string &name)
{
    auto &p = presets();
    auto it = p.find(name);
    return it != p.end() ? &it->second : nullptr;
}

std::vector<std::string> listPresets()
{
    std::vector<std::string> out;
    for (auto &kv : presets())
        out.push_back(kv.first);
    std::sort(out.begin(), out.end());
    return out;
}

// ════════════════════════════════════════════════════════════════════
//  Procedural generation
// ════════════════════════════════════════════════════════════════════

TextEffectDef generateProcedural(uint32_t seed)
{
    auto next = [&seed]() -> float
    {
        seed = seed * 1103515245u + 12345u;
        return (float)((seed >> 16) & 0x7FFF) / 32767.0f;
    };

    TextEffectDef def;
    def.name = "proc_" + std::to_string(seed);

    // Pick a base hue
    float hue = next();
    float r, g, b;
    ColorConvertHSVtoRGB(hue, 0.8f, 1.0f, r, g, b);
    ImVec4 baseCol(r, g, b, 1.0f);

    // Secondary color (complementary hue)
    float hue2 = fmodf(hue + 0.4f + next() * 0.2f, 1.0f);
    ColorConvertHSVtoRGB(hue2, 0.7f, 1.0f, r, g, b);
    ImVec4 col2(r, g, b, 1.0f);

    // Always pick 1 color modifier
    {
        FXMod colorMods[] = {FXMod::Rainbow, FXMod::Gradient, FXMod::ColorWave,
                             FXMod::Tint, FXMod::Neon};
        int idx = (int)(next() * 5);
        if (idx > 4) idx = 4;
        FXModifier m;
        m.type = colorMods[idx];
        m.params.color = baseCol;
        m.params.color2 = col2;
        m.params.speed = 0.5f + next() * 2.0f;
        m.params.frequency = 0.5f + next() * 2.0f;
        def.modifiers.push_back(m);
    }

    // 60% chance position modifier
    if (next() < 0.6f)
    {
        FXMod posMods[] = {FXMod::Shake, FXMod::Wave, FXMod::Bounce,
                           FXMod::Wobble, FXMod::Jitter, FXMod::Spiral};
        int idx = (int)(next() * 6);
        if (idx > 5) idx = 5;
        FXModifier m;
        m.type = posMods[idx];
        m.params.amplitude = 1.0f + next() * 4.0f;
        m.params.speed = 0.5f + next() * 3.0f;
        m.params.frequency = 0.5f + next() * 2.0f;
        def.modifiers.push_back(m);
    }

    // 40% chance alpha modifier
    if (next() < 0.4f)
    {
        FXMod alphaMods[] = {FXMod::Pulse, FXMod::FadeWave, FXMod::Flicker};
        int idx = (int)(next() * 3);
        if (idx > 2) idx = 2;
        FXModifier m;
        m.type = alphaMods[idx];
        m.params.speed = 0.5f + next() * 2.0f;
        m.params.frequency = 0.5f + next() * 2.0f;
        def.modifiers.push_back(m);
    }

    // 50% chance decoration
    if (next() < 0.5f)
    {
        float which = next();
        FXModifier m;
        if (which < 0.5f)
        {
            m.type = FXMod::Glow;
            m.params.color = baseCol;
            m.params.radius = 1.5f + next() * 3.0f;
        }
        else if (which < 0.75f)
        {
            m.type = FXMod::Shadow;
            m.params.amplitude = 2.0f + next() * 2.0f;
        }
        else
        {
            m.type = FXMod::Outline;
            m.params.color = baseCol;
            m.params.radius = 1.0f + next() * 2.0f;
        }
        def.modifiers.push_back(m);
    }

    // 30% chance particles
    if (next() < 0.3f)
    {
        FXMod partMods[] = {FXMod::FireEmit, FXMod::FairyDust, FXMod::Snow, FXMod::Bubbles};
        int idx = (int)(next() * 4);
        if (idx > 3) idx = 3;
        FXModifier m;
        m.type = partMods[idx];
        m.params.color = baseCol;
        m.params.density = 1.0f + next() * 5.0f;
        m.params.lifetime = 0.5f + next() * 1.5f;
        def.modifiers.push_back(m);
    }

    return def;
}

// ════════════════════════════════════════════════════════════════════
//  Tag → TextEffectDef builder
// ════════════════════════════════════════════════════════════════════

static ImVec4 parseHexColor(const std::string &hex)
{
    std::string h = hex;
    if (!h.empty() && h[0] == '#') h = h.substr(1);
    unsigned int val = 0;
    for (char c : h)
    {
        val <<= 4;
        if (c >= '0' && c <= '9') val |= c - '0';
        else if (c >= 'a' && c <= 'f') val |= 10 + c - 'a';
        else if (c >= 'A' && c <= 'F') val |= 10 + c - 'A';
    }
    if (h.size() <= 6)
    {
        float r = ((val >> 16) & 0xFF) / 255.0f;
        float g = ((val >> 8) & 0xFF) / 255.0f;
        float b = (val & 0xFF) / 255.0f;
        return ImVec4(r, g, b, 1.0f);
    }
    float r = ((val >> 24) & 0xFF) / 255.0f;
    float g = ((val >> 16) & 0xFF) / 255.0f;
    float b = ((val >> 8) & 0xFF) / 255.0f;
    float a = (val & 0xFF) / 255.0f;
    return ImVec4(r, g, b, a);
}

static float parseFloatSafe(const std::string &s)
{
    try { return std::stof(s); }
    catch (...) { return 0.0f; }
}

// Strip unit suffixes like "ms", "s", "px"
static float parseTimeSafe(const std::string &s)
{
    float v = parseFloatSafe(s);
    if (s.find("ms") != std::string::npos) v /= 1000.0f;
    return v;
}

TextEffectDef buildFromTag(
    const std::string &tagName,
    const std::vector<std::pair<std::string, std::string>> &attrs,
    const std::vector<std::string> &bareWords)
{
    TextEffectDef def;

    // Check for preset
    for (auto &kv : attrs)
    {
        if (kv.first == "preset")
        {
            auto *p = getPreset(kv.second);
            if (p)
            {
                def = *p;
                return def;
            }
        }
    }

    // Check for procedural seed
    for (auto &kv : attrs)
    {
        if (kv.first == "seed")
        {
            uint32_t seed = (uint32_t)std::strtoul(kv.second.c_str(), nullptr, 10);
            return generateProcedural(seed);
        }
    }

    // Build modifier list from tag name + bare words
    auto addModFromName = [&](const std::string &name)
    {
        FXMod mod = parseModName(name);
        if (mod != FXMod::Count)
        {
            FXModifier m;
            m.type = mod;
            def.modifiers.push_back(m);
        }
    };

    // If the tag itself is a known modifier, add it
    if (tagName != "effect")
        addModFromName(tagName);

    // Add bare words as modifier names
    for (auto &w : bareWords)
        addModFromName(w);

    // If still empty (e.g. <effect> with no modifiers), add rainbow as fallback
    if (def.modifiers.empty())
    {
        FXModifier m;
        m.type = FXMod::Rainbow;
        def.modifiers.push_back(m);
    }

    // Apply common attributes to ALL modifiers in the def
    for (auto &kv : attrs)
    {
        std::string key = kv.first;
        std::string val = kv.second;

        for (auto &mod : def.modifiers)
        {
            if (key == "speed")           mod.params.speed = parseFloatSafe(val);
            else if (key == "amplitude" || key == "amp" || key == "intensity")
                                          mod.params.amplitude = parseFloatSafe(val);
            else if (key == "frequency" || key == "freq")
                                          mod.params.frequency = parseFloatSafe(val);
            else if (key == "phase")      mod.params.phase = parseFloatSafe(val);
            else if (key == "color")      mod.params.color = parseHexColor(val);
            else if (key == "color2")     mod.params.color2 = parseHexColor(val);
            else if (key == "radius" || key == "rad")
                                          mod.params.radius = parseFloatSafe(val);
            else if (key == "density")    mod.params.density = parseFloatSafe(val);
            else if (key == "lifetime" || key == "life")
                                          mod.params.lifetime = parseFloatSafe(val);
            else if (key == "cycle")      mod.params.speed = 1.0f / parseTimeSafe(val);
            else if (key == "effect")
            {
                // <particle effect=fire> → override type
                FXMod eff = parseModName(val);
                if (eff != FXMod::Count)
                    mod.type = eff;
            }
        }
    }

    return def;
}

} // namespace TextEffectSystem
