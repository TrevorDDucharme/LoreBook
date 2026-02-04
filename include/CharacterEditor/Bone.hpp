#pragma once
#include <string>
#include <vector>
#include <unordered_map>
#include <cstdint>
#include <glm/glm.hpp>
#include <CharacterEditor/Transform.hpp>
#include <CharacterEditor/PartRole.hpp>

namespace CharacterEditor {

// Forward declaration
struct Socket;

/**
 * @brief Bone structure for skeletal animation
 * 
 * Bones named "socket_*" are automatically extracted as sockets.
 */
struct Bone {
    uint32_t id = 0;
    std::string name;
    
    // Transforms
    Transform localTransform;           // Local transform relative to parent
    Transform inverseBindMatrix;        // Inverse bind pose for skinning
    
    // Hierarchy
    uint32_t parentID = UINT32_MAX;     // UINT32_MAX = root bone
    std::vector<uint32_t> childIDs;
    
    // Part role classification
    PartRole role = PartRole::Appendage;
    std::string customRoleName;         // For PartRole::Custom
    
    // Socket extraction (bones named "socket_*" become sockets)
    bool isSocketBone = false;
    uint32_t extractedSocketID = 0;     // ID of the socket if extracted
    
    Bone() = default;
    
    Bone(uint32_t id_, const std::string& name_)
        : id(id_), name(name_) {
        checkIfSocket();
    }
    
    /**
     * @brief Check if this bone should be extracted as a socket
     */
    bool isSocket() const {
        return name.rfind("socket_", 0) == 0;
    }
    
    /**
     * @brief Extract socket profile from bone name
     * 
     * Pattern: socket_<profile>[_<optional_name>]
     * Example: "socket_humanoid_hand_v1" -> profile="humanoid_hand_v1"
     * Example: "socket_accessory_small_belt" -> profile="accessory_small", name="belt"
     */
    std::string extractSocketProfile() const {
        if (!isSocket()) return "";
        
        // Remove "socket_" prefix
        std::string remainder = name.substr(7);
        
        // The profile is everything up to the last underscore segment
        // (if the last segment looks like a name rather than version)
        return remainder;
    }
    
    /**
     * @brief Check if this bone is a root bone (no parent)
     */
    bool isRoot() const {
        return parentID == UINT32_MAX;
    }
    
    /**
     * @brief Check if this bone has children
     */
    bool hasChildren() const {
        return !childIDs.empty();
    }
    
    /**
     * @brief Check if this bone is a leaf (no children)
     */
    bool isLeaf() const {
        return childIDs.empty();
    }
    
    /**
     * @brief Get the role info for this bone
     */
    PartRoleInfo getRoleInfo() const {
        PartRoleInfo info;
        info.role = role;
        info.customRoleName = customRoleName;
        return info;
    }
    
    /**
     * @brief Get the default constraints for this bone's role
     */
    const PartRoleConstraints& getDefaultConstraints() const {
        return PartRoleConstraintLibrary::instance().getDefaults(role);
    }
    
private:
    void checkIfSocket() {
        isSocketBone = isSocket();
    }
};

/**
 * @brief Skeleton - a hierarchy of bones
 */
struct Skeleton {
    std::vector<Bone> bones;
    std::vector<uint32_t> rootBoneIndices;  // Indices of root bones
    std::unordered_map<std::string, int32_t> boneNameToIndex;  // Fast lookup
    
    Skeleton() = default;
    
    /**
     * @brief Find a bone by name
     * @return Pointer to bone or nullptr if not found
     */
    Bone* findBone(const std::string& name) {
        auto it = boneNameToIndex.find(name);
        if (it != boneNameToIndex.end() && it->second >= 0 && 
            static_cast<size_t>(it->second) < bones.size()) {
            return &bones[it->second];
        }
        // Fallback to linear search
        for (auto& bone : bones) {
            if (bone.name == name) return &bone;
        }
        return nullptr;
    }
    
    const Bone* findBone(const std::string& name) const {
        auto it = boneNameToIndex.find(name);
        if (it != boneNameToIndex.end() && it->second >= 0 && 
            static_cast<size_t>(it->second) < bones.size()) {
            return &bones[it->second];
        }
        // Fallback to linear search
        for (const auto& bone : bones) {
            if (bone.name == name) return &bone;
        }
        return nullptr;
    }
    
    /**
     * @brief Find a bone by ID
     */
    Bone* findBoneByID(uint32_t id) {
        for (auto& bone : bones) {
            if (bone.id == id) return &bone;
        }
        return nullptr;
    }
    
    const Bone* findBoneByID(uint32_t id) const {
        for (const auto& bone : bones) {
            if (bone.id == id) return &bone;
        }
        return nullptr;
    }
    
    /**
     * @brief Get all socket bones in the skeleton
     */
    std::vector<Bone*> getSocketBones() {
        std::vector<Bone*> result;
        for (auto& bone : bones) {
            if (bone.isSocketBone) {
                result.push_back(&bone);
            }
        }
        return result;
    }
    
    /**
     * @brief Calculate world transform for a bone
     */
    Transform getWorldTransform(uint32_t boneIndex) const {
        if (boneIndex >= bones.size()) return Transform();
        
        const Bone& bone = bones[boneIndex];
        if (bone.isRoot()) {
            return bone.localTransform;
        }
        
        // Recursively compute parent transform
        Transform parentWorld = getWorldTransform(bone.parentID);
        return parentWorld.compose(bone.localTransform);
    }
    
    /**
     * @brief Build hierarchy from flat bone list
     * 
     * Call after loading bones to set up parent/child relationships.
     */
    void buildHierarchy() {
        rootBoneIndices.clear();
        
        // Clear existing child lists
        for (auto& bone : bones) {
            bone.childIDs.clear();
        }
        
        // Build child lists and find roots
        for (size_t i = 0; i < bones.size(); ++i) {
            Bone& bone = bones[i];
            if (bone.isRoot()) {
                rootBoneIndices.push_back(static_cast<uint32_t>(i));
            } else if (bone.parentID < bones.size()) {
                bones[bone.parentID].childIDs.push_back(static_cast<uint32_t>(i));
            }
        }
    }
    
    /**
     * @brief Get bone count
     */
    size_t size() const {
        return bones.size();
    }
    
    /**
     * @brief Check if skeleton is empty
     */
    bool empty() const {
        return bones.empty();
    }
};

} // namespace CharacterEditor