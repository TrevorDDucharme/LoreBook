#include "Util.cl"

inline void atomic_add_float(__global float* addr, float val) {
    __global volatile uint* iptr = (__global volatile uint*)addr;
    uint old = *iptr;
    while (1) {
        uint expected = old;
        float oldf = as_float(expected);
        float newf = oldf + val;
        uint desired = as_uint(newf);
        uint prev = atomic_cmpxchg(iptr, expected, desired);
        if (prev == expected) break;
        old = prev;
    }
}

inline float2 river_flow_dir(
    int x,
    int y,
    __global const float* elevation,
    __global const float* water,
    int longitude,
    int latitude
)
{

    if (x <= 0 || y <= 0 || x >= longitude-1 || y >= latitude-1)
    {
        return (float2)(0.0f, 0.0f);
    }

    int idx = x + y * longitude;

    // underwater → no river source
    if (water[idx] > 0.0f)
    {
        return (float2)(0.0f, 0.0f);
    }

    /*
        [-1,-1] [0,-1] [1,-1]
        [-1, 0] [0, 0] [1, 0]
        [-1, 1] [0, 1] [1, 1]
        multiplyd by elevation difference to get gradient
    */
    float2 dir = (float2)(0.0f, 0.0f);
    float h0 = elevation[idx];
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx;
            int ny = y + dy;
            int ni = nx + ny * longitude;
            float dh = h0 - elevation[ni];
            dir += (float2)(dx, dy) * dh;
        }
    }
    return dir;
}

__kernel void river_flow_source(
    __global const float* elevation,
    __global const float* water,
    int latitude,
    int longitude,
    __global float* flow,
    float minHeight,
    float maxHeight,
    float chance
)
{
    int x = get_global_id(0);
    int y = get_global_id(1);

    if (x >= longitude || y >= latitude)
        return;

    int idx = x + y * longitude;

    float h0 = elevation[idx];

    // if underwater, no flow source
    if (water[idx] > 0.0f ||
     elevation[idx] <= minHeight ||
     elevation[idx] >= maxHeight) {
        flow[idx] = 0.0f;
        return;
    }

    flow[idx] = rand_chance(12345, x, y, chance) ? 1.0f : 0.0f;
}

__kernel void river_flow_accumulate(
    __global const float* elevation,
    __global const float* water,
    __global const float* flow_sources,
    int latitude,
    int longitude,
    __global const float* flow_in,
    __global float* flow_out
)
{
    int x = get_global_id(0);
    int y = get_global_id(1);

    if (x >= longitude || y >= latitude)
        return;

    int idx = x + y * longitude;
    
    float2 flow_vec = river_flow_dir(x, y, elevation, water, longitude, latitude);
    float mag = length(flow_vec);
    
    float in_flow = flow_in[idx] + flow_sources[idx];
    
    /*
        Use magnitude to control spread:
        - High magnitude = tight/focused spread (steep terrain)
        - Low magnitude = broad spread (gentle terrain)
        - Zero magnitude = even distribution (flat terrain)
    */
    if (mag <= 0.0f)
    {
        // Completely flat - distribute evenly to all 8 neighbors (excluding center)
        float distributed = in_flow / 8.0f;
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                if (dx == 0 && dy == 0) continue;
                int nx = x + dx;
                int ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= longitude || ny >= latitude)
                    continue;
                int nidx = nx + ny * longitude;
                atomic_add_float(&flow_out[nidx], distributed);
            }
        }
        return;
    }
    
    float2 dir = normalize(flow_vec);
    
    // Use magnitude to control sharpness: higher magnitude = more focused flow
    // Exponential mapping for much sharper directional flow at high magnitudes
    float sharpness = exp(mag * 20.0f);
    
    float total_weight = 0.0f;
    float weights[3][3];
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) {
                weights[dy+1][dx+1] = 0.0f;
                continue;
            }
            float2 offset = (float2)(dx, dy);
            float proj = dot(normalize(offset), dir);
            float weight = fmax(proj, 0.0f);
            // Apply sharpness based on magnitude
            weight = pow(weight, sharpness);
            weights[dy+1][dx+1] = weight;
            total_weight += weight;
        }
    }
    
    // Distribute flow to neighbors
    if (total_weight > 0.0f) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                float weight = weights[dy+1][dx+1];
                if (weight <= 0.0f) continue;
                int nx = x + dx;
                int ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= longitude || ny >= latitude)
                    continue;
                int nidx = nx + ny * longitude;
                float distributed_flow = in_flow * (weight / total_weight);
                atomic_add_float(&flow_out[nidx], distributed_flow);
            }
        }
    }
}