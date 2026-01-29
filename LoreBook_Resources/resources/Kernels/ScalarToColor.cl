__kernel void scalar_to_rgba_float4(__global const float* scalar,
                                         int W, int H, int D,
                                         int colorCount,
                                         __global const float4* palette,
                                         __global float4* outRGBA){
    int x = get_global_id(0);
    int y = get_global_id(1);
    int z = get_global_id(2);
    if (x >= W || y >= H || z >= D) return;
    int idx = x + y * W + z * W * H;
    float val = scalar[idx];
    // clamp input value and compute continuous position in palette
    float v = clamp(val, 0.0f, 1.0f);
    float pos = v * (colorCount - 1);
    int i0 = (int)floor(pos);
    int i1 = i0 + 1;
    i0 = clamp(i0, 0, colorCount - 1);
    i1 = clamp(i1, 0, colorCount - 1);
    float t = pos - (float)i0;
    float4 col0 = palette[i0];
    float4 col1 = palette[i1];
    float4 col = col0 * (1.0f - t) + col1 * t; // linear interpolation (ramp)
    // respect palette alpha and composite over existing destination color (SRC over DST)
    float srcA = clamp(col.w, 0.0f, 1.0f);
    float4 dst = outRGBA[idx];
    float3 outRgb = col.xyz * srcA + dst.xyz * (1.0f - srcA);
    float outA = srcA + dst.w * (1.0f - srcA);
    outRGBA[idx] = (float4)(outRgb.x, outRgb.y, outRgb.z, outA);
}

