
// Latitude for 2D lat/lon texture
// Conventions: get_global_id(0) -> latitude (x), get_global_id(1) -> longitude (y)
// idx = latitude + longitude * latitudeResolution

float latitude_util(
    int latitude,
    int longitude,
    int latitudeResolution,
    int longitudeResolution
)
{
    // 1.0 at equator, 0.0 at poles
    return 1.0f - fabs(((float)longitude / (float)(longitudeResolution - 1)) * 2.0f - 1.0f);
}


__kernel void latitude(
    __global float* output,
    int latitudeResolution,
    int longitudeResolution
)
{
    int latitude = get_global_id(0);
    int longitude = get_global_id(1);

    if (latitude >= latitudeResolution || longitude >= longitudeResolution)
        return;

    int idx = latitude + longitude * latitudeResolution;

    output[idx] = latitude_util(latitude, longitude, latitudeResolution, longitudeResolution);
}