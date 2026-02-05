#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <functional>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <CharacterEditor/Part.hpp>
#include <CharacterEditor/Bone.hpp>
#include <sqlite3.h>

namespace CharacterEditor {

/**
 * @brief Result of attaching a part to a socket
 */
struct AttachmentResult {
    bool success = false;
    std::string error;
    
    std::string attachmentID;        // Unique ID for this attachment
    Transform resolvedTransform;     // Final transform of the part
    
    // Skeleton join info
    uint32_t hostBoneIndex = UINT32_MAX;     // Bone in host skeleton the part attaches to
    uint32_t partRootBoneIndex = UINT32_MAX; // Root bone in part skeleton
    
    AttachmentResult() = default;
    
    static AttachmentResult Success(const std::string& id, const Transform& t) {
        AttachmentResult r;
        r.success = true;
        r.attachmentID = id;
        r.resolvedTransform = t;
        return r;
    }
    
    static AttachmentResult Failure(const std::string& err) {
        AttachmentResult r;
        r.success = false;
        r.error = err;
        return r;
    }
};

/**
 * @brief Active attachment instance tracking
 */
struct ActiveAttachment {
    std::string attachmentID;
    std::string partID;
    std::string socketID;
    Transform transform;
    
    // Skeleton joining info
    uint32_t hostBoneIndex = UINT32_MAX;       // Bone in host skeleton
    uint32_t partRootBoneIndex = UINT32_MAX;   // Root bone in part skeleton
    std::vector<uint32_t> remappedBoneIndices; // Part bone -> combined skeleton index
    
    ActiveAttachment() = default;
};

/**
 * @brief Part summary for UI display (lightweight)
 */
struct PartSummary {
    int64_t dbID = 0;              // Database ID
    std::string partID;            // Part UUID
    std::string name;
    std::string category;
    std::string rootSocket;        // Root socket profile this part attaches via
    int vertexCount = 0;
    int boneCount = 0;
    int socketCount = 0;
    std::string tags;
    bool hasThumbnail = false;
    int64_t createdAt = 0;
    int64_t modifiedAt = 0;
    
    PartSummary() = default;
};

/**
 * @brief Combined skeleton with parts attached
 * 
 * When parts are attached to sockets, their skeletons are joined
 * to create a unified skeleton for IK solving.
 */
struct CombinedSkeleton {
    Skeleton skeleton;             // The unified skeleton
    
    // Mapping from source part/bone to combined skeleton
    struct BoneSource {
        std::string sourcePartID;  // "" for base skeleton
        uint32_t sourceBoneIndex;
        uint32_t combinedIndex;
    };
    std::vector<BoneSource> boneSources;
    
    // Track socket connections for cross-part IK
    struct SocketConnection {
        std::string socketID;
        uint32_t hostBoneIndex;     // In combined skeleton
        uint32_t attachedPartRootIndex; // In combined skeleton
    };
    std::vector<SocketConnection> socketConnections;
    
    /**
     * @brief Find bone in combined skeleton by original part/bone
     */
    uint32_t findBone(const std::string& partID, uint32_t originalIndex) const {
        for (const auto& src : boneSources) {
            if (src.sourcePartID == partID && src.sourceBoneIndex == originalIndex) {
                return src.combinedIndex;
            }
        }
        return UINT32_MAX;
    }
    
    /**
     * @brief Get socket connection by socket ID
     */
    const SocketConnection* findConnection(const std::string& socketID) const {
        for (const auto& conn : socketConnections) {
            if (conn.socketID == socketID) return &conn;
        }
        return nullptr;
    }
};

/**
 * @brief Part Library - manages parts stored in a vault database
 * 
 * The PartLibrary handles:
 * - Loading/saving parts to SQLite
 * - Part attachment to sockets
 * - Skeleton joining when parts are attached
 * - Cross-part IK chain building
 */
class PartLibrary {
public:
    PartLibrary();
    ~PartLibrary();
    
    /**
     * @brief Initialize with database connection
     * @param db SQLite database (owned by Vault)
     */
    bool initialize(sqlite3* db);
    
    /**
     * @brief Create required tables if they don't exist
     */
    bool createTables();
    
    // ===== Part Management =====
    
    /**
     * @brief Save a part to the library
     * @return Database ID of the saved part, or -1 on failure
     */
    int64_t savePart(const Part& part);
    
    /**
     * @brief Load a part by database ID
     */
    std::unique_ptr<Part> loadPart(int64_t dbID);
    
    /**
     * @brief Load a part by UUID
     */
    std::unique_ptr<Part> loadPartByUUID(const std::string& uuid);
    
    /**
     * @brief Delete a part from the library
     */
    bool deletePart(int64_t dbID);
    
    /**
     * @brief Get all part summaries (for UI listing)
     */
    std::vector<PartSummary> getAllPartSummaries();
    
    /**
     * @brief Get parts by category
     */
    std::vector<PartSummary> getPartsByCategory(const std::string& category);
    
    /**
     * @brief Get parts compatible with a socket profile
     */
    std::vector<PartSummary> getPartsForSocket(const std::string& socketProfile);
    
    /**
     * @brief Search parts by name/tags
     */
    std::vector<PartSummary> searchParts(const std::string& query);
    
    /**
     * @brief Get all categories in the library
     */
    std::vector<std::string> getAllCategories();
    
    /**
     * @brief Load part thumbnail
     */
    std::vector<uint8_t> loadThumbnail(int64_t dbID);
    
    /**
     * @brief Import a part from a model file (GLB/FBX)
     */
    std::unique_ptr<Part> importFromFile(const std::string& filePath, std::string* outError = nullptr);
    
    // ===== Attachment System =====
    
    /**
     * @brief Attach a part to a socket
     * 
     * This will:
     * 1. Validate socket compatibility
     * 2. Compute the attachment transform
     * 3. Join the part's skeleton to the host skeleton
     * 4. Update the combined skeleton for IK
     */
    AttachmentResult attachPart(Part& part, Socket& socket, Skeleton& hostSkeleton);
    
    /**
     * @brief Detach a part from a socket
     */
    bool detachPart(const std::string& attachmentID);
    
    /**
     * @brief Get all active attachments
     */
    const std::vector<ActiveAttachment>& getActiveAttachments() const { return m_activeAttachments; }
    
    // ===== Combined Skeleton =====
    
    /**
     * @brief Get the combined skeleton with all attached parts
     */
    const CombinedSkeleton& getCombinedSkeleton() const { return m_combinedSkeleton; }
    
    /**
     * @brief Rebuild the combined skeleton from current attachments
     */
    void rebuildCombinedSkeleton(const Skeleton& baseSkeleton);
    
    /**
     * @brief Add a part's skeleton to the combined skeleton
     */
    void addPartToCombinedSkeleton(const Part& part, uint32_t hostBoneIndex, 
                                    const Transform& attachTransform);
    
    // ===== Serialization =====
    
    /**
     * @brief Serialize a part to binary blob
     */
    static std::vector<uint8_t> serializePart(const Part& part);
    
    /**
     * @brief Deserialize a part from binary blob
     */
    static std::unique_ptr<Part> deserializePart(const std::vector<uint8_t>& data);
    
    // ===== Utilities =====
    
    /**
     * @brief Generate a new UUID for a part
     */
    static std::string generatePartID();
    
    /**
     * @brief Get last error message
     */
    const std::string& getLastError() const { return m_lastError; }
    
private:
    sqlite3* m_db = nullptr;
    std::string m_lastError;
    
    // Active attachments
    std::vector<ActiveAttachment> m_activeAttachments;
    
    // Combined skeleton
    CombinedSkeleton m_combinedSkeleton;
    
    // Loaded parts cache
    std::map<std::string, std::unique_ptr<Part>> m_partCache;
    
    // Helper methods
    void setError(const std::string& err) { m_lastError = err; }
    
    // Serialize/deserialize helpers
    static void writeString(std::vector<uint8_t>& buf, const std::string& str);
    static std::string readString(const uint8_t*& ptr, const uint8_t* end);
    static void writeFloat(std::vector<uint8_t>& buf, float f);
    static float readFloat(const uint8_t*& ptr);
    static void writeUint32(std::vector<uint8_t>& buf, uint32_t v);
    static uint32_t readUint32(const uint8_t*& ptr);
    static void writeVec3(std::vector<uint8_t>& buf, const glm::vec3& v);
    static glm::vec3 readVec3(const uint8_t*& ptr);
    static void writeQuat(std::vector<uint8_t>& buf, const glm::quat& q);
    static glm::quat readQuat(const uint8_t*& ptr);
};

} // namespace CharacterEditor
