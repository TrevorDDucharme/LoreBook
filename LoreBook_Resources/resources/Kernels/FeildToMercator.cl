__kernel void field3d_to_mercator(
    __global const float* field3d,
    int fieldW,
    int fieldH,
    int fieldD,

    __global float* output2d,
    int outW,
    int outH,

    float radius
)
{
    int u = get_global_id(0);
    int v = get_global_id(1);

    if (u >= outW || v >= outH)
        return;

    // Normalized screen coords
    float nx = (float)u / (float)outW;
    float ny = (float)v / (float)outH;

    // Longitude [-pi, pi]
    float lon = (nx - 0.5f) * 6.2831853f;

    // Mercator inverse
    float y = (0.5f - ny) * 2.0f;
    float lat = 2.0f * atan(exp(y)) - 1.5707963f;

    // Spherical to Cartesian
    float3 p;
    p.x = radius * cos(lat) * cos(lon);
    p.y = radius * sin(lat);
    p.z = radius * cos(lat) * sin(lon);

    // Map from [-r, r] → [0,1]
    float3 uvw = (p / (radius * 2.0f)) + 0.5f;

    // Clamp
    uvw = clamp(uvw, 0.0f, 0.9999f);

    // Sample field volume (nearest)
    int ix = (int)(uvw.x * fieldW);
    int iy = (int)(uvw.y * fieldH);
    int iz = (int)(uvw.z * fieldD);

    int idx3d = ix + iy * fieldW + iz * fieldW * fieldH;
    int idx2d = u + v * outW;

    output2d[idx2d] = field3d[idx3d];
}
