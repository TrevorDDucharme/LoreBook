/// Copy a small chunk buffer into a sub-rectangle of a larger viewport buffer.
///
/// The chunk data is row-major float4 (RGBA).  destX / destY specify the
/// top-left corner in the viewport where this chunk should be placed.
///
/// NDRange = {chunkW, chunkH}

__kernel void copy_chunk_to_viewport_rgba(
    __global const float4* chunkData,
    int chunkW,
    int chunkH,
    __global float4* viewportData,
    int viewportW,
    int viewportH,
    int destX,
    int destY)
{
    int cx = get_global_id(0);
    int cy = get_global_id(1);

    if (cx >= chunkW || cy >= chunkH)
        return;

    int dx = destX + cx;
    int dy = destY + cy;

    if (dx < 0 || dx >= viewportW || dy < 0 || dy >= viewportH)
        return;

    viewportData[dy * viewportW + dx] = chunkData[cy * chunkW + cx];
}

/// Scalar (float) variant for sample data.
__kernel void copy_chunk_to_viewport_scalar(
    __global const float* chunkData,
    int chunkW,
    int chunkH,
    __global float* viewportData,
    int viewportW,
    int viewportH,
    int destX,
    int destY)
{
    int cx = get_global_id(0);
    int cy = get_global_id(1);

    if (cx >= chunkW || cy >= chunkH)
        return;

    int dx = destX + cx;
    int dy = destY + cy;

    if (dx < 0 || dx >= viewportW || dy < 0 || dy >= viewportH)
        return;

    viewportData[dy * viewportW + dx] = chunkData[cy * chunkW + cx];
}

/// Clear a viewport buffer to a solid RGBA color (typically transparent black).
/// NDRange = {viewportW, viewportH}
__kernel void clear_viewport_rgba(
    __global float4* viewportData,
    int viewportW,
    int viewportH,
    float4 clearColor)
{
    int x = get_global_id(0);
    int y = get_global_id(1);

    if (x >= viewportW || y >= viewportH)
        return;

    viewportData[y * viewportW + x] = clearColor;
}
