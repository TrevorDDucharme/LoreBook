// Sparkle/glitter particle simulation kernel
// Particles twinkle in place with brief bursts of brightness

#include "common.cl"

__kernel void updateSparkle(
    __global Particle* particles,
    __read_only image2d_t collision,
    const float deltaTime,
    const float twinkleSpeed,  // How fast sparkles blink
    const float driftSpeed,    // Gentle random drift
    const float time,
    const float scrollY,       // Document scroll offset
    const float maskHeight,    // Collision mask height in pixels
    const uint count
) {
    uint gid = get_global_id(0);
    if (gid >= count) return;
    
    Particle p = particles[gid];
    
    if (p.life <= 0.0f) return;
    if (p.behaviorID != BEHAVIOR_SPARKLE) return;
    
    uint rngState = gid ^ (uint)(time * 1000.0f);
    
    // Minimal drift
    p.vel.x += randRange(&rngState, -1.0f, 1.0f) * driftSpeed;
    p.vel.y += randRange(&rngState, -1.0f, 1.0f) * driftSpeed;
    p.vel *= 0.95f; // Strong drag keeps them in place
    
    p.pos += p.vel * deltaTime;
    
    // Life decay
    p.life -= deltaTime * 0.5f;
    
    // Twinkle effect - rapid brightness oscillation
    float twinklePhase = time * twinkleSpeed + (float)gid * 2.718f;
    float twinkle = pow(max(0.0f, sin(twinklePhase)), 8.0f); // Sharp peaks
    
    // Base brightness fades with life
    float baseBright = p.life / p.maxLife;
    float brightness = baseBright * (0.3f + twinkle * 0.7f);
    
    // Sparkles are white/golden
    p.color = (float4)(
        brightness,
        brightness * 0.95f,
        brightness * 0.7f,
        brightness
    );
    
    // Size pulses with twinkle
    p.size = mix(p.size * 0.8f, p.size * 1.5f, twinkle);
    
    // Rapid rotation
    p.rotation.z += twinkleSpeed * deltaTime * 2.0f;
    
    particles[gid] = p;
}

__kernel void emitSparkle(
    __global Particle* particles,
    __global uint* deadIndices,
    __global uint* deadCount,
    const float2 center,
    const float radius,
    const uint emitCount,
    const float baseLife,
    const float baseSize,
    const uint seed
) {
    uint gid = get_global_id(0);
    if (gid >= emitCount) return;
    
    uint idx = atomic_dec(deadCount) - 1;
    if (idx >= *deadCount + emitCount) return;
    
    uint particleIdx = deadIndices[idx];
    uint rngState = seed ^ gid ^ particleIdx;
    
    Particle p;
    
    // Emit in circle around center
    float angle = randRange(&rngState, 0.0f, 2.0f * M_PI_F);
    float r = sqrt(randFloat(&rngState)) * radius; // sqrt for uniform distribution
    p.pos = center + (float2)(cos(angle), sin(angle)) * r;
    
    // Nearly stationary
    p.vel = (float2)(
        randRange(&rngState, -5.0f, 5.0f),
        randRange(&rngState, -5.0f, 5.0f)
    );
    
    // Z-depth for sparkle layer
    p.z = randRange(&rngState, -2.0f, 2.0f);
    p.zVel = 0.0f;
    
    // Start bright
    p.color = (float4)(1.0f, 0.95f, 0.7f, 1.0f);
    p.size = randRange(&rngState, baseSize * 0.5f, baseSize * 1.2f);
    p.meshID = 0;
    p.rotation = (float3)(0.0f, 0.0f, randRange(&rngState, 0.0f, 2.0f * M_PI_F));
    p.rotVel = (float3)(0.0f, 0.0f, randRange(&rngState, -5.0f, 5.0f));
    p.behaviorID = 2; // sparkle behavior
    p.life = randRange(&rngState, baseLife * 0.5f, baseLife * 1.5f);
    p.maxLife = p.life;
    
    particles[particleIdx] = p;
}
