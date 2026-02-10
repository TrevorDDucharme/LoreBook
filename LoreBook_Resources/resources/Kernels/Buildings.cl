// Buildings.cl — Renders floorplan footprints (rooms, walls, doors, windows,
// furniture, staircases) onto the world map.
//
// Kernel receives two categories of geometry, both grouped by building for
// bounding-box culling:
//   1. Room polygons  – filled with their fill colour (point-in-polygon test).
//   2. Line segments   – drawn with per-segment colour and thickness.
//
// Rooms are rendered first (background) then segments are drawn on top,
// giving the expected z-ordering.

// ── helpers ─────────────────────────────────────────────────────────────────

// Squared distance from point p to the closest point on segment a→b.
float distToSegmentSq(float2 p, float2 a, float2 b)
{
    float2 ab = b - a;
    float  ab2 = dot(ab, ab);
    if (ab2 < 1e-12f)                       // degenerate segment
        return dot(p - a, p - a);
    float  t   = clamp(dot(p - a, ab) / ab2, 0.0f, 1.0f);
    float2 proj = a + t * ab;
    float2 diff = p - proj;
    return dot(diff, diff);
}

// Ray-casting point-in-polygon test.
// verts is a closed polygon (first vertex == last vertex is optional; the
// caller passes vertCount covering only the unique vertices and the test
// wraps around automatically).
bool pointInPolygon(float2 p,
                    __global const float2* verts,
                    int                    offset,
                    int                    count)
{
    bool inside = false;
    for (int i = 0, j = count - 1; i < count; j = i++) {
        float2 vi = verts[offset + i];
        float2 vj = verts[offset + j];
        if (((vi.y > p.y) != (vj.y > p.y)) &&
            (p.x < (vj.x - vi.x) * (p.y - vi.y) / (vj.y - vi.y) + vi.x))
        {
            inside = !inside;
        }
    }
    return inside;
}

// ── kernel ──────────────────────────────────────────────────────────────────

__kernel void drawBuildings(
    // ── room polygon data ──
    __global const float2* roomVertices,    // all room verts, flattened
    __global const int*    roomVertStart,   // start index into roomVertices[]
    __global const int*    roomVertCount,   // vertex count per room
    __global const float4* roomFillColors,  // fill colour per room
    __global const float4* roomBounds,      // (minX, minY, maxX, maxY) per room
    const int              totalRoomCount,

    // ── segment data (grouped by building) ──
    __global const float4* segments,        // (x0, y0, x1, y1) per segment
    __global const float4* segColors,       // RGBA per segment
    __global const float*  segHalfThick,    // half-thickness (cells) per segment
    const int              segCount,

    // ── per-building culling data ──
    __global const float4* buildingBounds,  // (minX, minY, maxX, maxY) per bldg
    __global const int*    buildingSegStart,// first segment index per building
    __global const int*    buildingSegEnd,  // one-past-last segment index
    const int              buildingCount,

    // ── output ──
    const int              latRes,
    const int              lonRes,
    __global float4*       output)
{
    int lat = get_global_id(0);
    int lon = get_global_id(1);
    if (lat >= latRes || lon >= lonRes) return;

    int   idx = lat * lonRes + lon;
    float px  = (float)lat;
    float py  = (float)lon;

    // Start with a visible background (light parchment) so the layer
    // is not solid-black when viewed on its own.
    float4 result = (float4)(0.88f, 0.85f, 0.78f, 1.0f);

    // ── 1. Room fills (background) ──────────────────────────────────────────
    for (int r = 0; r < totalRoomCount; r++) {
        float4 rb = roomBounds[r]; // (minX, minY, maxX, maxY)
        if (px < rb.x || px > rb.z || py < rb.y || py > rb.w)
            continue;

        float2 p = (float2)(px, py);
        if (pointInPolygon(p, roomVertices, roomVertStart[r], roomVertCount[r])) {
            float4 fc = roomFillColors[r];
            // Alpha-composite room fill over current result
            float srcA = clamp(fc.w, 0.0f, 1.0f);
            result.xyz = fc.xyz * srcA + result.xyz * (1.0f - srcA);
            result.w   = srcA + result.w * (1.0f - srcA);
        }
    }

    // ── 2. Segments (foreground) ────────────────────────────────────────────
    float  bestDistSq = 1e20f;
    float4 bestColor  = (float4)(0.0f, 0.0f, 0.0f, 0.0f);

    for (int b = 0; b < buildingCount; b++) {
        float4 bb = buildingBounds[b];
        float  margin = 20.0f; // cells — must cover thickest wall at high cellsPerMeter
        if (px < bb.x - margin || px > bb.z + margin ||
            py < bb.y - margin || py > bb.w + margin)
            continue;

        int sStart = buildingSegStart[b];
        int sEnd   = buildingSegEnd[b];
        for (int s = sStart; s < sEnd; s++) {
            float4 seg = segments[s];
            float2 a   = (float2)(seg.x, seg.y);
            float2 bpt = (float2)(seg.z, seg.w);
            float2 p   = (float2)(px, py);

            float dSq = distToSegmentSq(p, a, bpt);
            float ht  = segHalfThick[s];
            float threshSq = ht * ht;

            if (dSq < threshSq && dSq <= bestDistSq) {
                bestDistSq = dSq;
                bestColor  = segColors[s];
            }
        }
    }

    // Alpha-composite closest segment over room fill.
    if (bestDistSq < 1e19f) {
        float srcA = clamp(bestColor.w, 0.0f, 1.0f);
        result.xyz = bestColor.xyz * srcA + result.xyz * (1.0f - srcA);
        result.w   = srcA + result.w * (1.0f - srcA);
    }

    output[idx] = result;
}
