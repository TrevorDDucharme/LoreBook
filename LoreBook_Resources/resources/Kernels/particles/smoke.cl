// Smoke particle simulation kernel  
// Particles rise and expand, becoming more transparent

#include "common.cl"

__kernel void updateSmoke(
    __global Particle* particles,
    __read_only image2d_t collision,
    const float deltaTime,
    const float2 wind,         // Wind direction and strength
    const float riseSpeed,     // Upward drift speed
    const float expansion,     // Rate of size increase
    const float dissipation,   // How fast smoke fades
    const uint count
) {
    uint gid = get_global_id(0);
    if (gid >= count) return;
    
    Particle p = particles[gid];
    
    if (p.life <= 0.0f) return;
    
    uint rngState = gid ^ (uint)(p.pos.x * 100.0f);
    
    // Buoyancy - smoke rises
    p.vel.y -= riseSpeed * deltaTime;
    
    // Wind influence increases with altitude
    float windInfluence = 1.0f - (p.life / p.maxLife); // Older smoke = more wind
    p.vel += wind * windInfluence * deltaTime;
    
    // Turbulent motion
    p.vel.x += randRange(&rngState, -10.0f, 10.0f) * deltaTime;
    p.vel.y += randRange(&rngState, -5.0f, 5.0f) * deltaTime;
    
    // Strong air resistance
    p.vel *= 0.96f;
    
    // Update position
    float2 newPos = p.pos + p.vel * deltaTime;
    
    // Collision - smoke wraps around obstacles
    float collisionVal = sampleCollision(collision, collisionSampler, newPos);
    if (collisionVal > 0.5f) {
        float2 normal = surfaceNormal(collision, collisionSampler, newPos);
        // Deflect along surface
        p.vel = p.vel - normal * dot(p.vel, normal) * 1.5f;
        newPos = p.pos + p.vel * deltaTime;
    }
    
    p.pos = newPos;
    
    // Life decay
    p.life -= dissipation * deltaTime;
    
    // Expand over time
    p.size += expansion * deltaTime;
    
    // Color fades from dark grey to transparent
    float age = 1.0f - (p.life / p.maxLife);
    float brightness = 0.2f + age * 0.1f; // Gets slightly lighter
    float alpha = (p.life / p.maxLife) * 0.6f; // Fades out
    p.color = (float4)(brightness, brightness, brightness, alpha);
    
    // Slow rotation
    p.rotation.z += randRange(&rngState, -0.5f, 0.5f) * deltaTime;
    
    particles[gid] = p;
}

__kernel void emitSmoke(
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
    
    uint idx = atomic_dec(deadCount) - 1;
    if (idx >= *deadCount + emitCount) return;
    
    uint particleIdx = deadIndices[idx];
    uint rngState = seed ^ gid ^ particleIdx;
    
    Particle p;
    
    // Emit at base with small spread
    float angle = randRange(&rngState, 0.0f, 2.0f * M_PI_F);
    float radius = randRange(&rngState, 0.0f, emitRadius);
    p.pos = emitPos + (float2)(cos(angle), sin(angle)) * radius;
    
    // Initial upward velocity with spread
    p.vel = (float2)(
        randRange(&rngState, -15.0f, 15.0f),
        randRange(&rngState, -30.0f, -10.0f) // Upward
    );
    
    // Z-depth for smoke layer
    p.z = randRange(&rngState, -15.0f, -5.0f); // Behind text
    p.zVel = randRange(&rngState, -2.0f, 2.0f);
    
    // Dark grey smoke
    float grey = randRange(&rngState, 0.15f, 0.25f);
    p.color = (float4)(grey, grey, grey, 0.5f);
    
    p.size = randRange(&rngState, baseSize * 0.8f, baseSize * 1.2f);
    p.meshID = 0;
    p.rotation = (float3)(0.0f, 0.0f, randRange(&rngState, 0.0f, 2.0f * M_PI_F));
    p.rotVel = (float3)(0.0f, 0.0f, randRange(&rngState, -0.5f, 0.5f));
    p.behaviorID = 3; // smoke behavior
    p.life = randRange(&rngState, baseLife * 0.7f, baseLife * 1.3f);
    p.maxLife = p.life;
    
    particles[particleIdx] = p;
}
