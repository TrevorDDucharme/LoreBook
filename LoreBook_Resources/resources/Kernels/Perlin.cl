// ------------------------------------------------------------
// Hash & noise utilities (seeded)
// ------------------------------------------------------------

inline uint hash_u(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

inline float hash3_seeded(int3 p, uint seed)
{
    uint h =
        hash_u((uint)p.x + seed) ^
        hash_u((uint)p.y + seed * 31u) ^
        hash_u((uint)p.z + seed * 131u);

    return (float)(h & 0x00FFFFFF) / (float)0x01000000;
}

inline float3 gradient(int3 p, uint seed)
{
    float h = hash3_seeded(p, seed) * 6.2831853f;
    return (float3)(cos(h), sin(h), cos(h * 0.5f));
}

inline float3 fade(float3 t)
{
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

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