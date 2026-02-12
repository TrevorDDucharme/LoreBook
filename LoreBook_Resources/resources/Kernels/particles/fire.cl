// Fire particle simulation kernel
// Particles rise with turbulence, fade from yellow to red to black

#include "common.cl"

__kernel void updateFire(
    __global Particle* particles,
    __read_only image2d_t collision,
    const float deltaTime,
    const float2 gravity,      // Usually (0, -80) for upward rise
    const float turbulence,    // Horizontal turbulence strength
    const float heatDecay,     // How fast particles cool (life drain)
    const float scrollY,       // Document scroll offset
    const float maskHeight,    // Collision mask height in pixels
    const uint count
) {
    uint gid = get_global_id(0);
    if (gid >= count) return;
    
    Particle p = particles[gid];
    
    // Skip dead particles and particles not belonging to this kernel
    if (p.life <= 0.0f) return;
    if (p.behaviorID != BEHAVIOR_FIRE) return;
    
    // Initialize random state from particle position and time
    uint rngState = (uint)(p.pos.x * 1000.0f) ^ (uint)(p.pos.y * 1000.0f) ^ gid;
    
    // Apply gravity (upward for fire)
    p.vel += gravity * deltaTime;
    
    // Add turbulent horizontal motion
    float noise = randRange(&rngState, -1.0f, 1.0f);
    p.vel.x += noise * turbulence * deltaTime;
    
    // Apply drag
    p.vel *= 0.98f;
    
    // Update position
    float2 newPos = p.pos + p.vel * deltaTime;
    
    // Two-point collision check: only bounce when entering solid from outside
    // Particles are emitted from within glyph bounds so they start inside —
    // must allow them to escape.
    float2 newMaskPos = docToMask(newPos, scrollY, maskHeight);
    float2 curMaskPos = docToMask(p.pos, scrollY, maskHeight);
    float newCol = sampleCollision(collision, collisionSampler, newMaskPos);
    float curCol = sampleCollision(collision, collisionSampler, curMaskPos);
    
    if (newCol > 0.5f && curCol <= 0.5f) {
        // Entering solid from outside — deflect along surface
        float2 maskNorm = surfaceNormal(collision, collisionSampler, newMaskPos);
        float2 docNorm = (float2)(maskNorm.x, -maskNorm.y);
        p.vel = reflect_f2(p.vel, docNorm) * 0.3f;
        newPos = p.pos; // Don't penetrate
    }
    // If both inside (just emitted) or both outside — allow movement
    
    p.pos = newPos;
    
    // Decay life
    p.life -= heatDecay * deltaTime;
    
    // Update color based on temperature (life ratio)
    float heat = p.life / p.maxLife;
    if (heat > 0.7f) {
        // Hot core: white-yellow
        float t = (heat - 0.7f) / 0.3f;
        p.color = (float4)(1.0f, 1.0f, t, 1.0f);
    } else if (heat > 0.4f) {
        // Middle: yellow to orange
        float t = (heat - 0.4f) / 0.3f;
        p.color = (float4)(1.0f, 0.5f + 0.5f * t, 0.0f, 1.0f);
    } else if (heat > 0.1f) {
        // Cool: orange to red
        float t = (heat - 0.1f) / 0.3f;
        p.color = (float4)(1.0f, 0.1f + 0.4f * t, 0.0f, 0.8f);
    } else {
        // Dying: red to black smoke
        float t = heat / 0.1f;
        p.color = (float4)(t * 0.5f, 0.0f, 0.0f, t * 0.5f);
    }
    
    // Size grows as heat decreases (smoke expansion)
    p.size = mix(p.size, p.size * 1.5f, (1.0f - heat) * 0.02f);
    
    // Spin
    p.rotation.z += randRange(&rngState, -2.0f, 2.0f) * deltaTime;
    
    particles[gid] = p;
}

__kernel void emitFire(
    __global Particle* particles,
    __global uint* deadIndices,
    __global uint* deadCount,
    const float2 emitPos,
    const float emitRadius,
    const uint emitCount,
    const float baseLife,
    const float baseSize,
    const uint seed
) {
    uint gid = get_global_id(0);
    if (gid >= emitCount) return;
    
    // Get a dead particle index to reuse
    uint idx = atomic_dec(deadCount) - 1;
    if (idx >= *deadCount + emitCount) return; // No more dead particles
    
    uint particleIdx = deadIndices[idx];
    uint rngState = seed ^ gid ^ particleIdx;
    
    Particle p;
    
    // Emit in a circle around emit position
    float angle = randRange(&rngState, 0.0f, 2.0f * M_PI_F);
    float radius = randRange(&rngState, 0.0f, emitRadius);
    p.pos = emitPos + (float2)(cos(angle), sin(angle)) * radius;
    
    // Initial velocity: mostly upward with some spread
    float speed = randRange(&rngState, 30.0f, 60.0f);
    float spread = randRange(&rngState, -0.3f, 0.3f);
    p.vel = (float2)(spread * speed, -speed); // -Y is up in screen coords
    
    // Z-depth (slight variation for visual interest)
    p.z = randRange(&rngState, -5.0f, 5.0f);
    p.zVel = 0.0f;
    
    // Fire starts white-hot
    p.color = (float4)(1.0f, 1.0f, 0.8f, 1.0f);
    p.size = randRange(&rngState, baseSize * 0.5f, baseSize * 1.5f);
    p.meshID = 0;
    p.rotation = (float3)(0.0f, 0.0f, randRange(&rngState, 0.0f, 2.0f * M_PI_F));
    p.rotVel = (float3)(0.0f, 0.0f, randRange(&rngState, -2.0f, 2.0f));
    p.behaviorID = 0; // fire behavior
    p.life = randRange(&rngState, baseLife * 0.8f, baseLife * 1.2f);
    p.maxLife = p.life;
    
    particles[particleIdx] = p;
}
