#include <CharacterEditor/PartLibrary.hpp>
#include <CharacterEditor/ModelLoader.hpp>
#include <plog/Log.h>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <random>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <filesystem>

namespace CharacterEditor {

// ============================================================
// Binary Serialization Helpers
// ============================================================

void PartLibrary::writeString(std::vector<uint8_t>& buf, const std::string& str) {
    uint32_t len = static_cast<uint32_t>(str.size());
    writeUint32(buf, len);
    buf.insert(buf.end(), str.begin(), str.end());
}

std::string PartLibrary::readString(const uint8_t*& ptr, const uint8_t* end) {
    if (ptr + 4 > end) return "";
    uint32_t len = readUint32(ptr, end);
    if (ptr + len > end) return "";
    std::string result(reinterpret_cast<const char*>(ptr), len);
    ptr += len;
    return result;
}

void PartLibrary::writeFloat(std::vector<uint8_t>& buf, float f) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&f);
    buf.insert(buf.end(), bytes, bytes + 4);
}

float PartLibrary::readFloat(const uint8_t*& ptr, const uint8_t* end) {
    if (ptr + 4 > end) { ptr = end; return 0.0f; }
    float f;
    std::memcpy(&f, ptr, 4);
    ptr += 4;
    return f;
}

void PartLibrary::writeUint32(std::vector<uint8_t>& buf, uint32_t v) {
    buf.push_back((v >> 0) & 0xFF);
    buf.push_back((v >> 8) & 0xFF);
    buf.push_back((v >> 16) & 0xFF);
    buf.push_back((v >> 24) & 0xFF);
}

uint32_t PartLibrary::readUint32(const uint8_t*& ptr, const uint8_t* end) {
    if (ptr + 4 > end) { ptr = end; return 0; }
    uint32_t v = static_cast<uint32_t>(ptr[0])
              | (static_cast<uint32_t>(ptr[1]) << 8)
              | (static_cast<uint32_t>(ptr[2]) << 16)
              | (static_cast<uint32_t>(ptr[3]) << 24);
    ptr += 4;
    return v;
}

void PartLibrary::writeVec3(std::vector<uint8_t>& buf, const glm::vec3& v) {
    writeFloat(buf, v.x);
    writeFloat(buf, v.y);
    writeFloat(buf, v.z);
}

glm::vec3 PartLibrary::readVec3(const uint8_t*& ptr, const uint8_t* end) {
    glm::vec3 v;
    v.x = readFloat(ptr, end);
    v.y = readFloat(ptr, end);
    v.z = readFloat(ptr, end);
    return v;
}

void PartLibrary::writeQuat(std::vector<uint8_t>& buf, const glm::quat& q) {
    writeFloat(buf, q.w);
    writeFloat(buf, q.x);
    writeFloat(buf, q.y);
    writeFloat(buf, q.z);
}

glm::quat PartLibrary::readQuat(const uint8_t*& ptr, const uint8_t* end) {
    glm::quat q;
    q.w = readFloat(ptr, end);
    q.x = readFloat(ptr, end);
    q.y = readFloat(ptr, end);
    q.z = readFloat(ptr, end);
    return q;
}

// ============================================================
// PartLibrary Implementation
// ============================================================

PartLibrary::PartLibrary() = default;
PartLibrary::~PartLibrary() = default;

bool PartLibrary::initialize(sqlite3* db) {
    if (!db) {
        setError("Null database pointer");
        return false;
    }
    m_db = db;
    return createTables();
}

bool PartLibrary::createTables() {
    if (!m_db) {
        setError("Database not initialized");
        return false;
    }
    
    const char* createParts = R"SQL(
        CREATE TABLE IF NOT EXISTS Parts (
            ID INTEGER PRIMARY KEY AUTOINCREMENT,
            PartID TEXT UNIQUE NOT NULL,
            Name TEXT NOT NULL,
            Category TEXT DEFAULT '',
            RootSocket TEXT DEFAULT '',
            Tags TEXT DEFAULT '',
            Role INTEGER DEFAULT 0,
            VertexCount INTEGER DEFAULT 0,
            BoneCount INTEGER DEFAULT 0,
            SocketCount INTEGER DEFAULT 0,
            PartData BLOB,
            Thumbnail BLOB,
            SourceFile TEXT,
            CreatedAt INTEGER DEFAULT (strftime('%s', 'now')),
            ModifiedAt INTEGER DEFAULT (strftime('%s', 'now'))
        );
    )SQL";
    
    char* errMsg = nullptr;
    int rc = sqlite3_exec(m_db, createParts, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        setError(std::string("Failed to create Parts table: ") + (errMsg ? errMsg : "unknown"));
        sqlite3_free(errMsg);
        return false;
    }
    
    // Create indices
    const char* createIndices = R"SQL(
        CREATE INDEX IF NOT EXISTS idx_Parts_Category ON Parts(Category);
        CREATE INDEX IF NOT EXISTS idx_Parts_RootSocket ON Parts(RootSocket);
        CREATE INDEX IF NOT EXISTS idx_Parts_Name ON Parts(Name);
    )SQL";
    
    rc = sqlite3_exec(m_db, createIndices, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        PLOGW << "Failed to create indices: " << (errMsg ? errMsg : "unknown");
        sqlite3_free(errMsg);
    }
    
    PLOGI << "PartLibrary tables initialized";
    return true;
}

std::string PartLibrary::generatePartID() {
    // Simple UUID v4 generation
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;
    
    uint64_t a = dis(gen);
    uint64_t b = dis(gen);
    
    // Set version (4) and variant bits
    a = (a & 0xFFFFFFFFFFFF0FFFULL) | 0x0000000000004000ULL;
    b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
    
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    oss << std::setw(8) << ((a >> 32) & 0xFFFFFFFF) << '-';
    oss << std::setw(4) << ((a >> 16) & 0xFFFF) << '-';
    oss << std::setw(4) << (a & 0xFFFF) << '-';
    oss << std::setw(4) << ((b >> 48) & 0xFFFF) << '-';
    oss << std::setw(12) << (b & 0xFFFFFFFFFFFFULL);
    
    return oss.str();
}

// ============================================================
// Part Serialization
// ============================================================

std::vector<uint8_t> PartLibrary::serializePart(const Part& part) {
    std::vector<uint8_t> data;
    
    // Magic number and version
    writeUint32(data, 0x50415254);  // "PART"
    writeUint32(data, 1);           // Version 1
    
    // Basic info
    writeString(data, part.id);
    writeString(data, part.name);
    writeString(data, part.category);
    writeUint32(data, static_cast<uint32_t>(part.primaryRole));
    writeString(data, part.customRoleName);
    
    // Tags
    writeUint32(data, static_cast<uint32_t>(part.tags.size()));
    for (const auto& tag : part.tags) {
        writeString(data, tag);
    }
    
    // Local transform
    writeVec3(data, part.localTransform.position);
    writeQuat(data, part.localTransform.rotation);
    writeVec3(data, part.localTransform.scale);
    
    // Meshes (support multiple)
    writeUint32(data, static_cast<uint32_t>(part.meshes.size()));
    for (const Mesh& mesh : part.meshes) {
        writeString(data, mesh.name);
    
    // Vertices
    writeUint32(data, static_cast<uint32_t>(mesh.vertices.size()));
    for (const auto& v : mesh.vertices) {
        writeVec3(data, v.position);
        writeVec3(data, v.normal);
        writeFloat(data, v.uv.x);
        writeFloat(data, v.uv.y);
        writeFloat(data, v.tangent.x);
        writeFloat(data, v.tangent.y);
        writeFloat(data, v.tangent.z);
        writeFloat(data, v.tangent.w);
        
        // Bone IDs and weights
        writeUint32(data, static_cast<uint32_t>(v.boneIDs.x));
        writeUint32(data, static_cast<uint32_t>(v.boneIDs.y));
        writeUint32(data, static_cast<uint32_t>(v.boneIDs.z));
        writeUint32(data, static_cast<uint32_t>(v.boneIDs.w));
        writeFloat(data, v.boneWeights.x);
        writeFloat(data, v.boneWeights.y);
        writeFloat(data, v.boneWeights.z);
        writeFloat(data, v.boneWeights.w);
    }
    
    // Indices
    writeUint32(data, static_cast<uint32_t>(mesh.indices.size()));
    for (uint32_t idx : mesh.indices) {
        writeUint32(data, idx);
    }
    
    // Materials
    writeUint32(data, static_cast<uint32_t>(mesh.materials.size()));
    for (const auto& mat : mesh.materials) {
        writeString(data, mat.name);
        writeFloat(data, mat.baseColor.r);
        writeFloat(data, mat.baseColor.g);
        writeFloat(data, mat.baseColor.b);
        writeFloat(data, mat.baseColor.a);
        writeFloat(data, mat.metallicRoughness.x);
        writeFloat(data, mat.metallicRoughness.y);
    }
    
    // Skeleton
    const Skeleton& skel = mesh.skeleton;
    writeUint32(data, static_cast<uint32_t>(skel.bones.size()));
    for (const auto& bone : skel.bones) {
        writeUint32(data, bone.id);
        writeString(data, bone.name);
        writeVec3(data, bone.localTransform.position);
        writeQuat(data, bone.localTransform.rotation);
        writeVec3(data, bone.localTransform.scale);
        writeVec3(data, bone.inverseBindMatrix.position);
        writeQuat(data, bone.inverseBindMatrix.rotation);
        writeVec3(data, bone.inverseBindMatrix.scale);
        writeUint32(data, bone.parentID);
        writeUint32(data, static_cast<uint32_t>(bone.role));
    }
    } // End of meshes loop
    
    // Sockets
    writeUint32(data, static_cast<uint32_t>(part.socketsOut.size()));
    for (const auto& socket : part.socketsOut) {
        writeString(data, socket.id);
        writeString(data, socket.name);
        writeString(data, socket.profile.profileID);
        writeString(data, socket.profile.category);
        writeUint32(data, socket.profile.version);
        writeUint32(data, static_cast<uint32_t>(socket.space));
        writeString(data, socket.boneName);
        writeVec3(data, socket.localOffset.position);
        writeQuat(data, socket.localOffset.rotation);
        writeVec3(data, socket.localOffset.scale);
        writeFloat(data, socket.influenceRadius);
    }
    
    // Attachment specs
    writeUint32(data, static_cast<uint32_t>(part.attachmentSpecs.size()));
    for (const auto& spec : part.attachmentSpecs) {
        writeString(data, spec.requiredProfile.profileID);
        writeString(data, spec.requiredProfile.category);
        writeUint32(data, spec.requiredProfile.version);
        writeUint32(data, spec.priority);
    }
    
    return data;
}

std::unique_ptr<Part> PartLibrary::deserializePart(const std::vector<uint8_t>& data) {
    if (data.size() < 8) return nullptr;
    
    const uint8_t* ptr = data.data();
    const uint8_t* end = ptr + data.size();
    
    // Check magic and version
    uint32_t magic = readUint32(ptr, end);
    uint32_t version = readUint32(ptr, end);
    
    if (magic != 0x50415254 || version != 1) {
        PLOGE << "Invalid part data: bad magic or version";
        return nullptr;
    }
    
    auto part = std::make_unique<Part>();
    
    // Basic info
    part->id = readString(ptr, end);
    part->name = readString(ptr, end);
    part->category = readString(ptr, end);
    part->primaryRole = static_cast<PartRole>(readUint32(ptr, end));
    part->customRoleName = readString(ptr, end);
    
    if (ptr >= end) {
        PLOGE << "Part data truncated after basic info";
        return part;
    }
    
    // Tags
    uint32_t tagCount = readUint32(ptr, end);
    if (tagCount > 1000) {
        PLOGE << "Part data corrupt: tagCount=" << tagCount;
        return part;
    }
    for (uint32_t i = 0; i < tagCount && ptr < end; ++i) {
        part->tags.push_back(readString(ptr, end));
    }
    
    // Local transform
    part->localTransform.position = readVec3(ptr, end);
    part->localTransform.rotation = readQuat(ptr, end);
    part->localTransform.scale = readVec3(ptr, end);
    
    if (ptr >= end) {
        PLOGE << "Part data truncated after transform";
        return part;
    }
    
    // Meshes (support multiple)
    uint32_t meshCount = readUint32(ptr, end);
    if (meshCount > 256) {
        PLOGE << "Part data corrupt: meshCount=" << meshCount;
        return part;
    }
    part->meshes.resize(meshCount);
    for (uint32_t mi = 0; mi < meshCount && ptr < end; ++mi) {
        Mesh& mesh = part->meshes[mi];
        mesh.name = readString(ptr, end);
    
        // Vertices
        uint32_t vertCount = readUint32(ptr, end);
        if (vertCount > 10000000 || ptr >= end) {
            PLOGE << "Part data corrupt: vertCount=" << vertCount;
            return part;
        }
        mesh.vertices.resize(vertCount);
        for (uint32_t i = 0; i < vertCount && ptr < end; ++i) {
            auto& v = mesh.vertices[i];
            v.position = readVec3(ptr, end);
            v.normal = readVec3(ptr, end);
            v.uv.x = readFloat(ptr, end);
            v.uv.y = readFloat(ptr, end);
            v.tangent.x = readFloat(ptr, end);
            v.tangent.y = readFloat(ptr, end);
            v.tangent.z = readFloat(ptr, end);
            v.tangent.w = readFloat(ptr, end);
            
            v.boneIDs.x = static_cast<int>(readUint32(ptr, end));
            v.boneIDs.y = static_cast<int>(readUint32(ptr, end));
            v.boneIDs.z = static_cast<int>(readUint32(ptr, end));
            v.boneIDs.w = static_cast<int>(readUint32(ptr, end));
            v.boneWeights.x = readFloat(ptr, end);
            v.boneWeights.y = readFloat(ptr, end);
            v.boneWeights.z = readFloat(ptr, end);
            v.boneWeights.w = readFloat(ptr, end);
        }
        
        // Indices
        uint32_t indexCount = readUint32(ptr, end);
        if (indexCount > 50000000 || ptr >= end) {
            PLOGE << "Part data corrupt: indexCount=" << indexCount;
            return part;
        }
        mesh.indices.resize(indexCount);
        for (uint32_t i = 0; i < indexCount && ptr < end; ++i) {
            mesh.indices[i] = readUint32(ptr, end);
        }
        
        // Materials
        uint32_t matCount = readUint32(ptr, end);
        if (matCount > 100 || ptr >= end) {
            PLOGE << "Part data corrupt: matCount=" << matCount;
            return part;
        }
        mesh.materials.resize(matCount);
        for (uint32_t i = 0; i < matCount && ptr < end; ++i) {
            auto& mat = mesh.materials[i];
            mat.name = readString(ptr, end);
            mat.baseColor.r = readFloat(ptr, end);
            mat.baseColor.g = readFloat(ptr, end);
            mat.baseColor.b = readFloat(ptr, end);
            mat.baseColor.a = readFloat(ptr, end);
            mat.metallicRoughness.x = readFloat(ptr, end);
            mat.metallicRoughness.y = readFloat(ptr, end);
        }
        
        // Skeleton
        uint32_t boneCount = readUint32(ptr, end);
        if (boneCount > 1024 || ptr >= end) {
            PLOGE << "Part data corrupt: boneCount=" << boneCount;
            return part;
        }
        mesh.skeleton.bones.resize(boneCount);
        for (uint32_t i = 0; i < boneCount && ptr < end; ++i) {
            auto& bone = mesh.skeleton.bones[i];
            bone.id = readUint32(ptr, end);
            bone.name = readString(ptr, end);
            bone.localTransform.position = readVec3(ptr, end);
            bone.localTransform.rotation = readQuat(ptr, end);
            bone.localTransform.scale = readVec3(ptr, end);
            bone.inverseBindMatrix.position = readVec3(ptr, end);
            bone.inverseBindMatrix.rotation = readQuat(ptr, end);
            bone.inverseBindMatrix.scale = readVec3(ptr, end);
            bone.parentID = readUint32(ptr, end);
            bone.role = static_cast<PartRole>(readUint32(ptr, end));
            
            mesh.skeleton.boneNameToIndex[bone.name] = static_cast<int32_t>(i);
        }
        mesh.skeleton.buildHierarchy();
        mesh.computeBounds();
    } // End of meshes loop
    
    if (ptr >= end) {
        PLOGI << "Part deserialized (no sockets/specs section): " << part->name;
        return part;
    }
    
    // Sockets
    uint32_t socketCount = readUint32(ptr, end);
    if (socketCount > 256) {
        PLOGE << "Part data corrupt: socketCount=" << socketCount;
        return part;
    }
    part->socketsOut.resize(socketCount);
    for (uint32_t i = 0; i < socketCount && ptr < end; ++i) {
        auto& socket = part->socketsOut[i];
        socket.id = readString(ptr, end);
        socket.name = readString(ptr, end);
        socket.profile.profileID = readString(ptr, end);
        socket.profile.category = readString(ptr, end);
        socket.profile.version = readUint32(ptr, end);
        socket.space = static_cast<SpaceType>(readUint32(ptr, end));
        socket.boneName = readString(ptr, end);
        socket.localOffset.position = readVec3(ptr, end);
        socket.localOffset.rotation = readQuat(ptr, end);
        socket.localOffset.scale = readVec3(ptr, end);
        socket.influenceRadius = readFloat(ptr, end);
        
        // Resolve ownerBoneIndex from boneName using first mesh's skeleton
        if (!socket.boneName.empty() && !part->meshes.empty()) {
            const Skeleton* skel = part->getSkeleton();
            if (skel) {
                auto it = skel->boneNameToIndex.find(socket.boneName);
                if (it != skel->boneNameToIndex.end() && it->second >= 0) {
                    socket.ownerBoneIndex = static_cast<uint32_t>(it->second);
                }
            }
        }
    }
    
    if (ptr >= end) {
        PLOGI << "Part deserialized (no specs section): " << part->name;
        return part;
    }
    
    // Attachment specs
    uint32_t specCount = readUint32(ptr, end);
    if (specCount > 256) {
        PLOGE << "Part data corrupt: specCount=" << specCount;
        return part;
    }
    part->attachmentSpecs.resize(specCount);
    for (uint32_t i = 0; i < specCount && ptr < end; ++i) {
        auto& spec = part->attachmentSpecs[i];
        spec.requiredProfile.profileID = readString(ptr, end);
        spec.requiredProfile.category = readString(ptr, end);
        spec.requiredProfile.version = readUint32(ptr, end);
        spec.priority = static_cast<int>(readUint32(ptr, end));
    }
    
    PLOGI << "Part deserialized: " << part->name << " (" << part->meshes.size() 
          << " meshes, " << part->socketsOut.size() << " sockets)";
    return part;
}

// ============================================================
// Part Database Operations
// ============================================================

int64_t PartLibrary::savePart(const Part& part) {
    if (!m_db) {
        setError("Database not initialized");
        return -1;
    }
    
    // Serialize the part
    std::vector<uint8_t> partData = serializePart(part);
    
    // Join tags
    std::string tagsStr;
    for (size_t i = 0; i < part.tags.size(); ++i) {
        if (i > 0) tagsStr += ",";
        tagsStr += part.tags[i];
    }
    
    // Get root socket profile (from first attachment spec)
    std::string rootSocket;
    if (!part.attachmentSpecs.empty()) {
        rootSocket = part.attachmentSpecs[0].requiredProfile.profileID;
    }
    
    // Check if part already exists
    const char* checkSql = "SELECT ID FROM Parts WHERE PartID = ?;";
    sqlite3_stmt* checkStmt = nullptr;
    int64_t existingID = -1;
    
    if (sqlite3_prepare_v2(m_db, checkSql, -1, &checkStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(checkStmt, 1, part.id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(checkStmt) == SQLITE_ROW) {
            existingID = sqlite3_column_int64(checkStmt, 0);
        }
        sqlite3_finalize(checkStmt);
    }
    
    sqlite3_stmt* stmt = nullptr;
    int rc;
    
    if (existingID > 0) {
        // Update existing
        const char* sql = R"SQL(
            UPDATE Parts SET 
                Name = ?, Category = ?, RootSocket = ?, Tags = ?,
                Role = ?, VertexCount = ?, BoneCount = ?, SocketCount = ?,
                PartData = ?, Thumbnail = ?, ModifiedAt = strftime('%s', 'now')
            WHERE ID = ?;
        )SQL";
        
        rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            setError("Failed to prepare update statement");
            return -1;
        }
        
        sqlite3_bind_text(stmt, 1, part.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, part.category.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, rootSocket.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, tagsStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 5, static_cast<int>(part.primaryRole));
        {
            int vertexCount = 0;
            int boneCount = 0;
            if (!part.meshes.empty()) {
                vertexCount = static_cast<int>(part.meshes[0].vertices.size());
                boneCount = static_cast<int>(part.meshes[0].skeleton.bones.size());
            }
            sqlite3_bind_int(stmt, 6, vertexCount);
            sqlite3_bind_int(stmt, 7, boneCount);
        }
        sqlite3_bind_int(stmt, 8, static_cast<int>(part.socketsOut.size()));
        sqlite3_bind_blob(stmt, 9, partData.data(), static_cast<int>(partData.size()), SQLITE_TRANSIENT);
        
        if (!part.thumbnail.empty()) {
            sqlite3_bind_blob(stmt, 10, part.thumbnail.data(), 
                             static_cast<int>(part.thumbnail.size()), SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, 10);
        }
        
        sqlite3_bind_int64(stmt, 11, existingID);
    } else {
        // Insert new
        const char* sql = R"SQL(
            INSERT INTO Parts (
                PartID, Name, Category, RootSocket, Tags, Role,
                VertexCount, BoneCount, SocketCount, PartData, Thumbnail, SourceFile
            ) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);
        )SQL";
        
        rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            setError("Failed to prepare insert statement");
            return -1;
        }
        
        sqlite3_bind_text(stmt, 1, part.id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, part.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, part.category.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, rootSocket.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, tagsStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 6, static_cast<int>(part.primaryRole));
        {
            int vertexCount = 0;
            int boneCount = 0;
            if (!part.meshes.empty()) {
                vertexCount = static_cast<int>(part.meshes[0].vertices.size());
                boneCount = static_cast<int>(part.meshes[0].skeleton.bones.size());
            }
            sqlite3_bind_int(stmt, 7, vertexCount);
            sqlite3_bind_int(stmt, 8, boneCount);
        }
        sqlite3_bind_int(stmt, 9, static_cast<int>(part.socketsOut.size()));
        sqlite3_bind_blob(stmt, 10, partData.data(), static_cast<int>(partData.size()), SQLITE_TRANSIENT);
        
        if (!part.thumbnail.empty()) {
            sqlite3_bind_blob(stmt, 11, part.thumbnail.data(), 
                             static_cast<int>(part.thumbnail.size()), SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, 11);
        }
        
        sqlite3_bind_text(stmt, 12, "", -1, SQLITE_TRANSIENT);  // SourceFile
    }
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        setError("Failed to save part: " + std::string(sqlite3_errmsg(m_db)));
        return -1;
    }
    
    int64_t resultID = (existingID > 0) ? existingID : sqlite3_last_insert_rowid(m_db);
    PLOGI << "Saved part '" << part.name << "' with ID " << resultID;
    return resultID;
}

std::unique_ptr<Part> PartLibrary::loadPart(int64_t dbID) {
    if (!m_db) {
        setError("Database not initialized");
        return nullptr;
    }
    
    const char* sql = "SELECT PartData FROM Parts WHERE ID = ?;";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        setError("Failed to prepare load statement");
        return nullptr;
    }
    
    sqlite3_bind_int64(stmt, 1, dbID);
    
    std::unique_ptr<Part> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        int blobSize = sqlite3_column_bytes(stmt, 0);
        
        if (blob && blobSize > 0) {
            std::vector<uint8_t> data(static_cast<const uint8_t*>(blob),
                                      static_cast<const uint8_t*>(blob) + blobSize);
            result = deserializePart(data);
        }
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::unique_ptr<Part> PartLibrary::loadPartByUUID(const std::string& uuid) {
    if (!m_db) {
        setError("Database not initialized");
        return nullptr;
    }
    
    const char* sql = "SELECT PartData FROM Parts WHERE PartID = ?;";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        setError("Failed to prepare load statement");
        return nullptr;
    }
    
    sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
    
    std::unique_ptr<Part> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        int blobSize = sqlite3_column_bytes(stmt, 0);
        
        if (blob && blobSize > 0) {
            std::vector<uint8_t> data(static_cast<const uint8_t*>(blob),
                                      static_cast<const uint8_t*>(blob) + blobSize);
            result = deserializePart(data);
        }
    }
    
    sqlite3_finalize(stmt);
    return result;
}

bool PartLibrary::deletePart(int64_t dbID) {
    if (!m_db) {
        setError("Database not initialized");
        return false;
    }
    
    const char* sql = "DELETE FROM Parts WHERE ID = ?;";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        setError("Failed to prepare delete statement");
        return false;
    }
    
    sqlite3_bind_int64(stmt, 1, dbID);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

std::vector<PartSummary> PartLibrary::getAllPartSummaries() {
    std::vector<PartSummary> result;
    
    if (!m_db) return result;
    
    const char* sql = R"SQL(
        SELECT ID, PartID, Name, Category, RootSocket, Tags,
               VertexCount, BoneCount, SocketCount, 
               Thumbnail IS NOT NULL, CreatedAt, ModifiedAt
        FROM Parts ORDER BY Category, Name;
    )SQL";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PartSummary summary;
        summary.dbID = sqlite3_column_int64(stmt, 0);
        summary.partID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        summary.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        
        const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        summary.category = cat ? cat : "";
        
        const char* rs = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        summary.rootSocket = rs ? rs : "";
        
        const char* tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        summary.tags = tags ? tags : "";
        
        summary.vertexCount = sqlite3_column_int(stmt, 6);
        summary.boneCount = sqlite3_column_int(stmt, 7);
        summary.socketCount = sqlite3_column_int(stmt, 8);
        summary.hasThumbnail = sqlite3_column_int(stmt, 9) != 0;
        summary.createdAt = sqlite3_column_int64(stmt, 10);
        summary.modifiedAt = sqlite3_column_int64(stmt, 11);
        
        result.push_back(summary);
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::vector<PartSummary> PartLibrary::getPartsByCategory(const std::string& category) {
    std::vector<PartSummary> result;
    
    if (!m_db) return result;
    
    const char* sql = R"SQL(
        SELECT ID, PartID, Name, Category, RootSocket, Tags,
               VertexCount, BoneCount, SocketCount,
               Thumbnail IS NOT NULL, CreatedAt, ModifiedAt
        FROM Parts WHERE Category = ? ORDER BY Name;
    )SQL";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }
    
    sqlite3_bind_text(stmt, 1, category.c_str(), -1, SQLITE_TRANSIENT);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PartSummary summary;
        summary.dbID = sqlite3_column_int64(stmt, 0);
        summary.partID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        summary.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        
        const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        summary.category = cat ? cat : "";
        
        const char* rs = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        summary.rootSocket = rs ? rs : "";
        
        const char* tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        summary.tags = tags ? tags : "";
        
        summary.vertexCount = sqlite3_column_int(stmt, 6);
        summary.boneCount = sqlite3_column_int(stmt, 7);
        summary.socketCount = sqlite3_column_int(stmt, 8);
        summary.hasThumbnail = sqlite3_column_int(stmt, 9) != 0;
        summary.createdAt = sqlite3_column_int64(stmt, 10);
        summary.modifiedAt = sqlite3_column_int64(stmt, 11);
        
        result.push_back(summary);
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::vector<PartSummary> PartLibrary::getPartsForSocket(const std::string& socketProfile) {
    std::vector<PartSummary> result;
    
    if (!m_db) return result;
    
    const char* sql = R"SQL(
        SELECT ID, PartID, Name, Category, RootSocket, Tags,
               VertexCount, BoneCount, SocketCount,
               Thumbnail IS NOT NULL, CreatedAt, ModifiedAt
        FROM Parts WHERE RootSocket = ? ORDER BY Category, Name;
    )SQL";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }
    
    sqlite3_bind_text(stmt, 1, socketProfile.c_str(), -1, SQLITE_TRANSIENT);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PartSummary summary;
        summary.dbID = sqlite3_column_int64(stmt, 0);
        summary.partID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        summary.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        
        const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        summary.category = cat ? cat : "";
        
        const char* rs = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        summary.rootSocket = rs ? rs : "";
        
        const char* tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        summary.tags = tags ? tags : "";
        
        summary.vertexCount = sqlite3_column_int(stmt, 6);
        summary.boneCount = sqlite3_column_int(stmt, 7);
        summary.socketCount = sqlite3_column_int(stmt, 8);
        summary.hasThumbnail = sqlite3_column_int(stmt, 9) != 0;
        summary.createdAt = sqlite3_column_int64(stmt, 10);
        summary.modifiedAt = sqlite3_column_int64(stmt, 11);
        
        result.push_back(summary);
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::vector<PartSummary> PartLibrary::searchParts(const std::string& query) {
    std::vector<PartSummary> result;
    
    if (!m_db || query.empty()) return result;
    
    const char* sql = R"SQL(
        SELECT ID, PartID, Name, Category, RootSocket, Tags,
               VertexCount, BoneCount, SocketCount,
               Thumbnail IS NOT NULL, CreatedAt, ModifiedAt
        FROM Parts 
        WHERE Name LIKE ? OR Category LIKE ? OR Tags LIKE ?
        ORDER BY Category, Name;
    )SQL";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }
    
    std::string pattern = "%" + query + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, pattern.c_str(), -1, SQLITE_TRANSIENT);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PartSummary summary;
        summary.dbID = sqlite3_column_int64(stmt, 0);
        summary.partID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        summary.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        
        const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        summary.category = cat ? cat : "";
        
        const char* rs = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        summary.rootSocket = rs ? rs : "";
        
        const char* tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        summary.tags = tags ? tags : "";
        
        summary.vertexCount = sqlite3_column_int(stmt, 6);
        summary.boneCount = sqlite3_column_int(stmt, 7);
        summary.socketCount = sqlite3_column_int(stmt, 8);
        summary.hasThumbnail = sqlite3_column_int(stmt, 9) != 0;
        summary.createdAt = sqlite3_column_int64(stmt, 10);
        summary.modifiedAt = sqlite3_column_int64(stmt, 11);
        
        result.push_back(summary);
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::vector<std::string> PartLibrary::getAllCategories() {
    std::vector<std::string> result;
    
    if (!m_db) return result;
    
    const char* sql = "SELECT DISTINCT Category FROM Parts WHERE Category != '' ORDER BY Category;";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (cat) result.push_back(cat);
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::vector<uint8_t> PartLibrary::loadThumbnail(int64_t dbID) {
    std::vector<uint8_t> result;
    
    if (!m_db) return result;
    
    const char* sql = "SELECT Thumbnail FROM Parts WHERE ID = ?;";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }
    
    sqlite3_bind_int64(stmt, 1, dbID);
    
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const void* blob = sqlite3_column_blob(stmt, 0);
        int blobSize = sqlite3_column_bytes(stmt, 0);
        
        if (blob && blobSize > 0) {
            result.assign(static_cast<const uint8_t*>(blob),
                         static_cast<const uint8_t*>(blob) + blobSize);
        }
    }
    
    sqlite3_finalize(stmt);
    return result;
}

// ============================================================
// Part Import
// ============================================================

std::unique_ptr<Part> PartLibrary::importFromFile(const std::string& filePath, std::string* outError) {
    auto loadResult = ModelLoader::loadFromFile(filePath);
    
    if (!loadResult.success) {
        if (outError) *outError = loadResult.getError();
        return nullptr;
    }
    
    // Create part from load result
    auto part = std::make_unique<Part>();
    part->id = generatePartID();
    
    // Extract name from file path
    std::filesystem::path path(filePath);
    part->name = path.stem().string();
    
    // Copy ALL meshes from the loaded model
    part->meshes = loadResult.meshes;
    
    // Assign skeleton to first mesh (or create one if needed)
    if (!part->meshes.empty()) {
        if (loadResult.skeleton.empty() && part->meshes[0].hasSkeleton()) {
            // First mesh already has skeleton, use it
        } else {
            // Assign loaded skeleton to first mesh
            part->meshes[0].skeleton = loadResult.skeleton;
        }
    }
    
    // Copy sockets
    part->socketsOut = loadResult.extractedSockets;
    
    // Auto-detect attachment specs from sockets
    // If there's a socket that could be the "root" attachment point, use it
    for (const auto& socket : loadResult.extractedSockets) {
        AttachmentSpec spec(socket.profile);
        spec.priority = 0;
        part->attachmentSpecs.push_back(spec);
    }
    
    int totalVertices = 0;
    for (const auto& mesh : part->meshes) {
        totalVertices += static_cast<int>(mesh.vertices.size());
    }
    const Skeleton* partSkel = part->getSkeleton();
    
    PLOGI << "Imported part '" << part->name << "' with " 
          << part->meshes.size() << " meshes, "
          << totalVertices << " total vertices, "
          << (partSkel ? partSkel->bones.size() : 0) << " bones, "
          << part->socketsOut.size() << " sockets";
    
    return part;
}

// ============================================================
// Attachment System
// ============================================================

AttachmentResult PartLibrary::attachPart(Part& part, Socket& socket, Skeleton& hostSkeleton) {
    // Validate compatibility
    if (!part.canAttachTo(socket)) {
        return AttachmentResult::Failure("Part is not compatible with socket profile: " + socket.profile.profileID);
    }
    
    if (socket.isOccupied) {
        return AttachmentResult::Failure("Socket is already occupied");
    }
    
    // Generate attachment ID
    std::string attachID = generatePartID();
    
    // Resolve socket bone index by name if not already set
    uint32_t resolvedBoneIndex = socket.ownerBoneIndex;
    if (resolvedBoneIndex == UINT32_MAX && !socket.boneName.empty()) {
        auto it = hostSkeleton.boneNameToIndex.find(socket.boneName);
        if (it != hostSkeleton.boneNameToIndex.end() && it->second >= 0) {
            resolvedBoneIndex = static_cast<uint32_t>(it->second);
            socket.ownerBoneIndex = resolvedBoneIndex;  // Cache for future use
            PLOGI << "Resolved socket '" << socket.name << "' bone '" 
                  << socket.boneName << "' to index " << resolvedBoneIndex;
        } else {
            PLOGW << "Could not resolve socket bone '" << socket.boneName 
                  << "' in host skeleton";
        }
    }
    
    // Get socket's world transform based on host skeleton
    Transform socketWorld;
    if (socket.space == SpaceType::Bone && resolvedBoneIndex < hostSkeleton.bones.size()) {
        socketWorld = hostSkeleton.getWorldTransform(resolvedBoneIndex);
    } else {
        socketWorld = socket.localOffset;
    }
    
    // Compute part transform (align to socket)
    Transform partWorld = socketWorld.compose(part.localTransform);
    
    // Mark socket as occupied
    socket.isOccupied = true;
    socket.attachedPartID = part.id;
    
    // Create active attachment
    ActiveAttachment attachment;
    attachment.attachmentID = attachID;
    attachment.partID = part.id;
    attachment.socketID = socket.id;
    attachment.transform = partWorld;
    attachment.hostBoneIndex = resolvedBoneIndex;
    
    // Join skeletons if the part has bones
    if (part.hasSkeleton()) {
        const Skeleton* partSkeleton = part.getSkeleton();
        if (!partSkeleton) {
            setError("Part has skeleton flag but no skeleton found");
            return AttachmentResult::Failure("Part has skeleton but no skeleton found");
        }
        
        // Find the part's socket bone that matches the host socket profile
        // This is the bone in the part that should align with the host socket
        uint32_t partSocketBoneIndex = UINT32_MAX;
        for (const auto& partSocket : part.socketsOut) {
            if (partSocket.profile.isCompatibleWith(socket.profile) &&
                partSocket.ownerBoneIndex != UINT32_MAX) {
                partSocketBoneIndex = partSocket.ownerBoneIndex;
                PLOGI << "Part socket bone for alignment: '" << partSocket.boneName 
                      << "' index=" << partSocketBoneIndex;
                break;
            }
        }
        
        size_t beforeSize = m_combinedSkeleton.skeleton.bones.size();
        addPartToCombinedSkeleton(part, resolvedBoneIndex, socketWorld, partSocketBoneIndex);
        size_t afterSize = m_combinedSkeleton.skeleton.bones.size();
        
        // Build remapped indices for all part bones
        // Part bones get sequential indices starting from beforeSize
        uint32_t nextIndex = static_cast<uint32_t>(beforeSize);
        for (size_t i = 0; i < partSkeleton->bones.size(); ++i) {
            attachment.remappedBoneIndices.push_back(nextIndex++);
        }
        
        // Root bone handling
        if (!partSkeleton->rootBoneIndices.empty()) {
            uint32_t rootIdx = partSkeleton->rootBoneIndices[0];
            if (rootIdx < attachment.remappedBoneIndices.size()) {
                attachment.partRootBoneIndex = attachment.remappedBoneIndices[rootIdx];
            }
        }
        
        PLOGI << "Skeleton joined: " << beforeSize << " -> " << afterSize << " bones";
    }
    
    m_activeAttachments.push_back(attachment);
    
    PLOGI << "Attached part '" << part.name << "' to socket '" << socket.name << "'";
    
    return AttachmentResult::Success(attachID, partWorld);
}

bool PartLibrary::detachPart(const std::string& attachmentID) {
    auto it = std::find_if(m_activeAttachments.begin(), m_activeAttachments.end(),
        [&](const ActiveAttachment& a) { return a.attachmentID == attachmentID; });
    
    if (it == m_activeAttachments.end()) {
        setError("Attachment not found: " + attachmentID);
        return false;
    }
    
    // TODO: Mark socket as available, remove from combined skeleton
    m_activeAttachments.erase(it);
    
    PLOGI << "Detached part with attachment ID: " << attachmentID;
    return true;
}

// ============================================================
// Combined Skeleton
// ============================================================

void PartLibrary::rebuildCombinedSkeleton(const Skeleton& baseSkeleton) {
    m_combinedSkeleton.skeleton = baseSkeleton;
    m_combinedSkeleton.boneSources.clear();
    m_combinedSkeleton.socketConnections.clear();
    
    // Track base skeleton bones
    for (size_t i = 0; i < baseSkeleton.bones.size(); ++i) {
        CombinedSkeleton::BoneSource src;
        src.sourcePartID = "";  // Base skeleton
        src.sourceBoneIndex = static_cast<uint32_t>(i);
        src.combinedIndex = static_cast<uint32_t>(i);
        m_combinedSkeleton.boneSources.push_back(src);
    }
}

void PartLibrary::addPartToCombinedSkeleton(const Part& part, uint32_t hostBoneIndex,
                                             const Transform& attachTransform,
                                             uint32_t partSocketBoneIndex) {
    if (!part.hasSkeleton()) return;
    
    const Skeleton* partSkelPtr = part.getSkeleton();
    if (!partSkelPtr) return;
    
    const Skeleton& partSkel = *partSkelPtr;
    uint32_t baseIndex = static_cast<uint32_t>(m_combinedSkeleton.skeleton.bones.size());
    
    // Compute root offset so the part's socket bone aligns with the host socket bone.
    // Without this, the part's ROOT bone ends up at the host socket position,
    // but the actual connection point (the part's socket bone) is deeper in the
    // hierarchy, creating an incorrect offset.
    //
    // rootOffset = inverse(partSocketBoneWorldTransform)
    // This ensures the part's socket bone ends up at the host socket bone's position.
    Transform rootOffset; // Identity by default (no adjustment)
    if (partSocketBoneIndex != UINT32_MAX && partSocketBoneIndex < partSkel.bones.size()) {
        Transform partSocketWorld = partSkel.getWorldTransform(partSocketBoneIndex);
        rootOffset = partSocketWorld.inverse();
        PLOGI << "Socket alignment offset: partSocketBone[" << partSocketBoneIndex 
              << "] '" << partSkel.bones[partSocketBoneIndex].name 
              << "' worldPos=(" << partSocketWorld.position.x 
              << ", " << partSocketWorld.position.y 
              << ", " << partSocketWorld.position.z << ")";
    }
    
    // Build bone index remapping: all part bones get new sequential indices
    std::vector<uint32_t> boneIndexRemap(partSkel.bones.size());
    for (size_t i = 0; i < partSkel.bones.size(); ++i) {
        boneIndexRemap[i] = baseIndex + static_cast<uint32_t>(i);
    }
    
    // Add ALL bones from the part skeleton to the combined skeleton
    for (size_t i = 0; i < partSkel.bones.size(); ++i) {
        Bone newBone = partSkel.bones[i];
        newBone.id = boneIndexRemap[i];
        
        if (partSkel.bones[i].parentID != UINT32_MAX && 
            partSkel.bones[i].parentID < partSkel.bones.size()) {
            // Remap parent to the new combined index
            newBone.parentID = boneIndexRemap[partSkel.bones[i].parentID];
        } else {
            // Root bone in the part — parent it under the host socket bone
            if (hostBoneIndex != UINT32_MAX && hostBoneIndex < m_combinedSkeleton.skeleton.bones.size()) {
                newBone.parentID = hostBoneIndex;
                
                // Apply socket alignment offset to root bones so the part's
                // socket bone ends up at the host socket bone position
                newBone.localTransform = rootOffset.compose(newBone.localTransform);
                
                PLOGI << "Parenting part root bone '" << newBone.name 
                      << "' under host bone[" << hostBoneIndex << "] '"
                      << m_combinedSkeleton.skeleton.bones[hostBoneIndex].name 
                      << "' with socket alignment offset";
            }
            // else: host bone unknown, leave as root (UINT32_MAX)
        }
        
        m_combinedSkeleton.skeleton.bones.push_back(newBone);
        m_combinedSkeleton.skeleton.boneNameToIndex[newBone.name] = 
            static_cast<int32_t>(m_combinedSkeleton.skeleton.bones.size() - 1);
        
        // Track source
        CombinedSkeleton::BoneSource src;
        src.sourcePartID = part.id;
        src.sourceBoneIndex = static_cast<uint32_t>(i);
        src.combinedIndex = newBone.id;
        m_combinedSkeleton.boneSources.push_back(src);
    }
    
    // Track the socket connection
    if (hostBoneIndex != UINT32_MAX && !partSkel.rootBoneIndices.empty()) {
        CombinedSkeleton::SocketConnection conn;
        conn.hostBoneIndex = hostBoneIndex;
        conn.attachedPartRootIndex = boneIndexRemap[partSkel.rootBoneIndices[0]];
        m_combinedSkeleton.socketConnections.push_back(conn);
    }
    
    // Rebuild hierarchy
    m_combinedSkeleton.skeleton.buildHierarchy();
    
    PLOGI << "Added " << partSkel.bones.size() << " bones from part '" << part.name 
          << "' to combined skeleton (total: " << m_combinedSkeleton.skeleton.bones.size() << ")";
}

} // namespace CharacterEditor
