#pragma once
#include <CharacterEditor/IKSolver.hpp>
#include <CL/cl.h>
#include <vector>
#include <cstdint>
#include <string>

namespace CharacterEditor {

/**
 * @brief Entry for a batched GPU IK solve.
 *
 * Each entry represents one skeleton/chain/target combination to solve.
 * Multiple entries from different skeletons can be mixed freely.
 */
struct GPUIKBatchEntry {
    const Skeleton* skeleton = nullptr;
    const IKChain* chain = nullptr;
    IKTarget target;
};

/**
 * @brief GPU-accelerated batched FABRIK solver using OpenCL.
 *
 * Solves many IK chains in parallel on the GPU using ping-pong
 * position buffers for forward/backward reaching passes.
 *
 * Chains are batched based on available GPU compute resources.
 * If the total number of chains exceeds the batch size, they are
 * processed in sequential batches.
 *
 * Data flow per batch:
 *   1. Pack chain data into flat host arrays (positions, lengths, targets, roots)
 *   2. Upload to GPU buffer A (ping)
 *   3. Prepare pass: stretch unreachable chains in-place, set skip flags
 *   4. For each iteration:
 *        Forward  pass: buffer A (ping) -> buffer B (pong)
 *        Backward pass: buffer B (pong) -> buffer A (ping)
 *   5. Error kernel: compute tip-to-target distance in buffer A
 *   6. Read back positions + errors, convert to local transforms on CPU
 *
 * Usage:
 * @code
 *   GPUIKSolver solver;
 *   solver.initialize();
 *
 *   std::vector<GPUIKBatchEntry> entries;
 *   // ... populate entries for many characters ...
 *
 *   auto results = solver.solveBatch(entries);
 *   for (size_t i = 0; i < results.size(); ++i) {
 *       FABRIKSolver::applyResult(skeleton, *entries[i].chain, results[i]);
 *   }
 * @endcode
 */
class GPUIKSolver {
public:
    GPUIKSolver();
    ~GPUIKSolver();

    // Non-copyable
    GPUIKSolver(const GPUIKSolver&) = delete;
    GPUIKSolver& operator=(const GPUIKSolver&) = delete;

    /**
     * @brief Initialize OpenCL program and kernels.
     * @return true if initialization succeeded
     */
    bool initialize();

    /**
     * @brief Release all OpenCL resources.
     */
    void shutdown();

    /** @brief Set convergence tolerance (distance threshold). */
    void setTolerance(float tolerance) { m_tolerance = tolerance; }

    /** @brief Set maximum FABRIK iterations per solve. */
    void setMaxIterations(int maxIter) { m_maxIterations = maxIter; }

    /**
     * @brief Set explicit batch size (0 = auto-detect from GPU capabilities).
     *
     * The batch size determines how many chains are dispatched in a single
     * GPU kernel invocation.  Chains beyond this count are processed in
     * sequential batches.
     */
    void setBatchSize(uint32_t batchSize) { m_batchSize = batchSize; }

    /**
     * @brief Solve a batch of IK chains on the GPU.
     *
     * Chains are solved in parallel.  If the number of entries exceeds
     * the batch size, they are split into sequential GPU dispatches.
     *
     * The forward/backward passes use ping-pong GPU buffers to avoid
     * read-after-write hazards.
     *
     * @param entries Vector of skeleton/chain/target entries to solve
     * @return Vector of solve results, one per entry (same order)
     */
    std::vector<IKSolveResult> solveBatch(const std::vector<GPUIKBatchEntry>& entries);

    /** @brief Check if the solver is initialized and ready. */
    bool isInitialized() const { return m_initialized; }

    /** @brief Get the computed batch size (after auto-detection or manual set). */
    uint32_t getEffectiveBatchSize() const;

    /** @brief Get GPU device info string for debugging. */
    std::string getDeviceInfo() const;

private:
    /**
     * @brief Pack a range of entries into flat host-side staging buffers.
     * @return Total number of joints across all chains in this sub-batch
     */
    uint32_t packBatchData(const std::vector<GPUIKBatchEntry>& entries,
                           uint32_t startIdx, uint32_t count);

    /**
     * @brief Ensure GPU buffers are large enough for the given sizes.
     * Reallocates only when current capacity is insufficient.
     */
    void ensureBuffers(uint32_t totalJoints, uint32_t totalSegments,
                       uint32_t chainCount);

    /** @brief Release all GPU buffers. */
    void releaseBuffers();

    /**
     * @brief Solve one sub-batch already packed in staging buffers.
     *
     * Dispatches the prepare, forward/backward iteration, and error
     * kernels, then reads back results and converts to local transforms.
     */
    void solveBatchGPU(uint32_t chainCount, uint32_t totalJoints,
                       uint32_t totalSegments,
                       std::vector<IKSolveResult>& outResults,
                       const std::vector<GPUIKBatchEntry>& entries,
                       uint32_t startIdx);

    /** @brief Compute auto batch size from GPU device capabilities. */
    uint32_t computeAutoBatchSize() const;

    // ---- OpenCL resources ----
    cl_program m_program        = nullptr;
    cl_kernel  m_prepareKernel  = nullptr;
    cl_kernel  m_forwardKernel  = nullptr;
    cl_kernel  m_backwardKernel = nullptr;
    cl_kernel  m_errorKernel    = nullptr;

    // Ping-pong position buffers
    cl_mem m_posA = nullptr;            // positions buffer A (ping)
    cl_mem m_posB = nullptr;            // positions buffer B (pong)

    // Metadata buffers
    cl_mem m_gpuLengths    = nullptr;   // segment lengths      [totalSegments]
    cl_mem m_gpuTargets    = nullptr;   // target positions     [chainCount * 3]
    cl_mem m_gpuRoots      = nullptr;   // root positions       [chainCount * 3]
    cl_mem m_gpuChainLens  = nullptr;   // joints per chain     [chainCount]
    cl_mem m_gpuPosOffsets = nullptr;   // joint offset/chain   [chainCount]
    cl_mem m_gpuLenOffsets = nullptr;   // length offset/chain  [chainCount]
    cl_mem m_gpuTotalLens  = nullptr;   // total reach/chain    [chainCount]
    cl_mem m_gpuFlags      = nullptr;   // per-chain flags      [chainCount]
    cl_mem m_gpuErrors     = nullptr;   // per-chain errors     [chainCount]

    // Current buffer capacities (element counts, not bytes)
    uint32_t m_capJoints   = 0;
    uint32_t m_capSegments = 0;
    uint32_t m_capChains   = 0;

    // Host-side staging buffers (reused across batches to avoid allocs)
    std::vector<float>    m_hostPositions;   // [totalJoints * 3]
    std::vector<float>    m_hostLengths;     // [totalSegments]
    std::vector<float>    m_hostTargets;     // [chainCount * 3]
    std::vector<float>    m_hostRoots;       // [chainCount * 3]
    std::vector<uint32_t> m_hostChainLens;   // [chainCount]
    std::vector<uint32_t> m_hostPosOffsets;  // [chainCount]
    std::vector<uint32_t> m_hostLenOffsets;  // [chainCount]
    std::vector<float>    m_hostTotalLens;   // [chainCount]

    // Solver settings
    float    m_tolerance     = 0.001f;
    int      m_maxIterations = 10;
    uint32_t m_batchSize     = 0;       // 0 = auto

    bool m_initialized = false;
};

} // namespace CharacterEditor
