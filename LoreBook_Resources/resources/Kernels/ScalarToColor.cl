inline float4 scalar_to_rgba_float4_util(float val,
                                         int colorCount,
                                         __global const float4* palette){
    if (val <= 0.0f) {
        return palette[0];
    }
    if (colorCount <= 1) {
        return palette[0];
    }
    // map (0,1] to indices [1, colorCount-1]
    float v = clamp(val, 0.0f, 1.0f);
    int range = (colorCount > 2) ? (colorCount - 2) : 0;
    float pos = 1.0f + v * (float)range;
    int i0 = (int)floor(pos);
    int i1 = i0 + 1;
    i0 = clamp(i0, 1, colorCount - 1);
    i1 = clamp(i1, 1, colorCount - 1);
    float t = pos - (float)i0;
    float4 col0 = palette[i0];
    float4 col1 = palette[i1];
    return col0 * (1.0f - t) + col1 * t;
}

inline float4 weighed_scalar_to_rgba_float4_util(float val,
                                                  int colorCount,
                                                  __global const float4* palette,
                                                  __global const float* weights){
    // create dst from weights (background)
    float4 dst = (float4)(weights[0], weights[1], weights[2], weights[3]);
    if (val <= 0.0f) {
        return dst; // keep background when value is zero
    }
    if (colorCount <= 1) {
        return dst;
    }
    // map (0,1] to indices [1, colorCount-1]
    float v = clamp(val, 0.0f, 1.0f);
    int range = (colorCount > 2) ? (colorCount - 2) : 0;
    float pos = 1.0f + v * (float)range;
    int i0 = (int)floor(pos);
    int i1 = i0 + 1;
    i0 = clamp(i0, 1, colorCount - 1);
    i1 = clamp(i1, 1, colorCount - 1);
    float t = pos - (float)i0;
    float4 col0 = palette[i0];
    float4 col1 = palette[i1];
    float4 col = col0 * (1.0f - t) + col1 * t;
    // SRC over DST
    float srcA = clamp(col.w, 0.0f, 1.0f);
    float3 outRgb = col.xyz * srcA + dst.xyz * (1.0f - srcA);
    float outA = srcA + dst.w * (1.0f - srcA);
    return (float4)(outRgb.x, outRgb.y, outRgb.z, outA);
}

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
    outRGBA[idx] = scalar_to_rgba_float4_util(val, colorCount, palette);
}


__kernel void weighed_scalar_to_rgba_float4(__global const float* scalar,
                                         int W, int H, int D,
                                         int colorCount,
                                         __global const float4* palette,
                                         __global const float* weights,
                                         __global float4* outRGBA){
    int x = get_global_id(0);
    int y = get_global_id(1);
    int z = get_global_id(2);
    if (x >= W || y >= H || z >= D) return;
    int idx = x + y * W + z * W * H;
    float val = scalar[idx];
    outRGBA[idx] = weighed_scalar_to_rgba_float4_util(val, colorCount, palette, weights);
}

