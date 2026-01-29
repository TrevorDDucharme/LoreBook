// BiomeColorMap.cl
// Convert per-biome float masks into an RGBA float4 volume.
// mode: 0 = argmax (pick highest mask), 1 = blended (weighted average normalized by sum)
// Debug buffer layout (optional): int debugBuf[8]; [0]=x, [1]=y, [2]=z, [3]=as_int(value), [4]=written_flag, [5]=mode/info, [6]=extra, [7]=reserved

__kernel void biome_masks_to_rgba_float4_dbg(__global const float* masks,
                                         int W, int H, int D,
                                         int biomeCount,
                                         __global const float4* palette,
                                         int mode,
                                         __global float4* outRGBA,
                                         __global int* debugBuf)
{
    int x = get_global_id(0);
    int y = get_global_id(1);
    int z = get_global_id(2);
    if (x >= W || y >= H || z >= D) return;

    int idx = x + y * W + z * W * H;
    int voxels = W * H * D;
    int centerX = W / 2;
    int centerY = H / 2;
    int centerZ = D / 2;

    if (mode == 0) {
        // Argmax mapping
        float bestVal = -INFINITY;
        int bestIdx = 0;
        for (int b = 0; b < biomeCount; ++b) {
            float val = masks[b * voxels + idx];
            if (val > bestVal) {
                bestVal = val;
                bestIdx = b;
            }
        }
        float4 col = palette[bestIdx];
        col.w = 1.0f; // ensure opaque
        outRGBA[idx] = col;

        if (debugBuf != 0 && x == centerX && y == centerY && z == centerZ) {
            debugBuf[0] = x;
            debugBuf[1] = y;
            debugBuf[2] = z;
            debugBuf[3] = as_int(bestVal);
            debugBuf[4] = 1;
            debugBuf[5] = bestIdx;
        }
    } else {
        // Blended mapping: weighted average normalized by the sum of weights
        float4 acc = (float4)(0.0f, 0.0f, 0.0f, 0.0f);
        float sum = 0.0f;
        for (int b = 0; b < biomeCount; ++b) {
            float w = masks[b * voxels + idx];
            if (w <= 0.0f) continue;
            float4 c = palette[b];
            acc += c * w;
            sum += w;
        }
        if (sum <= 0.0f) {
            float4 col = palette[0];
            col.w = 1.0f;
            outRGBA[idx] = col;

            if (debugBuf != 0 && x == centerX && y == centerY && z == centerZ) {
                debugBuf[0] = x;
                debugBuf[1] = y;
                debugBuf[2] = z;
                debugBuf[3] = as_int(0.0f);
                debugBuf[4] = 1;
                debugBuf[5] = 0;
            }
        } else {
            float4 col = acc / sum;
            col.w = 1.0f;
            outRGBA[idx] = col;

            if (debugBuf != 0 && x == centerX && y == centerY && z == centerZ) {
                debugBuf[0] = x;
                debugBuf[1] = y;
                debugBuf[2] = z;
                debugBuf[3] = as_int(sum);
                debugBuf[4] = 1;
                debugBuf[5] = 0;
            }
        }
    }
}

// Backwards-compatible non-debug variant (keeps original signature for callers that don't set a debug arg)
__kernel void biome_masks_to_rgba_float4(__global const float* masks,
                                         int W, int H, int D,
                                         int biomeCount,
                                         __global const float4* palette,
                                         int mode,
                                         __global float4* outRGBA)
{
    int x = get_global_id(0);
    int y = get_global_id(1);
    int z = get_global_id(2);
    if (x >= W || y >= H || z >= D) return;

    int idx = x + y * W + z * W * H;
    int voxels = W * H * D;

    if (mode == 0) {
        // Argmax mapping
        float bestVal = -INFINITY;
        int bestIdx = 0;
        for (int b = 0; b < biomeCount; ++b) {
            float val = masks[b * voxels + idx];
            if (val > bestVal) {
                bestVal = val;
                bestIdx = b;
            }
        }
        float4 col = palette[bestIdx];
        col.w = 1.0f; // ensure opaque
        outRGBA[idx] = col;
    } else {
        // Blended mapping: weighted average normalized by the sum of weights
        float4 acc = (float4)(0.0f, 0.0f, 0.0f, 0.0f);
        float sum = 0.0f;
        for (int b = 0; b < biomeCount; ++b) {
            float w = masks[b * voxels + idx];
            if (w <= 0.0f) continue;
            float4 c = palette[b];
            acc += c * w;
            sum += w;
        }
        if (sum <= 0.0f) {
            float4 col = palette[0];
            col.w = 1.0f;
            outRGBA[idx] = col;
        } else {
            float4 col = acc / sum;
            col.w = 1.0f;
            outRGBA[idx] = col;
        }
    }
}
