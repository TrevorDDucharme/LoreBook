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
    uint32_t len = readUint32(ptr);
    if (ptr + len > end) return "";
    std::string result(reinterpret_cast<const char*>(ptr), len);
    ptr += len;
    return result;
}

void PartLibrary::writeFloat(std::vector<uint8_t>& buf, float f) {
    const uint8_t* bytes = reinterpret_cast<const uint8_t*>(&f);
    buf.insert(buf.end(), bytes, bytes + 4);
}

float PartLibrary::readFloat(const uint8_t*& ptr) {
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

uint32_t PartLibrary::readUint32(const uint8_t*& ptr) {
    uint32_t v = ptr[0] | (ptr[1] << 8) | (ptr[2] << 16) | (ptr[3] << 24);
    ptr += 4;
    return v;
}

void PartLibrary::writeVec3(std::vector<uint8_t>& buf, const glm::vec3& v) {
    writeFloat(buf, v.x);
    writeFloat(buf, v.y);
    writeFloat(buf, v.z);
}

glm::vec3 PartLibrary::readVec3(const uint8_t*& ptr) {
    glm::vec3 v;
    v.x = readFloat(ptr);
    v.y = readFloat(ptr);
    v.z = readFloat(ptr);
    return v;
}

void PartLibrary::writeQuat(std::vector<uint8_t>& buf, const glm::quat& q) {
    writeFloat(buf, q.w);
    writeFloat(buf, q.x);
    writeFloat(buf, q.y);
    writeFloat(buf, q.z);
}

glm::quat PartLibrary::readQuat(const uint8_t*& ptr) {
    glm::quat q;
    q.w = readFloat(ptr);
    q.x = readFloat(ptr);
    q.y = readFloat(ptr);
    q.z = readFloat(ptr);
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
    uint32_t magic = readUint32(ptr);
    uint32_t version = readUint32(ptr);
    
    if (magic != 0x50415254 || version != 1) {
        PLOGE << "Invalid part data: bad magic or version";
        return nullptr;
    }
    
    auto part = std::make_unique<Part>();
    
    // Basic info
    part->id = readString(ptr, end);
    part->name = readString(ptr, end);
    part->category = readString(ptr, end);
    part->primaryRole = static_cast<PartRole>(readUint32(ptr));
    part->customRoleName = readString(ptr, end);
    
    // Tags
    uint32_t tagCount = readUint32(ptr);
    for (uint32_t i = 0; i < tagCount && ptr < end; ++i) {
        part->tags.push_back(readString(ptr, end));
    }
    
    // Local transform
    part->localTransform.position = readVec3(ptr);
    part->localTransform.rotation = readQuat(ptr);
    part->localTransform.scale = readVec3(ptr);
    
    // Meshes (support multiple)
    uint32_t meshCount = readUint32(ptr);
    part->meshes.resize(meshCount);
    for (uint32_t mi = 0; mi < meshCount && ptr < end; ++mi) {
        Mesh& mesh = part->meshes[mi];
        mesh.name = readString(ptr, end);
    
        // Vertices
        uint32_t vertCount = readUint32(ptr);
        mesh.vertices.resize(vertCount);
        for (uint32_t i = 0; i < vertCount && ptr < end; ++i) {
            auto& v = mesh.vertices[i];
            v.position = readVec3(ptr);
            v.normal = readVec3(ptr);
            v.uv.x = readFloat(ptr);
            v.uv.y = readFloat(ptr);
            v.tangent.x = readFloat(ptr);
            v.tangent.y = readFloat(ptr);
            v.tangent.z = readFloat(ptr);
            v.tangent.w = readFloat(ptr);
            
            v.boneIDs.x = static_cast<int>(readUint32(ptr));
            v.boneIDs.y = static_cast<int>(readUint32(ptr));
            v.boneIDs.z = static_cast<int>(readUint32(ptr));
            v.boneIDs.w = static_cast<int>(readUint32(ptr));
            v.boneWeights.x = readFloat(ptr);
            v.boneWeights.y = readFloat(ptr);
            v.boneWeights.z = readFloat(ptr);
            v.boneWeights.w = readFloat(ptr);
        }
        
        // Indices
        uint32_t indexCount = readUint32(ptr);
        mesh.indices.resize(indexCount);
        for (uint32_t i = 0; i < indexCount && ptr < end; ++i) {
            mesh.indices[i] = readUint32(ptr);
        }
        
        // Materials
        uint32_t matCount = readUint32(ptr);
        mesh.materials.resize(matCount);
        for (uint32_t i = 0; i < matCount && ptr < end; ++i) {
            auto& mat = mesh.materials[i];
            mat.name = readString(ptr, end);
            mat.baseColor.r = readFloat(ptr);
            mat.baseColor.g = readFloat(ptr);
            mat.baseColor.b = readFloat(ptr);
            mat.baseColor.a = readFloat(ptr);
            mat.metallicRoughness.x = readFloat(ptr);
            mat.metallicRoughness.y = readFloat(ptr);
        }
        
        // Skeleton
        uint32_t boneCount = readUint32(ptr);
        mesh.skeleton.bones.resize(boneCount);
        for (uint32_t i = 0; i < boneCount && ptr < end; ++i) {
            auto& bone = mesh.skeleton.bones[i];
            bone.id = readUint32(ptr);
            bone.name = readString(ptr, end);
            bone.localTransform.position = readVec3(ptr);
            bone.localTransform.rotation = readQuat(ptr);
            bone.localTransform.scale = readVec3(ptr);
            bone.inverseBindMatrix.position = readVec3(ptr);
            bone.inverseBindMatrix.rotation = readQuat(ptr);
            bone.inverseBindMatrix.scale = readVec3(ptr);
            bone.parentID = readUint32(ptr);
            bone.role = static_cast<PartRole>(readUint32(ptr));
            
            mesh.skeleton.boneNameToIndex[bone.name] = static_cast<int32_t>(i);
        }
        mesh.skeleton.buildHierarchy();
    } // End of meshes loop
    
    // Sockets
    uint32_t socketCount = readUint32(ptr);
    part->socketsOut.resize(socketCount);
    for (uint32_t i = 0; i < socketCount && ptr < end; ++i) {
        auto& socket = part->socketsOut[i];
        socket.id = readString(ptr, end);
        socket.name = readString(ptr, end);
        socket.profile.profileID = readString(ptr, end);
        socket.profile.category = readString(ptr, end);
        socket.profile.version = readUint32(ptr);
        socket.space = static_cast<SpaceType>(readUint32(ptr));
        socket.boneName = readString(ptr, end);
        socket.localOffset.position = readVec3(ptr);
        socket.localOffset.rotation = readQuat(ptr);
        socket.localOffset.scale = readVec3(ptr);
        socket.influenceRadius = readFloat(ptr);
    }
    
    // Attachment specs
    uint32_t specCount = readUint32(ptr);
    part->attachmentSpecs.resize(specCount);
    for (uint32_t i = 0; i < specCount && ptr < end; ++i) {
        auto& spec = part->attachmentSpecs[i];
        spec.requiredProfile.profileID = readString(ptr, end);
        spec.requiredProfile.category = readString(ptr, end);
        spec.requiredProfile.version = readUint32(ptr);
        spec.priority = static_cast<int>(readUint32(ptr));
    }
    
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
    
    // Resolve the host bone index from socket.boneName if ownerBoneIndex isn't set.
    // The socket bone in the host skeleton is the attachment point.
    uint32_t resolvedHostBoneIndex = socket.ownerBoneIndex;
    if (resolvedHostBoneIndex == UINT32_MAX && !socket.boneName.empty()) {
        auto it = hostSkeleton.boneNameToIndex.find(socket.boneName);
        if (it != hostSkeleton.boneNameToIndex.end() && it->second >= 0) {
            resolvedHostBoneIndex = static_cast<uint32_t>(it->second);
            PLOGI << "Resolved socket bone '" << socket.boneName 
                  << "' to host bone index " << resolvedHostBoneIndex;
        } else {
            PLOGW << "Socket bone '" << socket.boneName << "' not found in host skeleton";
        }
    }
    
    // Get socket's world transform for part positioning
    Transform socketWorld;
    if (resolvedHostBoneIndex < hostSkeleton.bones.size()) {
        socketWorld = hostSkeleton.getWorldTransform(resolvedHostBoneIndex);
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
    attachment.hostBoneIndex = resolvedHostBoneIndex;
    
    // Join skeletons if the part has bones
    if (part.hasSkeleton()) {
        const Skeleton* partSkeleton = part.getSkeleton();
        if (!partSkeleton) {
            setError("Part has skeleton flag but no skeleton found");
            return AttachmentResult::Failure("Part has skeleton but no skeleton found");
        }
        
        size_t beforeSize = m_combinedSkeleton.skeleton.bones.size();
        
        // Find the socket bone in the part for remapping
        int32_t partSocketBoneIdx = -1;
        for (size_t i = 0; i < partSkeleton->bones.size(); ++i) {
            if (partSkeleton->bones[i].parentID == UINT32_MAX && partSkeleton->bones[i].isSocket()) {
                partSocketBoneIdx = static_cast<int32_t>(i);
                break;
            }
        }
        
        addPartToCombinedSkeleton(part, resolvedHostBoneIndex, Transform());
        
        // Build remapped bone indices for vertex bone ID remapping:
        // - Socket bone → resolvedHostBoneIndex (merged with host socket bone)
        // - Other bones → sequential combined indices starting at beforeSize
        // This must match the remapping in addPartToCombinedSkeleton exactly.
        uint32_t nextIndex = static_cast<uint32_t>(beforeSize);
        for (size_t i = 0; i < partSkeleton->bones.size(); ++i) {
            if (static_cast<int32_t>(i) == partSocketBoneIdx && resolvedHostBoneIndex != UINT32_MAX) {
                attachment.remappedBoneIndices.push_back(resolvedHostBoneIndex);
            } else {
                attachment.remappedBoneIndices.push_back(nextIndex++);
            }
        }
        
        // Root bone handling
        if (!partSkeleton->rootBoneIndices.empty()) {
            uint32_t rootIdx = partSkeleton->rootBoneIndices[0];
            attachment.partRootBoneIndex = attachment.remappedBoneIndices[rootIdx];
        }
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
                                             const Transform& attachTransform) {
    if (!part.hasSkeleton()) return;
    
    const Skeleton* partSkelPtr = part.getSkeleton();
    if (!partSkelPtr) return;
    
    const Skeleton& partSkel = *partSkelPtr;
    uint32_t baseIndex = static_cast<uint32_t>(m_combinedSkeleton.skeleton.bones.size());
    
    // Find the socket bone in the part (root bone with socket_ prefix).
    // This bone represents the attachment point and will be MERGED with the
    // host socket bone rather than added separately.
    int32_t partSocketBoneIdx = -1;
    for (size_t i = 0; i < partSkel.bones.size(); ++i) {
        const Bone& bone = partSkel.bones[i];
        if (bone.parentID == UINT32_MAX && bone.isSocket()) {
            partSocketBoneIdx = static_cast<int32_t>(i);
            PLOGI << "Found socket bone in part '" << part.name << "': " << bone.name
                  << " (index " << i << "), merging with host bone " << hostBoneIndex;
            break;
        }
    }
    
    // Build bone index remapping:
    // - Socket bone → hostBoneIndex (merged with host socket bone)
    // - All other bones → sequential new combined indices
    std::vector<uint32_t> boneIndexRemap(partSkel.bones.size());
    uint32_t nextCombinedIdx = baseIndex;
    for (size_t i = 0; i < partSkel.bones.size(); ++i) {
        if (static_cast<int32_t>(i) == partSocketBoneIdx && hostBoneIndex != UINT32_MAX) {
            boneIndexRemap[i] = hostBoneIndex;  // Merge with host
        } else {
            boneIndexRemap[i] = nextCombinedIdx++;
        }
    }
    
    // Add non-socket bones to the combined skeleton.
    // The socket bone is NOT added — its children are re-parented to the host bone.
    // Local transforms are preserved UNCHANGED. The inverse bind matrices from
    // Assimp naturally handle the coordinate space conversion during GPU skinning:
    //   skinMatrix = hostBoneWorld * childLocal * inverse(partSocketBind * childLocal)
    //             = hostBoneWorld * inverse(partSocketBind)
    // This correctly repositions vertices from the part's bind pose to the host socket.
    uint32_t addedCount = 0;
    for (size_t i = 0; i < partSkel.bones.size(); ++i) {
        if (static_cast<int32_t>(i) == partSocketBoneIdx && hostBoneIndex != UINT32_MAX) {
            continue;  // Socket bone merged with host — skip
        }
        
        Bone newBone = partSkel.bones[i];
        newBone.id = boneIndexRemap[i];
        
        // Remap parent ID
        if (partSkel.bones[i].parentID != UINT32_MAX) {
            newBone.parentID = boneIndexRemap[partSkel.bones[i].parentID];
        } else {
            // Non-socket root bone: parent to host bone
            if (hostBoneIndex != UINT32_MAX) {
                newBone.parentID = hostBoneIndex;
            }
        }
        
        // DO NOT modify localTransform — the inverse bind matrices
        // handle coordinate conversion from part bind space to host space.
        
        m_combinedSkeleton.skeleton.bones.push_back(newBone);
        m_combinedSkeleton.skeleton.boneNameToIndex[newBone.name] = 
            static_cast<int32_t>(m_combinedSkeleton.skeleton.bones.size() - 1);
        
        // Track source
        CombinedSkeleton::BoneSource src;
        src.sourcePartID = part.id;
        src.sourceBoneIndex = static_cast<uint32_t>(i);
        src.combinedIndex = newBone.id;
        m_combinedSkeleton.boneSources.push_back(src);
        addedCount++;
    }
    
    // Rebuild hierarchy
    m_combinedSkeleton.skeleton.buildHierarchy();
    
    PLOGI << "Added " << addedCount << " bones from part '" << part.name 
          << "' to combined skeleton (total: " << m_combinedSkeleton.skeleton.bones.size() << ")";
}

} // namespace CharacterEditor
