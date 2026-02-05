#include <CharacterEditor/CharacterManager.hpp>
#include <plog/Log.h>
#include <random>
#include <sstream>
#include <iomanip>

namespace CharacterEditor {

// ============================================================
// AttachedPartInstance Serialization
// ============================================================

nlohmann::json AttachedPartInstance::toJSON() const {
    return nlohmann::json{
        {"partID", partID},
        {"attachmentID", attachmentID},
        {"parentSocketID", parentSocketID},
        {"transform", {
            {"position", {localTransform.position.x, localTransform.position.y, localTransform.position.z}},
            {"rotation", {localTransform.rotation.w, localTransform.rotation.x, localTransform.rotation.y, localTransform.rotation.z}},
            {"scale", {localTransform.scale.x, localTransform.scale.y, localTransform.scale.z}}
        }}
    };
}

AttachedPartInstance AttachedPartInstance::fromJSON(const nlohmann::json& j) {
    AttachedPartInstance inst;
    inst.partID = j.value("partID", "");
    inst.attachmentID = j.value("attachmentID", "");
    inst.parentSocketID = j.value("parentSocketID", "");
    
    if (j.contains("transform")) {
        const auto& t = j["transform"];
        if (t.contains("position") && t["position"].is_array() && t["position"].size() == 3) {
            inst.localTransform.position = glm::vec3(
                t["position"][0].get<float>(),
                t["position"][1].get<float>(),
                t["position"][2].get<float>()
            );
        }
        if (t.contains("rotation") && t["rotation"].is_array() && t["rotation"].size() == 4) {
            inst.localTransform.rotation = glm::quat(
                t["rotation"][0].get<float>(),
                t["rotation"][1].get<float>(),
                t["rotation"][2].get<float>(),
                t["rotation"][3].get<float>()
            );
        }
        if (t.contains("scale") && t["scale"].is_array() && t["scale"].size() == 3) {
            inst.localTransform.scale = glm::vec3(
                t["scale"][0].get<float>(),
                t["scale"][1].get<float>(),
                t["scale"][2].get<float>()
            );
        }
    }
    
    return inst;
}

// ============================================================
// CharacterPrefab Serialization
// ============================================================

nlohmann::json CharacterPrefab::toJSON() const {
    nlohmann::json j;
    j["prefabID"] = prefabID;
    j["name"] = name;
    j["category"] = category;
    j["description"] = description;
    j["tags"] = tags;
    j["rootPartID"] = rootPartID;
    
    nlohmann::json partsArray = nlohmann::json::array();
    for (const auto& part : parts) {
        partsArray.push_back(part.toJSON());
    }
    j["parts"] = partsArray;
    
    return j;
}

CharacterPrefab CharacterPrefab::fromJSON(const nlohmann::json& j) {
    CharacterPrefab prefab;
    prefab.prefabID = j.value("prefabID", "");
    prefab.name = j.value("name", "");
    prefab.category = j.value("category", "");
    prefab.description = j.value("description", "");
    prefab.rootPartID = j.value("rootPartID", "");
    
    if (j.contains("tags") && j["tags"].is_array()) {
        for (const auto& tag : j["tags"]) {
            if (tag.is_string()) {
                prefab.tags.push_back(tag.get<std::string>());
            }
        }
    }
    
    if (j.contains("parts") && j["parts"].is_array()) {
        for (const auto& partJson : j["parts"]) {
            prefab.parts.push_back(AttachedPartInstance::fromJSON(partJson));
        }
    }
    
    return prefab;
}

// ============================================================
// Character Serialization
// ============================================================

nlohmann::json Character::toJSON() const {
    nlohmann::json j;
    j["characterID"] = characterID;
    j["name"] = name;
    j["description"] = description;
    j["tags"] = tags;
    j["basePrefabID"] = basePrefabID;
    j["customData"] = customData;
    
    nlohmann::json partsArray = nlohmann::json::array();
    for (const auto& part : parts) {
        partsArray.push_back(part.toJSON());
    }
    j["parts"] = partsArray;
    
    return j;
}

Character Character::fromJSON(const nlohmann::json& j) {
    Character character;
    character.characterID = j.value("characterID", "");
    character.name = j.value("name", "");
    character.description = j.value("description", "");
    character.basePrefabID = j.value("basePrefabID", 0);
    
    if (j.contains("tags") && j["tags"].is_array()) {
        for (const auto& tag : j["tags"]) {
            if (tag.is_string()) {
                character.tags.push_back(tag.get<std::string>());
            }
        }
    }
    
    if (j.contains("parts") && j["parts"].is_array()) {
        for (const auto& partJson : j["parts"]) {
            character.parts.push_back(AttachedPartInstance::fromJSON(partJson));
        }
    }
    
    if (j.contains("customData")) {
        character.customData = j["customData"];
    }
    
    return character;
}

// ============================================================
// CharacterManager Implementation
// ============================================================

CharacterManager::CharacterManager() = default;
CharacterManager::~CharacterManager() = default;

bool CharacterManager::initialize(sqlite3* db, PartLibrary* partLibrary) {
    if (!db) {
        setError("Null database pointer");
        return false;
    }
    m_db = db;
    m_partLibrary = partLibrary;
    return true;
}

std::string CharacterManager::generateUUID() {
    static std::random_device rd;
    static std::mt19937_64 gen(rd());
    static std::uniform_int_distribution<uint64_t> dis;
    
    uint64_t a = dis(gen);
    uint64_t b = dis(gen);
    
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
// Prefab Operations
// ============================================================

int64_t CharacterManager::savePrefab(const CharacterPrefab& prefab) {
    if (!m_db) {
        setError("Database not initialized");
        return -1;
    }
    
    std::string jsonStr = prefab.toJSON().dump();
    
    std::string tagsStr;
    for (size_t i = 0; i < prefab.tags.size(); ++i) {
        if (i > 0) tagsStr += ",";
        tagsStr += prefab.tags[i];
    }
    
    // Check if prefab exists
    const char* checkSql = "SELECT ID FROM CharacterPrefabs WHERE PrefabID = ?;";
    sqlite3_stmt* checkStmt = nullptr;
    int64_t existingID = -1;
    
    if (sqlite3_prepare_v2(m_db, checkSql, -1, &checkStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(checkStmt, 1, prefab.prefabID.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(checkStmt) == SQLITE_ROW) {
            existingID = sqlite3_column_int64(checkStmt, 0);
        }
        sqlite3_finalize(checkStmt);
    }
    
    sqlite3_stmt* stmt = nullptr;
    int rc;
    
    if (existingID > 0) {
        const char* sql = R"SQL(
            UPDATE CharacterPrefabs SET 
                Name = ?, Category = ?, Description = ?, PrefabJSON = ?, Tags = ?,
                Thumbnail = ?, ModifiedAt = strftime('%s', 'now')
            WHERE ID = ?;
        )SQL";
        
        rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            setError("Failed to prepare prefab update");
            return -1;
        }
        
        sqlite3_bind_text(stmt, 1, prefab.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, prefab.category.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, prefab.description.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, jsonStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, tagsStr.c_str(), -1, SQLITE_TRANSIENT);
        
        if (!prefab.thumbnail.empty()) {
            sqlite3_bind_blob(stmt, 6, prefab.thumbnail.data(), 
                             static_cast<int>(prefab.thumbnail.size()), SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, 6);
        }
        
        sqlite3_bind_int64(stmt, 7, existingID);
    } else {
        const char* sql = R"SQL(
            INSERT INTO CharacterPrefabs (PrefabID, Name, Category, Description, PrefabJSON, Tags, Thumbnail)
            VALUES (?, ?, ?, ?, ?, ?, ?);
        )SQL";
        
        rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            setError("Failed to prepare prefab insert");
            return -1;
        }
        
        sqlite3_bind_text(stmt, 1, prefab.prefabID.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, prefab.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, prefab.category.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, prefab.description.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, jsonStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, tagsStr.c_str(), -1, SQLITE_TRANSIENT);
        
        if (!prefab.thumbnail.empty()) {
            sqlite3_bind_blob(stmt, 7, prefab.thumbnail.data(), 
                             static_cast<int>(prefab.thumbnail.size()), SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, 7);
        }
    }
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        setError("Failed to save prefab: " + std::string(sqlite3_errmsg(m_db)));
        return -1;
    }
    
    int64_t resultID = (existingID > 0) ? existingID : sqlite3_last_insert_rowid(m_db);
    PLOGI << "Saved prefab '" << prefab.name << "' with ID " << resultID;
    return resultID;
}

std::unique_ptr<CharacterPrefab> CharacterManager::loadPrefab(int64_t dbID) {
    if (!m_db) return nullptr;
    
    const char* sql = "SELECT PrefabJSON, Thumbnail FROM CharacterPrefabs WHERE ID = ?;";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return nullptr;
    }
    
    sqlite3_bind_int64(stmt, 1, dbID);
    
    std::unique_ptr<CharacterPrefab> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* jsonText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (jsonText) {
            try {
                nlohmann::json j = nlohmann::json::parse(jsonText);
                result = std::make_unique<CharacterPrefab>(CharacterPrefab::fromJSON(j));
                
                // Load thumbnail
                const void* thumbBlob = sqlite3_column_blob(stmt, 1);
                int thumbSize = sqlite3_column_bytes(stmt, 1);
                if (thumbBlob && thumbSize > 0) {
                    result->thumbnail.assign(
                        static_cast<const uint8_t*>(thumbBlob),
                        static_cast<const uint8_t*>(thumbBlob) + thumbSize
                    );
                }
            } catch (const std::exception& e) {
                PLOGE << "Failed to parse prefab JSON: " << e.what();
            }
        }
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::unique_ptr<CharacterPrefab> CharacterManager::loadPrefabByUUID(const std::string& uuid) {
    if (!m_db) return nullptr;
    
    const char* sql = "SELECT ID FROM CharacterPrefabs WHERE PrefabID = ?;";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return nullptr;
    }
    
    sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
    
    int64_t dbID = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        dbID = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    
    if (dbID > 0) {
        return loadPrefab(dbID);
    }
    return nullptr;
}

bool CharacterManager::deletePrefab(int64_t dbID) {
    if (!m_db) return false;
    
    const char* sql = "DELETE FROM CharacterPrefabs WHERE ID = ?;";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_int64(stmt, 1, dbID);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

std::vector<PrefabSummary> CharacterManager::getAllPrefabSummaries() {
    std::vector<PrefabSummary> result;
    if (!m_db) return result;
    
    const char* sql = R"SQL(
        SELECT ID, PrefabID, Name, Category, Description, Tags,
               Thumbnail IS NOT NULL, CreatedAt, ModifiedAt, PrefabJSON
        FROM CharacterPrefabs ORDER BY Category, Name;
    )SQL";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PrefabSummary summary;
        summary.dbID = sqlite3_column_int64(stmt, 0);
        summary.prefabID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        summary.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        
        const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        summary.category = cat ? cat : "";
        
        const char* desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        summary.description = desc ? desc : "";
        
        const char* tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        summary.tags = tags ? tags : "";
        
        summary.hasThumbnail = sqlite3_column_int(stmt, 6) != 0;
        summary.createdAt = sqlite3_column_int64(stmt, 7);
        summary.modifiedAt = sqlite3_column_int64(stmt, 8);
        
        // Count parts from JSON
        const char* jsonText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 9));
        if (jsonText) {
            try {
                nlohmann::json j = nlohmann::json::parse(jsonText);
                if (j.contains("parts") && j["parts"].is_array()) {
                    summary.partCount = static_cast<int>(j["parts"].size());
                }
            } catch (...) {}
        }
        
        result.push_back(summary);
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::vector<PrefabSummary> CharacterManager::getPrefabsByCategory(const std::string& category) {
    std::vector<PrefabSummary> result;
    if (!m_db) return result;
    
    const char* sql = R"SQL(
        SELECT ID, PrefabID, Name, Category, Description, Tags,
               Thumbnail IS NOT NULL, CreatedAt, ModifiedAt
        FROM CharacterPrefabs WHERE Category = ? ORDER BY Name;
    )SQL";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }
    
    sqlite3_bind_text(stmt, 1, category.c_str(), -1, SQLITE_TRANSIENT);
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        PrefabSummary summary;
        summary.dbID = sqlite3_column_int64(stmt, 0);
        summary.prefabID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        summary.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        
        const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        summary.category = cat ? cat : "";
        
        const char* desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        summary.description = desc ? desc : "";
        
        const char* tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        summary.tags = tags ? tags : "";
        
        summary.hasThumbnail = sqlite3_column_int(stmt, 6) != 0;
        summary.createdAt = sqlite3_column_int64(stmt, 7);
        summary.modifiedAt = sqlite3_column_int64(stmt, 8);
        
        result.push_back(summary);
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::vector<PrefabSummary> CharacterManager::searchPrefabs(const std::string& query) {
    std::vector<PrefabSummary> result;
    if (!m_db || query.empty()) return result;
    
    const char* sql = R"SQL(
        SELECT ID, PrefabID, Name, Category, Description, Tags,
               Thumbnail IS NOT NULL, CreatedAt, ModifiedAt
        FROM CharacterPrefabs 
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
        PrefabSummary summary;
        summary.dbID = sqlite3_column_int64(stmt, 0);
        summary.prefabID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        summary.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        
        const char* cat = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        summary.category = cat ? cat : "";
        
        const char* desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        summary.description = desc ? desc : "";
        
        const char* tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        summary.tags = tags ? tags : "";
        
        summary.hasThumbnail = sqlite3_column_int(stmt, 6) != 0;
        summary.createdAt = sqlite3_column_int64(stmt, 7);
        summary.modifiedAt = sqlite3_column_int64(stmt, 8);
        
        result.push_back(summary);
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::vector<std::string> CharacterManager::getAllPrefabCategories() {
    std::vector<std::string> result;
    if (!m_db) return result;
    
    const char* sql = "SELECT DISTINCT Category FROM CharacterPrefabs WHERE Category != '' ORDER BY Category;";
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

// ============================================================
// Character Operations
// ============================================================

int64_t CharacterManager::saveCharacter(const Character& character) {
    if (!m_db) {
        setError("Database not initialized");
        return -1;
    }
    
    std::string jsonStr = character.toJSON().dump();
    
    std::string tagsStr;
    for (size_t i = 0; i < character.tags.size(); ++i) {
        if (i > 0) tagsStr += ",";
        tagsStr += character.tags[i];
    }
    
    // Check if character exists
    const char* checkSql = "SELECT ID FROM Characters WHERE CharacterID = ?;";
    sqlite3_stmt* checkStmt = nullptr;
    int64_t existingID = -1;
    
    if (sqlite3_prepare_v2(m_db, checkSql, -1, &checkStmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(checkStmt, 1, character.characterID.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(checkStmt) == SQLITE_ROW) {
            existingID = sqlite3_column_int64(checkStmt, 0);
        }
        sqlite3_finalize(checkStmt);
    }
    
    sqlite3_stmt* stmt = nullptr;
    int rc;
    
    if (existingID > 0) {
        const char* sql = R"SQL(
            UPDATE Characters SET 
                Name = ?, Description = ?, BasePrefabID = ?, CharacterJSON = ?, Tags = ?,
                Thumbnail = ?, ModifiedAt = strftime('%s', 'now')
            WHERE ID = ?;
        )SQL";
        
        rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            setError("Failed to prepare character update");
            return -1;
        }
        
        sqlite3_bind_text(stmt, 1, character.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, character.description.c_str(), -1, SQLITE_TRANSIENT);
        
        if (character.basePrefabID > 0) {
            sqlite3_bind_int64(stmt, 3, character.basePrefabID);
        } else {
            sqlite3_bind_null(stmt, 3);
        }
        
        sqlite3_bind_text(stmt, 4, jsonStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 5, tagsStr.c_str(), -1, SQLITE_TRANSIENT);
        
        if (!character.thumbnail.empty()) {
            sqlite3_bind_blob(stmt, 6, character.thumbnail.data(), 
                             static_cast<int>(character.thumbnail.size()), SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, 6);
        }
        
        sqlite3_bind_int64(stmt, 7, existingID);
    } else {
        const char* sql = R"SQL(
            INSERT INTO Characters (CharacterID, Name, Description, BasePrefabID, CharacterJSON, Tags, Thumbnail)
            VALUES (?, ?, ?, ?, ?, ?, ?);
        )SQL";
        
        rc = sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            setError("Failed to prepare character insert");
            return -1;
        }
        
        sqlite3_bind_text(stmt, 1, character.characterID.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, character.name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, character.description.c_str(), -1, SQLITE_TRANSIENT);
        
        if (character.basePrefabID > 0) {
            sqlite3_bind_int64(stmt, 4, character.basePrefabID);
        } else {
            sqlite3_bind_null(stmt, 4);
        }
        
        sqlite3_bind_text(stmt, 5, jsonStr.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 6, tagsStr.c_str(), -1, SQLITE_TRANSIENT);
        
        if (!character.thumbnail.empty()) {
            sqlite3_bind_blob(stmt, 7, character.thumbnail.data(), 
                             static_cast<int>(character.thumbnail.size()), SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(stmt, 7);
        }
    }
    
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    if (rc != SQLITE_DONE) {
        setError("Failed to save character: " + std::string(sqlite3_errmsg(m_db)));
        return -1;
    }
    
    int64_t resultID = (existingID > 0) ? existingID : sqlite3_last_insert_rowid(m_db);
    PLOGI << "Saved character '" << character.name << "' with ID " << resultID;
    return resultID;
}

std::unique_ptr<Character> CharacterManager::loadCharacter(int64_t dbID) {
    if (!m_db) return nullptr;
    
    const char* sql = "SELECT CharacterJSON, Thumbnail, BasePrefabID FROM Characters WHERE ID = ?;";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return nullptr;
    }
    
    sqlite3_bind_int64(stmt, 1, dbID);
    
    std::unique_ptr<Character> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* jsonText = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        if (jsonText) {
            try {
                nlohmann::json j = nlohmann::json::parse(jsonText);
                result = std::make_unique<Character>(Character::fromJSON(j));
                
                // Load thumbnail
                const void* thumbBlob = sqlite3_column_blob(stmt, 1);
                int thumbSize = sqlite3_column_bytes(stmt, 1);
                if (thumbBlob && thumbSize > 0) {
                    result->thumbnail.assign(
                        static_cast<const uint8_t*>(thumbBlob),
                        static_cast<const uint8_t*>(thumbBlob) + thumbSize
                    );
                }
                
                // Get base prefab ID
                if (sqlite3_column_type(stmt, 2) != SQLITE_NULL) {
                    result->basePrefabID = sqlite3_column_int64(stmt, 2);
                }
            } catch (const std::exception& e) {
                PLOGE << "Failed to parse character JSON: " << e.what();
            }
        }
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::unique_ptr<Character> CharacterManager::loadCharacterByUUID(const std::string& uuid) {
    if (!m_db) return nullptr;
    
    const char* sql = "SELECT ID FROM Characters WHERE CharacterID = ?;";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return nullptr;
    }
    
    sqlite3_bind_text(stmt, 1, uuid.c_str(), -1, SQLITE_TRANSIENT);
    
    int64_t dbID = -1;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        dbID = sqlite3_column_int64(stmt, 0);
    }
    sqlite3_finalize(stmt);
    
    if (dbID > 0) {
        return loadCharacter(dbID);
    }
    return nullptr;
}

bool CharacterManager::deleteCharacter(int64_t dbID) {
    if (!m_db) return false;
    
    const char* sql = "DELETE FROM Characters WHERE ID = ?;";
    sqlite3_stmt* stmt = nullptr;
    
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    
    sqlite3_bind_int64(stmt, 1, dbID);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    
    return rc == SQLITE_DONE;
}

std::vector<CharacterSummary> CharacterManager::getAllCharacterSummaries() {
    std::vector<CharacterSummary> result;
    if (!m_db) return result;
    
    const char* sql = R"SQL(
        SELECT ID, CharacterID, Name, Description, Tags,
               BasePrefabID, Thumbnail IS NOT NULL, CreatedAt, ModifiedAt
        FROM Characters ORDER BY Name;
    )SQL";
    
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return result;
    }
    
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        CharacterSummary summary;
        summary.dbID = sqlite3_column_int64(stmt, 0);
        summary.characterID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        summary.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        
        const char* desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        summary.description = desc ? desc : "";
        
        const char* tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        summary.tags = tags ? tags : "";
        
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
            summary.basePrefabID = sqlite3_column_int64(stmt, 5);
        }
        
        summary.hasThumbnail = sqlite3_column_int(stmt, 6) != 0;
        summary.createdAt = sqlite3_column_int64(stmt, 7);
        summary.modifiedAt = sqlite3_column_int64(stmt, 8);
        
        result.push_back(summary);
    }
    
    sqlite3_finalize(stmt);
    return result;
}

std::vector<CharacterSummary> CharacterManager::searchCharacters(const std::string& query) {
    std::vector<CharacterSummary> result;
    if (!m_db || query.empty()) return result;
    
    const char* sql = R"SQL(
        SELECT ID, CharacterID, Name, Description, Tags,
               BasePrefabID, Thumbnail IS NOT NULL, CreatedAt, ModifiedAt
        FROM Characters 
        WHERE Name LIKE ? OR Description LIKE ? OR Tags LIKE ?
        ORDER BY Name;
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
        CharacterSummary summary;
        summary.dbID = sqlite3_column_int64(stmt, 0);
        summary.characterID = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        summary.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        
        const char* desc = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        summary.description = desc ? desc : "";
        
        const char* tags = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        summary.tags = tags ? tags : "";
        
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) {
            summary.basePrefabID = sqlite3_column_int64(stmt, 5);
        }
        
        summary.hasThumbnail = sqlite3_column_int(stmt, 6) != 0;
        summary.createdAt = sqlite3_column_int64(stmt, 7);
        summary.modifiedAt = sqlite3_column_int64(stmt, 8);
        
        result.push_back(summary);
    }
    
    sqlite3_finalize(stmt);
    return result;
}

// ============================================================
// Creation Helpers
// ============================================================

CharacterPrefab CharacterManager::createPrefabFromParts(const std::string& name,
                                                         const std::vector<ActiveAttachment>& attachments) {
    CharacterPrefab prefab;
    prefab.prefabID = generateUUID();
    prefab.name = name;
    
    for (const auto& attachment : attachments) {
        AttachedPartInstance inst;
        inst.partID = attachment.partID;
        inst.attachmentID = attachment.attachmentID;
        inst.parentSocketID = attachment.socketID;
        inst.localTransform = attachment.transform;
        
        prefab.parts.push_back(inst);
        
        // First part is root
        if (prefab.rootPartID.empty()) {
            prefab.rootPartID = attachment.partID;
        }
    }
    
    return prefab;
}

Character CharacterManager::createCharacterFromPrefab(const std::string& name, const CharacterPrefab& prefab) {
    Character character;
    character.characterID = generateUUID();
    character.name = name;
    
    // Copy parts from prefab
    character.parts = prefab.parts;
    
    // Note: basePrefabID would be set when saving if we have the prefab's DB ID
    
    return character;
}

Character CharacterManager::createEmptyCharacter(const std::string& name) {
    Character character;
    character.characterID = generateUUID();
    character.name = name;
    return character;
}

} // namespace CharacterEditor
