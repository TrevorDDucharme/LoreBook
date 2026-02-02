inline float Humidity_util(
    int latitude,
    int longitude,
    __global const float* landtype,
    int landtypecount,
    __global const float* elevation,
    __global const float* watertable,
    __global const float* rivers,
    __global const float* temperature,
    const int latitudeResolution,
    const int longitudeResolution)
{   
    if (latitude >= latitudeResolution || longitude >= longitudeResolution)
        return;
    
    int index = longitude * latitudeResolution + latitude;
    
    float humidity = 0.0f;
    // Base humidity from landtype
    for (int i = 0; i < landtypecount; i++) {
        if (landtype[index] == (float)i) {
            // Example: assign humidity based on landtype index
            humidity += 0.1f * i; // Placeholder logic
        }
    }
    //
}

__kernel void Humidity(
    int latitude,
    int longitude,
    __global const float* landtype,
    int landtypecount,
    __global const float* elevation,
    __global const float* watertable,
    __global const float* rivers,
    __global const float* temperature,
    __global float4* output,
    const int latitudeResolution,
    const int longitudeResolution)
{
    int latitude = get_global_id(0);
    int longitude = get_global_id(1);
    
    if (latitude >= latitudeResolution || longitude >= longitudeResolution)
        return;
    
    int index = longitude * latitudeResolution + latitude;
    
    output[index] = Humidity_util(
        latitude,
        longitude,
        landtype,
        landtypecount,
        elevation,
        watertable,
        rivers,
        temperature,
        latitudeResolution,
        longitudeResolution);
}
