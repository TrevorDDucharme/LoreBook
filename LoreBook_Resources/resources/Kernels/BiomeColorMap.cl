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
        // Exponential falloff blend with argmax fallback to avoid unclassified zones
        // Tune these for stronger/weaker falloff:
        const float sharpness = 20.0f;            // higher => sharper peaks
        const float minNormalizedForBlend = 0.05f; // if best biome < this, use argmax

        // Find max (best) value and best index
        float maxW = -INFINITY;
        int bestIdx = 0;
        for (int b = 0; b < biomeCount; ++b) {
            float v = masks[b * voxels + idx];
            if (v > maxW) {
                maxW = v;
                bestIdx = b;
            }
        }

        // If all zero or negative, fall back to argmax palette color
        if (maxW <= 0.0f) {
            float4 col = palette[bestIdx];
            col.w = 1.0f;
            outRGBA[idx] = col;
        } else {
            // Compute exponential weights relative to maxW
            float accX = 0.0f, accY = 0.0f, accZ = 0.0f, accW = 0.0f;
            float sumExp = 0.0f;
            for (int b = 0; b < biomeCount; ++b) {
                float w = masks[b * voxels + idx];
                // treat negatives as zero influence
                if (w <= 0.0f) continue;
                float wexp = exp(sharpness * (w - maxW)); // max -> exp(0) = 1.0
                float4 c = palette[b];
                accX += c.x * wexp;
                accY += c.y * wexp;
                accZ += c.z * wexp;
                accW += c.w * wexp;
                sumExp += wexp;
            }

            if (sumExp <= 0.0f) {
                // Defensive fallback (shouldn't happen)
                float4 col = palette[bestIdx];
                col.w = 1.0f;
                outRGBA[idx] = col;
            } else {
                // If the best biome's normalized weight is too small, use argmax to ensure classification
                // best normalized weight = 1.0 / sumExp (since best wexp is 1.0)
                if ((1.0f / sumExp) < minNormalizedForBlend) {
                    float4 col = palette[bestIdx];
                    col.w = 1.0f;
                    outRGBA[idx] = col;
                } else {
                    float4 col = (float4)(accX / sumExp, accY / sumExp, accZ / sumExp, accW / sumExp);
                    col.w = 1.0f;
                    outRGBA[idx] = col;
                }
            }
        }
    }
}