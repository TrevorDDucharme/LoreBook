/**
 * FABRIKSolver.cl - GPU-accelerated batched FABRIK IK solver
 *
 * Solves many IK chains in parallel using ping-pong position buffers
 * for the forward/backward reaching passes.
 *
 * Data layout (all flat float/uint arrays):
 *   positions : float[totalJoints * 3]   - packed (x,y,z) world positions
 *   lengths   : float[totalSegments]     - segment lengths between consecutive joints
 *   targets   : float[totalChains * 3]   - per-chain target positions
 *   roots     : float[totalChains * 3]   - per-chain original root positions
 *   chainLens : uint [totalChains]       - number of joints per chain
 *   posOffsets: uint [totalChains]       - offset (in joints) into positions array
 *   lenOffsets: uint [totalChains]       - offset into lengths array
 *   totalLens : float[totalChains]       - total reach per chain (sum of segment lengths)
 *   flags     : uint [totalChains]       - per-chain flags (1 = unreachable/skip)
 *   errors    : float[totalChains]       - per-chain tip-to-target error
 *
 * Dispatch: 1D global size = totalChains.  One work-item per chain.
 */

// ---------------------------------------------------------------------------
// Helpers - read / write packed vec3 from flat float buffers
// ---------------------------------------------------------------------------

inline float3 readJoint(__global const float* buf, uint jointIdx) {
    uint b = jointIdx * 3;
    return (float3)(buf[b], buf[b + 1], buf[b + 2]);
}

inline void writeJoint(__global float* buf, uint jointIdx, float3 v) {
    uint b = jointIdx * 3;
    buf[b]     = v.x;
    buf[b + 1] = v.y;
    buf[b + 2] = v.z;
}

inline float3 readChainVec(__global const float* buf, uint chainIdx) {
    uint b = chainIdx * 3;
    return (float3)(buf[b], buf[b + 1], buf[b + 2]);
}

// ---------------------------------------------------------------------------
// fabrik_prepare - pre-pass: handle unreachable targets
// ---------------------------------------------------------------------------
// For unreachable chains: stretches the chain toward the target in-place
//   in the positions buffer and sets flags[gid] = 1.
// For reachable chains: leaves positions unchanged, sets flags[gid] = 0.
// Degenerate chains (< 2 joints): marked with flag = 1 (skipped).
// ---------------------------------------------------------------------------
__kernel void fabrik_prepare(
    __global float*       positions,   // [totalJoints*3]  modified in-place for unreachable
    __global const float* lengths,     // [totalSegments]
    __global const float* targets,     // [totalChains*3]
    __global const float* roots,       // [totalChains*3]
    __global const float* totalLens,   // [totalChains]
    __global uint*        flags,       // [totalChains]    output
    __global const uint*  chainLens,   // [totalChains]
    __global const uint*  posOffsets,  // [totalChains]
    __global const uint*  lenOffsets,  // [totalChains]
    const uint            totalChains)
{
    uint gid = get_global_id(0);
    if (gid >= totalChains) return;

    uint nJoints = chainLens[gid];
    uint pOff    = posOffsets[gid];
    uint lOff    = lenOffsets[gid];

    if (nJoints < 2) {
        flags[gid] = 1;  // degenerate, skip
        return;
    }

    float3 root   = readChainVec(roots,   gid);
    float3 target = readChainVec(targets, gid);
    float  dist   = length(target - root);

    if (dist > totalLens[gid]) {
        // Unreachable: stretch toward target in-place
        float3 dir = normalize(target - root);
        writeJoint(positions, pOff, root);
        for (uint i = 1; i < nJoints; ++i) {
            float3 prev = readJoint(positions, pOff + i - 1);
            writeJoint(positions, pOff + i, prev + dir * lengths[lOff + i - 1]);
        }
        flags[gid] = 1;
    } else {
        // Reachable: leave positions as-is for the iteration loop
        flags[gid] = 0;
    }
}

// ---------------------------------------------------------------------------
// fabrik_forward - forward reaching pass (tip to root)
// ---------------------------------------------------------------------------
// Reads from posIn (ping buffer), writes to posOut (pong buffer).
// Sets the tip joint to the target and sweeps backward, preserving
// segment lengths.  Skips chains with flags != 0.
// ---------------------------------------------------------------------------
__kernel void fabrik_forward(
    __global const float* posIn,       // [totalJoints*3]  read  (ping)
    __global float*       posOut,      // [totalJoints*3]  write (pong)
    __global const float* lengths,     // [totalSegments]
    __global const float* targets,     // [totalChains*3]
    __global const uint*  flags,       // [totalChains]
    __global const uint*  chainLens,   // [totalChains]
    __global const uint*  posOffsets,  // [totalChains]
    __global const uint*  lenOffsets,  // [totalChains]
    const uint            totalChains)
{
    uint gid = get_global_id(0);
    if (gid >= totalChains) return;
    if (flags[gid] != 0) return;       // skip unreachable / degenerate

    uint nJoints = chainLens[gid];
    uint pOff    = posOffsets[gid];
    uint lOff    = lenOffsets[gid];

    // Copy current positions to output buffer
    for (uint i = 0; i < nJoints; ++i)
        writeJoint(posOut, pOff + i, readJoint(posIn, pOff + i));

    // Set tip to target
    float3 target = readChainVec(targets, gid);
    writeJoint(posOut, pOff + nJoints - 1, target);

    // Backward sweep: from second-to-last toward root
    for (int i = (int)nJoints - 2; i >= 0; --i) {
        float3 cur  = readJoint(posOut, pOff + (uint)i);
        float3 next = readJoint(posOut, pOff + (uint)i + 1);
        float3 dir  = cur - next;
        float  d    = length(dir);
        if (d > 1e-7f)
            dir = dir / d;
        else
            dir = (float3)(0.0f, 1.0f, 0.0f);  // safe fallback
        writeJoint(posOut, pOff + (uint)i, next + dir * lengths[lOff + (uint)i]);
    }
}

// ---------------------------------------------------------------------------
// fabrik_backward - backward reaching pass (root to tip)
// ---------------------------------------------------------------------------
// Reads from posIn (pong buffer), writes to posOut (ping buffer).
// Snaps the root back to its original position and sweeps forward,
// preserving segment lengths.  Skips chains with flags != 0.
// ---------------------------------------------------------------------------
__kernel void fabrik_backward(
    __global const float* posIn,       // [totalJoints*3]  read  (pong)
    __global float*       posOut,      // [totalJoints*3]  write (ping)
    __global const float* lengths,     // [totalSegments]
    __global const float* roots,       // [totalChains*3]
    __global const uint*  flags,       // [totalChains]
    __global const uint*  chainLens,   // [totalChains]
    __global const uint*  posOffsets,  // [totalChains]
    __global const uint*  lenOffsets,  // [totalChains]
    const uint            totalChains)
{
    uint gid = get_global_id(0);
    if (gid >= totalChains) return;
    if (flags[gid] != 0) return;

    uint nJoints = chainLens[gid];
    uint pOff    = posOffsets[gid];
    uint lOff    = lenOffsets[gid];

    // Copy current positions to output buffer
    for (uint i = 0; i < nJoints; ++i)
        writeJoint(posOut, pOff + i, readJoint(posIn, pOff + i));

    // Snap root back to its original position
    float3 root = readChainVec(roots, gid);
    writeJoint(posOut, pOff, root);

    // Forward sweep: second joint toward tip
    for (uint i = 1; i < nJoints; ++i) {
        float3 cur  = readJoint(posOut, pOff + i);
        float3 prev = readJoint(posOut, pOff + i - 1);
        float3 dir  = cur - prev;
        float  d    = length(dir);
        if (d > 1e-7f)
            dir = dir / d;
        else
            dir = (float3)(0.0f, 1.0f, 0.0f);
        writeJoint(posOut, pOff + i, prev + dir * lengths[lOff + i - 1]);
    }
}

// ---------------------------------------------------------------------------
// fabrik_error - compute per-chain tip-to-target distance
// ---------------------------------------------------------------------------
// Writes the Euclidean distance between the chain tip and the target into
// errors[gid].  The host can read this back for convergence reporting.
// ---------------------------------------------------------------------------
__kernel void fabrik_error(
    __global const float* positions,   // [totalJoints*3]
    __global const float* targets,     // [totalChains*3]
    __global float*       errors,      // [totalChains]  output
    __global const uint*  chainLens,   // [totalChains]
    __global const uint*  posOffsets,  // [totalChains]
    const uint            totalChains)
{
    uint gid = get_global_id(0);
    if (gid >= totalChains) return;

    uint nJoints = chainLens[gid];
    uint pOff    = posOffsets[gid];

    float3 tip    = readJoint(positions, pOff + nJoints - 1);
    float3 target = readChainVec(targets, gid);

    errors[gid] = length(tip - target);
}