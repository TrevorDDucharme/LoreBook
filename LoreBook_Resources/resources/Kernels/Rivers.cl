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

__kernel void river_flow_dir(
    __global const float* elevation,
    __global const float* water,
    __global int* flow_dir,
    int width,
    int height
)
{
    int x = get_global_id(0);
    int y = get_global_id(1);

    if (x <= 0 || y <= 0 || x >= width-1 || y >= height-1)
    {
        int idx = x + y * width;
        flow_dir[idx] = -1;
        return;
    }

    int idx = x + y * width;

    // underwater → no river source
    if (water[idx] > 0.0f)
    {
        flow_dir[idx] = -1;
        return;
    }

    float h0 = elevation[idx];
    float bestDrop = 0.0f;
    int best = -1;
    int d = 0;

    for (int dy = -1; dy <= 1; dy++)
    for (int dx = -1; dx <= 1; dx++, d++)
    {
        if (dx == 0 && dy == 0) continue;

        int ni = (x+dx) + (y+dy) * width;

        float drop = h0 - elevation[ni];
        if (drop > bestDrop)
        {
            bestDrop = drop;
            best = d;
        }
    }

    flow_dir[idx] = best;
}

__kernel void river_flow_init(
    __global const float* elevation,
    __global const float* water,
    int width,
    int height,
    __global float* flow,
    float slopeScale,
    float slopeNorm
)
{
    int x = get_global_id(0);
    int y = get_global_id(1);

    if (x >= width || y >= height)
        return;

    int idx = x + y * width;

    // if underwater, no flow source
    if (water[idx] > 0.0f) {
        flow[idx] = 0.0f;
        return;
    }

    float h0 = elevation[idx];
    // compute maximum downhill drop to neighbors as a simple slope metric
    float maxDrop = 0.0f;
    for (int dy = -1; dy <= 1; dy++) {
        for (int dx = -1; dx <= 1; dx++) {
            if (dx == 0 && dy == 0) continue;
            int nx = x + dx;
            int ny = y + dy;
            if (nx < 0 || ny < 0 || nx >= width || ny >= height) continue;
            int ni = nx + ny * width;
            float drop = h0 - elevation[ni];
            if (drop > maxDrop) maxDrop = drop;
        }
    }

    // slopeWeight: larger for flatter cells (small maxDrop), baseline 1.0
    float t = clamp(maxDrop / slopeNorm, 0.0f, 1.0f);
    float slopeWeight = 1.0f + slopeScale * (1.0f - t);
    flow[idx] = slopeWeight;
}

__kernel void river_flow_accumulate(
    __global const int* flow_dir,
    int width,
    int height,
    __global const float* flow_in,
    __global float* flow_out
)
{
    int x = get_global_id(0);
    int y = get_global_id(1);

    if (x >= width || y >= height)
        return;

    int idx = x + y * width;
    flow_out[idx] = flow_in[idx];

    int d = flow_dir[idx];
    if (d < 0) return;

    int dy = (d / 3) % 3 - 1;
    int dx = d % 3 - 1;

    int ni = (x+dx) + (y+dy) * width;

    atomic_add_float(&flow_out[ni], flow_in[idx]);
}

__kernel void river_volume(
    __global const float* flow,
    int width,
    int height,
    __global float* river,
    float flowThreshold,
    float widthScale,
    float flowMax
)
{
    int x = get_global_id(0);
    int y = get_global_id(1);

    if (x >= width || y >= height)
        return;

    int idx = x + y * width;

    float f = flow[idx];
    if (f <= 0.0f || f < flowThreshold || flowMax <= flowThreshold)
    {
        river[idx] = 0.0f;
        return;
    }

    // log-space normalization between threshold and max to avoid saturation
    float l = log(f);
    float lmin = log(flowThreshold);
    float lmax = log(flowMax);
    float rel = (lmax > lmin) ? clamp((l - lmin) / (lmax - lmin), 0.0f, 1.0f) : 1.0f;

    float w = clamp(rel * widthScale, 0.0f, 1.0f);

    river[idx] = w;
}
