// OrbitalView.cl — Multi-sphere ray-tracing kernel for orbital system rendering
// Traces rays from a perspective camera and tests intersection against multiple spheres.
// Each sphere has a center, radius, and a texture region in a packed equirectangular atlas.

#define MAX_BODIES 32

// Per-body definition packed into float4s:
//   bodyDef[i*2+0] = (centerX, centerY, centerZ, radius)
//   bodyDef[i*2+1] = (texOffsetU, texOffsetV, texScaleU, texScaleV)
//     where texOffset/Scale map into the packed atlas buffer

__kernel void orbital_view_multisphere(
    __global const float4* bodyDefs,    // MAX_BODIES * 2 float4s
    int bodyCount,

    __global const float4* texAtlas,    // packed texture atlas (all body textures)
    __global const int2* texRegions,    // (atlasOffset, width*height) per body
    __global const int* texWidths,      // texture width per body
    __global const int* texHeights,     // texture height per body

    __global float4* output,
    int screenW,
    int screenH,

    float3 camPos,
    float3 camForward,
    float3 camRight,
    float3 camUp,
    float fovY,

    float4 bgColor                      // background color (e.g. dark space)
)
{
    int u = get_global_id(0);
    int v = get_global_id(1);
    if (u >= screenW || v >= screenH) return;

    // Ray direction from screen pixel
    float nx = (float)u / (float)screenW;
    float ny = (float)v / (float)screenH;
    float aspect = (float)screenW / (float)screenH;
    float px = (2.0f * nx - 1.0f) * tan(fovY * 0.5f) * aspect;
    float py = (1.0f - 2.0f * ny) * tan(fovY * 0.5f);
    float3 rayDir = normalize(px * camRight + py * camUp + camForward);

    // Find nearest sphere intersection
    float nearestT = 1e20f;
    int hitBody = -1;
    float3 hitPoint;

    int count = min(bodyCount, MAX_BODIES);
    for (int i = 0; i < count; ++i) {
        float4 def0 = bodyDefs[i * 2];
        float3 center = (float3)(def0.x, def0.y, def0.z);
        float R = def0.w;

        float3 oc = camPos - center;
        float a = dot(rayDir, rayDir);
        float b = 2.0f * dot(oc, rayDir);
        float c = dot(oc, oc) - R * R;
        float disc = b * b - 4.0f * a * c;

        if (disc <= 0.0f) continue;

        float sqrtD = sqrt(disc);
        float t0 = (-b - sqrtD) / (2.0f * a);
        float t1 = (-b + sqrtD) / (2.0f * a);

        float t = t0;
        if (t < 0.0f) t = t1;
        if (t < 0.0f) continue;

        if (t < nearestT) {
            nearestT = t;
            hitBody = i;
            hitPoint = camPos + t * rayDir;
        }
    }

    if (hitBody < 0) {
        output[u + v * screenW] = bgColor;
        return;
    }

    // Convert hit point to local sphere coordinates
    float4 def0 = bodyDefs[hitBody * 2];
    float3 center = (float3)(def0.x, def0.y, def0.z);
    float3 localHit = hitPoint - center;
    float R = def0.w;

    // Normalize to unit sphere for lon/lat
    float3 n = localHit / R;
    float lon = atan2(n.z, n.x) * (180.0f / 3.14159265358979323846f);
    float lat = asin(clamp(n.y, -1.0f, 1.0f)) * (180.0f / 3.14159265358979323846f);

    float texU = (lon + 180.0f) / 360.0f;
    float texV = (90.0f - lat) / 180.0f;

    // Sample from this body's texture region in the atlas
    int2 region = texRegions[hitBody];
    int atlasOffset = region.x;
    int tw = texWidths[hitBody];
    int th = texHeights[hitBody];

    int texX = clamp((int)(texU * (float)tw), 0, tw - 1);
    int texY = clamp((int)(texV * (float)th), 0, th - 1);

    float4 color = texAtlas[atlasOffset + texY * tw + texX];

    // Simple diffuse lighting for non-emissive bodies
    // Light direction: from hit point toward body 0 (assumed to be the star)
    float4 starDef = bodyDefs[0];
    float3 starCenter = (float3)(starDef.x, starDef.y, starDef.z);
    float3 lightDir = normalize(starCenter - hitPoint);
    float3 normal = n;
    float diffuse = max(dot(normal, lightDir), 0.0f);

    // Check if this body IS the star (body 0 and luminous)
    float4 def1 = bodyDefs[hitBody * 2 + 1];
    float luminosity = def1.x;

    if (luminosity > 0.0f) {
        // Stars are fully bright (emissive)
        output[u + v * screenW] = color;
    } else {
        // Ambient + diffuse
        float ambient = 0.08f;
        float brightness = ambient + (1.0f - ambient) * diffuse;
        output[u + v * screenW] = (float4)(color.x * brightness, color.y * brightness, color.z * brightness, color.w);
    }
}

// Simpler kernel for orbit path rendering (renders orbit ellipses as thin lines)
__kernel void orbital_view_orbit_lines(
    __global const float4* orbitPoints,  // array of 3D line segment endpoints (p0, p1, p0, p1, ...)
    int segmentCount,

    __global float4* output,
    int screenW,
    int screenH,

    float3 camPos,
    float3 camForward,
    float3 camRight,
    float3 camUp,
    float fovY,

    float4 lineColor,
    float lineWidth
)
{
    int u = get_global_id(0);
    int v = get_global_id(1);
    if (u >= screenW || v >= screenH) return;

    float nx = (float)u / (float)screenW;
    float ny = (float)v / (float)screenH;
    float aspect = (float)screenW / (float)screenH;

    // Project orbit points to screen and check distance to this pixel
    float minDist = 1e20f;
    float closestDepth = 1e20f;

    for (int i = 0; i < segmentCount; ++i) {
        float4 p4 = orbitPoints[i];
        float3 p = (float3)(p4.x, p4.y, p4.z);

        // Project to screen space
        float3 toP = p - camPos;
        float depth = dot(toP, camForward);
        if (depth <= 0.01f) continue;

        float screenX = dot(toP, camRight) / (depth * tan(fovY * 0.5f) * aspect);
        float screenY = dot(toP, camUp) / (depth * tan(fovY * 0.5f));

        // Convert from [-1,1] to [0,1]
        float sx = (screenX + 1.0f) * 0.5f;
        float sy = (1.0f - screenY) * 0.5f;

        float dx = sx - nx;
        float dy = sy - ny;
        float dist = sqrt(dx * dx + dy * dy) * (float)screenW;

        if (dist < minDist) {
            minDist = dist;
            closestDepth = depth;
        }
    }

    // Only draw if close enough to an orbit point
    if (minDist < lineWidth) {
        int idx = u + v * screenW;
        float4 existing = output[idx];
        float alpha = lineColor.w * clamp(1.0f - minDist / lineWidth, 0.0f, 1.0f);
        // Alpha blend over existing
        output[idx] = (float4)(
            existing.x * (1.0f - alpha) + lineColor.x * alpha,
            existing.y * (1.0f - alpha) + lineColor.y * alpha,
            existing.z * (1.0f - alpha) + lineColor.z * alpha,
            max(existing.w, alpha)
        );
    }
}
