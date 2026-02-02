inline float4 scalar_to_rgba_float4_util(float val,
                                         int colorCount,
                                         __global const float4* palette){
    if (colorCount <= 0) return (float4)(0.0f, 0.0f, 0.0f, 1.0f);
    if (val <= 0.0f) return palette[0];
    if (val >= 1.0f) return palette[colorCount - 1];
    float scaledValue = val * (colorCount - 1);
    int index = (int)scaledValue;
    float t = scaledValue - (float)index;
    float4 c1 = palette[index];
    float4 c2 = palette[index + 1];
    return (float4)(
        c1.x * (1.0f - t) + c2.x * t,
        c1.y * (1.0f - t) + c2.y * t,
        c1.z * (1.0f - t) + c2.z * t,
        c1.w * (1.0f - t) + c2.w * t
    );
}

inline float4 weighed_scalar_to_rgba_float4_util(float val,
                                                  int colorCount,
                                                  __global const float4* palette,
                                                  __global const float* weights){
    if (colorCount <= 0) return (float4)(0.0f, 0.0f, 0.0f, 1.0f);
    // Compute cumulative weights
    float totalWeight = 0.0f;
    for(int i = 0; i < colorCount; ++i){
        totalWeight += weights[i];
    }
    // Clamp val
    val = fmax(0.0f, fmin(1.0f, val));
    float scaledVal = val * totalWeight;
    // Find which segment val falls into
    int idx = 0;
    float cumulative = 0.0f;
    while(idx < colorCount && scaledVal > cumulative){
        cumulative += weights[idx];
        idx++;
    }
    if(idx == 0){
        return palette[0];
    } else if(idx >= colorCount){
        return palette[colorCount - 1];
    } else {
        float segmentStart = cumulative - weights[idx - 1];
        float segmentWeight = weights[idx - 1];
        float localT = (scaledVal - segmentStart) / segmentWeight;
        float4 c1 = palette[idx - 1];
        float4 c2 = palette[idx];
        return (float4)(
            c1.x * (1.0f - localT) + c2.x * localT,
            c1.y * (1.0f - localT) + c2.y * localT,
            c1.z * (1.0f - localT) + c2.z * localT,
            c1.w * (1.0f - localT) + c2.w * localT
        );
    }
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

