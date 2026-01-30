// RGBA variant: read float4 field and output float4 per pixel
__kernel void sphere_perspective_sample_rgba(
    __global const float4* field3d,
    int fieldW,
    int fieldH,
    int fieldD,

    __global float4* output,
    int screenW,
    int screenH,

    float3 camPos,
    float3 camForward,
    float3 camRight,
    float3 camUp,

    float fovY,          // radians
    float3 sphereCenter,
    float sphereRadius
)
{
    int x = get_global_id(0);
    int y = get_global_id(1);

    if (x >= screenW || y >= screenH)
        return;

    int idx = x + y * screenW;

    // -----------------------------
    // Generate ray direction
    // -----------------------------

    float nx = (2.0f * ((float)x + 0.5f) / (float)screenW - 1.0f);
    float ny = (1.0f - 2.0f * ((float)y + 0.5f) / (float)screenH);

    float aspect = (float)screenW / (float)screenH;
    float tanFov = tan(fovY * 0.5f);

    float3 rayDir =
        normalize(
            camForward +
            nx * aspect * tanFov * camRight +
            ny * tanFov * camUp
        );

    // -----------------------------
    // Ray–sphere intersection
    // -----------------------------

    float3 oc = camPos - sphereCenter;

    float b = dot(oc, rayDir);
    float c = dot(oc, oc) - sphereRadius * sphereRadius;
    float h = b * b - c;

    // No hit
    if (h < 0.0f)
    {
        output[idx] = (float4)(0.0f,0.0f,0.0f,0.0f);
        return;
    }

    float t = -b - sqrt(h);
    if (t < 0.0f)
    {
        output[idx] = (float4)(0.0f,0.0f,0.0f,0.0f);
        return;
    }

    float3 hitPos = camPos + rayDir * t;

    // -----------------------------
    // Map hit point to field volume
    // -----------------------------

    float3 localPos = (hitPos - sphereCenter) / (sphereRadius * 2.0f) + 0.5f;
    localPos = clamp(localPos, 0.0f, 0.9999f);

    int ix = (int)(localPos.x * fieldW);
    int iy = (int)(localPos.y * fieldH);
    int iz = (int)(localPos.z * fieldD);

    int fieldIdx = ix + iy * fieldW + iz * fieldW * fieldH;

    output[idx] = field3d[fieldIdx];
}