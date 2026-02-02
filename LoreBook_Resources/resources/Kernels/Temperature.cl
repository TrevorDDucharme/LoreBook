#include "Latitude.cl"
#include "Perlin.cl"

inline float temperature_map_util(
    int latitudeResolution,
    int longitudeResolution,
    float frequency,
    float lacunarity,
    int octaves,
    float persistence,
    uint seed
)
{
    int latitude = get_global_id(0);
    int longitude = get_global_id(1);

    if (latitude >= latitudeResolution || longitude >= longitudeResolution)
        return 0.0f;

    int idx = latitude + longitude * latitudeResolution;

    float latFactor = latitude_util(latitude, longitude, latitudeResolution, longitudeResolution)*.5f;

    float noiseValue = perlin_3d_sphere_sample_util(
        latitude,
        longitude,
        latitudeResolution,
        longitudeResolution,
        frequency, 
        lacunarity,
        octaves, 
        persistence, 
        seed
    );

    // Combine latitude factor and noise value to get temperature
    // Temperature ranges from -1.0 (cold) to 1.0 (hot)
    float temperature = latFactor + noiseValue * 0.5f;

    return temperature;
}

__kernel void temperature_map(
    int latitudeResolution,
    int longitudeResolution,
    float frequency,
    float lacunarity,
    int octaves,
    float persistence,
    uint seed,
    __global float* output
)
{
    int latitude = get_global_id(0);
    int longitude = get_global_id(1);

    if (latitude >= latitudeResolution || longitude >= longitudeResolution)
        return;

    int idx = latitude + longitude * latitudeResolution;

    output[idx] = temperature_map_util(
        latitudeResolution,
        longitudeResolution,
        frequency,
        lacunarity,
        octaves,
        persistence,
        seed
    );
}