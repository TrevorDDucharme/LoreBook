#pragma once
#include <WorldMaps/World/Chunk.hpp>
#include <OpenCLContext.hpp>
#include <vector>
#include <tracy/Tracy.hpp>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/// Assembles chunk GPU buffers into a single viewport-sized buffer.
///
/// Each chunk is a small (CHUNK_BASE_RES × CHUNK_BASE_RES) cl_mem.
/// The assembler copies each chunk into the correct sub-rectangle of
/// a larger viewport buffer using an OpenCL kernel.
class ChunkAssembler {
public:
    /// Information about one chunk to be assembled.
    struct ChunkEntry {
        ChunkCoord coord;
        cl_mem      buffer; // RGBA float4 or scalar float
        int         resX;   // actual resolution of the chunk buffer (columns)
        int         resY;   // actual resolution of the chunk buffer (rows)
    };

    /// Assemble RGBA chunk buffers into a viewport buffer.
    ///
    /// @param chunks       List of chunks with their buffers and resolutions.
    /// @param outputBuffer In/out: the assembled viewport buffer (reallocated if needed).
    /// @param viewportW    Viewport width in texels.
    /// @param viewportH    Viewport height in texels.
    /// @param viewLonMin   Viewport west edge (degrees).
    /// @param viewLonMax   Viewport east edge (degrees).
    /// @param viewLatMin   Viewport south edge (degrees).
    /// @param viewLatMax   Viewport north edge (degrees).
    static void assembleRGBA(
        const std::vector<ChunkEntry>& chunks,
        cl_mem& outputBuffer,
        int viewportW, int viewportH,
        float viewLonMin, float viewLonMax,
        float viewLatMin, float viewLatMax)
    {
        ZoneScopedN("ChunkAssembler::assembleRGBA");
        if (!OpenCLContext::get().isReady()) return;

        cl_int err = CL_SUCCESS;

        // Ensure output buffer is large enough
        size_t outSize = static_cast<size_t>(viewportW) * viewportH * sizeof(cl_float4);
        ensureBuffer(outputBuffer, outSize);

        // Clear the viewport buffer to transparent black
        clearViewport(outputBuffer, viewportW, viewportH);

        // Copy each chunk into the correct position
        for (const auto& chunk : chunks) {
            if (!chunk.buffer) continue;

            // Compute chunk bounds in degrees
            float cLonMin, cLonMax, cLatMin, cLatMax;
            chunk.coord.getBoundsDegrees(cLonMin, cLonMax, cLatMin, cLatMax);

            // Compute destination position in viewport texels
            float viewLonSpan = viewLonMax - viewLonMin;
            float viewLatSpan = viewLatMax - viewLatMin;
            if (viewLonSpan <= 0.0f || viewLatSpan <= 0.0f) continue;

            // destX = fraction along viewport width where chunk starts
            int destX = static_cast<int>(std::round(
                (cLonMin - viewLonMin) / viewLonSpan * viewportW));
            // destY = fraction along viewport height (north = row 0)
            int destY = static_cast<int>(std::round(
                (viewLatMax - cLatMax) / viewLatSpan * viewportH));

            copyChunkToViewport(
                chunk.buffer, chunk.resX, chunk.resY,
                outputBuffer, viewportW, viewportH,
                destX, destY);
        }

        // Flush the queue
        clFinish(OpenCLContext::get().getQueue());
    }

    /// Same as assembleRGBA but for scalar (float) buffers.
    static void assembleScalar(
        const std::vector<ChunkEntry>& chunks,
        cl_mem& outputBuffer,
        int viewportW, int viewportH,
        float viewLonMin, float viewLonMax,
        float viewLatMin, float viewLatMax)
    {
        ZoneScopedN("ChunkAssembler::assembleScalar");
        if (!OpenCLContext::get().isReady()) return;

        cl_int err = CL_SUCCESS;
        size_t outSize = static_cast<size_t>(viewportW) * viewportH * sizeof(float);
        ensureBuffer(outputBuffer, outSize);

        // Clear (zero fill)
        float zero = 0.0f;
        err = clEnqueueFillBuffer(OpenCLContext::get().getQueue(), outputBuffer,
                                   &zero, sizeof(float), 0, outSize,
                                   0, nullptr, nullptr);

        for (const auto& chunk : chunks) {
            if (!chunk.buffer) continue;

            float cLonMin, cLonMax, cLatMin, cLatMax;
            chunk.coord.getBoundsDegrees(cLonMin, cLonMax, cLatMin, cLatMax);

            float viewLonSpan = viewLonMax - viewLonMin;
            float viewLatSpan = viewLatMax - viewLatMin;
            if (viewLonSpan <= 0.0f || viewLatSpan <= 0.0f) continue;

            int destX = static_cast<int>(std::round(
                (cLonMin - viewLonMin) / viewLonSpan * viewportW));
            int destY = static_cast<int>(std::round(
                (viewLatMax - cLatMax) / viewLatSpan * viewportH));

            copyChunkToViewportScalar(
                chunk.buffer, chunk.resX, chunk.resY,
                outputBuffer, viewportW, viewportH,
                destX, destY);
        }

        clFinish(OpenCLContext::get().getQueue());
    }

private:
    /// Ensure a cl_mem buffer is at least `minSize` bytes.
    static void ensureBuffer(cl_mem& buf, size_t minSize) {
        if (buf) {
            size_t existing = 0;
            clGetMemObjectInfo(buf, CL_MEM_SIZE, sizeof(existing), &existing, nullptr);
            if (existing >= minSize) return;
            OpenCLContext::get().releaseMem(buf);
            buf = nullptr;
        }
        cl_int err = CL_SUCCESS;
        buf = OpenCLContext::get().createBuffer(CL_MEM_READ_WRITE, minSize,
                                                nullptr, &err, "viewport assemble buf");
    }

    /// Clear viewport to transparent black using the CL kernel.
    static void clearViewport(cl_mem buf, int w, int h) {
        ZoneScopedN("ChunkAssembler::clearViewport");
        static cl_program prog = nullptr;
        static cl_kernel  kern = nullptr;

        try {
            OpenCLContext::get().createProgram(prog, "Kernels/ChunkAssemble.cl");
            OpenCLContext::get().createKernelFromProgram(kern, prog, "clear_viewport_rgba");
        } catch (...) { return; }

        cl_float4 clearColor = {0.0f, 0.0f, 0.0f, 0.0f};
        clSetKernelArg(kern, 0, sizeof(cl_mem), &buf);
        clSetKernelArg(kern, 1, sizeof(int), &w);
        clSetKernelArg(kern, 2, sizeof(int), &h);
        clSetKernelArg(kern, 3, sizeof(cl_float4), &clearColor);

        size_t global[2] = { static_cast<size_t>(w), static_cast<size_t>(h) };
        clEnqueueNDRangeKernel(OpenCLContext::get().getQueue(), kern, 2,
                               nullptr, global, nullptr, 0, nullptr, nullptr);
    }

    /// Copy one RGBA chunk into the viewport buffer.
    static void copyChunkToViewport(cl_mem chunkBuf, int chunkW, int chunkH,
                                     cl_mem viewportBuf, int vpW, int vpH,
                                     int destX, int destY) {
        static cl_program prog = nullptr;
        static cl_kernel  kern = nullptr;

        try {
            OpenCLContext::get().createProgram(prog, "Kernels/ChunkAssemble.cl");
            OpenCLContext::get().createKernelFromProgram(kern, prog, "copy_chunk_to_viewport_rgba");
        } catch (...) { return; }

        clSetKernelArg(kern, 0, sizeof(cl_mem), &chunkBuf);
        clSetKernelArg(kern, 1, sizeof(int), &chunkW);
        clSetKernelArg(kern, 2, sizeof(int), &chunkH);
        clSetKernelArg(kern, 3, sizeof(cl_mem), &viewportBuf);
        clSetKernelArg(kern, 4, sizeof(int), &vpW);
        clSetKernelArg(kern, 5, sizeof(int), &vpH);
        clSetKernelArg(kern, 6, sizeof(int), &destX);
        clSetKernelArg(kern, 7, sizeof(int), &destY);

        size_t global[2] = { static_cast<size_t>(chunkW), static_cast<size_t>(chunkH) };
        clEnqueueNDRangeKernel(OpenCLContext::get().getQueue(), kern, 2,
                               nullptr, global, nullptr, 0, nullptr, nullptr);
    }

    /// Copy one scalar chunk into the viewport buffer.
    static void copyChunkToViewportScalar(cl_mem chunkBuf, int chunkW, int chunkH,
                                           cl_mem viewportBuf, int vpW, int vpH,
                                           int destX, int destY) {
        static cl_program prog = nullptr;
        static cl_kernel  kern = nullptr;

        try {
            OpenCLContext::get().createProgram(prog, "Kernels/ChunkAssemble.cl");
            OpenCLContext::get().createKernelFromProgram(kern, prog, "copy_chunk_to_viewport_scalar");
        } catch (...) { return; }

        clSetKernelArg(kern, 0, sizeof(cl_mem), &chunkBuf);
        clSetKernelArg(kern, 1, sizeof(int), &chunkW);
        clSetKernelArg(kern, 2, sizeof(int), &chunkH);
        clSetKernelArg(kern, 3, sizeof(cl_mem), &viewportBuf);
        clSetKernelArg(kern, 4, sizeof(int), &vpW);
        clSetKernelArg(kern, 5, sizeof(int), &vpH);
        clSetKernelArg(kern, 6, sizeof(int), &destX);
        clSetKernelArg(kern, 7, sizeof(int), &destY);

        size_t global[2] = { static_cast<size_t>(chunkW), static_cast<size_t>(chunkH) };
        clEnqueueNDRangeKernel(OpenCLContext::get().getQueue(), kern, 2,
                               nullptr, global, nullptr, 0, nullptr, nullptr);
    }
};
