#include <CharacterEditor/GPUIKSolver.hpp>
#include <OpenCLContext.hpp>
#include <plog/Log.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>
#include <sstream>

namespace CharacterEditor {

// ---------------------------------------------------------------------------
// CPU helper – identical to FABRIKSolver::positionsToTransforms but free-
// standing so GPUIKSolver doesn't depend on FABRIKSolver internals.
// ---------------------------------------------------------------------------
static std::vector<Transform> positionsToLocalTransforms(
    const Skeleton& skeleton,
    const IKChain& chain,
    const std::vector<glm::vec3>& positions)
{
    std::vector<Transform> transforms;
    transforms.reserve(chain.length());

    // Mutable working copy so each bone's rotation cascades to children.
    Skeleton working = skeleton;

    for (size_t i = 0; i < chain.length(); ++i) {
        uint32_t boneIdx = chain.boneIndices[i];
        const Bone& bone = working.bones[boneIdx];
        Transform localTrans = bone.localTransform;

        if (i < chain.length() - 1) {
            Transform thisWorld = working.getWorldTransform(boneIdx);
            Transform nextWorld = working.getWorldTransform(chain.boneIndices[i + 1]);
            glm::vec3 currentDir = glm::normalize(nextWorld.position - thisWorld.position);
            glm::vec3 targetDir  = glm::normalize(positions[i + 1] - positions[i]);

            if (glm::length(currentDir) > 0.0001f && glm::length(targetDir) > 0.0001f) {
                float dot = glm::clamp(glm::dot(currentDir, targetDir), -1.0f, 1.0f);

                if (dot < 0.9999f) {
                    glm::vec3 axis = glm::cross(currentDir, targetDir);
                    if (glm::length(axis) > 0.0001f) {
                        axis = glm::normalize(axis);
                        float angle = std::acos(dot);

                        glm::quat worldDelta = glm::angleAxis(angle, axis);

                        glm::quat parentWorldRot = glm::quat(1, 0, 0, 0);
                        if (bone.parentID != UINT32_MAX) {
                            parentWorldRot = working.getWorldTransform(bone.parentID).rotation;
                        }
                        glm::quat invParent  = glm::inverse(parentWorldRot);
                        glm::quat localDelta = invParent * worldDelta * parentWorldRot;

                        localTrans.rotation = localDelta * localTrans.rotation;
                        working.bones[boneIdx].localTransform.rotation = localTrans.rotation;
                    }
                }
            }
        }

        transforms.push_back(localTrans);
    }

    return transforms;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

GPUIKSolver::GPUIKSolver() = default;

GPUIKSolver::~GPUIKSolver() {
    shutdown();
}

// ---------------------------------------------------------------------------
// Initialization
// ---------------------------------------------------------------------------

bool GPUIKSolver::initialize() {
    ZoneScopedN("GPUIKSolver::initialize");

    if (m_initialized) return true;

    auto& cl = OpenCLContext::get();
    if (!cl.isReady()) {
        PLOGE << "GPUIKSolver: OpenCL not initialized";
        return false;
    }

    try {
        cl.createProgram(m_program, "Kernels/FABRIKSolver.cl");
        cl.createKernelFromProgram(m_prepareKernel,  m_program, "fabrik_prepare");
        cl.createKernelFromProgram(m_forwardKernel,  m_program, "fabrik_forward");
        cl.createKernelFromProgram(m_backwardKernel, m_program, "fabrik_backward");
        cl.createKernelFromProgram(m_errorKernel,    m_program, "fabrik_error");
    } catch (const std::runtime_error& e) {
        PLOGE << "GPUIKSolver: Failed to create OpenCL kernels: " << e.what();
        shutdown();
        return false;
    }

    m_initialized = true;
    PLOGI << "GPUIKSolver: Initialized. " << getDeviceInfo();
    return true;
}

void GPUIKSolver::shutdown() {
    releaseBuffers();

    if (m_errorKernel)    { clReleaseKernel(m_errorKernel);    m_errorKernel    = nullptr; }
    if (m_backwardKernel) { clReleaseKernel(m_backwardKernel); m_backwardKernel = nullptr; }
    if (m_forwardKernel)  { clReleaseKernel(m_forwardKernel);  m_forwardKernel  = nullptr; }
    if (m_prepareKernel)  { clReleaseKernel(m_prepareKernel);  m_prepareKernel  = nullptr; }
    if (m_program)        { clReleaseProgram(m_program);       m_program        = nullptr; }

    m_initialized = false;
}

// ---------------------------------------------------------------------------
// Device query helpers
// ---------------------------------------------------------------------------

uint32_t GPUIKSolver::computeAutoBatchSize() const {
    auto& cl = OpenCLContext::get();
    if (!cl.isReady()) return 1024;

    cl_uint computeUnits = 1;
    clGetDeviceInfo(cl.getDevice(), CL_DEVICE_MAX_COMPUTE_UNITS,
                    sizeof(computeUnits), &computeUnits, nullptr);

    size_t maxWGSize = 256;
    clGetDeviceInfo(cl.getDevice(), CL_DEVICE_MAX_WORK_GROUP_SIZE,
                    sizeof(maxWGSize), &maxWGSize, nullptr);

    // Aim for good occupancy: CUs * warpSize * multiplier
    uint32_t wg = static_cast<uint32_t>(std::min(maxWGSize, size_t(256)));
    uint32_t batch = computeUnits * wg * 4;
    return std::max(batch, 256u);
}

uint32_t GPUIKSolver::getEffectiveBatchSize() const {
    if (m_batchSize > 0) return m_batchSize;
    return computeAutoBatchSize();
}

std::string GPUIKSolver::getDeviceInfo() const {
    auto& cl = OpenCLContext::get();
    if (!cl.isReady()) return "OpenCL not ready";

    char name[256] = {};
    clGetDeviceInfo(cl.getDevice(), CL_DEVICE_NAME, sizeof(name), name, nullptr);

    cl_uint cu = 0;
    clGetDeviceInfo(cl.getDevice(), CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cu), &cu, nullptr);

    size_t maxWG = 0;
    clGetDeviceInfo(cl.getDevice(), CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(maxWG), &maxWG, nullptr);

    std::ostringstream oss;
    oss << "Device: " << name
        << ", CUs: " << cu
        << ", MaxWG: " << maxWG
        << ", BatchSize: " << getEffectiveBatchSize();
    return oss.str();
}

// ---------------------------------------------------------------------------
// Host-side data packing
// ---------------------------------------------------------------------------

uint32_t GPUIKSolver::packBatchData(const std::vector<GPUIKBatchEntry>& entries,
                                     uint32_t startIdx, uint32_t count) {
    ZoneScopedN("GPUIKSolver::packBatchData");

    // First pass: compute totals
    uint32_t totalJoints   = 0;
    uint32_t totalSegments = 0;

    for (uint32_t i = 0; i < count; ++i) {
        uint32_t n = static_cast<uint32_t>(entries[startIdx + i].chain->boneIndices.size());
        totalJoints   += n;
        totalSegments += (n > 1) ? n - 1 : 0;
    }

    // Resize staging buffers
    m_hostPositions.resize(static_cast<size_t>(totalJoints) * 3);
    m_hostLengths.resize(totalSegments);
    m_hostTargets.resize(static_cast<size_t>(count) * 3);
    m_hostRoots.resize(static_cast<size_t>(count) * 3);
    m_hostChainLens.resize(count);
    m_hostPosOffsets.resize(count);
    m_hostLenOffsets.resize(count);
    m_hostTotalLens.resize(count);

    // Second pass: fill data
    uint32_t jointCursor = 0;
    uint32_t segCursor   = 0;

    for (uint32_t ci = 0; ci < count; ++ci) {
        const auto& entry  = entries[startIdx + ci];
        const Skeleton& skel  = *entry.skeleton;
        const IKChain&  chain = *entry.chain;
        uint32_t n = static_cast<uint32_t>(chain.boneIndices.size());

        m_hostChainLens[ci]  = n;
        m_hostPosOffsets[ci] = jointCursor;
        m_hostLenOffsets[ci] = segCursor;

        // Target position
        m_hostTargets[ci * 3 + 0] = entry.target.position.x;
        m_hostTargets[ci * 3 + 1] = entry.target.position.y;
        m_hostTargets[ci * 3 + 2] = entry.target.position.z;

        // Extract world positions and compute segment lengths
        float totalLen = 0.0f;
        glm::vec3 prevPos(0.0f);

        for (uint32_t j = 0; j < n; ++j) {
            Transform world = skel.getWorldTransform(chain.boneIndices[j]);
            glm::vec3 pos   = world.position;

            size_t base = static_cast<size_t>(jointCursor + j) * 3;
            m_hostPositions[base + 0] = pos.x;
            m_hostPositions[base + 1] = pos.y;
            m_hostPositions[base + 2] = pos.z;

            if (j == 0) {
                m_hostRoots[ci * 3 + 0] = pos.x;
                m_hostRoots[ci * 3 + 1] = pos.y;
                m_hostRoots[ci * 3 + 2] = pos.z;
            } else {
                float segLen = glm::length(pos - prevPos);
                m_hostLengths[segCursor + j - 1] = segLen;
                totalLen += segLen;
            }

            prevPos = pos;
        }

        m_hostTotalLens[ci] = totalLen;
        jointCursor += n;
        segCursor   += (n > 1) ? n - 1 : 0;
    }

    return totalJoints;
}

// ---------------------------------------------------------------------------
// GPU buffer management
// ---------------------------------------------------------------------------

void GPUIKSolver::ensureBuffers(uint32_t totalJoints, uint32_t totalSegments,
                                 uint32_t chainCount) {
    ZoneScopedN("GPUIKSolver::ensureBuffers");
    auto& cl = OpenCLContext::get();
    cl_int err = CL_SUCCESS;

    // Position ping-pong buffers
    if (totalJoints > m_capJoints) {
        if (m_posA) cl.releaseMem(m_posA);
        if (m_posB) cl.releaseMem(m_posB);

        size_t posBytes = static_cast<size_t>(totalJoints) * 3 * sizeof(float);
        m_posA = cl.createBuffer(CL_MEM_READ_WRITE, posBytes, nullptr, &err, "FABRIK posA");
        m_posB = cl.createBuffer(CL_MEM_READ_WRITE, posBytes, nullptr, &err, "FABRIK posB");
        m_capJoints = totalJoints;
    }

    // Segment lengths buffer
    if (totalSegments > m_capSegments) {
        if (m_gpuLengths) cl.releaseMem(m_gpuLengths);
        m_gpuLengths = cl.createBuffer(CL_MEM_READ_ONLY,
            static_cast<size_t>(totalSegments) * sizeof(float), nullptr, &err, "FABRIK lengths");
        m_capSegments = totalSegments;
    }

    // Per-chain metadata buffers
    if (chainCount > m_capChains) {
        auto releaseIf = [&](cl_mem& buf) {
            if (buf) { cl.releaseMem(buf); buf = nullptr; }
        };
        releaseIf(m_gpuTargets);
        releaseIf(m_gpuRoots);
        releaseIf(m_gpuChainLens);
        releaseIf(m_gpuPosOffsets);
        releaseIf(m_gpuLenOffsets);
        releaseIf(m_gpuTotalLens);
        releaseIf(m_gpuFlags);
        releaseIf(m_gpuErrors);

        size_t c3f = static_cast<size_t>(chainCount) * 3 * sizeof(float);
        size_t cu  = static_cast<size_t>(chainCount) * sizeof(uint32_t);
        size_t cf  = static_cast<size_t>(chainCount) * sizeof(float);

        m_gpuTargets    = cl.createBuffer(CL_MEM_READ_ONLY,  c3f, nullptr, &err, "FABRIK targets");
        m_gpuRoots      = cl.createBuffer(CL_MEM_READ_ONLY,  c3f, nullptr, &err, "FABRIK roots");
        m_gpuChainLens  = cl.createBuffer(CL_MEM_READ_ONLY,  cu,  nullptr, &err, "FABRIK chainLens");
        m_gpuPosOffsets = cl.createBuffer(CL_MEM_READ_ONLY,  cu,  nullptr, &err, "FABRIK posOffsets");
        m_gpuLenOffsets = cl.createBuffer(CL_MEM_READ_ONLY,  cu,  nullptr, &err, "FABRIK lenOffsets");
        m_gpuTotalLens  = cl.createBuffer(CL_MEM_READ_ONLY,  cf,  nullptr, &err, "FABRIK totalLens");
        m_gpuFlags      = cl.createBuffer(CL_MEM_READ_WRITE, cu,  nullptr, &err, "FABRIK flags");
        m_gpuErrors     = cl.createBuffer(CL_MEM_READ_WRITE, cf,  nullptr, &err, "FABRIK errors");

        m_capChains = chainCount;
    }
}

void GPUIKSolver::releaseBuffers() {
    auto& cl = OpenCLContext::get();
    if (!cl.isReady()) return;

    auto release = [&](cl_mem& buf) {
        if (buf) { cl.releaseMem(buf); buf = nullptr; }
    };

    release(m_posA);
    release(m_posB);
    release(m_gpuLengths);
    release(m_gpuTargets);
    release(m_gpuRoots);
    release(m_gpuChainLens);
    release(m_gpuPosOffsets);
    release(m_gpuLenOffsets);
    release(m_gpuTotalLens);
    release(m_gpuFlags);
    release(m_gpuErrors);

    m_capJoints   = 0;
    m_capSegments = 0;
    m_capChains   = 0;
}

// ---------------------------------------------------------------------------
// GPU dispatch for a single sub-batch
// ---------------------------------------------------------------------------

void GPUIKSolver::solveBatchGPU(uint32_t chainCount, uint32_t totalJoints,
                                 uint32_t totalSegments,
                                 std::vector<IKSolveResult>& outResults,
                                 const std::vector<GPUIKBatchEntry>& entries,
                                 uint32_t startIdx) {
    ZoneScopedN("GPUIKSolver::solveBatchGPU");
    auto& cl = OpenCLContext::get();
    cl_command_queue queue = cl.getQueue();
    cl_int err;

    // --- Ensure GPU buffers ---
    ensureBuffers(totalJoints, totalSegments, chainCount);

    // --- Upload host data (non-blocking) ---
    size_t posBytes = static_cast<size_t>(totalJoints)   * 3 * sizeof(float);
    size_t lenBytes = static_cast<size_t>(totalSegments) * sizeof(float);
    size_t c3fBytes = static_cast<size_t>(chainCount)    * 3 * sizeof(float);
    size_t cuBytes  = static_cast<size_t>(chainCount)    * sizeof(uint32_t);
    size_t cfBytes  = static_cast<size_t>(chainCount)    * sizeof(float);

    clEnqueueWriteBuffer(queue, m_posA,          CL_FALSE, 0, posBytes, m_hostPositions.data(),  0, nullptr, nullptr);
    if (lenBytes > 0)
        clEnqueueWriteBuffer(queue, m_gpuLengths,CL_FALSE, 0, lenBytes, m_hostLengths.data(),    0, nullptr, nullptr);
    clEnqueueWriteBuffer(queue, m_gpuTargets,    CL_FALSE, 0, c3fBytes, m_hostTargets.data(),    0, nullptr, nullptr);
    clEnqueueWriteBuffer(queue, m_gpuRoots,      CL_FALSE, 0, c3fBytes, m_hostRoots.data(),      0, nullptr, nullptr);
    clEnqueueWriteBuffer(queue, m_gpuChainLens,  CL_FALSE, 0, cuBytes,  m_hostChainLens.data(),  0, nullptr, nullptr);
    clEnqueueWriteBuffer(queue, m_gpuPosOffsets, CL_FALSE, 0, cuBytes,  m_hostPosOffsets.data(), 0, nullptr, nullptr);
    clEnqueueWriteBuffer(queue, m_gpuLenOffsets, CL_FALSE, 0, cuBytes,  m_hostLenOffsets.data(), 0, nullptr, nullptr);
    clEnqueueWriteBuffer(queue, m_gpuTotalLens,  CL_FALSE, 0, cfBytes,  m_hostTotalLens.data(),  0, nullptr, nullptr);

    size_t globalSize = static_cast<size_t>(chainCount);

    // --- Prepare pass (in-place on posA) ---
    {
        ZoneScopedN("GPUIKSolver::prepare");
        clSetKernelArg(m_prepareKernel,  0, sizeof(cl_mem),    &m_posA);
        clSetKernelArg(m_prepareKernel,  1, sizeof(cl_mem),    &m_gpuLengths);
        clSetKernelArg(m_prepareKernel,  2, sizeof(cl_mem),    &m_gpuTargets);
        clSetKernelArg(m_prepareKernel,  3, sizeof(cl_mem),    &m_gpuRoots);
        clSetKernelArg(m_prepareKernel,  4, sizeof(cl_mem),    &m_gpuTotalLens);
        clSetKernelArg(m_prepareKernel,  5, sizeof(cl_mem),    &m_gpuFlags);
        clSetKernelArg(m_prepareKernel,  6, sizeof(cl_mem),    &m_gpuChainLens);
        clSetKernelArg(m_prepareKernel,  7, sizeof(cl_mem),    &m_gpuPosOffsets);
        clSetKernelArg(m_prepareKernel,  8, sizeof(cl_mem),    &m_gpuLenOffsets);
        clSetKernelArg(m_prepareKernel,  9, sizeof(uint32_t),  &chainCount);

        err = clEnqueueNDRangeKernel(queue, m_prepareKernel, 1, nullptr,
                                     &globalSize, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            PLOGE << "GPUIKSolver: fabrik_prepare failed (err=" << err << ")";
            return;
        }
    }

    // --- Iteration loop: forward (posA->posB) then backward (posB->posA) ---
    for (int iter = 0; iter < m_maxIterations; ++iter) {
        // Forward pass: ping -> pong
        {
            ZoneScopedN("GPUIKSolver::forward");
            clSetKernelArg(m_forwardKernel,  0, sizeof(cl_mem),    &m_posA);
            clSetKernelArg(m_forwardKernel,  1, sizeof(cl_mem),    &m_posB);
            clSetKernelArg(m_forwardKernel,  2, sizeof(cl_mem),    &m_gpuLengths);
            clSetKernelArg(m_forwardKernel,  3, sizeof(cl_mem),    &m_gpuTargets);
            clSetKernelArg(m_forwardKernel,  4, sizeof(cl_mem),    &m_gpuFlags);
            clSetKernelArg(m_forwardKernel,  5, sizeof(cl_mem),    &m_gpuChainLens);
            clSetKernelArg(m_forwardKernel,  6, sizeof(cl_mem),    &m_gpuPosOffsets);
            clSetKernelArg(m_forwardKernel,  7, sizeof(cl_mem),    &m_gpuLenOffsets);
            clSetKernelArg(m_forwardKernel,  8, sizeof(uint32_t),  &chainCount);

            err = clEnqueueNDRangeKernel(queue, m_forwardKernel, 1, nullptr,
                                         &globalSize, nullptr, 0, nullptr, nullptr);
            if (err != CL_SUCCESS) {
                PLOGE << "GPUIKSolver: fabrik_forward failed (err=" << err
                      << ", iter=" << iter << ")";
                return;
            }
        }

        // Backward pass: pong -> ping
        {
            ZoneScopedN("GPUIKSolver::backward");
            clSetKernelArg(m_backwardKernel, 0, sizeof(cl_mem),    &m_posB);
            clSetKernelArg(m_backwardKernel, 1, sizeof(cl_mem),    &m_posA);
            clSetKernelArg(m_backwardKernel, 2, sizeof(cl_mem),    &m_gpuLengths);
            clSetKernelArg(m_backwardKernel, 3, sizeof(cl_mem),    &m_gpuRoots);
            clSetKernelArg(m_backwardKernel, 4, sizeof(cl_mem),    &m_gpuFlags);
            clSetKernelArg(m_backwardKernel, 5, sizeof(cl_mem),    &m_gpuChainLens);
            clSetKernelArg(m_backwardKernel, 6, sizeof(cl_mem),    &m_gpuPosOffsets);
            clSetKernelArg(m_backwardKernel, 7, sizeof(cl_mem),    &m_gpuLenOffsets);
            clSetKernelArg(m_backwardKernel, 8, sizeof(uint32_t),  &chainCount);

            err = clEnqueueNDRangeKernel(queue, m_backwardKernel, 1, nullptr,
                                         &globalSize, nullptr, 0, nullptr, nullptr);
            if (err != CL_SUCCESS) {
                PLOGE << "GPUIKSolver: fabrik_backward failed (err=" << err
                      << ", iter=" << iter << ")";
                return;
            }
        }
    }
    // After all iterations, final solved positions are in posA (backward writes to posA).

    // --- Convergence error computation ---
    {
        ZoneScopedN("GPUIKSolver::error");
        clSetKernelArg(m_errorKernel, 0, sizeof(cl_mem),    &m_posA);
        clSetKernelArg(m_errorKernel, 1, sizeof(cl_mem),    &m_gpuTargets);
        clSetKernelArg(m_errorKernel, 2, sizeof(cl_mem),    &m_gpuErrors);
        clSetKernelArg(m_errorKernel, 3, sizeof(cl_mem),    &m_gpuChainLens);
        clSetKernelArg(m_errorKernel, 4, sizeof(cl_mem),    &m_gpuPosOffsets);
        clSetKernelArg(m_errorKernel, 5, sizeof(uint32_t),  &chainCount);

        err = clEnqueueNDRangeKernel(queue, m_errorKernel, 1, nullptr,
                                     &globalSize, nullptr, 0, nullptr, nullptr);
        if (err != CL_SUCCESS) {
            PLOGE << "GPUIKSolver: fabrik_error failed (err=" << err << ")";
        }
    }

    // --- Read back results ---
    std::vector<float>    resultPositions(static_cast<size_t>(totalJoints) * 3);
    std::vector<float>    resultErrors(chainCount);
    std::vector<uint32_t> resultFlags(chainCount);

    {
        ZoneScopedN("GPUIKSolver::readback");
        clEnqueueReadBuffer(queue, m_posA,      CL_FALSE, 0, posBytes,
                            resultPositions.data(), 0, nullptr, nullptr);
        clEnqueueReadBuffer(queue, m_gpuErrors, CL_FALSE, 0, cfBytes,
                            resultErrors.data(),    0, nullptr, nullptr);
        // Block on the last read so all data is available.
        clEnqueueReadBuffer(queue, m_gpuFlags,  CL_TRUE,  0, cuBytes,
                            resultFlags.data(),     0, nullptr, nullptr);
    }

    // --- Convert GPU world positions back to local transforms (CPU) ---
    {
        ZoneScopedN("GPUIKSolver::toLocalTransforms");
        for (uint32_t ci = 0; ci < chainCount; ++ci) {
            const auto& entry  = entries[startIdx + ci];
            const IKChain& chain = *entry.chain;
            uint32_t n    = static_cast<uint32_t>(chain.boneIndices.size());
            uint32_t pOff = m_hostPosOffsets[ci];

            IKSolveResult result;
            result.finalError = resultErrors[ci];
            result.iterations = m_maxIterations;
            result.converged  = (result.finalError < m_tolerance);

            // Extract solved world positions for this chain
            std::vector<glm::vec3> solvedPositions(n);
            for (uint32_t j = 0; j < n; ++j) {
                size_t base = static_cast<size_t>(pOff + j) * 3;
                solvedPositions[j] = glm::vec3(
                    resultPositions[base],
                    resultPositions[base + 1],
                    resultPositions[base + 2]
                );
            }

            // Convert to local transforms (reuses same logic as CPU solver)
            result.solvedTransforms = positionsToLocalTransforms(
                *entry.skeleton, chain, solvedPositions);

            outResults.push_back(std::move(result));
        }
    }
}

// ---------------------------------------------------------------------------
// Public entry point – batch orchestration
// ---------------------------------------------------------------------------

std::vector<IKSolveResult> GPUIKSolver::solveBatch(
    const std::vector<GPUIKBatchEntry>& entries)
{
    ZoneScopedN("GPUIKSolver::solveBatch");
    std::vector<IKSolveResult> results;
    results.reserve(entries.size());

    if (entries.empty() || !m_initialized) return results;

    // Validate all entries before dispatch
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        if (!e.skeleton || !e.chain || !e.chain->isValid() || e.chain->length() < 2) {
            PLOGW << "GPUIKSolver: Invalid entry at index " << i
                  << ", returning empty results for entire batch";
            results.resize(entries.size());  // default-constructed (empty)
            return results;
        }
    }

    uint32_t totalEntries = static_cast<uint32_t>(entries.size());
    uint32_t batchSize    = getEffectiveBatchSize();
    uint32_t numBatches   = (totalEntries + batchSize - 1) / batchSize;

    PLOGD << "GPUIKSolver: Solving " << totalEntries << " chains in "
          << numBatches << " batch(es) of up to " << batchSize;

    for (uint32_t b = 0; b < numBatches; ++b) {
        uint32_t startIdx = b * batchSize;
        uint32_t count    = std::min(batchSize, totalEntries - startIdx);

        // Pack this batch's data into staging buffers
        uint32_t totalJoints   = packBatchData(entries, startIdx, count);
        uint32_t totalSegments = (totalJoints > count) ? totalJoints - count : 0;

        // Dispatch to GPU and collect results
        solveBatchGPU(count, totalJoints, totalSegments, results, entries, startIdx);
    }

    return results;
}

} // namespace CharacterEditor
