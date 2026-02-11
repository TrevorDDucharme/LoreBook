// ── TextEffects.cl ──────────────────────────────────────────────────
// OpenCL kernels for the text effects overlay system.
// All kernels operate on a viewport-sized RGBA output image and
// optionally read from the alpha mask (R8 document-height texture)
// and the particle buffer.
// ────────────────────────────────────────────────────────────────────

// ── Particle struct (must match C++ TextParticle layout) ────────────
typedef struct {
    float posX, posY;       // document-space position
    float velX, velY;       // velocity (px/s)
    float life;             // remaining life (s)
    float maxLife;          // initial lifetime
    float r, g, b, a;      // current color
    int   effectIdx;        // index into effect params
    int   _pad;
} Particle;

// ── Effect parameters struct (matches CLEffectParams) ───────────────
typedef struct {
    int   type;             // TextEffectType enum
    float cycleSec;
    float intensity;
    float colorR, colorG, colorB, colorA;
    float radius;
    float lifetimeSec;
    float speed;
    float density;
    float boundsMinX, boundsMinY;
    float boundsMaxX, boundsMaxY;
    float _pad;
} EffectParams;

// ── Effect type constants ───────────────────────────────────────────
#define EFFECT_NONE    0
#define EFFECT_PULSE   1
#define EFFECT_GLOW    2
#define EFFECT_SHAKE   3
#define EFFECT_FIRE    4
#define EFFECT_SPARKLE 5
#define EFFECT_RAINBOW 6

// ── Simple hash-based PRNG ──────────────────────────────────────────
uint wang_hash(uint seed) {
    seed = (seed ^ 61u) ^ (seed >> 16u);
    seed *= 9u;
    seed = seed ^ (seed >> 4u);
    seed *= 0x27d4eb2du;
    seed = seed ^ (seed >> 15u);
    return seed;
}

float rand_float(uint seed) {
    return (float)(wang_hash(seed) & 0x00FFFFFFu) / (float)0x01000000u;
}

// ── HSV to RGB conversion ───────────────────────────────────────────
float3 hsv_to_rgb(float h, float s, float v) {
    float c = v * s;
    float x = c * (1.0f - fabs(fmod(h * 6.0f, 2.0f) - 1.0f));
    float m = v - c;
    float3 rgb;
    if      (h < 1.0f/6.0f) rgb = (float3)(c, x, 0);
    else if (h < 2.0f/6.0f) rgb = (float3)(x, c, 0);
    else if (h < 3.0f/6.0f) rgb = (float3)(0, c, x);
    else if (h < 4.0f/6.0f) rgb = (float3)(0, x, c);
    else if (h < 5.0f/6.0f) rgb = (float3)(x, 0, c);
    else                     rgb = (float3)(c, 0, x);
    return rgb + (float3)(m, m, m);
}

// ════════════════════════════════════════════════════════════════════
// Kernel: clear the output texture to transparent black
// ════════════════════════════════════════════════════════════════════
__kernel void clear_output(
    __write_only image2d_t output,
    int width, int height)
{
    int x = get_global_id(0);
    int y = get_global_id(1);
    if (x >= width || y >= height) return;
    write_imagef(output, (int2)(x, y), (float4)(0.0f, 0.0f, 0.0f, 0.0f));
}

// ════════════════════════════════════════════════════════════════════
// Kernel: spawn particles from the alpha mask
// ════════════════════════════════════════════════════════════════════
__kernel void particle_spawn(
    __read_only  image2d_t alphaMask,   // R8 alpha mask (document height)
    __global Particle *particles,
    __global int *particleCount,        // atomic counter
    __global const EffectParams *effects,
    int numEffects,
    int maxParticles,
    float time,
    float dt,
    int maskWidth, int maskHeight,
    __global uint *randSeeds)
{
    int gid = get_global_id(0);
    if (gid >= numEffects) return;

    EffectParams ep = effects[gid];

    // Only fire and sparkle effects spawn particles
    if (ep.type != EFFECT_FIRE && ep.type != EFFECT_SPARKLE) return;

    float spawnRate = ep.density * 50.0f; // base spawn rate per second
    if (ep.type == EFFECT_FIRE) spawnRate *= 2.0f;

    int toSpawn = (int)(spawnRate * dt);
    if (toSpawn < 1) {
        // Probabilistic spawn for low rates
        uint seed = wang_hash((uint)(time * 1000.0f) + (uint)gid * 7919u);
        if (rand_float(seed) < spawnRate * dt) toSpawn = 1;
        else return;
    }

    // Clamp spawn count
    if (toSpawn > 32) toSpawn = 32;

    sampler_t samp = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;

    for (int s = 0; s < toSpawn; s++) {
        uint seed = wang_hash((uint)(time * 10000.0f) + (uint)gid * 31337u + (uint)s * 997u);

        // Random position within effect bounds
        float rx = rand_float(seed);
        seed = wang_hash(seed);
        float ry = rand_float(seed);
        seed = wang_hash(seed);

        float px = ep.boundsMinX + rx * (ep.boundsMaxX - ep.boundsMinX);
        float py = ep.boundsMinY + ry * (ep.boundsMaxY - ep.boundsMinY);

        // Sample alpha mask at this position to check if text is present
        int mx = (int)px;
        int my = (int)py;
        if (mx < 0 || mx >= maskWidth || my < 0 || my >= maskHeight) continue;

        float4 maskVal = read_imagef(alphaMask, samp, (int2)(mx, my));
        float alpha = maskVal.x; // R channel holds alpha

        // Use alpha as spawn probability — particles more likely on text
        float spawnProb = alpha * 0.8f + 0.2f; // still some particles outside text for ambient feel
        if (ep.type == EFFECT_FIRE) spawnProb = alpha; // fire only from text
        
        seed = wang_hash(seed);
        if (rand_float(seed) > spawnProb) continue;

        // Atomically reserve a slot
        int idx = atomic_add(particleCount, 1);
        if (idx >= maxParticles) {
            atomic_sub(particleCount, 1);
            return;
        }

        Particle p;
        p.posX = px;
        p.posY = py;
        p.effectIdx = gid;
        p.maxLife = ep.lifetimeSec;
        p.life = ep.lifetimeSec;

        seed = wang_hash(seed);
        float vx = (rand_float(seed) - 0.5f) * 20.0f;
        seed = wang_hash(seed);

        if (ep.type == EFFECT_FIRE) {
            // Fire: drift upward
            p.velX = vx;
            p.velY = -40.0f - rand_float(seed) * 30.0f; // negative Y = upward
            p.r = 1.0f; p.g = 0.9f; p.b = 0.2f; p.a = 1.0f;
        } else {
            // Sparkle: random direction, short life
            p.velX = vx * 0.5f;
            seed = wang_hash(seed);
            p.velY = (rand_float(seed) - 0.5f) * 10.0f;
            p.r = 1.0f; p.g = 1.0f; p.b = 1.0f; p.a = 1.0f;
            p.maxLife = 0.2f + rand_float(wang_hash(seed)) * 0.3f;
            p.life = p.maxLife;
        }

        p._pad = 0;
        particles[idx] = p;
    }
}

// ════════════════════════════════════════════════════════════════════
// Kernel: step particles (advance position, age, recolor, kill dead)
// ════════════════════════════════════════════════════════════════════
__kernel void particle_step(
    __global Particle *particles,
    __global int *particleCount,
    int maxParticles,
    float dt)
{
    int gid = get_global_id(0);
    int count = *particleCount;
    if (gid >= count) return;

    Particle p = particles[gid];
    if (p.life <= 0.0f) return; // already dead

    // Advance position
    p.posX += p.velX * dt;
    p.posY += p.velY * dt;
    p.life -= dt;

    if (p.life <= 0.0f) {
        p.a = 0.0f;
    } else {
        float t = 1.0f - (p.life / p.maxLife); // 0→1 over lifetime
        // Fire color transition: yellow → orange → red → transparent
        if (p.r > 0.5f && p.g > 0.3f) { // looks like fire particle
            p.r = 1.0f;
            p.g = max(0.0f, 0.9f - t * 1.2f);
            p.b = max(0.0f, 0.2f - t * 0.5f);
            p.a = 1.0f - t * t; // quadratic fadeout
        } else {
            // Sparkle: simple fadeout
            p.a = 1.0f - t;
        }
    }

    particles[gid] = p;
}

// ════════════════════════════════════════════════════════════════════
// Kernel: render all effects into the output texture
//   Each work item = one pixel in the viewport-sized output.
// ════════════════════════════════════════════════════════════════════
__kernel void render_effects(
    __read_only  image2d_t alphaMask,     // R8 document-space alpha mask
    __write_only image2d_t output,        // RGBA8 viewport-sized output
    __global const Particle *particles,
    __global const int *particleCount,
    __global const EffectParams *effects,
    int numEffects,
    int outWidth, int outHeight,
    int maskWidth, int maskHeight,
    float scrollY,
    float time)
{
    int px = get_global_id(0);
    int py = get_global_id(1);
    if (px >= outWidth || py >= outHeight) return;

    // Map viewport pixel to document space
    float docX = (float)px;
    float docY = (float)py + scrollY;

    sampler_t samp = CLK_NORMALIZED_COORDS_FALSE | CLK_ADDRESS_CLAMP_TO_EDGE | CLK_FILTER_NEAREST;

    float4 result = (float4)(0.0f, 0.0f, 0.0f, 0.0f);

    // ── Process non-particle effects ──
    for (int i = 0; i < numEffects; i++) {
        EffectParams ep = effects[i];

        // Check if this pixel is within the effect region bounds
        if (docX < ep.boundsMinX || docX > ep.boundsMaxX ||
            docY < ep.boundsMinY || docY > ep.boundsMaxY)
            continue;

        // Read alpha mask at document position
        int mx = (int)docX;
        int my = (int)docY;
        float alpha = 0.0f;
        if (mx >= 0 && mx < maskWidth && my >= 0 && my < maskHeight) {
            float4 maskVal = read_imagef(alphaMask, samp, (int2)(mx, my));
            alpha = maskVal.x;
        }

        if (ep.type == EFFECT_PULSE && alpha > 0.01f) {
            float pulse = 0.5f + 0.5f * sin(2.0f * 3.14159f * time / ep.cycleSec);
            float pa = alpha * pulse * 0.6f;
            // White-ish pulse overlay
            result += (float4)(1.0f, 1.0f, 1.0f, pa);
        }
        else if (ep.type == EFFECT_GLOW) {
            // Sample alpha at neighboring offsets
            float glowAccum = 0.0f;
            int rad = (int)ceil(ep.radius);
            for (int dy = -rad; dy <= rad; dy++) {
                for (int dx = -rad; dx <= rad; dx++) {
                    if (dx == 0 && dy == 0) continue;
                    float dist = sqrt((float)(dx*dx + dy*dy));
                    if (dist > ep.radius) continue;
                    int sx = mx + dx;
                    int sy = my + dy;
                    if (sx >= 0 && sx < maskWidth && sy >= 0 && sy < maskHeight) {
                        float4 sv = read_imagef(alphaMask, samp, (int2)(sx, sy));
                        float falloff = 1.0f - (dist / ep.radius);
                        glowAccum += sv.x * falloff;
                    }
                }
            }
            if (glowAccum > 0.0f) {
                float ga = min(1.0f, glowAccum * 0.15f);
                result += (float4)(ep.colorR, ep.colorG, ep.colorB, ga);
            }
        }
        else if (ep.type == EFFECT_RAINBOW && alpha > 0.01f) {
            float hue = fmod(docX * 0.01f + time * ep.speed, 1.0f);
            float3 rgb = hsv_to_rgb(hue, 1.0f, 1.0f);
            // Blend rainbow color onto text
            result += (float4)(rgb.x, rgb.y, rgb.z, alpha * 0.7f);
        }
    }

    // ── Accumulate particle contributions ──
    int pCount = *particleCount;
    for (int i = 0; i < pCount; i++) {
        Particle p = particles[i];
        if (p.life <= 0.0f || p.a <= 0.0f) continue;

        // Distance from this pixel (in document space) to particle
        float dx = docX - p.posX;
        float dy = docY - p.posY;
        float dist2 = dx * dx + dy * dy;

        // Particle radius (visual) — 3px for fire, 2px for sparkle
        float pRadius = 3.0f;
        float pRadius2 = pRadius * pRadius;

        if (dist2 < pRadius2) {
            float falloff = 1.0f - sqrt(dist2) / pRadius;
            falloff = falloff * falloff; // quadratic
            float4 pc = (float4)(p.r, p.g, p.b, p.a * falloff);
            // Additive blend
            result.x += pc.x * pc.w;
            result.y += pc.y * pc.w;
            result.z += pc.z * pc.w;
            result.w = max(result.w, pc.w);
        }
    }

    // Clamp result
    result = clamp(result, (float4)(0.0f), (float4)(1.0f));
    write_imagef(output, (int2)(px, py), result);
}
