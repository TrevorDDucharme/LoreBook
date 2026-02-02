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

__kernel void river_flow_source(
    __global const float* elevation,
    __global const float* water,
    int latitudeResolution,
    int longitudeResolution,
    __global float* flow,
    float minHeight,
    float maxHeight,
    float chance
)
{
    int x = get_global_id(0);
    int y = get_global_id(1);

    if (x >= longitudeResolution || y >= latitudeResolution)
        return;

    int idx = x + y * longitudeResolution;

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

inline int2 compute_flow_direction(
    int latitude,
    int longitude,
    __global const float* elevation,
    __global const float* water_table,
    int latitudeResolution,
    int longitudeResolution
) {
    if (latitude <= 0 || longitude <= 0 ||
        latitude >= latitudeResolution - 1 ||
        longitude >= longitudeResolution - 1) {
        return (int2)(latitude, longitude);
    }

    int idx = longitude * latitudeResolution + latitude;

    // Ocean is terminal
    if (water_table[idx] > 0.0f) {
        return (int2)(latitude, longitude);
    }

    float min_h = elevation[idx];
    int2 best = (int2)(latitude, longitude);

    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;

            int nx = latitude + dx;
            int ny = longitude + dy;
            int nidx = ny * latitudeResolution + nx;

            // Hard preference: drain into ocean
            if (water_table[nidx] > 0.0f) {
                return (int2)(nx, ny);
            }

            float nh = elevation[nidx];
            if (nh < min_h) {
                min_h = nh;
                best = (int2)(nx, ny);
            }
        }
    }

    return best;
}

__kernel void river_flow_accumulate(
    __global const float* elevation,
    __global const float* water,
    __global const float* flow_sources,
    int latitudeResolution,
    int longitudeResolution,
    __global const float* flow_in,
    __global float* flow_out,
    __global const float2* momentum_in,
    __global float2* momentum_out,
    const float river_threshold,
    const float slope_epsilon,
    const float height_epsilon,
    const float momentum_strength
) {
    int latitude  = get_global_id(0);
    int longitude = get_global_id(1);

    if (latitude <= 0 || longitude <= 0 ||
        latitude >= latitudeResolution - 1 ||
        longitude >= longitudeResolution - 1) {
        return;
    }

    int idx = longitude * latitudeResolution + latitude;

    // Ocean absorbs everything
    if (water[idx] > 0.0f) {
        flow_out[idx] = 0.0f;
        return;
    }

    // Start with incoming flow + local source
    float local_flow = flow_in[idx] + flow_sources[idx];

    // Get gravity-based flow direction (steepest descent)
    int2 gravity_dir = compute_flow_direction(
        latitude,
        longitude,
        elevation,
        water,
        latitudeResolution,
        longitudeResolution
    );

    // Compute gravity direction vector
    float2 gravity_vec = (float2)(
        (float)(gravity_dir.x - latitude),
        (float)(gravity_dir.y - longitude)
    );

    // Get previous momentum
    float2 prev_momentum = momentum_in[idx];

    // Blend momentum with gravity (momentum gives rivers inertia)
    float2 new_momentum = normalize(prev_momentum * momentum_strength + gravity_vec * (1.0f - momentum_strength));
    
    // Handle zero vector case
    if (length(new_momentum) < 0.001f) {
        new_momentum = gravity_vec;
    }
    
    // Store updated momentum
    momentum_out[idx] = new_momentum;

    // Convert momentum back to discrete direction
    int2 d = (int2)(
        latitude + (int)round(new_momentum.x),
        longitude + (int)round(new_momentum.y)
    );

    // Clamp to valid range
    d.x = clamp(d.x, 0, latitudeResolution - 1);
    d.y = clamp(d.y, 0, longitudeResolution - 1);

    int didx = d.y * latitudeResolution + d.x;

    // Push flow downstream (atomic for safety)
    if (didx != idx && water[didx] <= 0.0f) {
        atomic_add_float(&flow_out[didx], local_flow);
    }

    // Keep own value for river detection
    flow_out[idx] += local_flow;

    // River logic
    if (flow_out[idx] < river_threshold)
        return;

    float slope = elevation[idx] - elevation[didx];

    // Plateau spreading (braiding / floodplain)
    if (slope < slope_epsilon) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                int nx = latitude + dx;
                int ny = longitude + dy;
                int nidx = ny * latitudeResolution + nx;

                if (water[nidx] <= 0.0f &&
                    fabs(elevation[nidx] - elevation[idx]) < height_epsilon) {

                    atomic_add_float(&flow_out[nidx], local_flow * 0.25f);
                }
            }
        }
    }
}
