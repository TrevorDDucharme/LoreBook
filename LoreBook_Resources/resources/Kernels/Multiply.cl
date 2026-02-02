__kernel void multiplyColor(
    __global const float4* input_one,
    __global const float4* input_two,
    __global float4* output,
    const int width,
    const int height)
{
    int x = get_global_id(0);
    int y = get_global_id(1);
    
    if (x >= width || y >= height)
        return;
    
    int index = y * width + x;
    float4 color_one = input_one[index];
    float4 color_two = input_two[index];
    
    // Multiply each channel
    output[index] = color_one * color_two;
}