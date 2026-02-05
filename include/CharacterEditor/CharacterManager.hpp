#pragma once
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <cstdint>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>
#include <CharacterEditor/Part.hpp>
#include <CharacterEditor/PartLibrary.hpp>
#include <sqlite3.h>

namespace CharacterEditor {

/**
 * @brief Attached part instance in a prefab/character
 */
struct AttachedPartInstance {
    std::string partID;          // Part UUID
    std::string attachmentID;    // Unique attachment instance ID
    std::string parentSocketID;  // Socket ID this part attaches to (empty for root)
    Transform localTransform;    // Local transform adjustments
    
    // Serialization
    nlohmann::json toJSON() const;
    static AttachedPartInstance fromJSON(const nlohmann::json& j);
};

/**
 * @brief Summary info for prefab listing
 */
struct PrefabSummary {
    int64_t dbID = 0;
    std::string prefabID;
    std::string name;
    std::string category;
    std::string description;
    std::string tags;
    int partCount = 0;
    bool hasThumbnail = false;
    int64_t createdAt = 0;
    int64_t modifiedAt = 0;
};

/**
 * @brief Character prefab - reusable template of combined parts
 */
struct CharacterPrefab {
    std::string prefabID;
    std::string name;
    std::string category;
    std::string description;
    std::vector<std::string> tags;
    
    // Parts in this prefab
    std::vector<AttachedPartInstance> parts;
    
    // Root part (typically a body/torso)
    std::string rootPartID;
    
    // Thumbnail
    std::vector<uint8_t> thumbnail;
    
    CharacterPrefab() = default;
    
    // Serialization
    nlohmann::json toJSON() const;
    static CharacterPrefab fromJSON(const nlohmann::json& j);
    
    int getPartCount() const { return static_cast<int>(parts.size()); }
};

/**
 * @brief Summary info for character listing
 */
struct CharacterSummary {
    int64_t dbID = 0;
    std::string characterID;
    std::string name;
    std::string description;
    std::string tags;
    int64_t basePrefabID = 0;
    bool hasThumbnail = false;
    int64_t createdAt = 0;
    int64_t modifiedAt = 0;
};

/**
 * @brief Character instance - can be from scratch or derived from prefab
 */
struct Character {
    std::string characterID;
    std::string name;
    std::string description;
    std::vector<std::string> tags;
    
    // Base prefab (if any)
    int64_t basePrefabID = 0;
    
    // Parts configuration (may override/extend prefab)
    std::vector<AttachedPartInstance> parts;
    
    // Custom data (character-specific properties)
    nlohmann::json customData;
    
    // Thumbnail
    std::vector<uint8_t> thumbnail;
    
    Character() = default;
    
    // Serialization
    nlohmann::json toJSON() const;
    static Character fromJSON(const nlohmann::json& j);
};

/**
 * @brief CharacterManager - handles prefab and character storage/manipulation
 * 
 * Works with PartLibrary to:
 * - Create prefabs from sets of attached parts
 * - Create characters from scratch or fine-tune from prefabs
 * - Save/load to vault database
 */
class CharacterManager {
public:
    CharacterManager();
    ~CharacterManager();
    
    /**
     * @brief Initialize with database and part library
     */
    bool initialize(sqlite3* db, PartLibrary* partLibrary);
    
    // ===== Prefab Operations =====
    
    /**
     * @brief Save a prefab to the database
     * @return Database ID or -1 on failure
     */
    int64_t savePrefab(const CharacterPrefab& prefab);
    
    /**
     * @brief Load a prefab by database ID
     */
    std::unique_ptr<CharacterPrefab> loadPrefab(int64_t dbID);
    
    /**
     * @brief Load a prefab by UUID
     */
    std::unique_ptr<CharacterPrefab> loadPrefabByUUID(const std::string& uuid);
    
    /**
     * @brief Delete a prefab
     */
    bool deletePrefab(int64_t dbID);
    
    /**
     * @brief Get all prefab summaries
     */
    std::vector<PrefabSummary> getAllPrefabSummaries();
    
    /**
     * @brief Get prefabs by category
     */
    std::vector<PrefabSummary> getPrefabsByCategory(const std::string& category);
    
    /**
     * @brief Search prefabs by name/tags
     */
    std::vector<PrefabSummary> searchPrefabs(const std::string& query);
    
    /**
     * @brief Get all prefab categories
     */
    std::vector<std::string> getAllPrefabCategories();
    
    // ===== Character Operations =====
    
    /**
     * @brief Save a character to the database
     * @return Database ID or -1 on failure
     */
    int64_t saveCharacter(const Character& character);
    
    /**
     * @brief Load a character by database ID
     */
    std::unique_ptr<Character> loadCharacter(int64_t dbID);
    
    /**
     * @brief Load a character by UUID
     */
    std::unique_ptr<Character> loadCharacterByUUID(const std::string& uuid);
    
    /**
     * @brief Delete a character
     */
    bool deleteCharacter(int64_t dbID);
    
    /**
     * @brief Get all character summaries
     */
    std::vector<CharacterSummary> getAllCharacterSummaries();
    
    /**
     * @brief Search characters by name/tags
     */
    std::vector<CharacterSummary> searchCharacters(const std::string& query);
    
    // ===== Creation Helpers =====
    
    /**
     * @brief Create a new prefab from current attached parts
     */
    CharacterPrefab createPrefabFromParts(const std::string& name, 
                                           const std::vector<ActiveAttachment>& attachments);
    
    /**
     * @brief Create a new character from a prefab
     */
    Character createCharacterFromPrefab(const std::string& name, const CharacterPrefab& prefab);
    
    /**
     * @brief Create an empty character (from scratch)
     */
    Character createEmptyCharacter(const std::string& name);
    
    /**
     * @brief Generate a new UUID
     */
    static std::string generateUUID();
    
    /**
     * @brief Get last error message
     */
    const std::string& getLastError() const { return m_lastError; }
    
private:
    sqlite3* m_db = nullptr;
    PartLibrary* m_partLibrary = nullptr;
    std::string m_lastError;
    
    void setError(const std::string& err) { m_lastError = err; }
};

} // namespace CharacterEditor
