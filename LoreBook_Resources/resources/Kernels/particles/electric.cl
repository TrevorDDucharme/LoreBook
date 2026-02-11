// Electric/lightning particle simulation kernel
// Particles jump erratically with bright flashes

#include "common.cl"

__kernel void updateElectric(
    __global Particle* particles,
    __read_only image2d_t collision,
    const float deltaTime,
    const float jumpChance,    // Probability of teleport per frame
    const float jumpDistance,  // Max teleport distance  
    const float arcAttraction, // Attraction toward collision surfaces
    const float time,
    const uint count
) {
    uint gid = get_global_id(0);
    if (gid >= count) return;
    
    Particle p = particles[gid];
    
    if (p.life <= 0.0f) return;
    
    uint rngState = gid ^ (uint)(time * 10000.0f) ^ (uint)(p.pos.x);
    
    // Random chance to "jump" (teleport)
    if (randFloat(&rngState) < jumpChance * deltaTime) {
        float angle = randRange(&rngState, 0.0f, 2.0f * M_PI_F);
        float dist = randRange(&rngState, jumpDistance * 0.3f, jumpDistance);
        p.pos += (float2)(cos(angle), sin(angle)) * dist;
        
        // Flash bright on jump
        p.color = (float4)(1.0f, 1.0f, 1.0f, 1.0f);
        p.size *= 2.0f;
    } else {
        // Normal movement - attracted to surfaces
        float nearDist = 50.0f;
        float2 closest = p.pos;
        float minAlpha = 1.0f;
        
        // Sample nearby collision to find surfaces to arc toward
        for (int i = 0; i < 8; i++) {
            float angle = (float)i * M_PI_F / 4.0f;
            float2 samplePos = p.pos + (float2)(cos(angle), sin(angle)) * nearDist;
            float alpha = sampleCollision(collision, collisionSampler, samplePos);
            if (alpha > 0.1f && alpha < minAlpha) {
                minAlpha = alpha;
                closest = samplePos;
            }
        }
        
        // Arc toward surface
        if (minAlpha < 1.0f) {
            float2 toSurface = normalize(closest - p.pos);
            p.vel += toSurface * arcAttraction * deltaTime;
        }
        
        // Apply velocity with high damping
        p.vel *= 0.8f;
        p.pos += p.vel * deltaTime;
        
        // Return to blue-white after flash
        float baseBright = 0.6f + 0.4f * sin(time * 50.0f + (float)gid);
        p.color = (float4)(0.5f * baseBright, 0.7f * baseBright, baseBright, baseBright);
        
        // Size returns to normal
        p.size = mix(p.size, p.size * 0.7f, 10.0f * deltaTime);
    }
    
    // Rapid life decay
    p.life -= deltaTime;
    
    // Rapid chaotic rotation
    p.rotation.z += randRange(&rngState, -20.0f, 20.0f) * deltaTime;
    
    particles[gid] = p;
}

__kernel void emitElectric(
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
    
    // Emit near source
    float angle = randRange(&rngState, 0.0f, 2.0f * M_PI_F);
    float radius = randRange(&rngState, 0.0f, emitRadius);
    p.pos = emitPos + (float2)(cos(angle), sin(angle)) * radius;
    
    // High initial velocity in random direction
    float speed = randRange(&rngState, 100.0f, 200.0f);
    float dir = randRange(&rngState, 0.0f, 2.0f * M_PI_F);
    p.vel = (float2)(cos(dir), sin(dir)) * speed;
    
    // Z-depth for electric arcs
    p.z = randRange(&rngState, -3.0f, 3.0f);
    p.zVel = randRange(&rngState, -10.0f, 10.0f);
    
    // Bright blue-white
    p.color = (float4)(0.7f, 0.8f, 1.0f, 1.0f);
    p.size = randRange(&rngState, baseSize * 0.3f, baseSize * 0.8f);
    p.meshID = 0;
    p.rotation = (float3)(0.0f, 0.0f, randRange(&rngState, 0.0f, 2.0f * M_PI_F));
    p.rotVel = (float3)(0.0f, 0.0f, randRange(&rngState, -20.0f, 20.0f));
    p.behaviorID = 4; // electric behavior
    p.life = randRange(&rngState, baseLife * 0.3f, baseLife * 0.8f); // Short lived
    p.maxLife = p.life;
    
    particles[particleIdx] = p;
}
