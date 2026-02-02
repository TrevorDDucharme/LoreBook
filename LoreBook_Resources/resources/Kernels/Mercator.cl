__kernel void sphere_to_mercator_rgba(
    __global const float4* sphereTex,
    int latitudeResolution,
    int longitudeResolution,

    __global float4* output2d,
    int outW,
    int outH,

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
    // Horizontal: full width maps to 360 degrees; vertical: full height maps to 2*PI in Mercator Y.
    // Zoom semantics: larger zoom = closer (smaller world span), so divide by zoom.
    float lon = centerLon + (nx - 0.5f) * (360.0f / zoom);
    const float PI = 3.14159265358979323846f;
    float mercY = centerMercY + (0.5f - ny) * (2.0f * PI / zoom);

    const float RAD_TO_DEG = 180.0f / PI;
    // Convert to lat from Mercator y
    float lat = atan(sinh(mercY)) * RAD_TO_DEG;

    // Convert to texture coords
    float texU = (lon + 180.0f) / 360.0f;
    float texV = (90.0f - lat) / 180.0f;

    // Robust integer texture coords and wrapping
    float texUF = texU * (float)longitudeResolution;
    int texX = (int)floor(texUF);
    texX = texX % longitudeResolution;
    if (texX < 0) texX += longitudeResolution;

    int texY = (int)floor(texV * (float)latitudeResolution);
    if (texY < 0) texY = 0;
    if (texY >= latitudeResolution) texY = latitudeResolution - 1;

    int texIdx = texX + texY * longitudeResolution;
    float4 color = sphereTex[texIdx];
    output2d[u + v * outW] = color;
}
