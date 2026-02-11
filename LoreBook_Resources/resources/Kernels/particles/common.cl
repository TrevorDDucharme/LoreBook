// Common particle definitions and utilities
// Shared across all particle kernels

// Must match C++ Particle struct in PreviewEffectSystem.hpp
typedef struct {
    float2 pos;          // 2D physics position
    float2 vel;          // 2D velocity
    float z;             // depth for 3D rendering
    float zVel;          // z velocity
    float life;          // remaining life (seconds)
    float maxLife;       // initial life (for ratio calc)
    float4 color;        // RGBA
    float size;          // quad size
    uint meshID;         // 0 = quad, else mesh index
    float3 rotation;     // euler angles
    float3 rotVel;       // angular velocity
    uint behaviorID;     // which kernel updates this particle
} Particle;

// Random number generation using xorshift
uint xorshift32(uint state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float randFloat(uint* state) {
    *state = xorshift32(*state);
    return (float)(*state) / (float)0xFFFFFFFF;
}

float randRange(uint* state, float minVal, float maxVal) {
    return minVal + randFloat(state) * (maxVal - minVal);
}

// Sample collision mask (R8 texture, 0 = empty, 255 = solid)
float sampleCollision(__read_only image2d_t collision, sampler_t sampler, float2 pos) {
    return read_imagef(collision, sampler, pos).x;
}

// Calculate surface normal from collision mask gradient
float2 surfaceNormal(__read_only image2d_t collision, sampler_t sampler, float2 pos) {
    float step = 1.0f;
    float left = read_imagef(collision, sampler, pos + (float2)(-step, 0)).x;
    float right = read_imagef(collision, sampler, pos + (float2)(step, 0)).x;
    float up = read_imagef(collision, sampler, pos + (float2)(0, -step)).x;
    float down = read_imagef(collision, sampler, pos + (float2)(0, step)).x;
    
    float2 grad = (float2)(right - left, down - up);
    float len = length(grad);
    return len > 0.001f ? grad / len : (float2)(0, -1);
}

// Common sampler (nearest, clamp to edge)
__constant sampler_t collisionSampler = CLK_NORMALIZED_COORDS_FALSE | 
                                        CLK_ADDRESS_CLAMP_TO_EDGE | 
                                        CLK_FILTER_NEAREST;
