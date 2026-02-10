#include <CharacterEditor/IKSolver.hpp>
#include <plog/Log.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>
#include <algorithm>
#include <cmath>

namespace CharacterEditor {

IKChain FABRIKSolver::buildChain(const Skeleton& skeleton, 
                                  uint32_t startBoneIndex, 
                                  uint32_t endBoneIndex) {
    IKChain chain;
    chain.rootBoneIndex = startBoneIndex;
    chain.tipBoneIndex = endBoneIndex;
    
    if (startBoneIndex >= skeleton.bones.size() || 
        endBoneIndex >= skeleton.bones.size()) {
        PLOGW << "IKSolver: Invalid bone indices for chain";
        return chain;
    }
    
    // Trace from end back to start to build the chain
    std::vector<uint32_t> reversePath;
    uint32_t current = endBoneIndex;
    
    while (current != UINT32_MAX) {
        reversePath.push_back(current);
        
        if (current == startBoneIndex) {
            // Found the start - we have a valid path
            break;
        }
        
        current = skeleton.bones[current].parentID;
    }
    
    // Check if we reached the start bone
    if (reversePath.empty() || reversePath.back() != startBoneIndex) {
        PLOGW << "IKSolver: End bone is not a descendant of start bone";
        return chain;
    }
    
    // Reverse to get root-to-tip order
    chain.boneIndices.reserve(reversePath.size());
    for (auto it = reversePath.rbegin(); it != reversePath.rend(); ++it) {
        chain.boneIndices.push_back(*it);
    }
    
    // Generate name
    chain.name = skeleton.bones[startBoneIndex].name + " -> " + 
                 skeleton.bones[endBoneIndex].name;
    
    PLOGI << "IKSolver: Built chain '" << chain.name << "' with " 
          << chain.boneIndices.size() << " bones";
    
    return chain;
}

IKSolveResult FABRIKSolver::solve(const Skeleton& skeleton,
                                   const IKChain& chain,
                                   const IKTarget& target) {
    IKSolveResult result;
    
    if (!chain.isValid() || chain.length() < 2) {
        PLOGW << "IKSolver: Invalid chain for solving";
        return result;
    }
    
    // Get world positions of all joints in the chain
    std::vector<glm::vec3> positions;
    std::vector<float> lengths;
    positions.reserve(chain.length());
    lengths.reserve(chain.length() - 1);
    
    for (size_t i = 0; i < chain.length(); ++i) {
        Transform worldTrans = skeleton.getWorldTransform(chain.boneIndices[i]);
        positions.push_back(worldTrans.position);
        
        if (i > 0) {
            float len = glm::length(positions[i] - positions[i-1]);
            lengths.push_back(len);
        }
    }
    
    // Calculate total reach
    float totalLength = 0.0f;
    for (float len : lengths) {
        totalLength += len;
    }
    
    // Check if target is reachable
    glm::vec3 rootPos = positions[0];
    float distToTarget = glm::length(target.position - rootPos);
    
    if (distToTarget > totalLength) {
        // Target unreachable - stretch towards it
        glm::vec3 dir = glm::normalize(target.position - rootPos);
        for (size_t i = 1; i < positions.size(); ++i) {
            positions[i] = positions[i-1] + dir * lengths[i-1];
        }
        result.converged = false;
        result.finalError = distToTarget - totalLength;
    } else {
        // FABRIK iteration
        for (int iter = 0; iter < m_maxIterations; ++iter) {
            // Forward pass: move tip to target, adjust chain backwards
            forwardPass(positions, lengths, target.position);
            
            // Backward pass: move root back to original, adjust chain forwards
            backwardPass(positions, lengths, rootPos);
            
            // Check convergence
            float error = glm::length(positions.back() - target.position);
            if (error < m_tolerance) {
                result.converged = true;
                result.iterations = iter + 1;
                result.finalError = error;
                break;
            }
            
            result.iterations = iter + 1;
            result.finalError = error;
        }
    }
    
    // Convert solved positions to local transforms
    result.solvedTransforms = positionsToTransforms(skeleton, chain, positions);
    
    return result;
}

void FABRIKSolver::forwardPass(std::vector<glm::vec3>& positions,
                                const std::vector<float>& lengths,
                                const glm::vec3& target) {
    // Set tip to target
    positions.back() = target;
    
    // Work backwards from tip to root
    for (int i = static_cast<int>(positions.size()) - 2; i >= 0; --i) {
        glm::vec3 dir = glm::normalize(positions[i] - positions[i + 1]);
        positions[i] = positions[i + 1] + dir * lengths[i];
    }
}

void FABRIKSolver::backwardPass(std::vector<glm::vec3>& positions,
                                 const std::vector<float>& lengths,
                                 const glm::vec3& rootPos) {
    // Set root back to original position
    positions[0] = rootPos;
    
    // Work forwards from root to tip
    for (size_t i = 1; i < positions.size(); ++i) {
        glm::vec3 dir = glm::normalize(positions[i] - positions[i - 1]);
        positions[i] = positions[i - 1] + dir * lengths[i - 1];
    }
}

std::vector<Transform> FABRIKSolver::positionsToTransforms(
    const Skeleton& skeleton,
    const IKChain& chain,
    const std::vector<glm::vec3>& positions) {
    
    std::vector<Transform> transforms;
    transforms.reserve(chain.length());
    
    // Use a mutable working copy so each bone's rotation change cascades
    // to subsequent bones in the chain (they need the updated parent world).
    Skeleton working = skeleton;
    
    for (size_t i = 0; i < chain.length(); ++i) {
        uint32_t boneIdx = chain.boneIndices[i];
        const Bone& bone = working.bones[boneIdx];
        Transform localTrans = bone.localTransform;
        
        if (i < chain.length() - 1) {
            // Current world-space direction to next joint (from working skeleton)
            Transform thisWorld = working.getWorldTransform(boneIdx);
            Transform nextWorld = working.getWorldTransform(chain.boneIndices[i + 1]);
            glm::vec3 currentDir = glm::normalize(nextWorld.position - thisWorld.position);
            
            // Desired world-space direction from solved positions
            glm::vec3 targetDir = glm::normalize(positions[i + 1] - positions[i]);
            
            if (glm::length(currentDir) > 0.0001f && glm::length(targetDir) > 0.0001f) {
                float dot = glm::clamp(glm::dot(currentDir, targetDir), -1.0f, 1.0f);
                
                if (dot < 0.9999f) {
                    glm::vec3 axis = glm::cross(currentDir, targetDir);
                    if (glm::length(axis) > 0.0001f) {
                        axis = glm::normalize(axis);
                        float angle = std::acos(dot);
                        
                        // Delta rotation in world space
                        glm::quat worldDelta = glm::angleAxis(angle, axis);
                        
                        // Convert to local space:
                        // localDelta = inverse(parentWorldRot) * worldDelta * parentWorldRot
                        glm::quat parentWorldRot = glm::quat(1, 0, 0, 0);
                        if (bone.parentID != UINT32_MAX) {
                            parentWorldRot = working.getWorldTransform(bone.parentID).rotation;
                        }
                        glm::quat invParent = glm::inverse(parentWorldRot);
                        glm::quat localDelta = invParent * worldDelta * parentWorldRot;
                        
                        localTrans.rotation = localDelta * localTrans.rotation;
                        
                        // Update working skeleton so subsequent bones see the change
                        working.bones[boneIdx].localTransform.rotation = localTrans.rotation;
                    }
                }
            }
        }
        
        transforms.push_back(localTrans);
    }
    
    return transforms;
}

void FABRIKSolver::applyResult(Skeleton& skeleton,
                                const IKChain& chain,
                                const IKSolveResult& result,
                                float blendWeight) {
    if (result.solvedTransforms.size() != chain.boneIndices.size()) {
        PLOGW << "IKSolver: Result transform count mismatch";
        return;
    }
    
    blendWeight = glm::clamp(blendWeight, 0.0f, 1.0f);
    
    for (size_t i = 0; i < chain.boneIndices.size(); ++i) {
        Bone& bone = skeleton.bones[chain.boneIndices[i]];
        const Transform& solved = result.solvedTransforms[i];
        
        if (blendWeight >= 0.9999f) {
            bone.localTransform = solved;
        } else if (blendWeight > 0.0001f) {
            // Blend between original and solved
            bone.localTransform.position = glm::mix(
                bone.localTransform.position, 
                solved.position, 
                blendWeight);
            bone.localTransform.rotation = glm::slerp(
                bone.localTransform.rotation,
                solved.rotation,
                blendWeight);
            bone.localTransform.scale = glm::mix(
                bone.localTransform.scale,
                solved.scale,
                blendWeight);
        }
    }
}

} // namespace CharacterEditor
