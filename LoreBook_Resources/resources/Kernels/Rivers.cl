// Rivers.cl - generate_river_paths_3d (radius-aware, sphere-surface sampling)
//
// Notes:
// - Host must zero `outVolume` before launch.
// - Each seed will attempt up to `max_steps` to walk to water.
// - `radius` sets the spherical sampling distance for each step (use >=1).
// - Writes 1.0f for water / river voxels; leaves other voxels as-is (expected 0.0f).

inline uint lcg(uint state) { return state * 1664525u + 1013904223u; }
inline float randf(uint *state) { *state = lcg(*state); return (float)(*state & 0xFFFFFFu) / (float)0x1000000u; }

inline int distance3(int x0, int y0, int z0, int x1, int y1, int z1) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    int dz = z1 - z0;
    return (int)sqrt((float)(dx * dx + dy * dy + dz * dz));
}

__kernel void generate_river_paths(
    __global const float* elevation,
    __global const float* watertable,
    int W, int H, int D,
    int radius,            // spherical step radius
    uint max_steps,
    __global float* outVolume) // must be zeroed by host
{
    int x = get_global_id(0);
    int y = get_global_id(1);
    int z = get_global_id(2);
    if (x >= W || y >= H || z >= D) return;

    const int idx = x + y * W + z * W * H;
    const float elev = elevation[idx];
    const float waterLevel = watertable[idx];

    // Mark existing water voxels immediately
    if (waterLevel>0.0f) {
        outVolume[idx] = 1.0f;
        return;
    }

    // RNG seed: deterministic per-voxel
    uint state = (uint)idx + 0x9e3779b9u;

    // Quick local-highpoint check: voxel must be >= all neighbors within 'radius'
    // (also prevents too many seeds). We'll limit the search radius to a small cap for perf.
    int capRadius = radius;
    if (capRadius > 8) capRadius = 8; // prevent excessive per-thread work
    bool isHighpoint = true;
    for (int dz = -capRadius; dz <= capRadius && isHighpoint; ++dz) {
        int oz = z + dz;
        if (oz < 0 || oz >= D) continue;
        for (int dy = -capRadius; dy <= capRadius && isHighpoint; ++dy) {
            int oy = y + dy;
            if (oy < 0 || oy >= H) continue;
            for (int dx = -capRadius; dx <= capRadius; ++dx) {
                int ox = x + dx;
                if (ox < 0 || ox >= W) continue;
                if (dx==0 && dy==0 && dz==0) continue;
                int sq = dx*dx + dy*dy + dz*dz;
                if (sq > capRadius*capRadius) continue;
                int oidx = ox + oy * W + oz * W * H;
                if (elevation[oidx] > elev + 1e-6f) {
                    isHighpoint = false;
                    break;
                }
            }
        }
    }

    // Seed probability: only some highpoints start a river (avoid flooding)
    float seedProb = 0.25f; // tweakable; higher -> more rivers
    if (!isHighpoint || randf(&state) > seedProb) {
        // not a starting seed
        return;
    }

    // Walk downhill from (x,y,z). Use spherical-surface sampling with radius.
    int curX = x, curY = y, curZ = z;
    float curElev = elev;

    // Precompute radius squared
    int r = radius > 0 ? radius : 1;
    int r2 = r * r;
    // tolerance band to approximate surface (discrete grid)
    int tol = max(1, r / 2);

    for (uint step = 0; step < max_steps; ++step) {
        // mark current voxel as part of river (1.0)
        int curIdx = curX + curY * W + curZ * W * H;
        outVolume[curIdx] = 1.0f;

        // stop if at water level
        if (curElev <= watertable[curIdx] + 1e-6f) break;

        // Search for lowest elevation on the approximate spherical shell centered at current pos
        float bestElev = curElev;
        int bestX = -1, bestY = -1, bestZ = -1;

        // iterate neighbors in bounding box of radius (could be heavy for large radius)
        // restrict to voxels whose squared distance is within [r2 - tol, r2 + tol] -> approx surface
        int minD = max(0, r - tol);
        int minD2 = minD * minD;
        int maxD2 = r2 + tol * tol;

        int xmin = max(0, curX - r);
        int xmax = min(W - 1, curX + r);
        int ymin = max(0, curY - r);
        int ymax = min(H - 1, curY + r);
        int zmin = max(0, curZ - r);
        int zmax = min(D - 1, curZ + r);

        for (int oz = zmin; oz <= zmax; ++oz) {
            int dz = oz - curZ;
            int dz2 = dz*dz;
            for (int oy = ymin; oy <= ymax; ++oy) {
                int dy = oy - curY;
                int dy2 = dy*dy;
                for (int ox = xmin; ox <= xmax; ++ox) {
                    int dx = ox - curX;
                    int sq = dx*dx + dy2 + dz2;
                    if (sq < minD2 || sq > maxD2) continue; // outside approx shell
                    int oidx = ox + oy * W + oz * W * H;
                    float oe = elevation[oidx];

                    if (oe < bestElev) {
                        bestElev = oe;
                        bestX = ox; bestY = oy; bestZ = oz;
                    }
                }
            }
        }

        // Fallback: if no surface candidate found, sample local 3x3x3 neighbors
        if (bestX == -1) {
            for (int oz = max(0, curZ-1); oz <= min(D-1, curZ+1); ++oz) {
                for (int oy = max(0, curY-1); oy <= min(H-1, curY+1); ++oy) {
                    for (int ox = max(0, curX-1); ox <= min(W-1, curX+1); ++ox) {
                        if (ox==curX && oy==curY && oz==curZ) continue;
                        int oidx = ox + oy * W + oz * W * H;
                        float oe = elevation[oidx];
                        if (oe < bestElev) {
                            bestElev = oe;
                            bestX = ox; bestY = oy; bestZ = oz;
                        }
                    }
                }
            }
        }

        // If no lower neighbor found, stop (stuck on local minimum or plateau)
        if (bestX == -1 || bestElev >= curElev - 1e-6f) {
            break;
        }

        // advance to chosen neighbor
        curX = bestX; curY = bestY; curZ = bestZ;
        curElev = bestElev;
    }
}