#include "Util.cl"

inline float perlin3(float3 p, uint seed)
{
    int3 i = convert_int3(floor(p));
    float3 f = p - floor(p);
    float3 u = fade(f);

    float n000 = dot(gradient(i, seed), f);
    float n100 = dot(gradient(i + (int3)(1,0,0), seed), f - (float3)(1,0,0));
    float n010 = dot(gradient(i + (int3)(0,1,0), seed), f - (float3)(0,1,0));
    float n110 = dot(gradient(i + (int3)(1,1,0), seed), f - (float3)(1,1,0));
    float n001 = dot(gradient(i + (int3)(0,0,1), seed), f - (float3)(0,0,1));
    float n101 = dot(gradient(i + (int3)(1,0,1), seed), f - (float3)(1,0,1));
    float n011 = dot(gradient(i + (int3)(0,1,1), seed), f - (float3)(0,1,1));
    float n111 = dot(gradient(i + (int3)(1,1,1), seed), f - (float3)(1,1,1));

    float nx00 = mix(n000, n100, u.x);
    float nx10 = mix(n010, n110, u.x);
    float nx01 = mix(n001, n101, u.x);
    float nx11 = mix(n011, n111, u.x);

    float nxy0 = mix(nx00, nx10, u.y);
    float nxy1 = mix(nx01, nx11, u.y);

    return mix(nxy0, nxy1, u.z);
}

inline float perlin_3d_util(
    int x,
    int y,
    int z,
    int width,
    int height,
    int depth,
    float frequency,
    float lacunarity,
    int octaves,
    float persistence,
    uint seed
)
{
    if (x >= width || y >= height || z >= depth)
        return 0.0f;

    int index = x + y * width + z * width * height;

    // Sample at voxel centers to avoid integer-lattice artifacts
    float3 p = ((float3)(x + 0.5f, y + 0.5f, z + 0.5f)) * frequency;

    float value = 0.0f;
    float amplitude = 1.0f;
    float maxAmp = 0.0f;

    for (int i = 0; i < octaves; i++)
    {
        value += perlin3(p, seed + i * 101u) * amplitude;
        maxAmp += amplitude;

        amplitude *= persistence;
        p *= lacunarity;
    }

    // Normalize to [-1,1] / [0,1]
    if (maxAmp <= 0.0f) {
        value = 0.0f;
    } else {
        value = value / maxAmp;
    }
    float outVal = value * 0.5f + 0.5f;
    return outVal;
}

inline float perlin_3d_channels_util(
    int x,
    int y,
    int z,
    int channel,
    int width,
    int height,
    int depth,
    int channels,
    __global const float* frequency,
    __global const float* lacunarity,
    __global const int* octaves,
    __global const float* persistence,
    __global const uint* seed
)
{
    if (x >= width || y >= height || z >= depth || channels <= 0)
        return 0.0f;

    float freq = frequency[channel];
    float lac = lacunarity[channel];
    int oct = octaves[channel];
    float pers = persistence[channel];
    uint s = seed[channel];
    return perlin_3d_util(
        x, y, z,
        width, height, depth,
        freq, lac, oct, pers, s
    );
}

inline float perlin_3d_sphere_sample_util(
    int lattitude,
    int longitude,
    int lattitude_resolution,
    int longitude_resolution,
    float frequency,
    float lacunarity,
    int octaves,
    float persistence,
    uint seed
){
    if (lattitude < 0 || lattitude >= lattitude_resolution ||
        longitude < 0 || longitude >= longitude_resolution)
    {
        return 0.0f;
    }

    float theta = ((float)lattitude + 0.5f) / (float)lattitude_resolution * 3.14159265f; // [0, pi]
    float phi = ((float)longitude + 0.5f) / (float)longitude_resolution * 2.0f * 3.14159265f; // [0, 2pi]

    float3 p = (float3)(
        sin(theta) * cos(phi),
        sin(theta) * sin(phi),
        cos(theta)
    ) * frequency;

    float value = 0.0f;
    float amplitude = 1.0f;
    float maxAmp = 0.0f;

    for (int i = 0; i < octaves; i++)
    {
        value += perlin3(p, seed + i * 101u) * amplitude;
        maxAmp += amplitude;

        amplitude *= persistence;
        p *= lacunarity;
    }

    // Normalize to [-1,1] / [0,1]
    if (maxAmp <= 0.0f) {
        value = 0.0f;
    } else {
        value = value / maxAmp;
    }
    float outVal = value * 0.5f + 0.5f;
    return outVal;
}

inline float perlin_3d_sphere_sample_channels_util(
    int lattitude,
    int longitude,
    int lattitude_resolution,
    int longitude_resolution,
    int channel,
    int channels,
    __global const float* frequency,
    __global const float* lacunarity,
    __global const int* octaves,
    __global const float* persistence,
    __global const uint* seed
){
    if (lattitude < 0 || lattitude >= lattitude_resolution ||
        longitude < 0 || longitude >= longitude_resolution ||
        channels <= 0)
    {
        return 0.0f;
    }

    float freq = frequency[channel];
    float lac = lacunarity[channel];
    int oct = octaves[channel];
    float pers = persistence[channel];
    uint s = seed[channel];
    return perlin_3d_sphere_sample_util(
        lattitude,
        longitude,
        lattitude_resolution,
        longitude_resolution,
        freq, lac, oct, pers, s
    );
}

// ------------------------------------------------------------
// FBM kernel
// ------------------------------------------------------------

__kernel void perlin_fbm_3d(
    __global float* output,
    int width,
    int height,
    int depth,
    float frequency,
    float lacunarity,
    int octaves,
    float persistence,
    uint seed
)
{
    int x = get_global_id(0);
    int y = get_global_id(1);
    int z = get_global_id(2);

    if (x >= width || y >= height || z >= depth)
        return;

    output[x + y * width + z * width * height] =
        perlin_3d_util(
            x, y, z,
            width, height, depth,
            frequency, lacunarity, octaves, persistence, seed
        );
}

__kernel void perlin_fbm_3d_channels(
    __global float* output,
    int width,
    int height,
    int depth,
    int channels,
    __global const float* frequency,
    __global const float* lacunarity,
    __global const int* octaves,
    __global const float* persistence,
    __global const uint* seed
)
{
    int x = get_global_id(0);
    int y = get_global_id(1);
    int z = get_global_id(2);

    if (x >= width || y >= height || z >= depth || channels <= 0)
        return;

    for (int c = 0; c < channels; c++) {
        int index = c * (width * height * depth) + x + y * width + z * width * height;
        output[index] =
            perlin_3d_channels_util(
                x, y, z, c,
                width, height, depth, channels,
                frequency, lacunarity, octaves, persistence, seed
            );
    }
}

__kernel void perlin_fbm_3d_sphere_sample(
    __global float* output,
    int lattitude_resolution,
    int longitude_resolution,
    float frequency,
    float lacunarity,
    int octaves,
    float persistence,
    uint seed
)
{
    int lattitude = get_global_id(0);
    int longitude = get_global_id(1);

    if (lattitude >= lattitude_resolution || longitude >= longitude_resolution)
        return;

    output[lattitude * longitude_resolution + longitude] =
        perlin_3d_sphere_sample_util(
            lattitude,
            longitude,
            lattitude_resolution,
            longitude_resolution,
            frequency, lacunarity, octaves, persistence, seed
        );
}

__kernel void perlin_fbm_3d_sphere_sample_channels(
    __global float* output,
    int lattitude_resolution,
    int longitude_resolution,
    int channels,
    __global const float* frequency,
    __global const float* lacunarity,
    __global const int* octaves,
    __global const float* persistence,
    __global const uint* seed
)
{
    int lattitude = get_global_id(0);
    int longitude = get_global_id(1);

    if (lattitude >= lattitude_resolution || longitude >= longitude_resolution || channels <= 0)
        return;

    for (int c = 0; c < channels; c++) {
        int index = c * (lattitude_resolution * longitude_resolution) + lattitude * longitude_resolution + longitude;
        output[index] =
            perlin_3d_sphere_sample_channels_util(
                lattitude,
                longitude,
                lattitude_resolution,
                longitude_resolution,
                c,
                channels,
                frequency, lacunarity, octaves, persistence, seed
            );
    }
}