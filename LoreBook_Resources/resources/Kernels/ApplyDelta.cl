/// Apply a delta grid to a procedurally generated buffer.
///
/// Supports two modes:
///   mode=0 (Add):  output = base + delta
///   mode=1 (Set):  output = (delta != 0) ? delta : base
///
/// NDRange = {resLat, resLon}

__kernel void apply_delta_scalar(
    __global float* base,          // in/out: generated data
    __global const float* delta,   // delta grid (same resolution)
    int resLat,
    int resLon,
    int mode)                      // 0=Add, 1=Set
{
    int latIdx = get_global_id(0);
    int lonIdx = get_global_id(1);

    if (latIdx >= resLat || lonIdx >= resLon)
        return;

    int idx = latIdx * resLon + lonIdx;
    float d = delta[idx];

    if (mode == 0) {
        // Add
        base[idx] += d;
    } else {
        // Set (only where delta is non-zero)
        if (d != 0.0f)
            base[idx] = d;
    }
}

/// RGBA variant: apply delta per-component.
__kernel void apply_delta_rgba(
    __global float4* base,
    __global const float4* delta,
    int resLat,
    int resLon,
    int mode)
{
    int latIdx = get_global_id(0);
    int lonIdx = get_global_id(1);

    if (latIdx >= resLat || lonIdx >= resLon)
        return;

    int idx = latIdx * resLon + lonIdx;
    float4 d = delta[idx];

    if (mode == 0) {
        base[idx] += d;
    } else {
        float4 b = base[idx];
        // Set component-wise where delta alpha > 0
        if (d.w > 0.0f)
            base[idx] = d;
    }
}

/// Multi-channel variant: channelCount channels per texel.
__kernel void apply_delta_channels(
    __global float* base,
    __global const float* delta,
    int resLat,
    int resLon,
    int channelCount,
    int mode)
{
    int latIdx = get_global_id(0);
    int lonIdx = get_global_id(1);

    if (latIdx >= resLat || lonIdx >= resLon)
        return;

    int baseIdx = (latIdx * resLon + lonIdx) * channelCount;

    for (int c = 0; c < channelCount; c++) {
        float d = delta[baseIdx + c];
        if (mode == 0) {
            base[baseIdx + c] += d;
        } else {
            if (d != 0.0f)
                base[baseIdx + c] = d;
        }
    }
}
