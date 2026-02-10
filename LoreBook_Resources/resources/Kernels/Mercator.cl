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

/// Region-aware Mercator projection.
/// The source buffer covers only [regionLonMinDeg, regionLonMaxDeg] ×
/// [regionLatMinDeg, regionLatMaxDeg] at (regionW × regionH) resolution.
__kernel void sphere_to_mercator_rgba_region(
    __global const float4* regionTex,
    int regionW,
    int regionH,
    float regionLonMinDeg,
    float regionLonMaxDeg,
    float regionLatMinDeg,
    float regionLatMaxDeg,

    __global float4* output2d,
    int outW,
    int outH,

    float centerLon,
    float centerMercY,
    float zoom)
{
    int u = get_global_id(0);
    int v = get_global_id(1);

    if (u >= outW || v >= outH)
        return;

    float nx = (float)u / (float)outW;
    float ny = (float)v / (float)outH;

    float lon = centerLon + (nx - 0.5f) * (360.0f / zoom);
    const float PI = 3.14159265358979323846f;
    float mercY = centerMercY + (0.5f - ny) * (2.0f * PI / zoom);

    const float RAD_TO_DEG = 180.0f / PI;
    float lat = atan(sinh(mercY)) * RAD_TO_DEG;

    // Map to region-local coordinates
    float regionLonSpan = regionLonMaxDeg - regionLonMinDeg;
    float regionLatSpan = regionLatMaxDeg - regionLatMinDeg;

    // Wrap longitude into the region (handle wrapping around ±180)
    float localLon = lon - regionLonMinDeg;
    // Normalize into [0, 360) then check against span
    while (localLon < 0.0f) localLon += 360.0f;
    while (localLon >= 360.0f) localLon -= 360.0f;
    // If the region doesn't wrap, check the span directly
    float regionLonSpanPos = regionLonSpan;
    if (regionLonSpanPos < 0.0f) regionLonSpanPos += 360.0f;

    float texU = localLon / regionLonSpanPos;
    // Latitude: regionLatMax = top (north), regionLatMin = bottom (south)
    // In the buffer: row 0 = north (latMax), row H-1 = south (latMin)
    float texV = (regionLatMaxDeg - lat) / regionLatSpan;

    if (texU < 0.0f || texU >= 1.0f || texV < 0.0f || texV >= 1.0f) {
        output2d[u + v * outW] = (float4)(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    int texX = clamp((int)(texU * (float)regionW), 0, regionW - 1);
    int texY = clamp((int)(texV * (float)regionH), 0, regionH - 1);

    output2d[u + v * outW] = regionTex[texY * regionW + texX];
}
