__kernel void scalar_to_rgba_float4(__global const float* scalar,
                                         int W, int H, int D,
                                         int colorCount,
                                         __global const float4* palette,
                                         __global float4* outRGBA,
                                         __global int* debugBuf){
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

    int centerX = W / 2;
    int centerY = H / 2;
    int centerZ = D / 2;
    if (debugBuf != 0 && x == centerX && y == centerY && z == centerZ) {
        debugBuf[0] = x;
        debugBuf[1] = y;
        debugBuf[2] = z;
        debugBuf[3] = as_int(val);
        debugBuf[4] = 1;
        debugBuf[5] = i0;
        debugBuf[6] = (int)(srcA * 255.0f);
    }
}

