__kernel void sphere_to_mercator_rgba(
    __global const float4* sphereTex,
    int latitudeResolution,
    int longitudeResolution,

    __global float4* output2d,
    int outW,
    int outH,

    float radius,
    float centerLon,
    float centerMercY,
    float zoom
)
{
    int u = get_global_id(0);
    int v = get_global_id(1);

    if (u >= outW || v >= outH)
        return;

    // Normalized screen coords
    float nx = (float)u / (float)outW;
    float ny = (float)v / (float)outH;

    // Mercator coords
    float mercX = (nx - 0.5f) * 2.0f * zoom;
    float mercY = (ny - 0.5f) * 2.0f * zoom;
    mercX += centerLon;
    mercY += centerMercY;
    // Convert to lat/lon
    float lon = mercX;;
    float lat = atan(sinh(mercY)) * (180.0f / 3.14159265358979323846f);
    // Convert to texture coords
    float texU = (lon + 180.0f) / 360.0f;
    float texV = (90.0f - lat) / 180.0f;
    // Sample sphere texture
    int texX = (int)(texU * (float)longitudeResolution) % longitudeResolution;
    int texY = (int)(texV * (float)latitudeResolution);
    if (texY < 0) texY = 0;
    if (texY >= latitudeResolution) texY = latitudeResolution - 1;
    int texIdx = texX + texY * longitudeResolution;
    float4 color = sphereTex[texIdx];
    output2d[u + v * outW] = color;
}
