#pragma once
#include <CharacterEditor/Bone.hpp>
#include <CharacterEditor/Transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <cstdint>

namespace CharacterEditor {

/**
 * @brief IK chain definition
 * 
 * Defines a chain of bones from root to tip for IK solving.
 */
struct IKChain {
    std::string name;
    std::vector<uint32_t> boneIndices;  // From root to tip (inclusive)
    uint32_t rootBoneIndex = UINT32_MAX;
    uint32_t tipBoneIndex = UINT32_MAX;
    
    // Constraints (optional)
    std::vector<float> minAngles;  // Per-joint min angle constraint
    std::vector<float> maxAngles;  // Per-joint max angle constraint
    
    bool isValid() const {
        return !boneIndices.empty() && 
               rootBoneIndex != UINT32_MAX && 
               tipBoneIndex != UINT32_MAX;
    }
    
    size_t length() const {
        return boneIndices.size();
    }
};

/**
 * @brief IK target for solving
 */
struct IKTarget {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    bool constrainRotation = false;
    float weight = 1.0f;  // 0-1 blend weight
};

/**
 * @brief Result of IK solve
 */
struct IKSolveResult {
    bool converged = false;
    int iterations = 0;
    float finalError = 0.0f;
    std::vector<Transform> solvedTransforms;  // Local transforms for chain bones
};

/**
 * @brief FABRIK (Forward And Backward Reaching Inverse Kinematics) solver
 * 
 * Simple, fast, and produces natural-looking results for chains.
 * Works by iteratively adjusting joint positions forward (tip to root)
 * and backward (root to tip) until convergence.
 */
class FABRIKSolver {
public:
    FABRIKSolver() = default;
    
    /**
     * @brief Set solver parameters
     */
    void setTolerance(float tolerance) { m_tolerance = tolerance; }
    void setMaxIterations(int maxIter) { m_maxIterations = maxIter; }
    
    /**
     * @brief Build an IK chain between two bones
     * 
     * @param skeleton The skeleton containing the bones
     * @param startBoneIndex Index of the chain root bone
     * @param endBoneIndex Index of the chain tip (end effector) bone
     * @return IKChain or invalid chain if bones aren't connected
     */
    static IKChain buildChain(const Skeleton& skeleton, 
                              uint32_t startBoneIndex, 
                              uint32_t endBoneIndex);
    
    /**
     * @brief Solve IK for a chain
     * 
     * @param skeleton The skeleton (for reading initial poses)
     * @param chain The IK chain to solve
     * @param target Target position/rotation
     * @return IKSolveResult with solved local transforms
     */
    IKSolveResult solve(const Skeleton& skeleton,
                        const IKChain& chain,
                        const IKTarget& target);
    
    /**
     * @brief Apply solved transforms to skeleton
     * 
     * Modifies the skeleton's bone local transforms with IK result.
     * 
     * @param skeleton Skeleton to modify
     * @param chain The chain that was solved
     * @param result The solve result
     * @param blendWeight 0-1 blend between original and solved pose
     */
    static void applyResult(Skeleton& skeleton,
                            const IKChain& chain,
                            const IKSolveResult& result,
                            float blendWeight = 1.0f);

private:
    float m_tolerance = 0.001f;    // Distance threshold for convergence
    int m_maxIterations = 10;      // Max solve iterations
    
    /**
     * @brief Forward reaching pass (tip to root)
     */
    void forwardPass(std::vector<glm::vec3>& positions,
                     const std::vector<float>& lengths,
                     const glm::vec3& target);
    
    /**
     * @brief Backward reaching pass (root to tip)
     */
    void backwardPass(std::vector<glm::vec3>& positions,
                      const std::vector<float>& lengths,
                      const glm::vec3& rootPos);
    
    /**
     * @brief Convert solved positions back to local transforms
     */
    std::vector<Transform> positionsToTransforms(
        const Skeleton& skeleton,
        const IKChain& chain,
        const std::vector<glm::vec3>& positions);
};

} // namespace CharacterEditor
