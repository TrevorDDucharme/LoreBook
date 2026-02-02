__kernel void AlphaBlend(
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
    float4 dst = input_one[index];  // background (landtype)
    float4 src = input_two[index];  // foreground (watertable)
    
    // Standard alpha compositing: SRC over DST
    float srcA = clamp(src.w, 0.0f, 1.0f);
    float3 outRgb = src.xyz * srcA + dst.xyz * (1.0f - srcA);
    float outA = srcA + dst.w * (1.0f - srcA);
    
    output[index] = (float4)(outRgb.x, outRgb.y, outRgb.z, outA);
}
