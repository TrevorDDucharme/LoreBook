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
    
    for (size_t i = 0; i < chain.length(); ++i) {
        const Bone& bone = skeleton.bones[chain.boneIndices[i]];
        Transform localTrans = bone.localTransform;
        
        if (i < chain.length() - 1) {
            // Calculate rotation to point towards next joint
            glm::vec3 currentDir;
            glm::vec3 targetDir = glm::normalize(positions[i + 1] - positions[i]);
            
            // Get the original direction this bone was pointing
            if (i == 0) {
                // For root, use the direction to the next bone in original pose
                Transform nextWorld = skeleton.getWorldTransform(chain.boneIndices[i + 1]);
                Transform thisWorld = skeleton.getWorldTransform(chain.boneIndices[i]);
                currentDir = glm::normalize(nextWorld.position - thisWorld.position);
            } else {
                // For other bones, get direction from parent's coordinate system
                Transform parentWorld = skeleton.getWorldTransform(chain.boneIndices[i - 1]);
                Transform thisWorld = skeleton.getWorldTransform(chain.boneIndices[i]);
                Transform nextWorld = skeleton.getWorldTransform(chain.boneIndices[i + 1]);
                currentDir = glm::normalize(nextWorld.position - thisWorld.position);
            }
            
            // Calculate rotation from current direction to target direction
            if (glm::length(currentDir) > 0.0001f && glm::length(targetDir) > 0.0001f) {
                float dot = glm::dot(currentDir, targetDir);
                dot = glm::clamp(dot, -1.0f, 1.0f);
                
                if (dot < 0.9999f) {
                    glm::vec3 axis = glm::cross(currentDir, targetDir);
                    if (glm::length(axis) > 0.0001f) {
                        axis = glm::normalize(axis);
                        float angle = std::acos(dot);
                        glm::quat deltaRot = glm::angleAxis(angle, axis);
                        
                        // Apply delta rotation to local transform
                        localTrans.rotation = deltaRot * localTrans.rotation;
                    }
                }
            }
        }
        
        // Update position for the local transform
        // The position change propagates through the hierarchy, so we mainly
        // care about rotations. Root position stays fixed.
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
