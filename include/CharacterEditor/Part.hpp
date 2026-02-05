#pragma once
#include <string>
#include <vector>
#include <map>
#include <algorithm>
#include <cstdint>
#include <CharacterEditor/Transform.hpp>
#include <CharacterEditor/Mesh.hpp>
#include <CharacterEditor/Socket.hpp>
#include <CharacterEditor/PartRole.hpp>

namespace CharacterEditor {

/**
 * @brief Deformation expectations for attachment
 */
struct DeformationExpectation {
    bool expectsSkeletalDeform = false;
    bool expectsSurfaceFollow = false;
    bool expectsPhysics = false;
    
    DeformationExpectation() = default;
};

/**
 * @brief Specification for how a part connects to sockets
 */
struct AttachmentSpec {
    SocketProfile requiredProfile;
    std::vector<SpaceType> supportedSpaceTypes;
    
    // Local transform for attachment
    Transform localAttachPoint;
    std::string attachmentBone;
    
    DeformationExpectation deformationExpectations;
    SeamSpec seamRequirements;
    
    // Priority for automatic socket selection
    int priority = 0;
    
    AttachmentSpec() = default;
    
    AttachmentSpec(const SocketProfile& profile)
        : requiredProfile(profile) {
        supportedSpaceTypes.push_back(SpaceType::Bone);
    }
    
    /**
     * @brief Check if this spec is compatible with a socket
     */
    bool isCompatibleWith(const Socket& socket) const {
        // Check profile compatibility
        if (!requiredProfile.isCompatibleWith(socket.profile)) {
            return false;
        }
        
        // Check space type support
        bool spaceTypeOk = supportedSpaceTypes.empty();
        for (auto st : supportedSpaceTypes) {
            if (st == socket.space) {
                spaceTypeOk = true;
                break;
            }
        }
        
        return spaceTypeOk;
    }
};

/**
 * @brief Part - a self-contained unit of character geometry
 * 
 * Each part carries everything it needs to render and deform.
 * Parts do not depend on external rig topology.
 * Parts expose typed socket interfaces for composition.
 */
struct Part {
    std::string id;
    std::string name;
    std::string category;
    
    // Geometry (parts can have multiple meshes)
    std::vector<Mesh> meshes;
    
    // Transform relative to attachment socket
    Transform localTransform;
    
    // Sockets this part provides (for other parts to attach to)
    std::vector<Socket> socketsOut;
    
    // How this part connects to other sockets
    std::vector<AttachmentSpec> attachmentSpecs;
    
    // Part classification
    PartRole primaryRole = PartRole::Appendage;
    std::string customRoleName;  // For PartRole::Custom
    
    // Metadata
    std::map<std::string, std::string> metadata;
    std::vector<std::string> tags;
    
    // Thumbnail/preview image (optional)
    std::vector<uint8_t> thumbnail;
    
    Part() = default;
    
    Part(const std::string& id_, const std::string& name_)
        : id(id_), name(name_) {}
    
    /**
     * @brief Get part role info
     */
    PartRoleInfo getRoleInfo() const {
        PartRoleInfo info;
        info.role = primaryRole;
        info.customRoleName = customRoleName;
        return info;
    }
    
    /**
     * @brief Check if this part can attach to the given socket
     */
    bool canAttachTo(const Socket& socket) const {
        for (const auto& spec : attachmentSpecs) {
            if (spec.isCompatibleWith(socket)) {
                return true;
            }
        }
        return false;
    }
    
    /**
     * @brief Get the best attachment spec for a socket
     */
    const AttachmentSpec* getBestAttachmentSpec(const Socket& socket) const {
        const AttachmentSpec* best = nullptr;
        int bestPriority = INT32_MIN;
        
        for (const auto& spec : attachmentSpecs) {
            if (spec.isCompatibleWith(socket) && spec.priority > bestPriority) {
                best = &spec;
                bestPriority = spec.priority;
            }
        }
        
        return best;
    }
    
    /**
     * @brief Find socket by name
     */
    Socket* findSocket(const std::string& socketName) {
        for (auto& s : socketsOut) {
            if (s.name == socketName) return &s;
        }
        return nullptr;
    }
    
    const Socket* findSocket(const std::string& socketName) const {
        for (const auto& s : socketsOut) {
            if (s.name == socketName) return &s;
        }
        return nullptr;
    }
    
    /**
     * @brief Find socket by profile
     */
    Socket* findSocketByProfile(const SocketProfile& profile) {
        for (auto& s : socketsOut) {
            if (s.profile.isCompatibleWith(profile)) return &s;
        }
        return nullptr;
    }
    
    /**
     * @brief Get all available (unoccupied) sockets
     */
    std::vector<Socket*> getAvailableSockets() {
        std::vector<Socket*> result;
        for (auto& s : socketsOut) {
            if (s.isAvailable()) {
                result.push_back(&s);
            }
        }
        return result;
    }
    
    /**
     * @brief Check if part has skeleton (any mesh)
     */
    bool hasSkeleton() const {
        for (const auto& mesh : meshes) {
            if (mesh.hasSkeleton()) return true;
        }
        return false;
    }
    
    /**
     * @brief Get the skeleton from the first mesh that has one
     */
    const Skeleton* getSkeleton() const {
        for (const auto& mesh : meshes) {
            if (mesh.hasSkeleton()) return &mesh.skeleton;
        }
        return nullptr;
    }
    
    /**
     * @brief Check if part has shape keys (any mesh)
     */
    bool hasShapeKeys() const {
        for (const auto& mesh : meshes) {
            if (mesh.hasShapeKeys()) return true;
        }
        return false;
    }
    
    /**
     * @brief Add a tag
     */
    void addTag(const std::string& tag) {
        if (std::find(tags.begin(), tags.end(), tag) == tags.end()) {
            tags.push_back(tag);
        }
    }
    
    /**
     * @brief Check if part has a tag
     */
    bool hasTag(const std::string& tag) const {
        return std::find(tags.begin(), tags.end(), tag) != tags.end();
    }
    
    /**
     * @brief Set metadata value
     */
    void setMetadata(const std::string& key, const std::string& value) {
        metadata[key] = value;
    }
    
    /**
     * @brief Get metadata value
     */
    std::string getMetadata(const std::string& key, const std::string& defaultValue = "") const {
        auto it = metadata.find(key);
        return it != metadata.end() ? it->second : defaultValue;
    }
};

} // namespace CharacterEditor