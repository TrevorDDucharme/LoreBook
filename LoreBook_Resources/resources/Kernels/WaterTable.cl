
float water_table_util(
    int latitude,
    int longitude,
    int latitude_resolution,
    int longitude_resolution,
    float* elevation,
    float water_table_level
)
{
    int index = latitude + longitude * latitude_resolution;
    float elev = elevation[index];
    //Simple model: water table is at a fixed level
    if (elev < water_table_level)
        //return the depth below the water table, normalized
        return (water_table_level - elev) / water_table_level;
    else
        return 0.0f; //above water
}

inline float4 water_table_weighed_scalar_to_rgba_float4_util(float val,
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

__kernel void water_table(
    __global float* output,
    __global float* elevation,
    int latitudeResolution,
    int longitudeResolution,
    float water_table_level
)
{
    int latitude = get_global_id(0);
    int longitude = get_global_id(1);

    if (latitude >= latitudeResolution || longitude >= longitudeResolution)
        return;

    output[latitude + longitude * latitudeResolution] =
        water_table_util(
            latitude, longitude,
            latitudeResolution, longitudeResolution,
            elevation,
            water_table_level
        );
}

__kernel void water_table_weighted_scalar_to_rgba_float4(__global const float* scalar,
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
    // call the local water-table specific helper (was referencing an unresolved external)
    outRGBA[idx] = water_table_weighed_scalar_to_rgba_float4_util(val, colorCount, palette, weights);
}