
float latitude_util(
    int x,
    int y,
    int z,
    int width,
    int height,
    int depth
)
{
    //1.0 at equator, 0.0 at poles
    float lat = 1.0f - fabs(((float)y / (float)(height - 1)) * 2.0f - 1.0f);
    return lat;
}


__kernel void latitude(
    __global float* output,
    int width,
    int height,
    int depth
)
{
    int x = get_global_id(0);
    int y = get_global_id(1);
    int z = get_global_id(2);

    if (x >= width || y >= height || z >= depth)
        return;

    output[x + y * width + z * width * height] =
        latitude_util(
            x, y, z,
            width, height, depth
        );
}