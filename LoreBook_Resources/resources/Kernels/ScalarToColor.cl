inline float4 scalar_to_rgba_float4_util(float val,
                                         int colorCount,
                                         __global const float4* palette){
                                            
    // map [0,1] to indices [0, colorCount-1]
    float v = clamp(val, 0.0f, 1.0f);
    int range = (colorCount > 1) ? (colorCount - 1) : 0;
    float pos = v * (float)range;
    int i0 = (int)floor(pos);
    int i1 = i0 + 1;
    i0 = clamp(i0, 0, colorCount - 1);
    i1 = clamp(i1, 0, colorCount - 1);
    float t = pos - (float)i0;
    float4 col0 = palette[i0];
    float4 col1 = palette[i1];
    float4 col = col0 * (1.0f - t) + col1 * t;
    return col;
}

inline float4 weighed_scalar_to_rgba_float4_util(float val,
                                                  int colorCount,
                                                  __global const float4* palette,
                                                  __global const float* weights){
                                                    
    // map [0,1] to indices [0, colorCount-1]
    float v = clamp(val, 0.0f, 1.0f);
    int range = (colorCount > 1) ? (colorCount - 1) : 0;
    float pos = v * (float)range;
    int i0 = (int)floor(pos);
    int i1 = i0 + 1;
    i0 = clamp(i0, 0, colorCount - 1);
    i1 = clamp(i1, 0, colorCount - 1);
    float t = pos - (float)i0;
    float weight0 = weights[i0];
    float weight1 = weights[i1];
    float4 col0 = palette[i0];
    float4 col1 = palette[i1];
    float4 col = (col0 * weight0 * (1.0f - t) + col1 * weight1 * t) / (weight0 * (1.0f - t) + weight1 * t);
    return col;    
}

__kernel void scalar_to_rgba_float4(__global const float* scalar,
                                         int latitudeResolution, int longitudeResolution,
                                         int colorCount,
                                         __global const float4* palette,
                                         __global float4* outRGBA){
    int x = get_global_id(0);
    int y = get_global_id(1);
    if (x >= latitudeResolution || y >= longitudeResolution) return;
    int idx = x + y * latitudeResolution;
    float val = scalar[idx];
    outRGBA[idx] = scalar_to_rgba_float4_util(val, colorCount, palette);
}


__kernel void weighted_scalar_to_rgba_float4(__global const float* scalar,
                                         int latitudeResolution, int longitudeResolution,
                                         int colorCount,
                                         __global const float4* palette,
                                         __global const float* weights,
                                         __global float4* outRGBA){
    int x = get_global_id(0);
    int y = get_global_id(1);
    if (x >= latitudeResolution || y >= longitudeResolution) return;
    int idx = x + y * latitudeResolution;
    float val = scalar[idx];
    outRGBA[idx] = weighed_scalar_to_rgba_float4_util(val, colorCount, palette, weights);
}

