__kernel void sphere_perspective_sample_rgba(
    __global const float4* sphereTex,
    int latitudeResolution,
    int longitudeResolution,

    __global float4* output,
    int screenW,
    int screenH,

    float3 camPos,
    float3 camForward,
    float3 camRight,
    float3 camUp,

    float fovY // radians
)
{
    int u = get_global_id(0);
    int v = get_global_id(1);

    if (u >= screenW || v >= screenH)
        return;

    // Normalized screen coords
    float nx = (float)u / (float)screenW;
    float ny = (float)v / (float)screenH;

    // Screen space to world space ray
    float aspect = (float)screenW / (float)screenH;
    float px = (2.0f * nx - 1.0f) * tan(fovY * 0.5f) * aspect;
    float py = (1.0f - 2.0f * ny) * tan(fovY * 0.5f);
    float3 rayDir = normalize(px * camRight + py * camUp + camForward);

    // Intersect ray with unit sphere centered at origin
    const float R = 1.0f;
    float a = dot(rayDir, rayDir);
    float b = 2.0f * dot(camPos, rayDir);
    float c = dot(camPos, camPos) - R * R;
    float disc = b * b - 4.0f * a * c;

    if (disc <= 0.0f)
    {
        // No intersection: write transparent pixel
        output[u + v * screenW] = (float4)(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float sqrtD = sqrt(disc);
    float t0 = (-b - sqrtD) / (2.0f * a);
    float t1 = (-b + sqrtD) / (2.0f * a);

    // Choose nearest positive intersection. If camera is inside sphere, t0 may be negative and t1 positive (exit).
    float t = t0;
    if (t < 0.0f) t = t1;
    if (t < 0.0f)
    {
        // Both intersections behind camera
        output[u + v * screenW] = (float4)(0.0f, 0.0f, 0.0f, 0.0f);
        return;
    }

    float3 hit = camPos + t * rayDir;

    // Convert hit point to spherical coordinates (lon/lat)
    const float RAD_TO_DEG = 180.0f / 3.14159265358979323846f;
    float lon = atan2(hit.z, hit.x) * RAD_TO_DEG;
    float lat = asin(clamp(hit.y, -1.0f, 1.0f)) * RAD_TO_DEG;

    // Convert to texture coords
    float texU = (lon + 180.0f) / 360.0f;
    float texV = (90.0f - lat) / 180.0f;

    // Robustly compute integer texture coords and clamp/wrap
    float texUF = texU * (float)longitudeResolution;
    int texX = (int)floor(texUF);
    // modulo that handles negative values
    texX = texX % longitudeResolution;
    if (texX < 0) texX += longitudeResolution;

    int texY = (int)floor(texV * (float)latitudeResolution);
    if (texY < 0) texY = 0;
    if (texY >= latitudeResolution) texY = latitudeResolution - 1;

    int texIdx = texX + texY * longitudeResolution;
    float4 color = sphereTex[texIdx];
    output[u + v * screenW] = color;
}