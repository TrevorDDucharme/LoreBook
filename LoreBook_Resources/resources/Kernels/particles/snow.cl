// Snow/falling particle simulation kernel
// Particles drift down with gentle swaying motion

#include "common.cl"

__kernel void updateSnow(
    __global Particle* particles,
    __read_only image2d_t collision,
    const float deltaTime,
    const float2 gravity,      // Usually (0, 30) for gentle fall
    const float swayAmount,    // Horizontal sway amplitude
    const float swayFreq,      // Sway frequency
    const float time,          // Current time for sine wave
    const uint count
) {
    uint gid = get_global_id(0);
    if (gid >= count) return;
    
    Particle p = particles[gid];
    
    if (p.life <= 0.0f) return;
    
    uint rngState = gid ^ (uint)(time * 1000.0f);
    
    // Gentle gravity
    p.vel += gravity * deltaTime;
    
    // Swaying motion - each particle has different phase based on position
    float phase = p.pos.x * 0.02f + time * swayFreq + (float)gid * 0.1f;
    float sway = sin(phase) * swayAmount;
    p.vel.x += sway * deltaTime;
    
    // Terminal velocity (air resistance)
    p.vel.y = min(p.vel.y, 80.0f);
    p.vel.x = clamp(p.vel.x, -30.0f, 30.0f);
    
    // Update position
    float2 newPos = p.pos + p.vel * deltaTime;
    
    // Collision - snow sticks
    float collisionVal = sampleCollision(collision, collisionSampler, newPos);
    if (collisionVal > 0.5f) {
        // Hit text/obstacle - accumulate briefly then die
        p.vel = (float2)(0.0f, 0.0f);
        p.life -= deltaTime * 5.0f; // Die faster when stopped
        newPos = p.pos;
    }
    
    p.pos = newPos;
    
    // Slow life decay
    p.life -= deltaTime * 0.3f;
    
    // Slight size variation over time
    float sizeWobble = 1.0f + sin(time * 3.0f + (float)gid) * 0.1f;
    p.size *= sizeWobble;
    
    // Gentle rotation
    p.rotation.z += (p.vel.x * 0.01f + 0.5f) * deltaTime;
    
    // Fade alpha as life decreases
    p.color.w = smoothstep(0.0f, 0.3f, p.life / p.maxLife);
    
    particles[gid] = p;
}

__kernel void emitSnow(
    __global Particle* particles,
    __global uint* deadIndices,
    __global uint* deadCount,
    const float2 areaMin,      // Top-left of emission area
    const float2 areaMax,      // Bottom-right of emission area
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
    
    // Emit across the top of the area
    p.pos.x = randRange(&rngState, areaMin.x, areaMax.x);
    p.pos.y = areaMin.y - randRange(&rngState, 0.0f, 20.0f);
    
    // Gentle initial velocity
    p.vel = (float2)(
        randRange(&rngState, -10.0f, 10.0f),
        randRange(&rngState, 10.0f, 30.0f)
    );
    
    // Z-depth variation
    p.z = randRange(&rngState, -10.0f, 10.0f);
    p.zVel = 0.0f;
    
    // Snow is white with slight blue tint
    float brightness = randRange(&rngState, 0.9f, 1.0f);
    p.color = (float4)(brightness, brightness, 1.0f, 0.9f);
    
    p.size = randRange(&rngState, baseSize * 0.3f, baseSize * 1.0f);
    p.meshID = 0;
    p.rotation = (float3)(0.0f, 0.0f, randRange(&rngState, 0.0f, 2.0f * M_PI_F));
    p.rotVel = (float3)(0.0f, 0.0f, randRange(&rngState, -1.0f, 1.0f));
    p.behaviorID = 1; // snow behavior
    p.life = randRange(&rngState, baseLife * 0.8f, baseLife * 1.5f);
    p.maxLife = p.life;
    
    particles[particleIdx] = p;
}
