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