inline float3 sphere_to_cartesian_float3(
    int lattitude,
    int longitude,
    int lattitude_resolution,
    int longitude_resolution
){
    float theta = ((float)lattitude + 0.5f) / (float)lattitude_resolution * 3.14159265f; // [0, pi]
    float phi = ((float)longitude + 0.5f) / (float)longitude_resolution * 2.0f * 3.14159265f; // [0, 2pi]

    return (float3)(
        sin(theta) * cos(phi),
        sin(theta) * sin(phi),
        cos(theta)
    );
}

//random helpers
inline int hash_int(int x)
{
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = ((x >> 16) ^ x) * 0x45d9f3b;
    x = (x >> 16) ^ x;
    return x;
}
inline float hash_float(int x)
{
    return (float)(hash_int(x) & 0x00FFFFFF) / (float)0x01000000;
}

inline float rand_float(int seed, int x, int y)
{
    return hash_float(seed + x * 73856093 + y * 19349663);
}

inline int rand_int(int seed, int x, int y, int max)
{
    return hash_int(seed + x * 73856093 + y * 19349663) % max;
}

inline bool rand_chance(int seed, int x, int y, float chance)
{
    return rand_float(seed, x, y) < chance;
}