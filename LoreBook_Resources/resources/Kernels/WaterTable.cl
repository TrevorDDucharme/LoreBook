
float water_table_util(
    int x,
    int y,
    int z,
    int width,
    int height,
    int depth,
    float* elevation,
    float water_table_level
)
{
    int index = x + y * width + z * width * height;
    float elev = elevation[index];
    //Simple model: water table is at a fixed level
    if (elev < water_table_level)
        //return the depth below the water table, normalized
        return (water_table_level - elev) / water_table_level;
    else
        return 0.0f; //above water
}


__kernel void water_table(
    __global float* output,
    __global float* elevation,
    int width,
    int height,
    int depth,
    float water_table_level
)
{
    int x = get_global_id(0);
    int y = get_global_id(1);
    int z = get_global_id(2);

    if (x >= width || y >= height || z >= depth)
        return;

    output[x + y * width + z * width * height] =
        water_table_util(
            x, y, z,
            width, height, depth,
            elevation,
            water_table_level
        );
}