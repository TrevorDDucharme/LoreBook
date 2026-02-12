// Common particle definitions and utilities
// Shared across all particle kernels

// Behavior IDs — must match Effect::getBehaviorID() values
// 0 = no particle kernel (shader-only effects)
#define BEHAVIOR_NONE     0
#define BEHAVIOR_FIRE     1
#define BEHAVIOR_BLOOD    2
#define BEHAVIOR_SNOW     3
#define BEHAVIOR_SPARKLE  4
#define BEHAVIOR_ELECTRIC 5
#define BEHAVIOR_SMOKE    6
#define BEHAVIOR_MAGIC    7
#define BEHAVIOR_BUBBLES  8

// Must match C++ Particle struct in PreviewEffectSystem.hpp
// C++ uses explicit padding so that glm::vec3 (12 bytes) + 4 bytes pad
// matches OpenCL float3 (16 bytes with 16-byte alignment).
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
    float _pad0[2];      // align rotation to 16 bytes
    float3 rotation;     // euler angles
    float3 rotVel;       // angular velocity
    uint behaviorID;     // which kernel updates this particle
    float _pad1[3];      // pad struct to 112 bytes
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
// Normal points AWAY from solid regions (outward from collision surface)
float2 surfaceNormal(__read_only image2d_t collision, sampler_t sampler, float2 pos) {
    float step = 1.0f;
    float left = read_imagef(collision, sampler, pos + (float2)(-step, 0)).x;
    float right = read_imagef(collision, sampler, pos + (float2)(step, 0)).x;
    float up = read_imagef(collision, sampler, pos + (float2)(0, -step)).x;
    float down = read_imagef(collision, sampler, pos + (float2)(0, step)).x;
    
    // Gradient points from low to high (into solid), so negate to get
    // outward-facing normal for proper collision reflection
    float2 grad = (float2)(left - right, up - down);
    float len = length(grad);
    return len > 0.001f ? grad / len : (float2)(0, -1);
}

// Common sampler (nearest, clamp to edge)
__constant sampler_t collisionSampler = CLK_NORMALIZED_COORDS_FALSE | 
                                        CLK_ADDRESS_CLAMP_TO_EDGE | 
                                        CLK_FILTER_NEAREST;

// Convert document-space position to collision mask texel coordinates.
// The collision mask is rendered with an ortho projection that flips Y.
// Document Y = scrollY maps to texel row maskHeight-1 (top of texture),
// Document Y = scrollY + maskHeight maps to texel row 0 (bottom of texture).
float2 docToMask(float2 docPos, float scrollY, float maskHeight) {
    return (float2)(docPos.x, (scrollY + maskHeight - 1.0f) - docPos.y);
}
