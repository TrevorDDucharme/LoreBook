// Tectonics.cl - Simple tectonic plate simulation

#define PI 3.141592653589793f

// ============================================================
// Simple Tectonics Simulation
// ============================================================
// 1. Voronoi plates on sphere
// 2. Each plate has a velocity
// 3. Simulation accumulates pressure at boundaries
// 4. Height = accumulated pressure

// ============================================================
// Cubemap Indexing
// ============================================================

inline int CubeMapIdx(int face, int u, int v, int uvRes) {
    return face * uvRes * uvRes + v * uvRes + u;
}

inline void globalIdToFaceUV(int gid0, int gid1, int uvRes, int* face, int* u, int* v) {
    *face = gid0 / uvRes;
    *u = gid0 % uvRes;
    *v = gid1;
}

// Convert cubemap face/uv to 3D direction vector (unit sphere)
inline float3 faceUVToDirection(int face, float u, float v) {
    float3 dir;
    switch(face) {
        case 0: dir = (float3)(u, 1.0f, v); break;      // Top (+Y)
        case 1: dir = (float3)(u, -1.0f, -v); break;    // Bottom (-Y)
        case 2: dir = (float3)(-1.0f, v, u); break;     // Left (-X)
        case 3: dir = (float3)(1.0f, v, -u); break;     // Right (+X)
        case 4: dir = (float3)(u, v, 1.0f); break;      // Front (+Z)
        case 5: dir = (float3)(-u, v, -1.0f); break;    // Back (-Z)
        default: dir = (float3)(0.0f, 1.0f, 0.0f); break;
    }
    return normalize(dir);
}

// ============================================================
// Hash helpers
// ============================================================

inline uint tec_hash(uint x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

inline float tec_hashFloat(uint x) {
    return (float)(tec_hash(x) & 0x00FFFFFFu) / (float)0x01000000u;
}

// ============================================================
// Plate helpers
// ============================================================

// Generate plate center on unit sphere from seed
inline float3 plateCenter(uint seed, int plateIdx) {
    uint h1 = tec_hash(seed + (uint)plateIdx * 7919u);
    uint h2 = tec_hash(seed + (uint)plateIdx * 7919u + 1000u);
    float phi = tec_hashFloat(h1) * 2.0f * PI;
    float cosTheta = 2.0f * tec_hashFloat(h2) - 1.0f;
    float sinTheta = sqrt(1.0f - cosTheta * cosTheta);
    return (float3)(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);
}

// Generate plate velocity (2D tangent velocity)
inline float2 plateVelocity(uint seed, int plateIdx) {
    uint h1 = tec_hash(seed + (uint)plateIdx * 13u + 5000u);
    uint h2 = tec_hash(seed + (uint)plateIdx * 17u + 6000u);
    float velMag = 0.02f + tec_hashFloat(h1) * 0.08f;  // 0.02 to 0.10
    float velAngle = tec_hashFloat(h2) * 2.0f * PI;
    return (float2)(velMag * cos(velAngle), velMag * sin(velAngle));
}

// Find which plate owns this point (Voronoi on sphere)
inline int findPlate(float3 dir, int numPlates, uint seed) {
    float minDist = 999.0f;
    int closest = 0;
    for (int p = 0; p < numPlates; p++) {
        float3 center = plateCenter(seed, p);
        float dist = acos(clamp(dot(dir, center), -1.0f, 1.0f));
        if (dist < minDist) {
            minDist = dist;
            closest = p;
        }
    }
    return closest;
}

// ============================================================
// Voxel structure - SIMPLE: just velocity and pressure
// ============================================================

typedef struct {
    float velX;         // tangent velocity X
    float velY;         // tangent velocity Y
    float pressure;     // accumulated pressure
    int plateId;        // which plate
} TecVoxel;

// ============================================================
// KERNEL: Initialize plates
// ============================================================
// Dispatch: global_size = (6 * uvRes, uvRes)

__kernel void tec_init(
    __global TecVoxel* voxels,
    int uvRes,
    int numPlates,
    uint seed
) {
    int x = get_global_id(0);
    int y = get_global_id(1);
    
    if (x >= 6 * uvRes || y >= uvRes) return;
    
    int face, u, v;
    globalIdToFaceUV(x, y, uvRes, &face, &u, &v);
    int idx = CubeMapIdx(face, u, v, uvRes);
    
    // Get direction on sphere
    float nu = 2.0f * ((float)u + 0.5f) / (float)uvRes - 1.0f;
    float nv = 2.0f * ((float)v + 0.5f) / (float)uvRes - 1.0f;
    float3 dir = faceUVToDirection(face, nu, nv);
    
    // Find plate
    int plate = findPlate(dir, numPlates, seed);
    
    // Get plate velocity
    float2 vel = plateVelocity(seed, plate);
    
    // Initialize voxel
    TecVoxel vox;
    vox.velX = vel.x;
    vox.velY = vel.y;
    vox.pressure = 0.0f;
    vox.plateId = plate;
    
    voxels[idx] = vox;
}

// ============================================================
// KERNEL: Simulation step - accumulate pressure at boundaries
// ============================================================
// Dispatch: global_size = (6 * uvRes, uvRes)

__kernel void tec_step(
    __global TecVoxel* voxelsIn,
    __global TecVoxel* voxelsOut,
    int uvRes,
    float dt
) {
    int x = get_global_id(0);
    int y = get_global_id(1);
    
    if (x >= 6 * uvRes || y >= uvRes) return;
    
    int face, u, v;
    globalIdToFaceUV(x, y, uvRes, &face, &u, &v);
    int idx = CubeMapIdx(face, u, v, uvRes);
    
    TecVoxel vox = voxelsIn[idx];
    
    // Get neighbors (simple - no face wrapping for now, clamp at edges)
    int uP = min(u + 1, uvRes - 1);
    int uN = max(u - 1, 0);
    int vP = min(v + 1, uvRes - 1);
    int vN = max(v - 1, 0);
    
    int idxPosU = CubeMapIdx(face, uP, v, uvRes);
    int idxNegU = CubeMapIdx(face, uN, v, uvRes);
    int idxPosV = CubeMapIdx(face, u, vP, uvRes);
    int idxNegV = CubeMapIdx(face, u, vN, uvRes);
    
    TecVoxel vPosU = voxelsIn[idxPosU];
    TecVoxel vNegU = voxelsIn[idxNegU];
    TecVoxel vPosV = voxelsIn[idxPosV];
    TecVoxel vNegV = voxelsIn[idxNegV];
    
    // Compute velocity divergence (compression = positive pressure)
    float divU = vPosU.velX - vNegU.velX;
    float divV = vPosV.velY - vNegV.velY;
    float divergence = -(divU + divV);  // negative div = compression = pressure
    
    // Accumulate pressure where plates collide
    float newPressure = vox.pressure + divergence * dt;
    
    // Diffuse pressure slightly to neighbors
    float avgPressure = (vPosU.pressure + vNegU.pressure + vPosV.pressure + vNegV.pressure) * 0.25f;
    newPressure = mix(newPressure, avgPressure, 0.1f);
    
    // Clamp pressure
    newPressure = clamp(newPressure, -1.0f, 2.0f);
    
    // Output
    TecVoxel outVox;
    outVox.velX = vox.velX;  // velocity stays constant (rigid plates)
    outVox.velY = vox.velY;
    outVox.pressure = newPressure;
    outVox.plateId = vox.plateId;
    
    voxelsOut[idx] = outVox;
}

// ============================================================
// KERNEL: Extract height from pressure
// ============================================================
// Dispatch: global_size = (6 * uvRes, uvRes)

__kernel void tec_extract_height(
    __global const TecVoxel* voxels,
    __global float* heightOut,
    int uvRes,
    int numPlates,
    uint seed
) {
    int x = get_global_id(0);
    int y = get_global_id(1);
    
    if (x >= 6 * uvRes || y >= uvRes) return;
    
    int face, u, v;
    globalIdToFaceUV(x, y, uvRes, &face, &u, &v);
    int idx = CubeMapIdx(face, u, v, uvRes);
    
    TecVoxel vox = voxels[idx];
    
    // Base height from plate (continental vs oceanic)
    float plateBase = tec_hashFloat(seed + (uint)vox.plateId * 111u);
    float height = 0.3f + plateBase * 0.15f;  // 0.3 to 0.45
    
    // Add pressure contribution (mountains where pressure accumulated)
    height += vox.pressure * 0.3f;
    
    // Clamp
    height = clamp(height, 0.0f, 1.0f);
    
    heightOut[idx] = height;
}

// ============================================================
// KERNEL: Project cubemap to lat/lon
// ============================================================
// Dispatch: global_size = (latRes, lonRes)

inline void directionToFaceUV(float3 dir, int* face, float* u, float* v) {
    float3 absDir = fabs(dir);
    
    if (absDir.x >= absDir.y && absDir.x >= absDir.z) {
        if (dir.x > 0) {
            *face = 3; *u = -dir.z / absDir.x; *v = dir.y / absDir.x;
        } else {
            *face = 2; *u = dir.z / absDir.x; *v = dir.y / absDir.x;
        }
    } else if (absDir.y >= absDir.x && absDir.y >= absDir.z) {
        if (dir.y > 0) {
            *face = 0; *u = dir.x / absDir.y; *v = dir.z / absDir.y;
        } else {
            *face = 1; *u = dir.x / absDir.y; *v = -dir.z / absDir.y;
        }
    } else {
        if (dir.z > 0) {
            *face = 4; *u = dir.x / absDir.z; *v = dir.y / absDir.z;
        } else {
            *face = 5; *u = -dir.x / absDir.z; *v = dir.y / absDir.z;
        }
    }
}

__kernel void tec_cubemap_to_latlon(
    __global const float* cubemap,
    __global float* latlon,
    int uvRes,
    int latRes,
    int lonRes
) {
    int lat_idx = get_global_id(0);
    int lon_idx = get_global_id(1);
    
    if (lat_idx >= latRes || lon_idx >= lonRes) return;
    
    // Convert to spherical
    float lat = PI * ((float)lat_idx + 0.5f) / (float)latRes - PI * 0.5f;
    float lon = 2.0f * PI * ((float)lon_idx + 0.5f) / (float)lonRes - PI;
    
    // To 3D direction
    float3 dir = (float3)(cos(lat) * cos(lon), sin(lat), cos(lat) * sin(lon));
    
    // Find cubemap face and UV
    int face;
    float fu, fv;
    directionToFaceUV(dir, &face, &fu, &fv);
    
    // Convert from [-1,1] to [0, uvRes-1]
    float uu = (fu + 1.0f) * 0.5f * (float)(uvRes - 1);
    float vv = (fv + 1.0f) * 0.5f * (float)(uvRes - 1);
    
    // Bilinear sample
    int u0 = clamp((int)floor(uu), 0, uvRes - 1);
    int v0 = clamp((int)floor(vv), 0, uvRes - 1);
    int u1 = clamp(u0 + 1, 0, uvRes - 1);
    int v1 = clamp(v0 + 1, 0, uvRes - 1);
    
    float su = uu - floor(uu);
    float sv = vv - floor(vv);
    
    float h00 = cubemap[CubeMapIdx(face, u0, v0, uvRes)];
    float h10 = cubemap[CubeMapIdx(face, u1, v0, uvRes)];
    float h01 = cubemap[CubeMapIdx(face, u0, v1, uvRes)];
    float h11 = cubemap[CubeMapIdx(face, u1, v1, uvRes)];
    
    float h0 = mix(h00, h10, su);
    float h1 = mix(h01, h11, su);
    float height = mix(h0, h1, sv);
    
    latlon[lat_idx * lonRes + lon_idx] = height;
}
