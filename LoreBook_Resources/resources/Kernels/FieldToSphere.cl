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
    float sphereRadius,

    __global int* debugBuf // optional small debug buffer (size >= 8 ints)
)
{
    int x = get_global_id(0);
    int y = get_global_id(1);

    if (x >= screenW || y >= screenH)
        return;

    int idx = x + y * screenW;

    // Mark kernel run so host always sees debug buffer written at least once
    if (debugBuf != 0 && x == 0 && y == 0) {
        // leave other debug fields to be set later; just set the written flag
        debugBuf[4] = 1;
    }

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
        // Debug: if top-left pixel, indicate no-hit
        if (debugBuf != 0 && x == 0 && y == 0) {
            debugBuf[0] = x;
            debugBuf[1] = y;
            debugBuf[2] = 0; // no-hit
            debugBuf[3] = 0;
            debugBuf[4] = 1;
        }
        output[idx] = (float4)(0.0f,0.0f,0.0f,0.0f);
        return;
    }

    float t = -b - sqrt(h);
    if (t < 0.0f)
    {
        // Debug: if top-left pixel, indicate no-hit (t negative)
        if (debugBuf != 0 && x == 0 && y == 0) {
            debugBuf[0] = x;
            debugBuf[1] = y;
            debugBuf[2] = 0; // no-hit
            debugBuf[3] = as_int(t);
            debugBuf[4] = 1;
        }
        output[idx] = (float4)(0.0f,0.0f,0.0f,0.0f);
        return;
    }

    float3 hitPos = camPos + rayDir * t;

    // -----------------------------
    // Map hit point to field volume
    // -----------------------------

    // Debug: for the center pixel write some values to debugBuf if provided
    // debugBuf[0] = x, [1] = y, [2] = hitFlag (1 if hit else 0), [3] = as_int(t), [4] = written flag
    int centerX = screenW / 2;
    int centerY = screenH / 2;
    if (debugBuf != 0 && x == centerX && y == centerY) {
        debugBuf[0] = x;
        debugBuf[1] = y;
        debugBuf[2] = 1; // hit
        debugBuf[3] = as_int(t);
        debugBuf[4] = 1;
    }

    float3 localPos = (hitPos - sphereCenter) / (sphereRadius * 2.0f) + 0.5f;
    localPos = clamp(localPos, 0.0f, 0.9999f);

    int ix = (int)(localPos.x * fieldW);
    int iy = (int)(localPos.y * fieldH);
    int iz = (int)(localPos.z * fieldD);

    int fieldIdx = ix + iy * fieldW + iz * fieldW * fieldH;

    output[idx] = field3d[fieldIdx];
    output[idx].w = 1.0f; // ensure alpha is 1.0
}
