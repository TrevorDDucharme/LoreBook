#include "Util.cl"

/// Region-bounded sphere-surface Perlin FBM.
/// Generates noise for a sub-region of the sphere defined by
/// [thetaMin, thetaMax] × [phiMin, phiMax] in colatitude/azimuth.
///
/// Colatitude theta ∈ [0, π]:  0 = north pole, π = south pole.
/// Azimuth    phi   ∈ [0, 2π]: maps to longitude.
///
/// The output buffer is row-major: output[latIdx * resLon + lonIdx].
/// NDRange = {resLat, resLon}.

inline float perlin3_region(float3 p, uint seed)
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

__kernel void perlin_fbm_3d_sphere_region(
    __global float* output,
    int resLat,           // vertical samples (rows)
    int resLon,           // horizontal samples (columns)
    float thetaMin,       // colatitude start (radians)
    float thetaMax,       // colatitude end   (radians)
    float phiMin,         // azimuth start    (radians)
    float phiMax,         // azimuth end      (radians)
    float frequency,
    float lacunarity,
    int   octaves,
    float persistence,
    uint  seed)
{
    int latIdx = get_global_id(0);
    int lonIdx = get_global_id(1);

    if (latIdx >= resLat || lonIdx >= resLon)
        return;

    // Map work-item index to theta/phi within the region
    float theta = thetaMin + ((float)latIdx + 0.5f) / (float)resLat * (thetaMax - thetaMin);
    float phi   = phiMin   + ((float)lonIdx + 0.5f) / (float)resLon * (phiMax   - phiMin);

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
        value += perlin3_region(p, seed + (uint)i * 101u) * amplitude;
        maxAmp += amplitude;
        amplitude *= persistence;
        p *= lacunarity;
    }

    if (maxAmp <= 0.0f)
        value = 0.0f;
    else
        value = value / maxAmp;

    output[latIdx * resLon + lonIdx] = value * 0.5f + 0.5f;
}

/// Multi-channel variant: per-channel parameters.
__kernel void perlin_fbm_3d_sphere_region_channels(
    __global float* output,
    int resLat,
    int resLon,
    float thetaMin,
    float thetaMax,
    float phiMin,
    float phiMax,
    int   channels,
    __global const float* frequency,
    __global const float* lacunarity,
    __global const int*   octaves,
    __global const float* persistence,
    __global const uint*  seed)
{
    int latIdx = get_global_id(0);
    int lonIdx = get_global_id(1);

    if (latIdx >= resLat || lonIdx >= resLon || channels <= 0)
        return;

    float theta = thetaMin + ((float)latIdx + 0.5f) / (float)resLat * (thetaMax - thetaMin);
    float phi   = phiMin   + ((float)lonIdx + 0.5f) / (float)resLon * (phiMax   - phiMin);

    for (int c = 0; c < channels; c++)
    {
        float3 p = (float3)(
            sin(theta) * cos(phi),
            sin(theta) * sin(phi),
            cos(theta)
        ) * frequency[c];

        float value = 0.0f;
        float amplitude = 1.0f;
        float maxAmp = 0.0f;

        for (int i = 0; i < octaves[c]; i++)
        {
            value += perlin3_region(p, seed[c] + (uint)i * 101u) * amplitude;
            maxAmp += amplitude;
            amplitude *= persistence[c];
            p *= lacunarity[c];
        }

        if (maxAmp <= 0.0f)
            value = 0.0f;
        else
            value = value / maxAmp;

        int idx = c * (resLat * resLon) + latIdx * resLon + lonIdx;
        output[idx] = value * 0.5f + 0.5f;
    }
}
