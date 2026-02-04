#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <glm/glm.hpp>
#include <CharacterEditor/Transform.hpp>

namespace CharacterEditor {

/**
 * @brief Socket space type - where the socket is anchored
 */
enum class SpaceType : uint32_t {
    Bone,           // Attached to bone transform
    MeshSurface,    // Attached to mesh surface (barycentric)
    Local           // Static local space
};

// Forward declarations
struct BarycentricCoord;
struct UVCoord;
struct LandmarkReference;

/**
 * @brief Socket profile defines compatibility contract between sockets
 * 
 * Socket compatibility is the ONLY global authority in the system.
 */
struct SocketProfile {
    std::string profileID;       // Unique identifier (e.g., "humanoid_hand_v1")
    std::string category;        // Grouping (e.g., "hand", "head", "accessory")
    uint32_t version = 1;
    
    SocketProfile() = default;
    
    SocketProfile(const std::string& id, const std::string& cat, uint32_t ver = 1)
        : profileID(id), category(cat), version(ver) {}
    
    /**
     * @brief Check compatibility with another profile
     * 
     * Profiles are compatible if they have the same profileID.
     * Version mismatches may still be compatible (handled by adapter system).
     */
    bool isCompatibleWith(const SocketProfile& other) const {
        return profileID == other.profileID;
    }
    
    /**
     * @brief Check exact match (same ID and version)
     */
    bool isExactMatch(const SocketProfile& other) const {
        return profileID == other.profileID && version == other.version;
    }
    
    bool operator==(const SocketProfile& other) const {
        return profileID == other.profileID && 
               category == other.category && 
               version == other.version;
    }
    
    bool operator!=(const SocketProfile& other) const {
        return !(*this == other);
    }
    
    /**
     * @brief Parse profile from bone name (e.g., "socket_humanoid_hand_v1" -> profile)
     */
    static SocketProfile fromBoneName(const std::string& boneName);
    
    /**
     * @brief Generate a bone name from this profile
     */
    std::string toBoneName() const {
        return "socket_" + profileID;
    }
};

/**
 * @brief Seam stitch mode for geometry joining
 */
enum class StitchMode : uint32_t {
    Snap,           // Move vertices to match
    Blend,          // Blend positions
    Bridge          // Generate bridging geometry
};

/**
 * @brief Specification for how geometry is joined at the seam
 */
struct SeamSpec {
    std::vector<uint32_t> seamLoopVertexIndices;
    float seamTolerance = 0.001f;
    bool requiresRemesh = false;
    StitchMode stitchMode = StitchMode::Snap;
    
    SeamSpec() = default;
    
    bool hasSeamLoop() const {
        return !seamLoopVertexIndices.empty();
    }
};

/**
 * @brief Barycentric coordinate for surface reference
 */
struct BarycentricCoord {
    uint32_t triangleIndex = 0;
    glm::vec3 barycentrics{1.0f/3.0f, 1.0f/3.0f, 1.0f/3.0f};  // u, v, w where u + v + w = 1
    
    BarycentricCoord() = default;
    
    BarycentricCoord(uint32_t tri, const glm::vec3& bary)
        : triangleIndex(tri), barycentrics(bary) {}
};

/**
 * @brief UV coordinate for surface reference
 */
struct UVCoord {
    float u = 0.0f;
    float v = 0.0f;
    uint32_t uvChannel = 0;
    
    UVCoord() = default;
    
    UVCoord(float u_, float v_, uint32_t channel = 0)
        : u(u_), v(v_), uvChannel(channel) {}
};

/**
 * @brief Landmark-based surface reference
 */
struct LandmarkReference {
    std::string landmarkName;
    glm::vec3 offset{0.0f};
    
    LandmarkReference() = default;
    
    LandmarkReference(const std::string& name, const glm::vec3& off = glm::vec3(0.0f))
        : landmarkName(name), offset(off) {}
};

/**
 * @brief Surface reference for mesh-surface sockets
 * 
 * Note: UV or landmark references are preferred over barycentric for stability.
 */
struct SurfaceReference {
    enum class Type : uint32_t {
        Barycentric,
        UV,
        Landmark
    } type = Type::UV;
    
    BarycentricCoord barycentric;
    UVCoord uv;
    LandmarkReference landmark;
    
    SurfaceReference() = default;
    
    static SurfaceReference fromBarycentric(uint32_t tri, const glm::vec3& bary) {
        SurfaceReference ref;
        ref.type = Type::Barycentric;
        ref.barycentric = BarycentricCoord(tri, bary);
        return ref;
    }
    
    static SurfaceReference fromUV(float u, float v, uint32_t channel = 0) {
        SurfaceReference ref;
        ref.type = Type::UV;
        ref.uv = UVCoord(u, v, channel);
        return ref;
    }
    
    static SurfaceReference fromLandmark(const std::string& name, const glm::vec3& offset = glm::vec3(0.0f)) {
        SurfaceReference ref;
        ref.type = Type::Landmark;
        ref.landmark = LandmarkReference(name, offset);
        return ref;
    }
};

/**
 * @brief Seam loop definition for surface sockets
 */
struct SeamLoopDefinition {
    // Closed loop of vertices defining the seam boundary
    std::vector<uint32_t> vertexIndices;
    
    // Alternative: parametric definition (e.g., "uv_circle(0.5, 0.5, 0.1)")
    std::string parametricExpression;
    
    bool isEmpty() const {
        return vertexIndices.empty() && parametricExpression.empty();
    }
};

/**
 * @brief Remesh policy for surface sockets
 */
enum class RemeshPolicy : uint32_t {
    None,               // No remeshing allowed
    LocalOnly,          // Remesh within influence radius
    GenerateBridge,     // Generate bridging geometry
    ShrinkWrap          // Project onto surface
};

/**
 * @brief Socket - a typed attachment interface
 * 
 * Sockets are the ONLY global authority for character composition.
 * Any part is valid if it exposes compatible sockets.
 */
struct Socket {
    std::string id;
    std::string name;
    SocketProfile profile;
    
    SpaceType space = SpaceType::Bone;
    Transform localOffset;
    OrientationFrame orientationFrame;
    
    // Bone-space reference
    std::string boneName;
    uint32_t ownerBoneIndex = UINT32_MAX;
    
    // Influence and seam
    float influenceRadius = 0.1f;      // For deformation and remeshing bounds
    SeamSpec seamSpec;
    
    // Surface-space data (if SpaceType::MeshSurface)
    SurfaceReference surfaceRef;
    SeamLoopDefinition seamLoopDefinition;
    RemeshPolicy remeshPolicy = RemeshPolicy::None;
    
    // Runtime state
    bool isOccupied = false;
    std::string attachedPartID;
    
    Socket() = default;
    
    Socket(const std::string& id_, const std::string& name_, const SocketProfile& prof)
        : id(id_), name(name_), profile(prof) {}
    
    /**
     * @brief Check if this socket matches the given profile (for attachment)
     */
    bool matchesProfile(const SocketProfile& other) const {
        return profile.isCompatibleWith(other);
    }
    
    /**
     * @brief Check if the socket is available for attachment
     */
    bool isAvailable() const {
        return !isOccupied;
    }
    
    /**
     * @brief Get the world transform of this socket given the owner's world transform
     */
    Transform getWorldTransform(const Transform& ownerWorld) const {
        return ownerWorld.compose(localOffset);
    }
    
    /**
     * @brief Check if this is a bone-space socket
     */
    bool isBoneSocket() const {
        return space == SpaceType::Bone;
    }
    
    /**
     * @brief Check if this is a surface socket
     */
    bool isSurfaceSocket() const {
        return space == SpaceType::MeshSurface;
    }
    
    // Aliases for backward compatibility
    SpaceType spaceType() const { return space; }
    const Transform& transform() const { return localOffset; }
};

/**
 * @brief Extended surface socket with stability features
 */
struct SurfaceSocket : public Socket {
    /**
     * @brief Check if the surface reference is stable between topology edits
     */
    bool isStable() const {
        // UV and landmark references are more stable than barycentric
        return surfaceRef.type == SurfaceReference::Type::UV ||
               surfaceRef.type == SurfaceReference::Type::Landmark;
    }
    
    SurfaceSocket() {
        space = SpaceType::MeshSurface;
    }
};

} // namespace CharacterEditor