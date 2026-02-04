#pragma once
#include <glm/glm.hpp>
#include <cstdint>

namespace CharacterEditor {

/**
 * @brief GPU-ready vertex structure with all required attributes.
 * 
 * Layout matches typical shader input requirements for skeletal mesh rendering.
 */
struct Vertex {
    // Position (12 bytes)
    glm::vec3 position{0.0f};
    
    // Normal (12 bytes)
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    
    // Texture coordinates (8 bytes)
    glm::vec2 uv{0.0f};
    
    // Tangent space (16 bytes) - w component stores handedness
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
    
    // Bitangent (12 bytes) - can be computed from normal x tangent * tangent.w
    glm::vec3 bitangent{0.0f, 0.0f, 1.0f};
    
    // Bone indices for skeletal animation (16 bytes)
    // Up to 4 bones per vertex
    glm::ivec4 boneIDs{-1, -1, -1, -1};
    
    // Bone weights (16 bytes)
    glm::vec4 boneWeights{0.0f};

    Vertex() = default;

    Vertex(const glm::vec3& pos, const glm::vec3& norm, const glm::vec2& texCoord)
        : position(pos), normal(norm), uv(texCoord) {}

    /**
     * @brief Compute bitangent from normal and tangent
     */
    void computeBitangent() {
        bitangent = glm::cross(normal, glm::vec3(tangent)) * tangent.w;
    }

    /**
     * @brief Normalize bone weights to sum to 1.0
     */
    void normalizeBoneWeights() {
        float sum = boneWeights.x + boneWeights.y + boneWeights.z + boneWeights.w;
        if (sum > 0.0001f) {
            boneWeights /= sum;
        }
    }

    /**
     * @brief Add a bone influence to this vertex
     * @param boneIndex Index of the bone
     * @param weight Weight of the bone's influence
     * @return true if bone was added, false if all 4 slots are full with higher weights
     */
    bool addBoneInfluence(int32_t boneIndex, float weight) {
        // Find the slot with the minimum weight
        int minSlot = 0;
        float minWeight = boneWeights[0];
        
        for (int i = 1; i < 4; ++i) {
            if (boneIDs[i] < 0) {
                // Empty slot found
                boneIDs[i] = boneIndex;
                boneWeights[i] = weight;
                return true;
            }
            if (boneWeights[i] < minWeight) {
                minWeight = boneWeights[i];
                minSlot = i;
            }
        }
        
        // Check first slot for empty
        if (boneIDs[0] < 0) {
            boneIDs[0] = boneIndex;
            boneWeights[0] = weight;
            return true;
        }
        
        // Replace minimum weight if new weight is larger
        if (weight > minWeight) {
            boneIDs[minSlot] = boneIndex;
            boneWeights[minSlot] = weight;
            return true;
        }
        
        return false;
    }

    bool operator==(const Vertex& other) const {
        return position == other.position &&
               normal == other.normal &&
               uv == other.uv &&
               tangent == other.tangent;
    }
};

/**
 * @brief Shape key delta for morph target animation.
 */
struct VertexDelta {
    uint32_t vertexIndex = 0;  // Which vertex this delta applies to (for sparse storage)
    glm::vec3 positionDelta{0.0f};
    glm::vec3 normalDelta{0.0f};
    glm::vec3 tangentDelta{0.0f};

    VertexDelta() = default;

    VertexDelta(const glm::vec3& posDelta, const glm::vec3& normDelta = glm::vec3(0.0f))
        : positionDelta(posDelta), normalDelta(normDelta) {}

    VertexDelta(const glm::vec3& posDelta, const glm::vec3& normDelta, const glm::vec3& tanDelta)
        : positionDelta(posDelta), normalDelta(normDelta), tangentDelta(tanDelta) {}

    VertexDelta operator*(float weight) const {
        return VertexDelta(
            positionDelta * weight,
            normalDelta * weight,
            tangentDelta * weight
        );
    }

    VertexDelta operator+(const VertexDelta& other) const {
        return VertexDelta(
            positionDelta + other.positionDelta,
            normalDelta + other.normalDelta,
            tangentDelta + other.tangentDelta
        );
    }
};

} // namespace CharacterEditor
