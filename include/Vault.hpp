#pragma once
#include <string>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <utility>
#include <imgui.h>
#include <imgui_stdlib.h>
#include <plog/Log.h>
#include "MarkdownText.hpp"
#include <sqlite3.h>
#include <unordered_set>
#include <unordered_map>
#include <sstream>
#include <queue>
#include <cctype>
#include <fstream>
#include <cstring>
#include <ctime>
#include <mutex>
#include <curl/curl.h>
#include <thread>
#include <memory>
#include "ModelViewer.hpp"
#include "CryptoHelpers.hpp"
#include "DBBackend.hpp"

// Configuration for opening a vault with either local sqlite or remote MySQL
struct VaultConfig {
    LoreBook::DBConnectionInfo connInfo;
    bool createIfMissing = true; // if true attempt to create DB/tables when opening remote
};

class Vault{
    // forward declaration for chat render helper
    friend void RenderVaultChat(Vault* vault);
    int64_t id;
    std::string name;
    int64_t selectedItemID = -1;
    sqlite3* dbConnection = nullptr;
    std::unique_ptr<LoreBook::IDBBackend> dbBackend = nullptr;

    // Content editor state
    std::string currentContent;
    std::string currentTitle;
    std::string currentTags;
    int64_t loadedItemID = -1;
    bool contentDirty = false;
    float lastSaveTime = 0.0f;

    // UI status
    std::string statusMessage;
    float statusTime = 0.0f;

    // Tag filter state (used by UI like GraphView)
    std::vector<std::string> activeTagFilter;
    bool tagFilterModeAll = true; // true=AND (all), false=OR (any)

    // UI context menu state
    int64_t renameTargetID = -1;
    char renameBuf[256] = "";
    bool showRenameModal = false;
    int64_t deleteTargetID = -1;
    bool showDeleteModal = false;

    // Import Markdown UI state (shown from context menu)
    bool showImportModal = false;
    std::filesystem::path importPath = std::filesystem::current_path();
    std::unordered_set<std::string> importSelectedFiles;
    int64_t importParentID = -1;
    // Path (ancestors) captured at modal open so imports inherit tags from the exact UI path
    std::vector<int64_t> importParentPath;

    // Import Asset UI state (file browser and upload)
    bool showImportAssetModal = false;
    std::filesystem::path importAssetPath = std::filesystem::current_path();
    std::unordered_set<std::string> importAssetSelectedFiles;
    bool showAssetUploadedModal = false;
    std::vector<std::string> lastUploadedExternalPaths; 
    // Virtual path destination for import (e.g. "folder/subfolder") - empty = root
    char importAssetDestFolderBuf[1024] = "";

    // Overwrite / conflict modal state when an exact vault path collision occurs
    bool showOverwriteConfirmModal = false;
    std::string overwritePendingLocalFile; // local filepath waiting for user decision
    std::string overwriteTargetExternalPath; // e.g. "vault://Assets/folder/file.png"
    int64_t overwriteExistingAttachmentID = -1; 

    // Attachment UI state
    bool showAttachmentPreview = false;
    int64_t previewAttachmentID = -1;
    std::vector<uint8_t> previewRawData;
    std::string previewMime;
    std::string previewName;
    bool previewIsRaw = false;
    char attachPathBuf[1024] = "";
    char attachmentSavePathBuf[1024] = "";
    // Display size state for previewed attachment (0 = unset/default)
    int previewDisplayWidth = 0;
    int previewDisplayHeight = 0;

    // Model viewer state
    bool showModelViewer = false;
    std::unique_ptr<ModelViewer> modelViewer;

    // Cached per-src inline model viewers (keyed by original src string)
    std::unordered_map<std::string, std::unique_ptr<ModelViewer>> modelViewerCache;

    // Pending reload requests from background fetches; entries are src keys (external URL or vault://attachment/<id>)
    std::vector<std::string> pendingViewerReloads;

    // Queue of tasks that must run on the main/UI thread (e.g., GL uploads after async fetch)
    std::vector<std::function<void()>> pendingMainThreadTasks;
    std::mutex pendingTasksMutex;

    // Per-node child filters (display-only). Keys are node IDs; values are a filter spec.
    struct ChildFilterSpec { std::string mode = "AND"; std::vector<std::string> tags; std::string expr; };
    std::unordered_map<int64_t, ChildFilterSpec> nodeChildFilters;

    // DB mutex for background fetch updates
    std::mutex dbMutex;

    // Current authenticated user
    int64_t currentUserID = -1;
    std::string currentUserDisplayName;

    // UI state for Set Filter modal
    bool showSetFilterModal = false;
    int64_t setFilterTargetID = -1;
    char setFilterBuf[256] = ""; // expression or helper text
    // Modal initialization and state helpers
    std::vector<std::string> setFilterInitialTags;
    int setFilterModeDefault = 0; // 0=AND,1=OR,2=EXPR
    std::vector<std::string> setFilterSelectedTags;
    int setFilterMode = 0;

public:
    // Return whether the underlying DB is open
    bool isOpen() const { return dbConnection != nullptr || (dbBackend && dbBackend->isOpen()); }

    // Factory: open a vault with a VaultConfig (supports local SQLite and remote MySQL)
    static std::unique_ptr<Vault> Open(const VaultConfig& cfg, std::string* outError = nullptr);

    // Expose raw DB connection for helpers (use carefully)
    sqlite3* getDBPublic() const { return dbConnection; }
    // Expose backend pointer for remote DBs (MySQL)
    LoreBook::IDBBackend* getDBBackendPublic() const { return dbBackend.get(); }

    // Public API for GraphView and other UI integrations
    std::vector<std::pair<int64_t,std::string>> getAllItemsPublic(){ return getAllItems(); }
    std::vector<int64_t> getParentsOfPublic(int64_t id){ return getParentsOf(id); }
    void selectItemByID(int64_t id){ selectedItemID = id; }
    int64_t getSelectedItemID() const { return selectedItemID; }

    // Tags helpers for UI
    std::vector<std::string> getTagsOfPublic(int64_t id){ return parseTags(getTagsOf(id)); }
    std::vector<std::string> getAllTagsPublic(){
        std::unordered_set<std::string> s;
        if(dbBackend && dbBackend->isOpen()){
            std::string err;
            auto stmt = dbBackend->prepare("SELECT Tags FROM VaultItems;", &err);
            if(!stmt){ PLOGW << "getAllTags prepare failed: " << err; }
            else{
                auto rs = stmt->executeQuery();
                while(rs && rs->next()){
                    auto tags = parseTags(rs->getString(0));
                    for(auto &t : tags) s.insert(t);
                }
            }
        } else {
            const char* sql = "SELECT Tags FROM VaultItems;";
            sqlite3_stmt* stmt = nullptr;
            if(dbConnection && sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
                while(sqlite3_step(stmt) == SQLITE_ROW){
                    const unsigned char* text = sqlite3_column_text(stmt, 0);
                    if(text){
                        auto tags = parseTags(reinterpret_cast<const char*>(text));
                        for(auto &t : tags) s.insert(t);
                    }
                }
            }
            if(stmt) sqlite3_finalize(stmt);
        }
        std::vector<std::string> out; out.reserve(s.size());
        for(auto &t : s) out.push_back(t);
        std::sort(out.begin(), out.end());
        return out;
    }

    // User & Auth API
    bool hasUsers() const;
    int64_t createUser(const std::string& username, const std::string& displayName, const std::string& passwordPlain, bool isAdmin);
    int64_t authenticateUser(const std::string& username, const std::string& passwordPlain);
    int64_t getCurrentUserID() const;
    std::string getCurrentUserDisplayName() const;
    void setCurrentUser(int64_t userID);
    void clearCurrentUser();
    struct User { int64_t id; std::string username; std::string displayName; bool isAdmin; };
    std::vector<User> listUsers() const;
    bool isUserAdmin(int64_t userID) const;
    bool updateUserDisplayName(int64_t userID, const std::string& newDisplayName);
    bool changeUserPassword(int64_t userID, const std::string& newPasswordPlain);
    bool setUserAdminFlag(int64_t userID, bool isAdmin);
    bool deleteUser(int64_t userID);

    // Permissions API
    struct ItemPermission { int64_t id; int64_t itemID; int64_t userID; int level; int64_t createdAt; };
    bool setItemPermission(int64_t itemID, int64_t userID, int level);
    bool removeItemPermission(int64_t itemID, int64_t userID);
    std::vector<ItemPermission> listItemPermissions(int64_t itemID) const;
    bool isItemVisibleToUser(int64_t itemID, int64_t userID) const;
    bool isItemEditableByUser(int64_t itemID, int64_t userID) const;
    std::vector<std::pair<int64_t,std::string>> getAllItemsForUser(int64_t userID);


    // Tag filter API (used by GraphView to filter tree)
    void setTagFilter(const std::vector<std::string>& tags, bool modeAll){ activeTagFilter = tags; tagFilterModeAll = modeAll; }
    void clearTagFilter(){ activeTagFilter.clear(); }
    std::vector<std::string> getActiveTagFilterPublic() const { return activeTagFilter; }
    bool getTagFilterModeAllPublic() const { return tagFilterModeAll; }
    bool nodeMatchesActiveFilter(int64_t id){
        if(activeTagFilter.empty()) return true;
        auto tags = parseTags(getTagsOf(id));
        if(tagFilterModeAll){
            for(auto &t : activeTagFilter) if(std::find(tags.begin(), tags.end(), t) == tags.end()) return false;
            return true;
        } else {
            for(auto &t : activeTagFilter) if(std::find(tags.begin(), tags.end(), t) != tags.end()) return true;
            return false;
        }
    }

    // Attachments: metadata and API
    struct Attachment {
        int64_t id = -1;
        int64_t itemID = -1;
        std::string name;
        std::string mimeType;
        int64_t size = 0;
        std::string externalPath;
        int64_t createdAt = 0; // unix timestamp
        int displayWidth = 0;  // pixels, 0 = unset
        int displayHeight = 0; // pixels, 0 = unset
    };

    // Adds an attachment (store bytes in DB BLOB). Returns attachment ID or -1 on error.
    int64_t addAttachment(int64_t itemID = -1, const std::string& name = "", const std::string& mimeType = "", const std::vector<uint8_t>& data = std::vector<uint8_t>(), const std::string& externalPath = "");

    // Add or find an attachment by external URL (caches vault-level); returns attachment id (may create placeholder and fetch asynchronously)
    int64_t addAttachmentFromURL(const std::string& url, const std::string& name = "");

    // Add an asset file into the vault Assets namespace (stores blob and sets ExternalPath like 'vault://Assets/<name>')
    int64_t addAssetFromFile(const std::string& filepath, const std::string& desiredName = "");

    // Sanitize a filename into a safe ExternalPath segment
    static std::string sanitizeExternalName(const std::string& name);
    // Sanitize a virtual path (components separated by '/') into a safe relative path
    static std::string sanitizeExternalPath(const std::string& path);

    // Find attachment by external path/url; returns -1 if not found
    int64_t findAttachmentByExternalPath(const std::string& path); 

    // Fetch attachment data synchronously and update DB (blocking) — returns true if fetched
    bool fetchAttachmentNow(int64_t attachmentID, const std::string& url);

    // Start async fetch to populate an existing attachment record's Data and MimeType
    void asyncFetchAndStoreAttachment(int64_t attachmentID, const std::string& url);

    // Retrieve attachment metadata
    Attachment getAttachmentMeta(int64_t attachmentID);

    // List attachments for an item
    std::vector<Attachment> listAttachments(int64_t itemID);

    // Retrieve raw attachment byte data
    std::vector<uint8_t> getAttachmentData(int64_t attachmentID);

    // Remove an attachment (metadata + blob)
    bool removeAttachment(int64_t attachmentID);

    // Set per-attachment display size (0 = unset/default)
    bool setAttachmentDisplaySize(int64_t attachmentID, int width, int height);

    // Open an attachment (or file) referenced by a markdown src (e.g. vault://attachment/123 or file:///path/to/img.png)
    void openPreviewFromSrc(const std::string& src);

    // Open a model preview (loads into ModelViewer).
    void openModelFromSrc(const std::string& src);

    // Get or create an inline ModelViewer for a markdown src (vault://..., http(s) or file://). The returned viewer may not be loaded yet.
    ModelViewer* getOrCreateModelViewerForSrc(const std::string& src);

    // Enqueue a callable to be executed on the main/UI thread. Used to schedule GL uploads and other main-thread-only operations
    void enqueueMainThreadTask(std::function<void()> fn){ std::lock_guard<std::mutex> l(pendingTasksMutex); pendingMainThreadTasks.push_back(std::move(fn)); }

    Vault(std::filesystem::path dbPath,std::string vaultName){
        name = vaultName;
        //open connection to the database
        if(sqlite3_open((dbPath/vaultName).string().c_str(), &dbConnection)){
            //handle error - placeholder
            sqlite3_close(dbConnection);
            dbConnection = nullptr;
        }

        //create tables if they don't exist
        const char* createTableSQL = "CREATE TABLE IF NOT EXISTS VaultItems ("
                                     "ID INTEGER PRIMARY KEY AUTOINCREMENT,"
                                     "Name TEXT NOT NULL,"
                                     "Content TEXT,"
                                     "Tags TEXT,"
                                     "IsRoot INTEGER DEFAULT 0"
                                     ");";
        char* errMsg = nullptr;
        if(sqlite3_exec(dbConnection, createTableSQL, nullptr, nullptr, &errMsg) != SQLITE_OK){
            //handle error - placeholder
            sqlite3_free(errMsg);
        }

        const char* createChildrenTableSQL = "CREATE TABLE IF NOT EXISTS VaultItemChildren ("
                                         "ParentID INTEGER,"
                                         "ChildID INTEGER,"
                                         "FOREIGN KEY(ParentID) REFERENCES VaultItems(ID),"
                                         "FOREIGN KEY(ChildID) REFERENCES VaultItems(ID)"
                                         ");";
        errMsg = nullptr;
        if(sqlite3_exec(dbConnection, createChildrenTableSQL, nullptr, nullptr, &errMsg) != SQLITE_OK){
            //handle error - placeholder
            sqlite3_free(errMsg);
        }

        // Ensure node filters table exists for persisting per-node child filters
        const char* createFiltersTableSQL = "CREATE TABLE IF NOT EXISTS VaultNodeFilters (NodeID INTEGER PRIMARY KEY, Mode TEXT, Tags TEXT, Expr TEXT);";
        errMsg = nullptr;
        if(sqlite3_exec(dbConnection, createFiltersTableSQL, nullptr, nullptr, &errMsg) != SQLITE_OK){ if(errMsg) sqlite3_free(errMsg); }
        // Load any persisted per-node filters
        loadNodeFiltersFromDB();

        // Ensure metadata table exists for schema versioning
        const char* createMetaSQL = "CREATE TABLE IF NOT EXISTS VaultMeta (Key TEXT PRIMARY KEY, Value TEXT);";
        errMsg = nullptr;
        if(sqlite3_exec(dbConnection, createMetaSQL, nullptr, nullptr, &errMsg) != SQLITE_OK){ if(errMsg) sqlite3_free(errMsg); }

        // Ensure attachments table exists for embedded resources
        const char* createAttachmentsSQL = "CREATE TABLE IF NOT EXISTS Attachments ("
                                           "ID INTEGER PRIMARY KEY AUTOINCREMENT,"
                                           "ItemID INTEGER,"
                                           "Name TEXT,"
                                           "MimeType TEXT,"
                                           "Data BLOB,"
                                           "ExternalPath TEXT,"
                                           "Size INTEGER,"
                                           "CreatedAt INTEGER DEFAULT (strftime('%s','now'))," 
                                           "DisplayWidth INTEGER,"
                                           "DisplayHeight INTEGER,"
                                           "FOREIGN KEY(ItemID) REFERENCES VaultItems(ID)"
                                           ");";
        errMsg = nullptr;
        if(sqlite3_exec(dbConnection, createAttachmentsSQL, nullptr, nullptr, &errMsg) != SQLITE_OK){ if(errMsg) sqlite3_free(errMsg); }

        // Migration: ensure DisplayWidth and DisplayHeight columns exist on older DBs
        {
            sqlite3_stmt* pragma = nullptr;
            const char* pragmaSQL = "PRAGMA table_info(Attachments);";
            bool hasDisplayWidth = false;
            bool hasDisplayHeight = false;
            if(sqlite3_prepare_v2(dbConnection, pragmaSQL, -1, &pragma, nullptr) == SQLITE_OK){
                while(sqlite3_step(pragma) == SQLITE_ROW){
                    const unsigned char* colName = sqlite3_column_text(pragma, 1);
                    if(colName){
                        std::string cname(reinterpret_cast<const char*>(colName));
                        if(cname == "DisplayWidth") hasDisplayWidth = true;
                        if(cname == "DisplayHeight") hasDisplayHeight = true;
                    }
                }
            }
            if(pragma) sqlite3_finalize(pragma);
            if(!hasDisplayWidth){
                const char* alter = "ALTER TABLE Attachments ADD COLUMN DisplayWidth INTEGER;";
                char* aErr = nullptr;
                if(sqlite3_exec(dbConnection, alter, nullptr, nullptr, &aErr) != SQLITE_OK){ if(aErr) sqlite3_free(aErr); }
                else { PLOGI << "vault:migration added DisplayWidth column"; }
            }
            if(!hasDisplayHeight){
                const char* alter = "ALTER TABLE Attachments ADD COLUMN DisplayHeight INTEGER;";
                char* aErr = nullptr;
                if(sqlite3_exec(dbConnection, alter, nullptr, nullptr, &aErr) != SQLITE_OK){ if(aErr) sqlite3_free(aErr); }
                else { PLOGI << "vault:migration added DisplayHeight column"; }
            }
        }

        // Ensure ExternalPath index exists
        const char* idxSQL = "CREATE INDEX IF NOT EXISTS idx_Attachments_ExternalPath ON Attachments(ExternalPath);";
        errMsg = nullptr;
        if(sqlite3_exec(dbConnection, idxSQL, nullptr, nullptr, &errMsg) != SQLITE_OK){ if(errMsg) sqlite3_free(errMsg); }

        // Migration: detect old Attachments schema where ItemID was declared NOT NULL, and rebuild table if necessary
        {
            sqlite3_stmt* pragma = nullptr;
            const char* pragmaSQL = "PRAGMA table_info(Attachments);";
            bool needRebuild = false;
            if(sqlite3_prepare_v2(dbConnection, pragmaSQL, -1, &pragma, nullptr) == SQLITE_OK){
                while(sqlite3_step(pragma) == SQLITE_ROW){
                    const unsigned char* colName = sqlite3_column_text(pragma, 1);
                    int notnull = sqlite3_column_int(pragma, 3);
                    if(colName && std::string(reinterpret_cast<const char*>(colName)) == "ItemID" && notnull == 1){ needRebuild = true; break; }
                }
            }
            if(pragma) sqlite3_finalize(pragma);

            if(needRebuild){
                // Rebuild table with nullable ItemID
                const char* beginT = "BEGIN TRANSACTION;";
                const char* createNew = "CREATE TABLE Attachments_new (ID INTEGER PRIMARY KEY AUTOINCREMENT, ItemID INTEGER, Name TEXT, MimeType TEXT, Data BLOB, ExternalPath TEXT, Size INTEGER, CreatedAt INTEGER DEFAULT (strftime('%s','now')));";
                const char* copySQL = "INSERT INTO Attachments_new (ID, ItemID, Name, MimeType, Data, ExternalPath, Size, CreatedAt) SELECT ID, ItemID, Name, MimeType, Data, ExternalPath, Size, CreatedAt FROM Attachments;";
                const char* dropOld = "DROP TABLE Attachments;";
                const char* rename = "ALTER TABLE Attachments_new RENAME TO Attachments;";
                const char* commitT = "COMMIT;";
                char* mErr = nullptr;
                if(sqlite3_exec(dbConnection, beginT, nullptr, nullptr, &mErr) != SQLITE_OK){ if(mErr) sqlite3_free(mErr); }
                mErr = nullptr; if(sqlite3_exec(dbConnection, createNew, nullptr, nullptr, &mErr) != SQLITE_OK){ if(mErr) sqlite3_free(mErr); }
                mErr = nullptr; if(sqlite3_exec(dbConnection, copySQL, nullptr, nullptr, &mErr) != SQLITE_OK){ if(mErr) sqlite3_free(mErr); }
                mErr = nullptr; if(sqlite3_exec(dbConnection, dropOld, nullptr, nullptr, &mErr) != SQLITE_OK){ if(mErr) sqlite3_free(mErr); }
                mErr = nullptr; if(sqlite3_exec(dbConnection, rename, nullptr, nullptr, &mErr) != SQLITE_OK){ if(mErr) sqlite3_free(mErr); }
                mErr = nullptr; if(sqlite3_exec(dbConnection, idxSQL, nullptr, nullptr, &mErr) != SQLITE_OK){ if(mErr) sqlite3_free(mErr); }
                mErr = nullptr; if(sqlite3_exec(dbConnection, commitT, nullptr, nullptr, &mErr) != SQLITE_OK){ if(mErr) sqlite3_free(mErr); }
            }
        }
        errMsg = nullptr;
        if(sqlite3_exec(dbConnection, createAttachmentsSQL, nullptr, nullptr, &errMsg) != SQLITE_OK){ if(errMsg) sqlite3_free(errMsg); }

        // Ensure Users and ItemPermissions tables exist for user & permission support
        const char* createUsersSQL = "CREATE TABLE IF NOT EXISTS Users ("
                                     "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
                                     "Username TEXT NOT NULL UNIQUE, "
                                     "DisplayName TEXT, "
                                     "PasswordHash TEXT, "
                                     "Salt TEXT, "
                                     "Iterations INTEGER DEFAULT 100000, "
                                     "IsAdmin INTEGER DEFAULT 0, "
                                     "CreatedAt INTEGER DEFAULT (strftime('%s','now'))"
                                     ");";
        errMsg = nullptr;
        if(sqlite3_exec(dbConnection, createUsersSQL, nullptr, nullptr, &errMsg) != SQLITE_OK){ if(errMsg) sqlite3_free(errMsg); }

        const char* createPermsSQL = "CREATE TABLE IF NOT EXISTS ItemPermissions ("
                                     "ID INTEGER PRIMARY KEY AUTOINCREMENT, "
                                     "ItemID INTEGER NOT NULL, "
                                     "UserID INTEGER NOT NULL, "
                                     "Level INTEGER NOT NULL DEFAULT 0, "
                                     "CreatedAt INTEGER DEFAULT (strftime('%s','now')), "
                                     "UNIQUE(ItemID, UserID), "
                                     "FOREIGN KEY(ItemID) REFERENCES VaultItems(ID), "
                                     "FOREIGN KEY(UserID) REFERENCES Users(ID)"
                                     ");";
        errMsg = nullptr;
        if(sqlite3_exec(dbConnection, createPermsSQL, nullptr, nullptr, &errMsg) != SQLITE_OK){ if(errMsg) sqlite3_free(errMsg); }

        const char* idxPermsSQL = "CREATE INDEX IF NOT EXISTS idx_ItemPermissions_ItemUser ON ItemPermissions(ItemID, UserID);";
        errMsg = nullptr;
        if(sqlite3_exec(dbConnection, idxPermsSQL, nullptr, nullptr, &errMsg) != SQLITE_OK){ if(errMsg) sqlite3_free(errMsg); }

        // Ensure SchemaVersion entry exists (set to 3 for user support)
        {
            sqlite3_stmt* verStmt = nullptr;
            const char* selectVer = "SELECT Value FROM VaultMeta WHERE Key = 'SchemaVersion';";
            bool hasVer = false;
            int verInt = 0;
            if(sqlite3_prepare_v2(dbConnection, selectVer, -1, &verStmt, nullptr) == SQLITE_OK){
                if(sqlite3_step(verStmt) == SQLITE_ROW){
                    const unsigned char* v = sqlite3_column_text(verStmt, 0);
                    if(v) verInt = std::atoi(reinterpret_cast<const char*>(v));
                    hasVer = true;
                }
            }
            if(verStmt) sqlite3_finalize(verStmt);
            if(!hasVer || verInt < 3){
                const char* insertVer = "INSERT OR REPLACE INTO VaultMeta (Key, Value) VALUES ('SchemaVersion', '3');";
                char* iErr = nullptr;
                if(sqlite3_exec(dbConnection, insertVer, nullptr, nullptr, &iErr) != SQLITE_OK){ if(iErr) sqlite3_free(iErr); }
            }
        }

        // Ensure legacy DBs get the IsRoot column so the root can be identified by ID instead of Name
        {
            sqlite3_stmt* pragma = nullptr;
            bool hasIsRoot = false;
            const char* pragmaSQL = "PRAGMA table_info(VaultItems);";
            if(sqlite3_prepare_v2(dbConnection, pragmaSQL, -1, &pragma, nullptr) == SQLITE_OK){
                while(sqlite3_step(pragma) == SQLITE_ROW){
                    const unsigned char* colName = sqlite3_column_text(pragma, 1);
                    if(colName && std::string(reinterpret_cast<const char*>(colName)) == "IsRoot"){
                        hasIsRoot = true;
                        break;
                    }
                }
            }
            if(pragma) sqlite3_finalize(pragma);
            if(!hasIsRoot){
                const char* alter = "ALTER TABLE VaultItems ADD COLUMN IsRoot INTEGER DEFAULT 0;";
                char* aErr = nullptr;
                if(sqlite3_exec(dbConnection, alter, nullptr, nullptr, &aErr) != SQLITE_OK){
                    sqlite3_free(aErr);
                }
            }
        }

    }

    // Load persisted node filters from DB into memory
            // New constructor: accept IDBBackend for remote backends (MySQL)
        Vault(std::unique_ptr<LoreBook::IDBBackend> backend, const std::string& vaultName){
            name = vaultName;
            dbBackend = std::move(backend);
            // No schema creation yet — existing schema expected or created elsewhere
        }

    void loadNodeFiltersFromDB(){
        if(dbBackend && dbBackend->isOpen()){
            std::string err;
            auto stmt = dbBackend->prepare("SELECT NodeID, Mode, Tags, Expr FROM VaultNodeFilters;", &err);
            if(!stmt){ PLOGW << "loadNodeFilters prepare failed: " << err; return; }
            auto rs = stmt->executeQuery();
            while(rs && rs->next()){
                int64_t nid = rs->getInt64(0);
                ChildFilterSpec s;
                s.mode = rs->getString(1);
                std::string tags = rs->getString(2);
                if(!tags.empty()){
                    size_t pos = 0;
                    while(pos < tags.size()){
                        size_t comma = tags.find(',', pos);
                        std::string part = tags.substr(pos, (comma==std::string::npos? tags.size()-pos : comma-pos));
                        // trim
                        while(!part.empty() && std::isspace((unsigned char)part.front())) part.erase(part.begin());
                        while(!part.empty() && std::isspace((unsigned char)part.back())) part.pop_back();
                        if(!part.empty()) s.tags.push_back(part);
                        if(comma==std::string::npos) break;
                        pos = comma+1;
                    }
                }
                s.expr = rs->getString(3);
                nodeChildFilters[nid] = s;
            }
            return;
        }

        if(!dbConnection) return;
        const char* sql = "SELECT NodeID, Mode, Tags, Expr FROM VaultNodeFilters;";
        sqlite3_stmt* stmt = nullptr;
        if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
            while(sqlite3_step(stmt) == SQLITE_ROW){
                int64_t nid = sqlite3_column_int64(stmt, 0);
                const unsigned char* mode = sqlite3_column_text(stmt,1);
                const unsigned char* tags = sqlite3_column_text(stmt,2);
                const unsigned char* expr = sqlite3_column_text(stmt,3);
                ChildFilterSpec s;
                if(mode) s.mode = reinterpret_cast<const char*>(mode);
                if(tags){
                    std::string t(reinterpret_cast<const char*>(tags));
                    size_t pos = 0;
                    while(pos < t.size()){
                        size_t comma = t.find(',', pos);
                        std::string part = t.substr(pos, (comma==std::string::npos? t.size()-pos : comma-pos));
                        // trim
                        while(!part.empty() && std::isspace((unsigned char)part.front())) part.erase(part.begin());
                        while(!part.empty() && std::isspace((unsigned char)part.back())) part.pop_back();
                        if(!part.empty()) s.tags.push_back(part);
                        if(comma==std::string::npos) break;
                        pos = comma+1;
                    }
                }
                if(expr) s.expr = reinterpret_cast<const char*>(expr);
                nodeChildFilters[nid] = s;
            }
        }
        if(stmt) sqlite3_finalize(stmt);
    }

    void saveNodeFilterToDB(int64_t nodeID, const ChildFilterSpec& spec){
        if(dbBackend && dbBackend->isOpen()){
            std::string err;
            auto stmt = dbBackend->prepare("INSERT OR REPLACE INTO VaultNodeFilters (NodeID, Mode, Tags, Expr) VALUES (?, ?, ?, ?);", &err);
            if(!stmt){ PLOGW << "saveNodeFilter prepare failed: " << err; return; }
            stmt->bindInt(1, nodeID);
            stmt->bindString(2, spec.mode);
            std::string joined;
            for(size_t i=0;i<spec.tags.size();++i){ if(i) joined += ","; joined += spec.tags[i]; }
            stmt->bindString(3, joined);
            stmt->bindString(4, spec.expr);
            stmt->execute();
            return;
        }

        if(!dbConnection) return;
        const char* sql = "INSERT OR REPLACE INTO VaultNodeFilters (NodeID, Mode, Tags, Expr) VALUES (?, ?, ?, ?);";
        sqlite3_stmt* stmt = nullptr;
        if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_int64(stmt,1,nodeID);
            sqlite3_bind_text(stmt,2,spec.mode.c_str(), -1, SQLITE_TRANSIENT);
            std::string joined;
            for(size_t i=0;i<spec.tags.size();++i){ if(i) joined += ","; joined += spec.tags[i]; }
            sqlite3_bind_text(stmt,3,joined.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt,4,spec.expr.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(stmt);
        }
        if(stmt) sqlite3_finalize(stmt);
    }

    void clearNodeFilterFromDB(int64_t nodeID){
        if(dbBackend && dbBackend->isOpen()){
            std::string err;
            auto stmt = dbBackend->prepare("DELETE FROM VaultNodeFilters WHERE NodeID = ?;", &err);
            if(!stmt){ PLOGW << "clearNodeFilter prepare failed: " << err; return; }
            stmt->bindInt(1, nodeID);
            stmt->execute();
            return;
        }

        if(!dbConnection) return;
        const char* sql = "DELETE FROM VaultNodeFilters WHERE NodeID = ?;";
        sqlite3_stmt* stmt = nullptr;
        if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_int64(stmt, 1, nodeID);
            sqlite3_step(stmt);
        }
        if(stmt) sqlite3_finalize(stmt);
    }

    

    int64_t getOrCreateRoot(){
        // If a remote backend is in use, prefer it
        if(dbBackend && dbBackend->isOpen()){
            int64_t id = -1;
            // Try IsRoot flag
            {
                std::string err;
                auto stmt = dbBackend->prepare("SELECT ID FROM VaultItems WHERE IsRoot = 1 LIMIT 1;", &err);
                if(stmt){ auto rs = stmt->executeQuery(); if(rs && rs->next()) return rs->getInt64(0); }
            }
            // Fall back to name-based lookup and set IsRoot
            {
                std::string err;
                auto stmt = dbBackend->prepare("SELECT ID FROM VaultItems WHERE Name = ? LIMIT 1;", &err);
                if(stmt){ stmt->bindString(1, name); auto rs = stmt->executeQuery(); if(rs && rs->next()){ id = rs->getInt64(0); } }
                if(id != -1){ auto u = dbBackend->prepare("UPDATE VaultItems SET IsRoot = 1 WHERE ID = ?;", &err); if(u){ u->bindInt(1, id); u->execute(); } return id; }
            }
            // Find candidate with no parents
            {
                std::string err;
                auto stmt = dbBackend->prepare("SELECT ID FROM VaultItems WHERE ID NOT IN (SELECT ChildID FROM VaultItemChildren) LIMIT 1;", &err);
                if(stmt){ auto rs = stmt->executeQuery(); if(rs && rs->next()){ id = rs->getInt64(0); } }
                if(id != -1){ std::string err2; auto u = dbBackend->prepare("UPDATE VaultItems SET IsRoot = 1 WHERE ID = ?;", &err2); if(u){ u->bindInt(1, id); u->execute(); } return id; }
            }
            // Ensure IsRoot column exists (add if missing)
            if(!dbBackend->hasColumn("VaultItems", "IsRoot")){
                std::string err;
                if(!dbBackend->execute("ALTER TABLE VaultItems ADD COLUMN IsRoot TINYINT DEFAULT 0;", &err)) PLOGW << "MySQL: failed to add IsRoot column: " << err;
            }
            // Insert new root
            {
                std::string err;
                auto stmt = dbBackend->prepare("INSERT INTO VaultItems (Name, Content, Tags, IsRoot) VALUES (?, ?, ?, 1);", &err);
                if(!stmt){ PLOGE << "getOrCreateRoot prepare failed: " << err; return -1; }
                stmt->bindString(1, name);
                stmt->bindString(2, "");
                stmt->bindString(3, "");
                if(!stmt->execute()){ PLOGE << "getOrCreateRoot insert failed"; return -1; }
                return dbBackend->lastInsertId();
            }
        }

        // Local SQLite path
        // Prefer explicit IsRoot flag (so the root is identified by ID, not by mutable Name)
        const char* findByFlag = "SELECT ID FROM VaultItems WHERE IsRoot = 1 LIMIT 1;";
        sqlite3_stmt* stmt = nullptr;
        int64_t id = -1;
        if(dbConnection && sqlite3_prepare_v2(dbConnection, findByFlag, -1, &stmt, nullptr) == SQLITE_OK){
            if(sqlite3_step(stmt) == SQLITE_ROW){
                id = sqlite3_column_int64(stmt, 0);
            }
        }
        if(stmt) sqlite3_finalize(stmt);
        if(id != -1) return id;

        // Fall back to legacy name-based lookup (for older DBs), and mark it IsRoot for future runs
        const char* findByName = "SELECT ID FROM VaultItems WHERE Name = ? LIMIT 1;";
        if(dbConnection && sqlite3_prepare_v2(dbConnection, findByName, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            if(sqlite3_step(stmt) == SQLITE_ROW){
                id = sqlite3_column_int64(stmt, 0);
            }
        }
        if(stmt) sqlite3_finalize(stmt);
        if(id != -1){
            const char* setFlag = "UPDATE VaultItems SET IsRoot = 1 WHERE ID = ?;";
            if(dbConnection && sqlite3_prepare_v2(dbConnection, setFlag, -1, &stmt, nullptr) == SQLITE_OK){
                sqlite3_bind_int64(stmt, 1, id);
                sqlite3_step(stmt);
            }
            if(stmt) sqlite3_finalize(stmt);
            return id;
        }

        // Another fallback: find an item that has no parents (candidate for root)
        const char* findNoParents = "SELECT ID FROM VaultItems WHERE ID NOT IN (SELECT ChildID FROM VaultItemChildren) LIMIT 1;";
        if(dbConnection && sqlite3_prepare_v2(dbConnection, findNoParents, -1, &stmt, nullptr) == SQLITE_OK){
            if(sqlite3_step(stmt) == SQLITE_ROW){
                id = sqlite3_column_int64(stmt, 0);
            }
        }
        if(stmt) sqlite3_finalize(stmt);
        if(id != -1){
            const char* setFlag = "UPDATE VaultItems SET IsRoot = 1 WHERE ID = ?;";
            if(dbConnection && sqlite3_prepare_v2(dbConnection, setFlag, -1, &stmt, nullptr) == SQLITE_OK){
                sqlite3_bind_int64(stmt, 1, id);
                sqlite3_step(stmt);
            }
            if(stmt) sqlite3_finalize(stmt);
            return id;
        }

        // Otherwise, create a new root record and mark it IsRoot
        const char* insertSQL = "INSERT INTO VaultItems (Name, Content, Tags, IsRoot) VALUES (?, ?, ?, 1);";
        if(dbConnection && sqlite3_prepare_v2(dbConnection, insertSQL, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, "", -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, "", -1, SQLITE_STATIC);
            sqlite3_step(stmt);
        }
        if(stmt) sqlite3_finalize(stmt);
        if(dbConnection) return sqlite3_last_insert_rowid(dbConnection);
        return -1;
    }

    void drawVaultTree(){
        ImGui::Begin("Vault Tree");

        // Toolbar: New Note button
        if(ImGui::Button("New Note")){
            ImGui::OpenPopup("Create Note");
        }
        ImGui::SameLine();
        if(ImGui::BeginPopupModal("Create Note", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
            static char newNoteName[256] = "New Note";
            ImGui::InputText("Name", newNoteName, sizeof(newNoteName));
            if(ImGui::Button("Create")){
                std::string nameStr(newNoteName);
                int64_t parent = (selectedItemID >= 0) ? selectedItemID : getOrCreateRoot();
                // Use union-of-ancestors as a toolbar fallback (no explicit UI path available)
                std::vector<std::string> inherited;
                if(selectedItemID >= 0) inherited = collectTagsUnionFromNode(parent);
                int64_t nid = createItem(nameStr, parent);
                if(nid != -1){
                    if(!inherited.empty()){
                        setTagsFor(nid, inherited);
                        std::string s;
                        for(size_t ii=0; ii<inherited.size(); ++ii){ if(ii) s += ","; s += inherited[ii]; }
                        statusMessage = std::string("Inherited tags: ") + s;
                        statusTime = ImGui::GetTime();
                    }
                    selectedItemID = nid;
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if(ImGui::Button("Cancel")){
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }


        // Tag search UI (mirrors GraphView tags) 🔎
        ImGui::Separator();
        ImGui::TextDisabled("Tag filter:"); ImGui::SameLine();
        // Input to add a tag by name
        static char tagSearchBuf[128] = "";
        ImGui::SetNextItemWidth(200);
        if(ImGui::InputTextWithHint("##tagsearch", "Add tag and press Enter", tagSearchBuf, sizeof(tagSearchBuf), ImGuiInputTextFlags_EnterReturnsTrue)){
            std::string t = std::string(tagSearchBuf);
            // trim
            while(!t.empty() && std::isspace((unsigned char)t.front())) t.erase(t.begin());
            while(!t.empty() && std::isspace((unsigned char)t.back())) t.pop_back();
            if(!t.empty()){
                if(std::find(activeTagFilter.begin(), activeTagFilter.end(), t) == activeTagFilter.end()){
                    activeTagFilter.push_back(t);
                }
                tagSearchBuf[0] = '\0';
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("Clear Tags")){
            clearTagFilter();
        }
        ImGui::SameLine();
        ImGui::Text("Mode:"); ImGui::SameLine();
        if(ImGui::RadioButton("All (AND)", tagFilterModeAll)) tagFilterModeAll = true; ImGui::SameLine();
        if(ImGui::RadioButton("Any (OR)", !tagFilterModeAll)) tagFilterModeAll = false;

        // Show active tags as chips
        ImGui::NewLine();
        for(size_t i=0;i<activeTagFilter.size();++i){
            ImGui::PushID(static_cast<int>(i));
            ImGui::TextUnformatted(activeTagFilter[i].c_str()); ImGui::SameLine();
            if(ImGui::SmallButton((std::string("x##t") + std::to_string(i)).c_str())){
                activeTagFilter.erase(activeTagFilter.begin()+i);
            }
            ImGui::SameLine();
            ImGui::PopID();
        }

        // If tag filter is active show matched nodes as a flat list (even if their parent doesn't match)
        if(!activeTagFilter.empty()){
            ImGui::Separator();
            ImGui::Text("Filtered results:");
            ImGui::BeginChild("FilteredList", ImVec2(0,200), true);
            auto all = getAllItems();
            for(auto &it : all){
                int64_t id = it.first; const std::string &name = it.second;
                if(nodeMatchesActiveFilter(id)){
                    std::string label = name + "##" + std::to_string(id);
                    if(ImGui::Selectable(label.c_str(), selectedItemID == id)){
                        selectedItemID = id;
                    }
                    // context menu for filtered view
                    if(ImGui::BeginPopupContextItem((std::string("ctx") + std::to_string(id)).c_str())){
                        if(ImGui::MenuItem("New Child")){
                            int64_t nid = createItem("New Note", id);
                            if(nid != -1){
                                auto inherited = collectTagsUnionFromNode(id);
                                if(!inherited.empty()){
                                    setTagsFor(nid, inherited);
                                    std::string s; for(size_t ii=0; ii<inherited.size(); ++ii){ if(ii) s += ","; s += inherited[ii]; }
                                    statusMessage = std::string("Inherited tags: ") + s; statusTime = ImGui::GetTime();
                                }
                                selectedItemID = nid;
                            }
                        }
                        if(ImGui::MenuItem("Rename")){
                            renameTargetID = id;
                            strncpy(renameBuf, getItemName(id).c_str(), sizeof(renameBuf));
                            showRenameModal = true;
                        }
                        if(ImGui::MenuItem("Delete")){
                            deleteTargetID = id;
                            showDeleteModal = true;
                        }
                        if(ImGui::MenuItem("Import Markdown...")){
                            showImportModal = true;
                            importParentID = id;
                            importParentPath.clear(); importParentPath.push_back(id);
                            importPath = std::filesystem::current_path();
                            importSelectedFiles.clear();
                        }
                        ImGui::EndPopup();
                    }
                }
            }
            ImGui::EndChild();

            // Done with tree; don't render hierarchical view
            ImGui::End();

            // Open rename/delete modals if requested (same as before)
            if(showRenameModal){ ImGui::OpenPopup("Rename Item"); showRenameModal = false; }
            if(ImGui::BeginPopupModal("Rename Item", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
                ImGui::InputText("New Name", renameBuf, sizeof(renameBuf));
                if(ImGui::Button("OK")){
                    if(renameTargetID >= 0){ std::string s(renameBuf); if(!s.empty()){ renameItem(renameTargetID, s); statusMessage = "Renamed"; statusTime = ImGui::GetTime(); } }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(); if(ImGui::Button("Cancel")){ ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }
            if(showDeleteModal){ ImGui::OpenPopup("Delete Item"); showDeleteModal = false; }
            if(ImGui::BeginPopupModal("Delete Item", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
                ImGui::Text("Delete this item? This will remove the item and its relations."); ImGui::Separator();
                if(ImGui::Button("Delete")){
                    if(deleteTargetID >= 0){ if(deleteItem(deleteTargetID)){ statusMessage = "Item deleted"; statusTime = ImGui::GetTime(); } else { statusMessage = "Failed to delete item"; statusTime = ImGui::GetTime(); } }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(); if(ImGui::Button("Cancel")){ ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }

            // Import Markdown modal
            if(showImportModal){ ImGui::OpenPopup("Import Markdown Files"); showImportModal = false; }
            if(ImGui::BeginPopupModal("Import Markdown Files", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
                ImGui::Text("Select Markdown files to import (multiple):");
                ImGui::Separator();
                ImGui::TextWrapped("%s", importPath.string().c_str()); ImGui::SameLine();
                if(ImGui::Button("Up")){
                    if(importPath.has_parent_path()) importPath = importPath.parent_path();
                }
                ImGui::Separator();
                try{
                    std::vector<std::filesystem::directory_entry> dirs, files;
                    for(auto &e : std::filesystem::directory_iterator(importPath)){
                        if(e.is_directory()) dirs.push_back(e);
                        else if(e.is_regular_file()){
                            auto ext = e.path().extension().string();
                            std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                            if(ext == ".md" || ext == ".markdown") files.push_back(e);
                        }
                    }
                    std::sort(dirs.begin(), dirs.end(), [](const auto &a, const auto &b){ return a.path().filename().string() < b.path().filename().string(); });
                    std::sort(files.begin(), files.end(), [](const auto &a, const auto &b){ return a.path().filename().string() < b.path().filename().string(); });
                    for(auto &d : dirs){
                        std::string label = std::string("[DIR] ") + d.path().filename().string();
                        if(ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_DontClosePopups)){
                            importPath = d.path();
                            importSelectedFiles.clear();
                        }
                    }
                    ImGui::Separator();
                    for(auto &f : files){
                        std::string fname = f.path().filename().string();
                        bool sel = importSelectedFiles.find(fname) != importSelectedFiles.end();
                        std::string flabel = (sel ? std::string("[x] ") : std::string("[ ] ")) + fname;
                        if(ImGui::Selectable(flabel.c_str(), sel, ImGuiSelectableFlags_DontClosePopups)){
                            if(sel) importSelectedFiles.erase(fname); else importSelectedFiles.insert(fname);
                        }
                    }
                } catch(...){ ImGui::TextColored(ImVec4(1,0.4f,0.4f,1.0f), "Failed to read directory"); }

                ImGui::Separator();
                if(ImGui::Button("Select All")){
                    try{
                        for(auto &e : std::filesystem::directory_iterator(importPath)){
                            if(e.is_regular_file()){
                                auto ext = e.path().extension().string(); std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                if(ext == ".md" || ext == ".markdown") importSelectedFiles.insert(e.path().filename().string());
                            }
                        }
                    } catch(...){}
                }
                ImGui::SameLine(); if(ImGui::Button("Clear")) importSelectedFiles.clear();
                ImGui::SameLine();
                if(importSelectedFiles.empty()) ImGui::BeginDisabled();
                if(importSelectedFiles.empty()) ImGui::BeginDisabled();
            if(ImGui::Button("Import")){
                int imported = 0;
                int64_t lastId = -1;
                for(auto &fname : importSelectedFiles){
                    try{
                        std::filesystem::path full = importPath / fname;
                        std::ifstream in(full, std::ios::in);
                        if(!in) continue;
                        std::ostringstream ss; ss << in.rdbuf();
                        std::string content = ss.str();
                        std::string title = full.stem().string();
                        std::vector<std::string> inherited = collectTagsFromPath(importParentPath);
                        int64_t id = createItemWithContent(title, content, inherited, importParentID);
                        if(id > 0){ imported++; lastId = id; }
                    } catch(...){ }
                }
                if(imported > 0){ statusMessage = std::string("Imported ") + std::to_string(imported) + " files"; statusTime = ImGui::GetTime(); if(lastId != -1) selectedItemID = lastId; }
                ImGui::CloseCurrentPopup();
            }
            if(importSelectedFiles.empty()) ImGui::EndDisabled();
                if(importSelectedFiles.empty()) ImGui::EndDisabled();
                ImGui::SameLine(); if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
                ImGui::EndPopup();
            }
            return;
        }

        // Draw the vault root (named after the vault) as the single top node
        int64_t rootID = getOrCreateRoot();
        std::vector<int64_t> path;
        // If filtering is active, only draw nodes that match the filter or have matching descendants
        drawVaultNode(-1, rootID, path);

        // Open rename modal if requested
        if(showRenameModal){
            ImGui::OpenPopup("Rename Item");
            showRenameModal = false;
        }
        if(ImGui::BeginPopupModal("Rename Item", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::InputText("New Name", renameBuf, sizeof(renameBuf));
            if(ImGui::Button("OK")){
                if(renameTargetID >= 0){
                    std::string s(renameBuf);
                    if(!s.empty()){
                        renameItem(renameTargetID, s);
                        statusMessage = "Renamed";
                        statusTime = ImGui::GetTime();
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if(ImGui::Button("Cancel")){
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Set Child Filter modal
        if(showSetFilterModal){ ImGui::OpenPopup("Set Child Filter"); showSetFilterModal = false; setFilterSelectedTags = setFilterInitialTags; setFilterMode = setFilterModeDefault; }
        if(ImGui::BeginPopupModal("Set Child Filter", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::Text("Child filter determines which children are displayed under this node.");
            ImGui::Separator();
            ImGui::Text("Mode:"); ImGui::SameLine();
            ImGui::RadioButton("AND", &setFilterMode, 0); ImGui::SameLine();
            ImGui::RadioButton("OR", &setFilterMode, 1); ImGui::SameLine();
            ImGui::RadioButton("Expression", &setFilterMode, 2);
            ImGui::Separator();
            if(setFilterMode != 2){
                // Tag picker from existing tags
                ImGui::Text("Pick tags (selected tags will be required/optional depending on mode):");
                ImGui::Separator();
                auto allTags = getAllTagsPublic();
                for(auto &t : allTags){
                    bool sel = std::find(setFilterSelectedTags.begin(), setFilterSelectedTags.end(), t) != setFilterSelectedTags.end();
                    if(ImGui::Selectable(t.c_str(), sel)){
                        if(sel) setFilterSelectedTags.erase(std::remove(setFilterSelectedTags.begin(), setFilterSelectedTags.end(), t), setFilterSelectedTags.end());
                        else setFilterSelectedTags.push_back(t);
                    }
                }
                ImGui::Separator();
                // Add custom tag
                static char newTagBuf[64];
                ImGui::InputTextWithHint("##newtag","Type a tag and press Add", newTagBuf, sizeof(newTagBuf)); ImGui::SameLine();
                if(ImGui::Button("Add")){
                    std::string nt(newTagBuf); if(!nt.empty()){
                        if(std::find(setFilterSelectedTags.begin(), setFilterSelectedTags.end(), nt) == setFilterSelectedTags.end()) setFilterSelectedTags.push_back(nt);
                        newTagBuf[0] = '\0';
                    }
                }
                // show selected
                ImGui::Text("Selected:");
                for(size_t i=0;i<setFilterSelectedTags.size();++i){ ImGui::SameLine(); ImGui::TextDisabled("%s", setFilterSelectedTags[i].c_str()); }
            } else {
                ImGui::Text("Expression (use && for AND, || for OR, ! for NOT, parentheses allowed):");
                ImGui::InputTextMultiline("##filterexpr", setFilterBuf, sizeof(setFilterBuf), ImVec2(400,120));
                ImGui::TextDisabled("Example: (location && !closed) || (region && tavern)");
            }
            ImGui::Separator();
            if(ImGui::Button("Set")){
                ChildFilterSpec spec;
                if(setFilterMode == 2){ spec.mode = "EXPR"; spec.expr = std::string(setFilterBuf); spec.tags.clear(); }
                else { spec.mode = (setFilterMode == 1) ? "OR" : "AND"; spec.tags = setFilterSelectedTags; spec.expr.clear(); }
                if(spec.mode != "EXPR" && spec.tags.empty()){
                    // empty filter means clear
                    clearNodeFilterFromDB(setFilterTargetID);
                    nodeChildFilters.erase(setFilterTargetID);
                } else {
                    nodeChildFilters[setFilterTargetID] = spec;
                    saveNodeFilterToDB(setFilterTargetID, spec);
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(); if(ImGui::Button("Clear")){ clearNodeFilterFromDB(setFilterTargetID); nodeChildFilters.erase(setFilterTargetID); ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(); if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // Open delete confirmation if requested
        if(showDeleteModal){
            ImGui::OpenPopup("Delete Item");
            showDeleteModal = false;
        }
        if(ImGui::BeginPopupModal("Delete Item", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::Text("Delete this item? This will remove the item and its relations.\nChildren that become parentless will be attached to the root.");
            ImGui::Separator();
            if(ImGui::Button("Delete")){
                if(deleteTargetID >= 0){
                    if(deleteItem(deleteTargetID)){
                        statusMessage = "Item deleted";
                        statusTime = ImGui::GetTime();
                    } else {
                        statusMessage = "Failed to delete item";
                        statusTime = ImGui::GetTime();
                    }
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if(ImGui::Button("Cancel")){
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Import Markdown modal
        if(showImportModal){ ImGui::OpenPopup("Import Markdown Files"); showImportModal = false; }
        if(ImGui::BeginPopupModal("Import Markdown Files", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::Text("Select Markdown files to import (multiple):");
            ImGui::Separator();
            ImGui::TextWrapped("%s", importPath.string().c_str()); ImGui::SameLine();
            if(ImGui::Button("Up")){
                if(importPath.has_parent_path()) importPath = importPath.parent_path();
            }
            ImGui::Separator();
            try{
                std::vector<std::filesystem::directory_entry> dirs, files;
                for(auto &e : std::filesystem::directory_iterator(importPath)){
                    if(e.is_directory()) dirs.push_back(e);
                    else if(e.is_regular_file()){
                        auto ext = e.path().extension().string();
                        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        if(ext == ".md" || ext == ".markdown") files.push_back(e);
                    }
                }
                std::sort(dirs.begin(), dirs.end(), [](const auto &a, const auto &b){ return a.path().filename().string() < b.path().filename().string(); });
                std::sort(files.begin(), files.end(), [](const auto &a, const auto &b){ return a.path().filename().string() < b.path().filename().string(); });
                for(auto &d : dirs){
                    std::string label = std::string("[DIR] ") + d.path().filename().string();
                    if(ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_DontClosePopups)){
                        importPath = d.path();
                        importSelectedFiles.clear();
                    }
                }
                ImGui::Separator();
                for(auto &f : files){
                    std::string fname = f.path().filename().string();
                    bool sel = importSelectedFiles.find(fname) != importSelectedFiles.end();
                    std::string flabel = (sel ? std::string("[x] ") : std::string("[ ] ")) + fname;
                    if(ImGui::Selectable(flabel.c_str(), sel, ImGuiSelectableFlags_DontClosePopups)){
                        if(sel) importSelectedFiles.erase(fname); else importSelectedFiles.insert(fname);
                    }
                }
            } catch(...){ ImGui::TextColored(ImVec4(1,0.4f,0.4f,1.0f), "Failed to read directory"); }

            ImGui::Separator();
            if(ImGui::Button("Select All")){
                try{
                    for(auto &e : std::filesystem::directory_iterator(importPath)){
                        if(e.is_regular_file()){
                            auto ext = e.path().extension().string(); std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                            if(ext == ".md" || ext == ".markdown") importSelectedFiles.insert(e.path().filename().string());
                        }
                    }
                } catch(...){}
            }
            ImGui::SameLine(); if(ImGui::Button("Clear")) importSelectedFiles.clear();
            ImGui::SameLine();
            if(ImGui::Button("Import")){
                int imported = 0;
                int64_t lastId = -1;
                for(auto &fname : importSelectedFiles){
                    try{
                        std::filesystem::path full = importPath / fname;
                        std::ifstream in(full, std::ios::in);
                        if(!in) continue;
                        std::ostringstream ss; ss << in.rdbuf();
                        std::string content = ss.str();
                        std::string title = full.stem().string();
                        std::vector<std::string> inherited = collectTagsFromPath(importParentPath);
                        int64_t id = createItemWithContent(title, content, inherited, importParentID);
                        if(id > 0){ imported++; lastId = id; }
                    } catch(...){ }
                }
                if(imported > 0){ statusMessage = std::string("Imported ") + std::to_string(imported) + " files"; statusTime = ImGui::GetTime(); if(lastId != -1) selectedItemID = lastId; }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(); if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::End();
    }


    // Return whether node or any descendant matches ancestor filter specs.
    bool nodeOrDescendantMatchesWithSpecs(int64_t nodeID, const std::vector<ChildFilterSpec>& inheritedSpecs, std::unordered_set<int64_t> &visited){
        if(visited.find(nodeID) != visited.end()) return false; // cycle protection
        visited.insert(nodeID);
        // Check whether this node satisfies all ancestor specs (if any were provided)
        if(!inheritedSpecs.empty()){
            bool ok = true;
            auto tags = parseTags(getTagsOf(nodeID));
            std::unordered_set<std::string> tagsLower;
            for(auto &t : tags){ std::string s = t; std::transform(s.begin(), s.end(), s.begin(), ::tolower); tagsLower.insert(s); }
            for(const auto &spec : inheritedSpecs){ if(!evalFilterSpecOnTags(spec, tagsLower)){ ok = false; break; } }
            if(ok) return true;
        }
        // Otherwise propagate specs to children, including this node's own child-filter if present
        std::vector<ChildFilterSpec> nextSpecs = inheritedSpecs;
        auto it = nodeChildFilters.find(nodeID);
        if(it != nodeChildFilters.end()) nextSpecs.push_back(it->second);
        std::vector<int64_t> children;
        getChildren(nodeID, children);
        for(auto c : children){ if(nodeOrDescendantMatchesWithSpecs(c, nextSpecs, visited)) return true; }
        return false;
    }

    // Return whether node or any descendant is visible to the user
    bool nodeOrDescendantVisible(int64_t nodeID, int64_t userID, std::unordered_set<int64_t> &visited){
        if(visited.find(nodeID) != visited.end()) return false; // cycle protection
        visited.insert(nodeID);
        if(isItemVisibleToUser(nodeID, userID)) return true;
        std::vector<int64_t> children;
        getChildren(nodeID, children);
        for(auto c : children){ if(nodeOrDescendantVisible(c, userID, visited)) return true; }
        return false;
    }

    // Backwards-compat wrapper used elsewhere where no filters are passed
    bool nodeOrDescendantMatches(int64_t nodeID, std::unordered_set<int64_t> &visited){
        std::vector<ChildFilterSpec> specs;
        return nodeOrDescendantMatchesWithSpecs(nodeID, specs, visited);
    }

    // Simple expression evaluator for filter expressions (supports &&, ||, !, parentheses)
    static std::string toLowerCopy(const std::string &s){ std::string o = s; std::transform(o.begin(), o.end(), o.begin(), ::tolower); return o; }
    static bool evalFilterExpr(const std::string &expr, const std::unordered_set<std::string> &tagsLower){
        // Tokenizer & parser inside
        struct P {
            const std::string &s; size_t p; const std::unordered_set<std::string> &tags; P(const std::string &s_, const std::unordered_set<std::string> &t_): s(s_), p(0), tags(t_){}
            void skip(){ while(p < s.size() && isspace((unsigned char)s[p])) ++p; }
            bool match(const std::string &t){ skip(); if(s.compare(p, t.size(), t)==0){ p += t.size(); return true;} return false; }
            bool parseExpr(){ auto v = parseOr(); skip(); return v; }
            bool parseOr(){ bool v = parseAnd(); skip(); while(true){ if(match("||") || match("or")){ bool r = parseAnd(); v = v || r; } else break; skip(); } return v; }
            bool parseAnd(){ bool v = parseUnary(); skip(); while(true){ if(match("&&") || match("and")){ bool r = parseUnary(); v = v && r; } else break; skip(); } return v; }
            bool parseUnary(){ skip(); if(match("!")) { return !parseUnary(); } if(match("not")) { return !parseUnary(); } return parsePrimary(); }
            bool parsePrimary(){ skip(); if(match("(")){ bool v = parseExpr(); skip(); if(p < s.size() && s[p]==')') ++p; return v; } // identifier
                // parse identifier
                skip(); size_t start = p;
                while(p < s.size() && (isalnum((unsigned char)s[p]) || s[p]=='_' || s[p]=='-' || s[p]=='.')) ++p;
                if(p > start){ std::string tok = s.substr(start, p-start); std::string low = tok; std::transform(low.begin(), low.end(), low.begin(), ::tolower); return tags.find(low) != tags.end(); }
                return false;
            }
        } parser(expr, tagsLower);
        try{ return parser.parseExpr(); } catch(...){ return false; }
    }

    static bool evalFilterSpecOnTags(const ChildFilterSpec &spec, const std::unordered_set<std::string> &tagsLower){
        if(spec.mode == "EXPR"){
            if(spec.expr.empty()) return false;
            return evalFilterExpr(spec.expr, tagsLower);
        } else if(spec.mode == "OR"){
            if(spec.tags.empty()) return false;
            for(auto &t : spec.tags){ std::string low = toLowerCopy(t); if(tagsLower.find(low) != tagsLower.end()) return true; }
            return false;
        } else { // AND
            if(spec.tags.empty()) return false;
            for(auto &t : spec.tags){ std::string low = toLowerCopy(t); if(tagsLower.find(low) == tagsLower.end()) return false; }
            return true;
        }
    }

    void drawVaultContent(){
        // Prevent the Vault Content window from scrolling (keep layout stable)
        ImGui::Begin("Vault Content", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        if(selectedItemID < 0){
            ImGui::TextWrapped("No item selected. Select a note from the Vault Tree on the left.");
            ImGui::End();
            return;
        }

        // If selection changed, save previous content if dirty then load content from DB
        if(loadedItemID != selectedItemID){
            if(contentDirty && loadedItemID >= 0){
                // Prefer remote backend
                if(dbBackend && dbBackend->isOpen()){
                    std::string err;
                    auto stmt = dbBackend->prepare("UPDATE VaultItems SET Content = ? WHERE ID = ?;", &err);
                    if(!stmt){ PLOGW << "save content prepare failed: " << err; }
                    else { stmt->bindString(1, currentContent); stmt->bindInt(2, loadedItemID); if(!stmt->execute()) PLOGW << "save content execute failed"; }
                    contentDirty = false; lastSaveTime = ImGui::GetTime();
                } else if(dbConnection){
                    const char* updateSQL = "UPDATE VaultItems SET Content = ? WHERE ID = ?;";
                    sqlite3_stmt* stmt = nullptr;
                    if(sqlite3_prepare_v2(dbConnection, updateSQL, -1, &stmt, nullptr) == SQLITE_OK){
                        sqlite3_bind_text(stmt, 1, currentContent.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int64(stmt, 2, loadedItemID);
                        sqlite3_step(stmt);
                    }
                    if(stmt) sqlite3_finalize(stmt);
                    contentDirty = false; lastSaveTime = ImGui::GetTime();
                }
            }

            currentContent.clear(); currentTitle.clear(); currentTags.clear();
            // Load content from DB (prefer remote)
            if(dbBackend && dbBackend->isOpen()){
                std::string err;
                auto stmt = dbBackend->prepare("SELECT Name, Content, Tags FROM VaultItems WHERE ID = ? LIMIT 1;", &err);
                if(!stmt){ PLOGW << "load content prepare failed: " << err; }
                else {
                    stmt->bindInt(1, selectedItemID);
                    auto rs = stmt->executeQuery();
                    if(rs && rs->next()){
                        currentTitle = rs->getString(0);
                        currentContent = rs->getString(1);
                        currentTags = rs->getString(2);
                    } else {
                        PLOGW << "load content: no row for ID=" << selectedItemID;
                    }
                }
            } else if(dbConnection){
                const char* sql = "SELECT Name, Content, Tags FROM VaultItems WHERE ID = ?;";
                sqlite3_stmt* stmt = nullptr;
                if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
                    sqlite3_bind_int64(stmt, 1, selectedItemID);
                    if(sqlite3_step(stmt) == SQLITE_ROW){
                        const unsigned char* nameText = sqlite3_column_text(stmt, 0);
                        const unsigned char* contentText = sqlite3_column_text(stmt, 1);
                        const unsigned char* tagsText = sqlite3_column_text(stmt, 2);
                        if(nameText) currentTitle = reinterpret_cast<const char*>(nameText);
                        if(contentText) currentContent = reinterpret_cast<const char*>(contentText);
                        if(tagsText) currentTags = reinterpret_cast<const char*>(tagsText);
                    }
                }
                if(stmt) sqlite3_finalize(stmt);
            }
            loadedItemID = selectedItemID;
            contentDirty = false;
        }

        // Compute edit permissions for current user
        bool canEdit = true;
        if(loadedItemID >= 0 && currentUserID > 0){
            canEdit = isItemEditableByUser(loadedItemID, currentUserID);
        }

        // Live Markdown editor + preview (split view)
        ImVec2 avail = ImGui::GetContentRegionAvail();
        // Persistent/resizable height at the bottom for meta (tags/parents)
        static float bottomMetaHeight = 140.0f; // user-adjustable persisted across frames
        const float minBottomMeta = 80.0f; // minimal reasonable metadata height
        const float minMainHeight = 120.0f; // minimal editor/preview height
        const float splitterHeight = 6.0f; // draggable splitter height
        float styleSpacingY = ImGui::GetStyle().ItemSpacing.y;
        // extra reserved space includes the splitter and one spacing for separator
        float extraReserved = splitterHeight + styleSpacingY + 2.0f;

        // Clamp bottomMetaHeight into available bounds so we never overflow
        float maxBottomMeta = avail.y - minMainHeight - extraReserved - 8.0f; // 8px padding
        if (maxBottomMeta < minBottomMeta) maxBottomMeta = minBottomMeta;
        if (bottomMetaHeight < minBottomMeta) bottomMetaHeight = minBottomMeta;
        if (bottomMetaHeight > maxBottomMeta) bottomMetaHeight = maxBottomMeta;

        float padding = 8.0f;
        float mainHeight = avail.y - bottomMetaHeight - extraReserved - padding;
        if(mainHeight < minMainHeight) {
            mainHeight = minMainHeight;
            // adjust bottomMetaHeight so overall sizes remain consistent
            bottomMetaHeight = avail.y - mainHeight - extraReserved - padding;
            if (bottomMetaHeight < minBottomMeta) bottomMetaHeight = minBottomMeta;
            if (bottomMetaHeight > maxBottomMeta) bottomMetaHeight = maxBottomMeta;
        }
        float leftWidth = avail.x * 0.5f;

        // Left: editor (scrollable)
        ImGui::BeginChild("VaultEditorLeft", ImVec2(leftWidth, mainHeight), true);
        ImGui::TextDisabled("Editor (Markdown)");
        ImGui::Separator();

        // Title editable
        char titleBuf[256];
        strncpy(titleBuf, currentTitle.c_str(), sizeof(titleBuf)); titleBuf[sizeof(titleBuf)-1] = '\0';
        if(!canEdit) ImGui::BeginDisabled();
        if(ImGui::InputText("Title", titleBuf, sizeof(titleBuf))){
            std::string newTitle = std::string(titleBuf);
            if(loadedItemID >= 0){
                if(dbBackend && dbBackend->isOpen()){
                    std::string err;
                    auto stmt = dbBackend->prepare("UPDATE VaultItems SET Name = ? WHERE ID = ?;", &err);
                    if(!stmt){ PLOGW << "update title prepare failed: " << err; }
                    else { stmt->bindString(1, newTitle); stmt->bindInt(2, loadedItemID); if(!stmt->execute()) PLOGW << "update title execute failed"; }
                    currentTitle = newTitle; statusMessage = "Title saved"; statusTime = ImGui::GetTime();
                } else if(dbConnection){
                    const char* updateSQL = "UPDATE VaultItems SET Name = ? WHERE ID = ?;";
                    sqlite3_stmt* stmt = nullptr;
                    if(sqlite3_prepare_v2(dbConnection, updateSQL, -1, &stmt, nullptr) == SQLITE_OK){
                        sqlite3_bind_text(stmt, 1, newTitle.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int64(stmt, 2, loadedItemID);
                        sqlite3_step(stmt);
                    }
                    if(stmt) sqlite3_finalize(stmt);
                    currentTitle = newTitle; statusMessage = "Title saved"; statusTime = ImGui::GetTime();
                } else {
                    currentTitle = newTitle;
                }
            } else {
                currentTitle = newTitle;
            }
        }
        if(!canEdit) ImGui::EndDisabled();

        ImGui::Separator();

        // Editor content
        ImVec2 editorSize = ImVec2(leftWidth - 16, mainHeight - 40);
        if(!canEdit) ImGui::BeginDisabled();
        if(ImGui::InputTextMultiline("##md_editor", &currentContent, editorSize, ImGuiInputTextFlags_AllowTabInput)){
            // Immediate save on modification
            if(loadedItemID >= 0){
                if(dbBackend && dbBackend->isOpen()){
                    std::string err;
                    auto stmt = dbBackend->prepare("UPDATE VaultItems SET Content = ? WHERE ID = ?;", &err);
                    if(!stmt){ PLOGW << "update content prepare failed: " << err; }
                    else { stmt->bindString(1, currentContent); stmt->bindInt(2, loadedItemID); if(!stmt->execute()) PLOGW << "update content execute failed"; }
                    contentDirty = false; lastSaveTime = ImGui::GetTime();
                } else if(dbConnection){
                    const char* updateSQL = "UPDATE VaultItems SET Content = ? WHERE ID = ?;";
                    sqlite3_stmt* stmt = nullptr;
                    if(sqlite3_prepare_v2(dbConnection, updateSQL, -1, &stmt, nullptr) == SQLITE_OK){
                        sqlite3_bind_text(stmt, 1, currentContent.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int64(stmt, 2, loadedItemID);
                        sqlite3_step(stmt);
                    }
                    if(stmt) sqlite3_finalize(stmt);
                    contentDirty = false; lastSaveTime = ImGui::GetTime();
                } else {
                    contentDirty = true;
                }
            } else {
                contentDirty = true;
            }
        }
        if(!canEdit) ImGui::EndDisabled();

        // Show saved indicator in editor if recently saved
        if(lastSaveTime > 0.0f && (ImGui::GetTime() - lastSaveTime) < 1.5f){
            ImGui::TextColored(ImVec4(0.3f,1.0f,0.3f,1.0f), "Saved");
        } else if(contentDirty){
            ImGui::TextColored(ImVec4(1.0f,0.6f,0.0f,1.0f), "Modified");
        }

        ImGui::EndChild();

        ImGui::SameLine();

        // Right: live preview (scrollable)
        ImGui::BeginChild("VaultPreviewRight", ImVec2(0, mainHeight), true);
        ImGui::TextDisabled("Preview");
        ImGui::Separator();
        // Render markdown using our MarkdownText helper (pass Vault context for attachment previews)
        ImGui::MarkdownText(currentContent.c_str(), this);

        // Process pending model viewer reloads triggered by background fetches
        {
            std::vector<std::string> pending;
            {
                std::lock_guard<std::mutex> l(dbMutex);
                if(!pendingViewerReloads.empty()) pending.swap(pendingViewerReloads);
            }
            for(const auto &k : pending){
                auto it = modelViewerCache.find(k);
                if(it != modelViewerCache.end()){
                    int64_t aid = -1;
                    const std::string vpfx = "vault://attachment/";
                    if(k.rfind(vpfx,0) == 0){ try{ aid = std::stoll(k.substr(vpfx.size())); } catch(...){} }
                    else aid = findAttachmentByExternalPath(k);
                    if(aid != -1){
                        auto meta = getAttachmentMeta(aid);
                        if(meta.size>0){
                            // Fetch blob off the UI thread then enqueue GL upload on main thread
                            std::thread([this, aid, mvPtr = it->second.get(), metaName = meta.name](){
                                std::shared_ptr<std::vector<uint8_t>> dataPtr;
                                {
                                    std::lock_guard<std::mutex> l(dbMutex);
                                    auto d = getAttachmentData(aid);
                                    if(!d.empty()) dataPtr = std::make_shared<std::vector<uint8_t>>(std::move(d));
                                }
                                if(dataPtr && !dataPtr->empty()){
                                    // Parse/upload async (non-blocking)
                                    mvPtr->loadFromMemoryAsync(*dataPtr, metaName);
                                    PLOGI << "vault:async parse queued model aid=" << aid;
                                }
                            }).detach();
                        }
                    }
                }
            }
        }

        // Execute any queued main-thread tasks (e.g., GL uploads) that were enqueued by background workers
        {
            std::vector<std::function<void()>> tasks;
            {
                std::lock_guard<std::mutex> l(pendingTasksMutex);
                if(!pendingMainThreadTasks.empty()) tasks.swap(pendingMainThreadTasks);
            }
            for(auto &t : tasks) t();
        }

        ImGui::EndChild();

        // Meta area: Tags & Parents (bottom) — resizable by dragging the splitter above
        // Draw a thin draggable splitter that adjusts bottomMetaHeight
        {
            ImGui::SetCursorPosY(ImGui::GetCursorPosY());
            ImGui::InvisibleButton("##meta_splitter", ImVec2(ImGui::GetContentRegionAvail().x, splitterHeight));
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                bottomMetaHeight -= ImGui::GetIO().MouseDelta.y;
                if (bottomMetaHeight < minBottomMeta) bottomMetaHeight = minBottomMeta;
                if (bottomMetaHeight > maxBottomMeta) bottomMetaHeight = maxBottomMeta;
            }
            if (ImGui::IsItemHovered()) ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
            ImGui::Separator();
        }
        // Use a reserved height for meta area to prevent preview from stealing space
        // Make the meta area scrollable so content isn't cut off when it exceeds the reserved height
        ImGui::BeginChild("VaultMeta", ImVec2(0, bottomMetaHeight), true); 

        // Tags UI as chips with remove X
        ImGui::TextDisabled("Tags:");
        ImGui::SameLine();
        std::vector<std::string> tags = parseTags(currentTags);
        for(size_t i = 0; i < tags.size(); ++i){
            ImGui::PushID(static_cast<int>(i));
            ImGui::Button(tags[i].c_str()); ImGui::SameLine();
            if(canEdit){
                if(ImGui::SmallButton((std::string("x##tag") + std::to_string(i)).c_str())){
                    if(removeTagFromItem(loadedItemID, tags[i])){
                        currentTags = getTagsOf(loadedItemID);
                        statusMessage = "Tag removed";
                        statusTime = ImGui::GetTime();
                    }
                }
            } else {
                ImGui::BeginDisabled(); ImGui::SmallButton((std::string("x##tag") + std::to_string(i)).c_str()); ImGui::EndDisabled();
            }
            ImGui::SameLine();
            ImGui::PopID();
        }
        ImGui::NewLine();
        static char addTagBuf[64] = "";
        ImGui::SetNextItemWidth(200);
        if(canEdit){
            if(ImGui::InputTextWithHint("##addtag", "Add tag and press Enter", addTagBuf, sizeof(addTagBuf), ImGuiInputTextFlags_EnterReturnsTrue)){
                std::string t = std::string(addTagBuf);
                // trim
                while(!t.empty() && std::isspace((unsigned char)t.front())) t.erase(t.begin());
                while(!t.empty() && std::isspace((unsigned char)t.back())) t.pop_back();
                if(!t.empty()){
                    if(addTagToItem(loadedItemID, t)){
                        currentTags = getTagsOf(loadedItemID);
                        statusMessage = "Tag added";
                    } else {
                        statusMessage = "Tag already exists or failed";
                    }
                    statusTime = ImGui::GetTime();
                    addTagBuf[0] = '\0';
                }
            }
            ImGui::SameLine();
            if(ImGui::Button("Add Tag")){
                std::string t = std::string(addTagBuf);
                while(!t.empty() && std::isspace((unsigned char)t.front())) t.erase(t.begin());
                while(!t.empty() && std::isspace((unsigned char)t.back())) t.pop_back();
                if(!t.empty()){
                    if(addTagToItem(loadedItemID, t)){
                        currentTags = getTagsOf(loadedItemID);
                        statusMessage = "Tag added";
                    } else {
                        statusMessage = "Tag already exists or failed";
                    }
                    statusTime = ImGui::GetTime();
                    addTagBuf[0] = '\0';
                }
            }
        } else {
            ImGui::BeginDisabled();
            ImGui::InputTextWithHint("##addtag", "Add tag and press Enter", addTagBuf, sizeof(addTagBuf), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine(); ImGui::Button("Add Tag");
            ImGui::EndDisabled();
        }

        ImGui::Separator();
        // Parents UI
        ImGui::TextDisabled("Parents:");
        ImGui::SameLine();
        std::vector<int64_t> parents = getParentsOf(loadedItemID);
        int64_t root = getOrCreateRoot();
        for(auto pid : parents){
            ImGui::TextUnformatted("- "); ImGui::SameLine();
            ImGui::TextUnformatted(getItemName(pid).c_str()); ImGui::SameLine();
            // Allow removing root as a parent only if the node has other parents (never allow removal that leaves zero parents)
            bool hasMultipleParents = (parents.size() > 1);
            if(pid == root && !hasMultipleParents){
                ImGui::TextDisabled("(root)");
            } else {
                if(ImGui::SmallButton((std::string("RemoveP##") + std::to_string(pid)).c_str())){
                    if(removeParentRelation(pid, loadedItemID)){
                        statusMessage = "Parent removed";
                    } else {
                        statusMessage = "Cannot remove parent";
                    }
                    statusTime = ImGui::GetTime();
                }
            }
        }

        // Add parent search & filter
        // If editing root node, disallow adding parents
        if(loadedItemID == root){
            ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1.0f), "Root node cannot have parents");
        } else {
            static char parentFilter[128] = "";
            ImGui::InputTextWithHint("##parent_filter", "Filter parents...", parentFilter, sizeof(parentFilter));

            // Calculate remaining height for parent list so it doesn't scroll internally
            float usedY = ImGui::GetCursorPosY();
            float remaining = bottomMetaHeight - usedY - 20.0f; // leave padding
            if(remaining < ImGui::GetTextLineHeightWithSpacing()) remaining = ImGui::GetTextLineHeightWithSpacing();

            ImGui::BeginChild("ParentList", ImVec2(0, remaining), true);
            auto all = getAllItems();
            for(auto &it : all){
                int64_t id = it.first; const std::string &name = it.second;
                if(id == loadedItemID) continue;
                if(std::find(parents.begin(), parents.end(), id) != parents.end()) continue;
                // cycles allowed, so don't skip by path
                std::string lowerName = name; std::string filter = parentFilter;
                std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(), ::tolower);
                std::transform(filter.begin(), filter.end(), filter.begin(), ::tolower);
                if(filter.size() && lowerName.find(filter) == std::string::npos) continue;
                std::string selLabel = name + "##" + std::to_string(id);
                if(canEdit){
                    if(ImGui::Selectable(selLabel.c_str())){
                        if(addParentRelation(id, loadedItemID)){
                            statusMessage = "Parent added";
                        } else {
                            statusMessage = "Failed to add parent";
                        }
                        statusTime = ImGui::GetTime();
                    }
                } else {
                    ImGui::BeginDisabled(); ImGui::Selectable(selLabel.c_str()); ImGui::EndDisabled();
                }
            }
            ImGui::EndChild();
        }

        // Visibility UI (per-user None/View/Edit). Only admins can change permissions.
        ImGui::Separator();
        ImGui::TextDisabled("Visibility:");
        ImGui::SameLine();
        {
            // determine if current user is admin (to allow edits to perms)
            bool currentIsAdmin = false;
            if(currentUserID > 0){
                sqlite3_stmt* astmt = nullptr;
                const char* aSql = "SELECT IsAdmin FROM Users WHERE ID = ? LIMIT 1;";
                if(sqlite3_prepare_v2(dbConnection, aSql, -1, &astmt, nullptr) == SQLITE_OK){
                    sqlite3_bind_int64(astmt, 1, currentUserID);
                    if(sqlite3_step(astmt) == SQLITE_ROW) currentIsAdmin = (sqlite3_column_int(astmt,0) != 0);
                }
                if(astmt) sqlite3_finalize(astmt);
            }
            ImGui::BeginChild("VisibilityList", ImVec2(0, 100), true);
            auto users = listUsers();
            if(users.empty()) ImGui::TextDisabled("No users defined");
            for(auto &u : users){
                ImGui::PushID(static_cast<int>(u.id));
                std::string label = (u.displayName.empty() ? u.username : u.displayName);
                ImGui::TextUnformatted(label.c_str()); ImGui::SameLine(220);
                // read explicit permission (default to View when not present)
                int sel = 1;
                sqlite3_stmt* pstm = nullptr;
                const char* q = "SELECT Level FROM ItemPermissions WHERE ItemID = ? AND UserID = ? LIMIT 1;";
                if(sqlite3_prepare_v2(dbConnection, q, -1, &pstm, nullptr) == SQLITE_OK){
                    sqlite3_bind_int64(pstm, 1, loadedItemID);
                    sqlite3_bind_int64(pstm, 2, u.id);
                    if(sqlite3_step(pstm) == SQLITE_ROW) sel = sqlite3_column_int(pstm, 0);
                }
                if(pstm) sqlite3_finalize(pstm);
                // If target user is an admin, treat as Edit and do not allow changing their perms (prevents self-lockout)
                if(u.isAdmin){ sel = 2; }
                const char* opts[] = {"None", "View", "Edit"};
                if(u.id == currentUserID && u.isAdmin){
                    // Show that the current admin has permanent Edit access and disable changing it
                    ImGui::BeginDisabled();
                    ImGui::TextDisabled("Admin (always Edit)");
                    ImGui::EndDisabled();
                } else if(currentIsAdmin){
                    if(ImGui::Combo((std::string("##perm") + std::to_string(u.id)).c_str(), &sel, opts, 3)){
                        setItemPermission(loadedItemID, u.id, sel);
                        statusMessage = "Permissions updated"; statusTime = ImGui::GetTime();
                    }
                } else {
                    ImGui::BeginDisabled(); ImGui::Combo((std::string("##perm") + std::to_string(u.id)).c_str(), &sel, opts, 3); ImGui::EndDisabled();
                }
                ImGui::SameLine(); ImGui::TextDisabled("(%s)", u.username.c_str());
                ImGui::PopID();
            }
            ImGui::EndChild();
        }

        // Attachments UI
        ImGui::Separator();
        ImGui::TextDisabled("Attachments:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(400);
        if(canEdit){
            if(ImGui::InputTextWithHint("##attach_path", "Path or URL (files only)", attachPathBuf, sizeof(attachPathBuf), ImGuiInputTextFlags_EnterReturnsTrue)){
                std::string p(attachPathBuf);
                if(!p.empty()){
                    std::ifstream in(p, std::ios::binary);
                    if(in){
                        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                        std::string name = std::filesystem::path(p).filename().string();
                        std::string ext = std::filesystem::path(p).extension().string(); std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        std::string mime = "application/octet-stream";
                        if(ext==".png" || ext==".jpg" || ext==".jpeg" || ext==".bmp" || ext==".gif") mime = std::string("image/") + (ext.size()>1? ext.substr(1) : "");
                        int64_t aid = addAttachment(loadedItemID, name, mime, data, p);
                        if(aid != -1){ statusMessage = "Attachment added"; statusTime = ImGui::GetTime(); attachPathBuf[0]='\0'; }
                        else { statusMessage = "Failed to add attachment"; statusTime = ImGui::GetTime(); }
                    } else { statusMessage = "Failed to open file"; statusTime = ImGui::GetTime(); }
                }
            }
            ImGui::SameLine(); if(ImGui::Button("Import Attachment")){
                std::string p(attachPathBuf);
                if(!p.empty()){
                    std::ifstream in(p, std::ios::binary);
                    if(in){
                        std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                        std::string name = std::filesystem::path(p).filename().string();
                        std::string ext = std::filesystem::path(p).extension().string(); std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                        std::string mime = "application/octet-stream";
                        if(ext==".png" || ext==".jpg" || ext==".jpeg" || ext==".bmp" || ext==".gif") mime = std::string("image/") + (ext.size()>1? ext.substr(1) : "");
                        int64_t aid = addAttachment(loadedItemID, name, mime, data, p);
                        if(aid != -1){ statusMessage = "Attachment added"; statusTime = ImGui::GetTime(); attachPathBuf[0]='\0'; }
                        else { statusMessage = "Failed to add attachment"; statusTime = ImGui::GetTime(); }
                    } else { statusMessage = "Failed to open file"; statusTime = ImGui::GetTime(); }
                }
            }
            ImGui::SameLine(); if(ImGui::Button("Add Asset...")){
                showImportAssetModal = true;
                importAssetPath = std::filesystem::current_path();
                importAssetSelectedFiles.clear();
            }
        } else {
            ImGui::BeginDisabled();
            ImGui::InputTextWithHint("##attach_path", "Path or URL (files only)", attachPathBuf, sizeof(attachPathBuf), ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SameLine(); ImGui::Button("Import Attachment"); ImGui::SameLine(); ImGui::Button("Add Asset...");
            ImGui::EndDisabled();
        }

        // List attachments
        auto attachments = listAttachments(loadedItemID);
        for(auto &a : attachments){
            std::string label = a.name + " [id:" + std::to_string(a.id) + "]";
            ImGui::TextUnformatted(label.c_str()); ImGui::SameLine(); ImGui::TextDisabled("(%lld bytes)", (long long)a.size); ImGui::SameLine();
            if(ImGui::Button((std::string("Preview##att") + std::to_string(a.id)).c_str())){ previewAttachmentID = a.id; previewIsRaw = false; auto meta = getAttachmentMeta(a.id); previewDisplayWidth = meta.displayWidth; previewDisplayHeight = meta.displayHeight; showAttachmentPreview = true; ImGui::OpenPopup("Attachment Preview"); }
            ImGui::SameLine(); if(ImGui::SmallButton((std::string("Remove##att") + std::to_string(a.id)).c_str())){ if(removeAttachment(a.id)){ statusMessage = "Attachment removed"; statusTime = ImGui::GetTime(); } else { statusMessage = "Failed to remove attachment"; statusTime = ImGui::GetTime(); } }
        }

        // Attachment preview popup
        if(showAttachmentPreview){
            if(ImGui::BeginPopupModal("Attachment Preview", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
                if(previewIsRaw && previewAttachmentID == -1){
                    ImGui::Text("Name: %s", previewName.c_str());
                    ImGui::Text("Mime: %s", previewMime.c_str());
                    ImGui::Text("Size: %lld bytes", (long long)previewRawData.size());
                    ImGui::Separator();
                    // Save As
                    ImGui::InputText("##savepath", attachmentSavePathBuf, sizeof(attachmentSavePathBuf)); ImGui::SameLine();
                    if(ImGui::Button("Save As")){
                        std::string outp(attachmentSavePathBuf);
                        if(!outp.empty()){
                            std::ofstream of(outp, std::ios::binary);
                            if(of){ of.write(reinterpret_cast<const char*>(previewRawData.data()), previewRawData.size()); of.close(); statusMessage = "Saved"; statusTime = ImGui::GetTime(); }
                            else { statusMessage = "Failed to save"; statusTime = ImGui::GetTime(); }
                        }
                    }
                    // Display controls for raw preview (cannot save to DB)
                    ImGui::Separator();
                    ImGui::Text("Display (px):"); ImGui::SameLine();
                    ImGui::PushID("display_sz_raw");
                    ImGui::InputInt("Width", &previewDisplayWidth);
                    ImGui::SameLine(); ImGui::InputInt("Height", &previewDisplayHeight);
                    ImGui::PopID();
                    ImGui::SameLine();
                    ImGui::BeginDisabled(); if(ImGui::Button("Save Display")){} ImGui::EndDisabled();
                    ImGui::SameLine(); if(ImGui::Button("Reset")){ previewDisplayWidth = 0; previewDisplayHeight = 0; statusMessage = "Display reset (temporary)"; statusTime = ImGui::GetTime(); }
                } else {
                    auto meta = getAttachmentMeta(previewAttachmentID);
                    ImGui::Text("Name: %s", meta.name.c_str());
                    ImGui::Text("Mime: %s", meta.mimeType.c_str());
                    ImGui::Text("Size: %lld bytes", (long long)meta.size);
                    ImGui::Separator();
                    // Save As
                    ImGui::InputText("##savepath", attachmentSavePathBuf, sizeof(attachmentSavePathBuf)); ImGui::SameLine();
                    if(ImGui::Button("Save As")){
                        std::string outp(attachmentSavePathBuf);
                        if(!outp.empty()){
                            auto data = getAttachmentData(meta.id);
                            std::ofstream of(outp, std::ios::binary);
                            if(of){ of.write(reinterpret_cast<const char*>(data.data()), data.size()); of.close(); statusMessage = "Saved"; statusTime = ImGui::GetTime(); }
                            else { statusMessage = "Failed to save"; statusTime = ImGui::GetTime(); }
                        }
                    }

                    // Display size controls (pixels). 0 = default/unset
                    ImGui::Separator();
                    ImGui::Text("Display (px):"); ImGui::SameLine();
                    ImGui::PushID("display_sz");
                    ImGui::InputInt("Width", &previewDisplayWidth);
                    ImGui::SameLine(); ImGui::InputInt("Height", &previewDisplayHeight);
                    ImGui::PopID();
                    ImGui::SameLine();
                    if(ImGui::Button("Save Display")){
                        int w = previewDisplayWidth == 0 ? -1 : previewDisplayWidth;
                        int h = previewDisplayHeight == 0 ? -1 : previewDisplayHeight;
                        if(setAttachmentDisplaySize(meta.id, w, h)){
                            statusMessage = "Display saved"; statusTime = ImGui::GetTime();
                        } else { statusMessage = "Failed to save display"; statusTime = ImGui::GetTime(); }
                    }
                    ImGui::SameLine(); if(ImGui::Button("Reset")){ previewDisplayWidth = 0; previewDisplayHeight = 0; setAttachmentDisplaySize(meta.id, -1, -1); statusMessage = "Display reset"; statusTime = ImGui::GetTime(); }
                }
                ImGui::Separator();
                // If model mime/extension, offer "View Model"
                auto checkAndShowModelBtn = [&](const std::string& mime, const std::string& filename, const std::vector<uint8_t>& raw, int64_t attID)->void{
                    auto isModelExt = [&](const std::string& f)->bool{
                        std::string ext = f;
                        try{ std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower); }
                        catch(...){}
                        if(ext.rfind('.',0) != 0) {
                            size_t pos = ext.find_last_of('.'); if(pos!=std::string::npos) ext = ext.substr(pos);
                        }
                        static const std::vector<std::string> models = {".obj",".fbx",".gltf",".glb",".ply",".dae",".stl"};
                        for(auto &m: models) if(ext == m) return true;
                        if(mime.find("model") != std::string::npos) return true;
                        return false;
                    };
                    if(isModelExt(filename)){
                        ImGui::SameLine();
                        if(ImGui::Button("View Model")){
                            // ensure modelViewer exists
                            if(!modelViewer) modelViewer = std::make_unique<ModelViewer>();
                            // load from available data if present
                            std::vector<uint8_t> d;
                            if(attID >= 0){ d = getAttachmentData(attID); }
                            else d = raw;
                            if(!d.empty()){
                                auto dataPtr = std::make_shared<std::vector<uint8_t>>(std::move(d));
                                auto mvPtr = modelViewer.get();
                                statusMessage = "Loading model..."; statusTime = ImGui::GetTime();
                                // start async parse/upload and open viewer
                                mvPtr->loadFromMemoryAsync(*dataPtr, filename);
                                showModelViewer = true;
                            } else {
                                // try fetch if external path exists
                                if(attID >= 0){ auto meta = getAttachmentMeta(attID); if(!meta.externalPath.empty()){ asyncFetchAndStoreAttachment(attID, meta.externalPath); statusMessage = "Fetching model..."; statusTime = ImGui::GetTime(); } }
                                else { statusMessage = "No data to load"; statusTime = ImGui::GetTime(); }
                            }
                        }
                    }
                };

                if(previewIsRaw && previewAttachmentID == -1){
                    checkAndShowModelBtn(previewMime, previewName, previewRawData, -1);
                    ImGui::Separator();
                    // Save As
                    ImGui::InputText("##savepath", attachmentSavePathBuf, sizeof(attachmentSavePathBuf)); ImGui::SameLine();
                    if(ImGui::Button("Save As")){
                        std::string outp(attachmentSavePathBuf);
                        if(!outp.empty()){
                            std::ofstream of(outp, std::ios::binary);
                            if(of){ of.write(reinterpret_cast<const char*>(previewRawData.data()), previewRawData.size()); of.close(); statusMessage = "Saved"; statusTime = ImGui::GetTime(); }
                            else { statusMessage = "Failed to save"; statusTime = ImGui::GetTime(); }
                        }
                    }
                } else {
                    auto meta = getAttachmentMeta(previewAttachmentID);
                    checkAndShowModelBtn(meta.mimeType, meta.name, std::vector<uint8_t>(), meta.id);
                    ImGui::Text("Name: %s", meta.name.c_str());
                    ImGui::Text("Mime: %s", meta.mimeType.c_str());
                    ImGui::Text("Size: %lld bytes", (long long)meta.size);
                    ImGui::Separator();
                    // Save As
                    ImGui::InputText("##savepath", attachmentSavePathBuf, sizeof(attachmentSavePathBuf)); ImGui::SameLine();
                    if(ImGui::Button("Save As")){
                        std::string outp(attachmentSavePathBuf);
                        if(!outp.empty()){
                            auto data = getAttachmentData(meta.id);
                            std::ofstream of(outp, std::ios::binary);
                            if(of){ of.write(reinterpret_cast<const char*>(data.data()), data.size()); of.close(); statusMessage = "Saved"; statusTime = ImGui::GetTime(); }
                            else { statusMessage = "Failed to save"; statusTime = ImGui::GetTime(); }
                        }
                    }
                }
                ImGui::Separator();
                if(ImGui::Button("Close")) { ImGui::CloseCurrentPopup(); showAttachmentPreview = false; }
                ImGui::EndPopup();
            }
        }

        // Import Asset(s) modal
        if(showImportAssetModal){ ImGui::OpenPopup("Import Asset(s)"); showImportAssetModal = false; }
        if(ImGui::BeginPopupModal("Import Asset(s)", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::Text("Select files to upload as Assets (multiple):");
            ImGui::Separator();
            ImGui::TextWrapped("%s", importAssetPath.string().c_str()); ImGui::SameLine();
            if(ImGui::Button("Up")){
                if(importAssetPath.has_parent_path()) importAssetPath = importAssetPath.parent_path();
            }
            ImGui::Separator();
            try{
                std::vector<std::filesystem::directory_entry> dirs, files;
                for(auto &e : std::filesystem::directory_iterator(importAssetPath)){
                    if(e.is_directory()) dirs.push_back(e);
                    else if(e.is_regular_file()) files.push_back(e);
                }
                std::sort(dirs.begin(), dirs.end(), [](const auto &a, const auto &b){ return a.path().filename().string() < b.path().filename().string(); });
                std::sort(files.begin(), files.end(), [](const auto &a, const auto &b){ return a.path().filename().string() < b.path().filename().string(); });
                for(auto &d : dirs){
                    std::string label = std::string("[DIR] ") + d.path().filename().string();
                    if(ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_DontClosePopups)){
                        importAssetPath = d.path();
                        importAssetSelectedFiles.clear();
                    }
                }
                ImGui::Separator();
                for(auto &f : files){
                    std::string fname = f.path().filename().string();
                    bool sel = importAssetSelectedFiles.find(fname) != importAssetSelectedFiles.end();
                    std::string flabel = (sel ? std::string("[x] ") : std::string("[ ] ")) + fname;
                    if(ImGui::Selectable(flabel.c_str(), sel, ImGuiSelectableFlags_DontClosePopups)){
                        if(sel) importAssetSelectedFiles.erase(fname); else importAssetSelectedFiles.insert(fname);
                    }
                }
            } catch(...){ ImGui::TextColored(ImVec4(1,0.4f,0.4f,1.0f), "Failed to read directory"); }

            ImGui::Separator();
            ImGui::TextWrapped("Target path (optional, e.g. subdir/folder). Leave empty for root:");
            ImGui::InputText("##ImportAssetDest", importAssetDestFolderBuf, sizeof(importAssetDestFolderBuf));
            ImGui::Separator();
            if(ImGui::Button("Select All")){
                try{
                    for(auto &e : std::filesystem::directory_iterator(importAssetPath)){
                        if(e.is_regular_file()) importAssetSelectedFiles.insert(e.path().filename().string());
                    }
                } catch(...){ }
            }
            ImGui::SameLine(); if(ImGui::Button("Clear")) importAssetSelectedFiles.clear();
            ImGui::SameLine();
            if(ImGui::Button("Upload")){
                lastUploadedExternalPaths.clear();
                for(auto &fname : importAssetSelectedFiles){
                    try{
                        std::filesystem::path full = importAssetPath / fname;
                        // construct desired virtual path
                        std::string dest(importAssetDestFolderBuf);
                        // normalize
                        while(!dest.empty() && (dest.front() == '/' || dest.front() == '\\')) dest.erase(dest.begin());
                        while(!dest.empty() && (dest.back() == '/' || dest.back() == '\\')) dest.pop_back();
                        std::string desired = dest.empty() ? fname : (dest + "/" + fname);
                        int64_t id = addAssetFromFile(full.string(), desired);
                        if(id > 0){ auto meta = getAttachmentMeta(id); if(!meta.externalPath.empty()) lastUploadedExternalPaths.push_back(meta.externalPath); }
                        // if a conflict arose the modal will be shown and addAssetFromFile returned -1; stop further uploads to let the user decide
                        if(showOverwriteConfirmModal) break;
                    } catch(...){ }
                }
                if(!lastUploadedExternalPaths.empty()){
                    // If there is no pending overwrite decision, close the Import modal and open the Uploaded modal immediately
                    if(!showOverwriteConfirmModal){
                        ImGui::CloseCurrentPopup();
                        ImGui::OpenPopup("Asset Uploaded");
                        showAssetUploadedModal = false; // already opened
                        statusMessage = "Uploaded assets"; statusTime = ImGui::GetTime();
                    } else {
                        // wait for overwrite decision; the asset modal will be opened after conflict resolution
                        showAssetUploadedModal = true;
                    }
                }
                // only close the import popup if we're not waiting for a user decision
                if(!showOverwriteConfirmModal) ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine(); if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // Asset Uploaded modal
        if(showAssetUploadedModal){ ImGui::OpenPopup("Asset Uploaded"); showAssetUploadedModal = false; }
        if(ImGui::BeginPopupModal("Asset Uploaded", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::Text("Uploaded %d asset(s):", (int)lastUploadedExternalPaths.size());
            ImGui::Separator();
            for(auto &ep : lastUploadedExternalPaths){
                std::string snippet = std::string("![](") + ep + ")";
                ImGui::TextUnformatted(snippet.c_str()); ImGui::SameLine();
                if(ImGui::Button((std::string("Copy##") + ep).c_str())){ ImGui::SetClipboardText(snippet.c_str()); statusMessage = "Copied to clipboard"; statusTime = ImGui::GetTime(); }
                ImGui::SameLine();
                if(ImGui::Button((std::string("Insert##") + ep).c_str())){ currentContent += std::string("\n") + snippet + std::string("\n"); contentDirty = true; }
            }
            ImGui::Separator();
            if(ImGui::Button("Close")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // Overwrite / conflict confirmation modal (triggered when an exact vault://Assets/... already exists)
        if(showOverwriteConfirmModal){ ImGui::OpenPopup("Asset conflict"); showOverwriteConfirmModal = false; }
        if(ImGui::BeginPopupModal("Asset conflict", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::TextWrapped("An asset already exists at:\n%s\n\nChoose action:", overwriteTargetExternalPath.c_str());
            ImGui::Separator();
            if(ImGui::Button("Replace")){
                // Overwrite existing attachment (preserve ID)
                std::ifstream in(overwritePendingLocalFile, std::ios::binary);
                std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                std::string name = std::filesystem::path(overwritePendingLocalFile).filename().string();
                std::string ext = std::filesystem::path(name).extension().string(); std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                std::string mime = "application/octet-stream";
                if(ext==".png" || ext==".jpg" || ext==".jpeg" || ext==".bmp" || ext==".gif") mime = std::string("image/") + (ext.size()>1? ext.substr(1) : "");
                {
                    std::lock_guard<std::mutex> l(dbMutex);
                    const char* upd = "UPDATE Attachments SET Data = ?, Size = ?, MimeType = ?, Name = ?, CreatedAt = ? WHERE ID = ?;";
                    sqlite3_stmt* stmt = nullptr;
                    if(sqlite3_prepare_v2(dbConnection, upd, -1, &stmt, nullptr) == SQLITE_OK){
                        if(!data.empty()) sqlite3_bind_blob(stmt, 1, reinterpret_cast<const void*>(data.data()), static_cast<int>(data.size()), SQLITE_TRANSIENT);
                        else sqlite3_bind_null(stmt, 1);
                        sqlite3_bind_int64(stmt, 2, static_cast<int64_t>(data.size()));
                        sqlite3_bind_text(stmt, 3, mime.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_text(stmt, 4, name.c_str(), -1, SQLITE_TRANSIENT);
                        sqlite3_bind_int64(stmt, 5, static_cast<int64_t>(time(nullptr)));
                        sqlite3_bind_int64(stmt, 6, overwriteExistingAttachmentID);
                        sqlite3_step(stmt);
                    }
                    if(stmt) sqlite3_finalize(stmt);
                    // schedule reloads for viewers
                    pendingViewerReloads.push_back(overwriteTargetExternalPath);
                    pendingViewerReloads.push_back(std::string("vault://attachment/") + std::to_string(overwriteExistingAttachmentID));
                }
                lastUploadedExternalPaths.clear(); lastUploadedExternalPaths.push_back(overwriteTargetExternalPath);
                showAssetUploadedModal = true; ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if(ImGui::Button("Upload as Copy")){
                // create a unique copy by adding a numeric suffix to the final component
                std::string baseRel = overwriteTargetExternalPath.substr(std::string("vault://Assets/").size());
                std::string dir = std::filesystem::path(baseRel).parent_path().string();
                std::string fname = std::filesystem::path(baseRel).filename().string();
                std::string stem = std::filesystem::path(fname).stem().string();
                std::string ext = std::filesystem::path(fname).extension().string();
                int suffix = 1;
                std::string candidateRel;
                int64_t existing = -1;
                do{
                    std::string tryName = stem + std::string("(") + std::to_string(suffix) + std::string(")") + ext;
                    candidateRel = dir.empty() ? tryName : (dir + std::string("/") + tryName);
                    std::string candidate = std::string("vault://Assets/") + candidateRel;
                    existing = findAttachmentByExternalPath(candidate);
                    if(existing != -1) suffix++; else break;
                } while(suffix < 10000);
                // read file and add as new attachment
                std::ifstream in(overwritePendingLocalFile, std::ios::binary);
                std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                std::string mime = "application/octet-stream";
                if(!ext.empty()){
                    std::string lowext = ext; std::transform(lowext.begin(), lowext.end(), lowext.begin(), ::tolower);
                    if(lowext==".png"||lowext==".jpg"||lowext==".jpeg"||lowext==".bmp"||lowext==".gif") mime = std::string("image/") + (lowext.size()>1? lowext.substr(1) : "");
                }
                int64_t id = addAttachment(-1, std::filesystem::path(candidateRel).filename().string(), mime, data, std::string("vault://Assets/") + candidateRel);
                if(id > 0){ lastUploadedExternalPaths.clear(); lastUploadedExternalPaths.push_back(std::string("vault://Assets/") + candidateRel); showAssetUploadedModal = true; }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        // Show status message briefly
        if(!statusMessage.empty() && (ImGui::GetTime() - statusTime) < 3.0f){
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f,0.8f,0.2f,1.0f), "%s", statusMessage.c_str());
        }

        ImGui::EndChild();

        ImGui::End(); 

        // Ensure pending parsed models are processed so uploads complete even if inline viewer isn't rendered
        for(auto &kv : modelViewerCache) if(kv.second) kv.second->processPendingUploads();
        if(modelViewer) modelViewer->processPendingUploads();

        // Model viewer window (dockable)
        if(showModelViewer && modelViewer){ modelViewer->renderWindow("Model Viewer", &showModelViewer); }

    }

    // Manage DB lifetime safely and prevent accidental copies
    ~Vault(){
        if(dbConnection) sqlite3_close(dbConnection);
        if(dbBackend && dbBackend->isOpen()) dbBackend->close();
    }
    Vault(const Vault&) = delete;
    Vault& operator=(const Vault&) = delete;
    Vault(Vault&& other) noexcept : id(other.id), name(std::move(other.name)), selectedItemID(other.selectedItemID), dbConnection(other.dbConnection), dbBackend(std::move(other.dbBackend)){
        other.dbConnection = nullptr;
    }
    Vault& operator=(Vault&& other) noexcept{
        if(this != &other){
            if(dbConnection) sqlite3_close(dbConnection);
            if(dbBackend && dbBackend->isOpen()) dbBackend->close();
            id = other.id;
            name = std::move(other.name);
            selectedItemID = other.selectedItemID;
            dbConnection = other.dbConnection;
            other.dbConnection = nullptr;
            dbBackend = std::move(other.dbBackend);
        }
        return *this;
    }

    // Static helper: creates a Vault at dbPath/vaultName seeded with example notes
    // Structure created:
    // ├── Note One
    // // │   ├── Note Two
    // // │   │   ├── Note Four
    // // │   ├── Note Three
    // // │       ├── Note Four
    static Vault createExampleStructure(const std::filesystem::path& dbPath, const std::string& vaultName){
        Vault v(dbPath, vaultName);
        sqlite3* db = v.dbConnection;
        if(!db) return v;

        const char* findSQL = "SELECT ID FROM VaultItems WHERE Name = ?;";
        const char* insertSQL = "INSERT INTO VaultItems (Name, Content, Tags) VALUES (?, ?, ?);";

        auto findOrCreate = [&](const std::string& name)->int64_t{
            sqlite3_stmt* stmt = nullptr;
            if(sqlite3_prepare_v2(db, findSQL, -1, &stmt, nullptr) == SQLITE_OK){
                sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
                if(sqlite3_step(stmt) == SQLITE_ROW){
                    int64_t id = sqlite3_column_int64(stmt, 0);
                    sqlite3_finalize(stmt);
                    return id;
                }
            }
            if(stmt) sqlite3_finalize(stmt);

            if(sqlite3_prepare_v2(db, insertSQL, -1, &stmt, nullptr) == SQLITE_OK){
                sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 2, "", -1, SQLITE_STATIC);
                sqlite3_bind_text(stmt, 3, "", -1, SQLITE_STATIC);
                sqlite3_step(stmt);
            }
            if(stmt) sqlite3_finalize(stmt);
            return sqlite3_last_insert_rowid(db);
        };

        const char* existsRel = "SELECT 1 FROM VaultItemChildren WHERE ParentID = ? AND ChildID = ?;";
        const char* insertRel = "INSERT INTO VaultItemChildren (ParentID, ChildID) VALUES (?, ?);";

        auto link = [&](int64_t parent, int64_t child){
            // Defensive: do not create self-parent relations
            if(parent == child) return;
            sqlite3_stmt* stmt = nullptr;
            if(sqlite3_prepare_v2(db, existsRel, -1, &stmt, nullptr) == SQLITE_OK){
                sqlite3_bind_int64(stmt,1,parent);
                sqlite3_bind_int64(stmt,2,child);
                if(sqlite3_step(stmt) == SQLITE_ROW){
                    sqlite3_finalize(stmt);
                    return;
                }
            }
            if(stmt){ sqlite3_finalize(stmt); stmt = nullptr; }

            if(sqlite3_prepare_v2(db, insertRel, -1, &stmt, nullptr) == SQLITE_OK){
                sqlite3_bind_int64(stmt,1,parent);
                sqlite3_bind_int64(stmt,2,child);
                sqlite3_step(stmt);
            }
            if(stmt){ sqlite3_finalize(stmt); stmt = nullptr; }
        };

        int64_t id1 = findOrCreate("Note One");
        int64_t id2 = findOrCreate("Note Two");
        int64_t id3 = findOrCreate("Note Three");
        int64_t id4 = findOrCreate("Note Four");

        link(id1, id2);
        link(id1, id3);
        link(id2, id4);
        link(id3, id4);

        // Ensure vault root exists and attach Note One under it
        int64_t root = v.getOrCreateRoot();
        link(root, id1);

        return v;
    }

    int64_t createItem(const std::string& name, int64_t parentID = -1){
        // Backend-aware insertion
        if(dbBackend && dbBackend->isOpen()){
            std::string err;
            auto stmt = dbBackend->prepare("INSERT INTO VaultItems (Name, Content, Tags) VALUES (?, ?, ?);", &err);
            if(!stmt){ PLOGE << "createItem prepare failed: " << err; return -1; }
            stmt->bindString(1, name);
            stmt->bindString(2, "");
            stmt->bindString(3, "");
            if(!stmt->execute()){ PLOGE << "createItem execute failed"; return -1; }
            int64_t id = dbBackend->lastInsertId();
            if(id <= 0) return -1;
            if(parentID == -1) parentID = getOrCreateRoot();
            addParentRelation(parentID, id);
            return id;
        }

        if(!dbConnection) return -1;
        sqlite3_stmt* stmt = nullptr;
        const char* insertSQL = "INSERT INTO VaultItems (Name, Content, Tags) VALUES (?, ?, ?);";
        if(sqlite3_prepare_v2(dbConnection, insertSQL, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, "", -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, "", -1, SQLITE_STATIC);
            sqlite3_step(stmt);
        }
        if(stmt) sqlite3_finalize(stmt);
        int64_t id = sqlite3_last_insert_rowid(dbConnection);
        if(id <= 0) return -1;
        if(parentID == -1) parentID = getOrCreateRoot();
        addParentRelation(parentID, id);
        return id;
    }

    // Convenience: create an item and set its content and tags atomically
    int64_t createItemWithContent(const std::string& name, const std::string& content, const std::vector<std::string>& tags, int64_t parentID = -1){
        int64_t id = createItem(name, parentID);
        if(id <= 0) return id;
        std::string joined = joinTags(tags);
        if(dbBackend && dbBackend->isOpen()){
            std::string err;
            auto stmt = dbBackend->prepare("UPDATE VaultItems SET Content = ?, Tags = ? WHERE ID = ?;", &err);
            if(!stmt){ PLOGE << "createItemWithContent prepare failed: " << err; return id; }
            stmt->bindString(1, content);
            stmt->bindString(2, joined);
            stmt->bindInt(3, id);
            if(!stmt->execute()) PLOGE << "createItemWithContent execute failed";
            return id;
        }

        const char* updateSQL = "UPDATE VaultItems SET Content = ?, Tags = ? WHERE ID = ?;";
        sqlite3_stmt* stmt = nullptr;
        if(sqlite3_prepare_v2(dbConnection, updateSQL, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_text(stmt,1,content.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt,2,joined.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt,3,id);
            sqlite3_step(stmt);
        }
        if(stmt) sqlite3_finalize(stmt);
        return id;
    }

    // Public helper to fetch content for a given item id
    std::string getItemContentPublic(int64_t id){
        const char* sql = "SELECT Content FROM VaultItems WHERE ID = ?;";
        sqlite3_stmt* stmt = nullptr;
        std::string out;
        if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_int64(stmt,1,id);
            if(sqlite3_step(stmt) == SQLITE_ROW){
                const unsigned char* text = sqlite3_column_text(stmt,0);
                if(text) out = reinterpret_cast<const char*>(text);
            }
        }
        if(stmt) sqlite3_finalize(stmt);
        return out;
    }

    std::string getItemNamePublic(int64_t id){ return getItemName(id); }

private:
    std::string getItemName(int64_t id){
        // Prefer remote backend when available
        if(dbBackend && dbBackend->isOpen()){
            std::string err;
            auto stmt = dbBackend->prepare("SELECT Name FROM VaultItems WHERE ID = ? LIMIT 1;", &err);
            if(stmt){
                stmt->bindInt(1, id);
                auto rs = stmt->executeQuery();
                if(rs && rs->next()){
                    std::string nm = rs->getString(0);
                    if(nm.empty()){
                        PLOGW << "getItemName: empty name for id=" << id;
                        return std::string("<unknown>");
                    }
                    return nm;
                } else {
                    PLOGW << "getItemName: no row for id=" << id;
                }
            } else {
                PLOGW << "getItemName: prepare failed: " << err;
            }
            return std::string("<unknown>");
        }

        // Local SQLite path
        const char* sql = "SELECT Name FROM VaultItems WHERE ID = ?;";
        sqlite3_stmt* stmt = nullptr;
        std::string name = "<unknown>";
        if(dbConnection && sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_int64(stmt, 1, id);
            if(sqlite3_step(stmt) == SQLITE_ROW){
                const unsigned char* text = sqlite3_column_text(stmt, 0);
                if(text) name = reinterpret_cast<const char*>(text);
            }
        }
        if(stmt) sqlite3_finalize(stmt);
        return name;
    }

    void getChildren(int64_t parentID, std::vector<int64_t>& outChildren){
        // Remote backend preferred
        if(dbBackend && dbBackend->isOpen()){
            std::string err;
            auto stmt = dbBackend->prepare("SELECT ChildID FROM VaultItemChildren WHERE ParentID = ?;", &err);
            if(stmt){
                stmt->bindInt(1, parentID);
                auto rs = stmt->executeQuery();
                while(rs && rs->next()){
                    outChildren.push_back(rs->getInt64(0));
                }
            } else {
                PLOGW << "getChildren: prepare failed: " << err;
            }
            return;
        }

        const char* sql = "SELECT ChildID FROM VaultItemChildren WHERE ParentID = ?;";
        sqlite3_stmt* stmt = nullptr;
        if(dbConnection && sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_int64(stmt, 1, parentID);
            while(sqlite3_step(stmt) == SQLITE_ROW){
                outChildren.push_back(sqlite3_column_int64(stmt, 0));
            }
        }
        if(stmt) sqlite3_finalize(stmt);
    }

    void drawVaultNode(int64_t parentID, int64_t nodeID, std::vector<int64_t>& path){
        // If filtering is active, skip subtrees that have no matching nodes
        if(!activeTagFilter.empty()){
            std::unordered_set<int64_t> visited;
            if(!nodeOrDescendantMatches(nodeID, visited)) return;
        }
        // If a user is logged in, skip nodes that are not visible to them (and have no visible descendants)
        if(currentUserID != -1){
            std::unordered_set<int64_t> vvisited;
            if(!nodeOrDescendantVisible(nodeID, currentUserID, vvisited)) return;
        }
        // Cycle protection: if node already in the current path, render as leaf with a cycle marker
        if(std::find(path.begin(), path.end(), nodeID) != path.end()){
            std::string cycName = getItemName(nodeID) + " (cycle)";
            ImGui::PushID(static_cast<int>(parentID));
            ImGui::PushID(static_cast<int>(nodeID));
            std::string cycLabel = cycName + "##" + std::to_string(nodeID);
            if(ImGui::Selectable(cycLabel.c_str(), selectedItemID == nodeID)){
                selectedItemID = nodeID;
            }
            ImGui::PopID();
            ImGui::PopID();
            return;
        }

        std::string name = getItemName(nodeID);
        std::vector<int64_t> children;
        getChildren(nodeID, children);

        bool isRootNode = (nodeID == getOrCreateRoot());
        // If this node has a child-filter, show it in the label for clarity
        std::string displayName = name + (isRootNode ? std::string(" (vault)") : std::string());
        auto fit = nodeChildFilters.find(nodeID);
        if(fit != nodeChildFilters.end()){
            if(fit->second.mode == "EXPR"){
                if(!fit->second.expr.empty()) displayName += std::string(" [filter: EXPR: ") + fit->second.expr + "]";
            } else if(!fit->second.tags.empty()){
                std::string fstr;
                for(size_t i=0;i<fit->second.tags.size();++i){ if(i) fstr += ","; fstr += fit->second.tags[i]; }
                displayName += std::string(" [filter: ") + fit->second.mode + std::string(": ") + fstr + "]";
            }
        }

        ImGui::PushID(static_cast<int>(parentID));
        ImGui::PushID(static_cast<int>(nodeID));

        if(children.empty()){
            std::string label = displayName + "##" + std::to_string(nodeID);
            if(ImGui::Selectable(label.c_str(), selectedItemID == nodeID)){
                selectedItemID = nodeID;
            }

            // Context menu for leaf node
            if(ImGui::BeginPopupContextItem("node_context")){
                if(ImGui::MenuItem("New Child")){
                    std::vector<int64_t> fullPath = path; fullPath.push_back(nodeID);
                    int64_t nid = createItem("New Note", nodeID);
                    if(nid != -1){
                        auto inherited = collectTagsFromPath(fullPath);
                        if(!inherited.empty()){
                            setTagsFor(nid, inherited);
                            std::string s; for(size_t ii=0; ii<inherited.size(); ++ii){ if(ii) s += ","; s += inherited[ii]; }
                            statusMessage = std::string("Inherited tags: ") + s; statusTime = ImGui::GetTime();
                        }
                        selectedItemID = nid;
                    }
                }
                if(ImGui::MenuItem("Rename")){
                    renameTargetID = nodeID;
                    strncpy(renameBuf, getItemName(nodeID).c_str(), sizeof(renameBuf));
                    showRenameModal = true;
                }
                if(ImGui::MenuItem("Delete", nullptr, false, !isRootNode)){
                    deleteTargetID = nodeID;
                    showDeleteModal = true;
                }
                if(ImGui::MenuItem("Import Markdown...")){
                    showImportModal = true;
                    importParentID = nodeID;
                    importParentPath = path; importParentPath.push_back(nodeID);
                    importPath = std::filesystem::current_path();
                    importSelectedFiles.clear();
                }
                if(ImGui::MenuItem("Set Child Filter...")){
                    setFilterTargetID = nodeID;
                    auto it = nodeChildFilters.find(nodeID);
                    setFilterInitialTags.clear();
                    setFilterModeDefault = 0;
                    setFilterBuf[0] = '\0';
                    if(it != nodeChildFilters.end()){
                        if(it->second.mode == "OR") setFilterModeDefault = 1;
                        else if(it->second.mode == "EXPR") setFilterModeDefault = 2;
                        else setFilterModeDefault = 0;
                        setFilterInitialTags = it->second.tags;
                        if(!it->second.expr.empty()) strncpy(setFilterBuf, it->second.expr.c_str(), sizeof(setFilterBuf));
                    }
                    showSetFilterModal = true;
                }
                if(ImGui::MenuItem("Clear Child Filter", nullptr, false, nodeChildFilters.find(nodeID) != nodeChildFilters.end())){
                    clearNodeFilterFromDB(nodeID);
                    nodeChildFilters.erase(nodeID);
                }
                ImGui::EndPopup();
            }
        } else {
            ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
            if(isRootNode) nodeFlags |= ImGuiTreeNodeFlags_DefaultOpen;
            bool open = ImGui::TreeNodeEx((void*)(intptr_t)nodeID, nodeFlags, "%s", displayName.c_str());
            if(ImGui::IsItemClicked()){
                selectedItemID = nodeID;
            }

            // Context menu for tree node (right-click the label)
            if(ImGui::BeginPopupContextItem("node_context")){
                if(ImGui::MenuItem("New Child")){
                    std::vector<int64_t> fullPath = path; fullPath.push_back(nodeID);
                    int64_t nid = createItem("New Note", nodeID);
                    if(nid != -1){
                        auto inherited = collectTagsFromPath(fullPath);
                        if(!inherited.empty()){
                            setTagsFor(nid, inherited);
                            std::string s; for(size_t ii=0; ii<inherited.size(); ++ii){ if(ii) s += ","; s += inherited[ii]; }
                            statusMessage = std::string("Inherited tags: ") + s; statusTime = ImGui::GetTime();
                        }
                        selectedItemID = nid;
                    }
                }
                if(ImGui::MenuItem("Rename")){
                    renameTargetID = nodeID;
                    strncpy(renameBuf, getItemName(nodeID).c_str(), sizeof(renameBuf));
                    showRenameModal = true;
                }
                if(ImGui::MenuItem("Delete", nullptr, false, !isRootNode)){
                    deleteTargetID = nodeID;
                    showDeleteModal = true;
                }
                if(ImGui::MenuItem("Import Markdown...")){
                    showImportModal = true;
                    importParentID = nodeID;
                    importParentPath = path; importParentPath.push_back(nodeID);
                    importPath = std::filesystem::current_path();
                    importSelectedFiles.clear();
                }
                if(ImGui::MenuItem("Set Child Filter...")){
                    setFilterTargetID = nodeID;
                    auto it = nodeChildFilters.find(nodeID);
                    setFilterInitialTags.clear();
                    setFilterModeDefault = 0;
                    setFilterBuf[0] = '\0';
                    if(it != nodeChildFilters.end()){
                        if(it->second.mode == "OR") setFilterModeDefault = 1;
                        else if(it->second.mode == "EXPR") setFilterModeDefault = 2;
                        else setFilterModeDefault = 0;
                        setFilterInitialTags = it->second.tags;
                        if(!it->second.expr.empty()) strncpy(setFilterBuf, it->second.expr.c_str(), sizeof(setFilterBuf));
                    }
                    showSetFilterModal = true;
                }
                if(ImGui::MenuItem("Clear Child Filter", nullptr, false, nodeChildFilters.find(nodeID) != nodeChildFilters.end())){
                    clearNodeFilterFromDB(nodeID);
                    nodeChildFilters.erase(nodeID);
                }
                ImGui::EndPopup();
            }

            if(open){
                path.push_back(nodeID);
                // Build filters to pass to children: start with inherited Filters passed to this call
                // We need to obtain inherited filters from the caller; since this function signature didn't have it,
                // use an ad-hoc approach: check if current node has any child filters and pass them down cumulatively.
                // To make this correct, we modify recursive calls to gather filters along the path.
                // Gather filters from ancestors by walking the path and collecting nodeChildFilters entries.
                std::vector<ChildFilterSpec> inheritedSpecs;
                for(auto anc : path){
                    if(anc == nodeID) continue; // skip current node
                    auto it = nodeChildFilters.find(anc);
                    if(it != nodeChildFilters.end()) inheritedSpecs.push_back(it->second);
                }
                // Also include this node's filter for its children
                auto itcur = nodeChildFilters.find(nodeID);
                if(itcur != nodeChildFilters.end()) inheritedSpecs.push_back(itcur->second);

                for(auto child : children){
                    // If inherited specs exclude this child and it has no matching descendants, skip rendering this child
                    std::unordered_set<int64_t> visited;
                    if(!inheritedSpecs.empty()){
                        if(!nodeOrDescendantMatchesWithSpecs(child, inheritedSpecs, visited)) continue;
                    }
                    drawVaultNode(nodeID, child, path);
                }
                path.pop_back();
                ImGui::TreePop();
            }
        }

        ImGui::PopID();
        ImGui::PopID();
    }

    // Helper: get all items (id,name)
    std::vector<std::pair<int64_t,std::string>> getAllItems(){
        std::vector<std::pair<int64_t,std::string>> out;
        if(dbBackend && dbBackend->isOpen()){
            std::string err;
            auto stmt = dbBackend->prepare("SELECT ID, Name FROM VaultItems ORDER BY Name;", &err);
            if(!stmt){ PLOGW << "getAllItems prepare failed: " << err; return out; }
            auto rs = stmt->executeQuery();
            while(rs && rs->next()){
                out.emplace_back(rs->getInt64(0), rs->getString(1));
            }
            return out;
        }

        const char* sql = "SELECT ID, Name FROM VaultItems ORDER BY Name;";
        sqlite3_stmt* stmt = nullptr;
        if(dbConnection && sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
            while(sqlite3_step(stmt) == SQLITE_ROW){
                int64_t id = sqlite3_column_int64(stmt, 0);
                const unsigned char* text = sqlite3_column_text(stmt, 1);
                std::string name = text ? reinterpret_cast<const char*>(text) : std::string();
                out.emplace_back(id, name);
            }
        }
        if(stmt) sqlite3_finalize(stmt);
        return out;
    }

    // Get parents of a child
    std::vector<int64_t> getParentsOf(int64_t childID){
        std::vector<int64_t> out;
        if(dbBackend && dbBackend->isOpen()){
            std::string err;
            auto stmt = dbBackend->prepare("SELECT ParentID FROM VaultItemChildren WHERE ChildID = ?;", &err);
            if(!stmt){ PLOGW << "getParentsOf prepare failed: " << err; }
            else{
                stmt->bindInt(1, childID);
                auto rs = stmt->executeQuery();
                while(rs && rs->next()){
                    int64_t pid = rs->getInt64(0);
                    if(pid == childID){
                        // remove invalid self relation
                        auto del = dbBackend->prepare("DELETE FROM VaultItemChildren WHERE ParentID = ? AND ChildID = ?;", &err);
                        if(del){ del->bindInt(1, pid); del->bindInt(2, childID); del->execute(); }
                        continue;
                    }
                    out.push_back(pid);
                }
            }
        } else {
            const char* sql = "SELECT ParentID FROM VaultItemChildren WHERE ChildID = ?;";
            sqlite3_stmt* stmt = nullptr;
            if(dbConnection && sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
                sqlite3_bind_int64(stmt, 1, childID);
                while(sqlite3_step(stmt) == SQLITE_ROW){
                    int64_t pid = sqlite3_column_int64(stmt, 0);
                    if(pid == childID){
                        const char* del = "DELETE FROM VaultItemChildren WHERE ParentID = ? AND ChildID = ?;";
                        sqlite3_stmt* dstmt = nullptr;
                        if(sqlite3_prepare_v2(dbConnection, del, -1, &dstmt, nullptr) == SQLITE_OK){
                            sqlite3_bind_int64(dstmt, 1, pid);
                            sqlite3_bind_int64(dstmt, 2, childID);
                            sqlite3_step(dstmt);
                        }
                        if(dstmt) sqlite3_finalize(dstmt);
                        continue;
                    }
                    out.push_back(pid);
                }
            }
            if(stmt) sqlite3_finalize(stmt);
        }

        // If there are no parents, ensure the item is attached to the root
        int64_t root = getOrCreateRoot();
        if(out.empty() && childID != root){
            // Add root as parent and return it in the list
            addParentRelation(root, childID);
            out.push_back(root);
        }
        return out;
    }

    bool addParentRelation(int64_t parentID, int64_t childID){
        if(parentID == childID) return false;
        // Root may not have parents
        int64_t root = getOrCreateRoot();
        if(childID == root) return false;

        // Remote backend path
        if(dbBackend && dbBackend->isOpen()){
            std::string err;
            // prevent duplicate
            auto chk = dbBackend->prepare("SELECT 1 FROM VaultItemChildren WHERE ParentID = ? AND ChildID = ? LIMIT 1;", &err);
            if(!chk){ PLOGW << "addParentRelation: prepare check failed: " << err; return false; }
            chk->bindInt(1, parentID);
            chk->bindInt(2, childID);
            auto rs = chk->executeQuery();
            if(rs && rs->next()) return false;

            // insert relation
            auto ins = dbBackend->prepare("INSERT INTO VaultItemChildren (ParentID, ChildID) VALUES (?, ?);", &err);
            if(!ins){ PLOGW << "addParentRelation: prepare insert failed: " << err; return false; }
            ins->bindInt(1, parentID);
            ins->bindInt(2, childID);
            if(!ins->execute()){ PLOGW << "addParentRelation: insert execute failed"; return false; }
            return true;
        }

        // Local SQLite path
        // prevent duplicate
        const char* exists = "SELECT 1 FROM VaultItemChildren WHERE ParentID = ? AND ChildID = ?;";
        sqlite3_stmt* stmt = nullptr;
        if(dbConnection && sqlite3_prepare_v2(dbConnection, exists, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_int64(stmt,1,parentID);
            sqlite3_bind_int64(stmt,2,childID);
            if(sqlite3_step(stmt) == SQLITE_ROW){
                sqlite3_finalize(stmt);
                return false;
            }
        }
        if(stmt) sqlite3_finalize(stmt);

        // NOTE: cycles are allowed now, so we don't prevent them here

        const char* insert = "INSERT INTO VaultItemChildren (ParentID, ChildID) VALUES (?, ?);";
        if(dbConnection && sqlite3_prepare_v2(dbConnection, insert, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_int64(stmt,1,parentID);
            sqlite3_bind_int64(stmt,2,childID);
            sqlite3_step(stmt);
        }
        if(stmt) sqlite3_finalize(stmt);
        return true;
    }

    bool removeParentRelation(int64_t parentID, int64_t childID){
        int64_t root = getOrCreateRoot();
        // Never allow modifying parents of the root itself
        if(childID == root) return false;

        // Remote backend path
        if(dbBackend && dbBackend->isOpen()){
            std::string err;
            // Count current parents
            auto cnt = dbBackend->prepare("SELECT COUNT(*) FROM VaultItemChildren WHERE ChildID = ?;", &err);
            if(!cnt){ PLOGW << "removeParentRelation: count prepare failed: " << err; return false; }
            cnt->bindInt(1, childID);
            auto rs = cnt->executeQuery();
            int parentCount = 0;
            if(rs && rs->next()) parentCount = rs->getInt(0);

            // Disallow removing the last remaining parent (never leave a node parentless)
            if(parentCount <= 1) return false;

            // Otherwise, removal is allowed
            auto del = dbBackend->prepare("DELETE FROM VaultItemChildren WHERE ParentID = ? AND ChildID = ?;", &err);
            if(!del){ PLOGW << "removeParentRelation: delete prepare failed: " << err; return false; }
            del->bindInt(1, parentID);
            del->bindInt(2, childID);
            if(!del->execute()){ PLOGW << "removeParentRelation: delete execute failed"; return false; }
            return true;
        }

        // Local SQLite path
        // Count current parents
        int parentCount = 0;
        const char* countSQL = "SELECT COUNT(*) FROM VaultItemChildren WHERE ChildID = ?;";
        sqlite3_stmt* stmt = nullptr;
        if(dbConnection && sqlite3_prepare_v2(dbConnection, countSQL, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_int64(stmt, 1, childID);
            if(sqlite3_step(stmt) == SQLITE_ROW) parentCount = sqlite3_column_int(stmt, 0);
        }
        if(stmt) sqlite3_finalize(stmt);

        // Disallow removing the last remaining parent (never leave a node parentless)
        if(parentCount <= 1) return false;

        // Otherwise, removal is allowed (including removing the root if there are other parents)
        const char* del = "DELETE FROM VaultItemChildren WHERE ParentID = ? AND ChildID = ?;";
        stmt = nullptr;
        if(dbConnection && sqlite3_prepare_v2(dbConnection, del, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_int64(stmt,1,parentID);
            sqlite3_bind_int64(stmt,2,childID);
            sqlite3_step(stmt);
        }
        if(stmt) sqlite3_finalize(stmt);

        return true;
    }

    // Return true if there is a path from start -> target via child edges
    bool hasPath(int64_t start, int64_t target){
        if(start == target) return true;
        std::vector<int64_t> stack;
        std::unordered_set<int64_t> visited;
        stack.push_back(start);
        while(!stack.empty()){
            int64_t cur = stack.back(); stack.pop_back();
            if(visited.find(cur) != visited.end()) continue;
            visited.insert(cur);
            std::vector<int64_t> children;
            getChildren(cur, children);
            for(auto c : children){
                if(c == target) return true;
                if(visited.find(c) == visited.end()) stack.push_back(c);
            }
        }
        return false;
    }

    // Tags helpers
    static inline std::string trim(const std::string &s){
        size_t l = 0; while(l < s.size() && std::isspace((unsigned char)s[l])) ++l;
        size_t r = s.size(); while(r > l && std::isspace((unsigned char)s[r-1])) --r;
        return s.substr(l, r-l);
    }

    std::vector<std::string> parseTags(const std::string &s){
        std::vector<std::string> out;
        std::stringstream ss(s);
        std::string item;
        while(std::getline(ss, item, ',')){
            std::string t = trim(item);
            if(!t.empty()) out.push_back(t);
        }
        return out;
    }

    std::string joinTags(const std::vector<std::string> &tags){
        std::string out;
        for(size_t i=0;i<tags.size();++i){ if(i) out += ","; out += tags[i]; }
        return out;
    }

    // Collect tags along a specific UI path (path contains ancestor IDs from root..node).
    // We iterate from nearest parent to root so immediate parent's tags take precedence.
    std::vector<std::string> collectTagsFromPath(const std::vector<int64_t> &path){
        std::vector<std::string> out;
        std::unordered_set<std::string> seenLower;
        for(int i = (int)path.size()-1; i >= 0; --i){
            int64_t id = path[i];
            try{
                auto tags = parseTags(getTagsOf(id));
                for(auto &t : tags){
                    std::string low = toLowerCopy(t);
                    if(seenLower.insert(low).second) out.push_back(t);
                }
            } catch(...){ }
        }
        return out;
    }

    // Fallback: collect union of ancestor tags reachable from a node (BFS upward), nearest ancestors first
    std::vector<std::string> collectTagsUnionFromNode(int64_t nodeID){
        std::vector<std::string> out;
        if(nodeID < 0) return out;
        std::unordered_set<std::string> seenLower;
        std::vector<int64_t> q;
        std::unordered_set<int64_t> visited;
        q.push_back(nodeID); visited.insert(nodeID);
        size_t qi = 0;
        while(qi < q.size()){
            int64_t cur = q[qi++];
            try{
                auto tags = parseTags(getTagsOf(cur));
                for(auto &t : tags){
                    std::string low = toLowerCopy(t);
                    if(seenLower.insert(low).second) out.push_back(t);
                }
                auto parents = getParentsOf(cur);
                for(auto p : parents){ if(visited.insert(p).second) q.push_back(p); }
            } catch(...){ }
        }
        return out;
    }

    bool setTagsFor(int64_t id, const std::vector<std::string> &tags){
        const char* updateSQL = "UPDATE VaultItems SET Tags = ? WHERE ID = ?;";
        sqlite3_stmt* stmt = nullptr;
        std::string joined = joinTags(tags);
        if(sqlite3_prepare_v2(dbConnection, updateSQL, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_text(stmt,1, joined.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt,2, id);
            sqlite3_step(stmt);
        }
        if(stmt) sqlite3_finalize(stmt);
        return true;
    }

    std::string getTagsOf(int64_t id){
        const char* sql = "SELECT Tags FROM VaultItems WHERE ID = ?;";
        sqlite3_stmt* stmt = nullptr;
        std::string tags;
        if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_int64(stmt,1,id);
            if(sqlite3_step(stmt) == SQLITE_ROW){
                const unsigned char* text = sqlite3_column_text(stmt,0);
                if(text) tags = reinterpret_cast<const char*>(text);
            }
        }
        if(stmt) sqlite3_finalize(stmt);
        return tags;
    }

    bool renameItem(int64_t id, const std::string &newName){
        if(id < 0) return false;
        const char* sql = "UPDATE VaultItems SET Name = ? WHERE ID = ?;";
        sqlite3_stmt* stmt = nullptr;
        if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_text(stmt,1,newName.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt,2, id);
            sqlite3_step(stmt);
        }
        if(stmt) sqlite3_finalize(stmt);
        return true;
    }

    bool deleteItem(int64_t id){
        if(id <= 0) return false;
        int64_t root = getOrCreateRoot();
        if(id == root) return false; // never delete root

        // collect children before deletion
        std::vector<int64_t> children;
        getChildren(id, children);

        // delete relations involving this node
        const char* delRel = "DELETE FROM VaultItemChildren WHERE ParentID = ? OR ChildID = ?;";
        sqlite3_stmt* stmt = nullptr;
        if(sqlite3_prepare_v2(dbConnection, delRel, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_int64(stmt,1,id);
            sqlite3_bind_int64(stmt,2,id);
            sqlite3_step(stmt);
        }
        if(stmt) sqlite3_finalize(stmt);

        // delete the item itself
        const char* delItem = "DELETE FROM VaultItems WHERE ID = ?;";
        if(sqlite3_prepare_v2(dbConnection, delItem, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_int64(stmt,1,id);
            sqlite3_step(stmt);
        }
        if(stmt) sqlite3_finalize(stmt);

        // ensure children are attached to root if they lost all parents
        for(auto c : children){
            int count = 0;
            const char* countSQL = "SELECT COUNT(*) FROM VaultItemChildren WHERE ChildID = ?;";
            if(sqlite3_prepare_v2(dbConnection, countSQL, -1, &stmt, nullptr) == SQLITE_OK){
                sqlite3_bind_int64(stmt,1,c);
                if(sqlite3_step(stmt) == SQLITE_ROW) count = sqlite3_column_int(stmt,0);
            }
            if(stmt) sqlite3_finalize(stmt);
            if(count == 0) addParentRelation(root, c);
        }

        // clear selection/content if we deleted the selected/loaded item
        if(selectedItemID == id) selectedItemID = -1;
        if(loadedItemID == id){ loadedItemID = -1; currentContent.clear(); currentTitle.clear(); currentTags.clear(); contentDirty = false; }

        return true;
    }

    bool addTagToItem(int64_t id, const std::string &tag){
        if(id < 0) return false;
        auto tags = parseTags(getTagsOf(id));
        for(auto &t : tags) if(t == tag) return false;
        tags.push_back(tag);
        return setTagsFor(id, tags);
    }

    bool removeTagFromItem(int64_t id, const std::string &tag){
        if(id < 0) return false;
        auto tags = parseTags(getTagsOf(id));
        auto it = std::remove(tags.begin(), tags.end(), tag);
        if(it == tags.end()) return false;
        tags.erase(it, tags.end());
        return setTagsFor(id, tags);
    }
};

inline int64_t Vault::addAttachment(int64_t itemID, const std::string& name, const std::string& mimeType, const std::vector<uint8_t>& data, const std::string& externalPath){
    // Prefer backend abstraction when available (remote MySQL); fall back to SQLite when local
    if(dbBackend && dbBackend->isOpen()){
        std::string err;
        auto stmt = dbBackend->prepare("INSERT INTO Attachments (ItemID, Name, MimeType, Data, ExternalPath, Size) VALUES (?, ?, ?, ?, ?, ?);", &err);
        if(!stmt){ PLOGE << "addAttachment prepare failed: " << err; return -1; }
        if(itemID >= 0) stmt->bindInt(1, itemID); else stmt->bindNull(1);
        stmt->bindString(2, name);
        stmt->bindString(3, mimeType);
        if(!data.empty()) stmt->bindBlob(4, reinterpret_cast<const void*>(data.data()), data.size()); else stmt->bindNull(4);
        if(!externalPath.empty()) stmt->bindString(5, externalPath); else stmt->bindNull(5);
        stmt->bindInt(6, static_cast<int64_t>(data.size()));
        if(!stmt->execute()) { PLOGE << "addAttachment execute failed"; return -1; }
        return dbBackend->lastInsertId();
    }

    if(!dbConnection) return -1;
    const char* sql = "INSERT INTO Attachments (ItemID, Name, MimeType, Data, ExternalPath, Size) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        if(itemID >= 0) sqlite3_bind_int64(stmt, 1, itemID); else sqlite3_bind_null(stmt, 1);
        sqlite3_bind_text(stmt, 2, name.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, mimeType.c_str(), -1, SQLITE_TRANSIENT);
        if(!data.empty()) sqlite3_bind_blob(stmt, 4, reinterpret_cast<const void*>(data.data()), static_cast<int>(data.size()), SQLITE_TRANSIENT);
        else sqlite3_bind_null(stmt, 4);
        if(!externalPath.empty()) sqlite3_bind_text(stmt, 5, externalPath.c_str(), -1, SQLITE_TRANSIENT);
        else sqlite3_bind_null(stmt, 5);
        sqlite3_bind_int64(stmt, 6, static_cast<int64_t>(data.size()));
        sqlite3_step(stmt);
    }
    if(stmt) sqlite3_finalize(stmt);
    return sqlite3_last_insert_rowid(dbConnection);
}

inline Vault::Attachment Vault::getAttachmentMeta(int64_t attachmentID){
    Attachment a;
    PLOGV << "vault:getAttachmentMeta id=" << attachmentID;
    if(dbBackend && dbBackend->isOpen()){
        std::string err;
        auto stmt = dbBackend->prepare("SELECT ID, ItemID, Name, MimeType, Size, ExternalPath, CreatedAt, DisplayWidth, DisplayHeight FROM Attachments WHERE ID = ? LIMIT 1;", &err);
        if(!stmt){ PLOGE << "getAttachmentMeta prepare failed: " << err; return a; }
        stmt->bindInt(1, attachmentID);
        auto rs = stmt->executeQuery();
        if(rs && rs->next()){
            a.id = rs->getInt64(0);
            a.itemID = rs->getInt64(1);
            a.name = rs->getString(2);
            a.mimeType = rs->getString(3);
            a.size = rs->getInt64(4);
            a.externalPath = rs->getString(5);
            a.createdAt = rs->getInt64(6);
            // Display prefs might be nullable; attempt to read but tolerate empty
            try{ a.displayWidth = rs->getInt(7); } catch(...){}
            try{ a.displayHeight = rs->getInt(8); } catch(...){}
            PLOGV << "vault:getAttachmentMeta got row id=" << a.id << " name='" << a.name << "' size=" << a.size << " externalPath='" << a.externalPath << "' mime='" << a.mimeType << "'";
        }
        return a;
    }

    if(!dbConnection) return a;
    const char* sql = "SELECT ID, ItemID, Name, MimeType, Size, ExternalPath, CreatedAt, DisplayWidth, DisplayHeight FROM Attachments WHERE ID = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(stmt, 1, attachmentID);
        int step = sqlite3_step(stmt);
        if(step == SQLITE_ROW){
            a.id = sqlite3_column_int64(stmt, 0);
            a.itemID = sqlite3_column_int64(stmt, 1);
            const unsigned char* t0 = sqlite3_column_text(stmt, 2); if(t0) a.name = reinterpret_cast<const char*>(t0);
            const unsigned char* t1 = sqlite3_column_text(stmt, 3); if(t1) a.mimeType = reinterpret_cast<const char*>(t1);
            a.size = sqlite3_column_int64(stmt, 4);
            const unsigned char* t2 = sqlite3_column_text(stmt, 5); if(t2) a.externalPath = reinterpret_cast<const char*>(t2);
            a.createdAt = sqlite3_column_int64(stmt, 6);
            // Display preferences (may be NULL)
            if(sqlite3_column_type(stmt, 7) != SQLITE_NULL) a.displayWidth = static_cast<int>(sqlite3_column_int(stmt, 7));
            if(sqlite3_column_type(stmt, 8) != SQLITE_NULL) a.displayHeight = static_cast<int>(sqlite3_column_int(stmt, 8));
            PLOGV << "vault:getAttachmentMeta got row id=" << a.id << " name='" << a.name << "' size=" << a.size << " externalPath='" << a.externalPath << "' mime='" << a.mimeType << "'";
        } else {
            PLOGW << "vault:getAttachmentMeta no row for id=" << attachmentID << " (sqlite_step=" << step << ")";
        }
    } else {
        PLOGE << "vault:getAttachmentMeta prepare failed for id=" << attachmentID << " err=" << sqlite3_errmsg(dbConnection);
    }
    if(stmt) sqlite3_finalize(stmt);
    return a;
}

inline std::vector<Vault::Attachment> Vault::listAttachments(int64_t itemID){
    std::vector<Attachment> out;
    if(dbBackend && dbBackend->isOpen()){
        std::string err;
        auto stmt = dbBackend->prepare("SELECT ID, ItemID, Name, MimeType, Size, ExternalPath, CreatedAt FROM Attachments WHERE ItemID = ? ORDER BY ID ASC;", &err);
        if(!stmt){ PLOGE << "listAttachments prepare failed: " << err; return out; }
        stmt->bindInt(1, itemID);
        auto rs = stmt->executeQuery();
        while(rs && rs->next()){
            Attachment a;
            a.id = rs->getInt64(0);
            a.itemID = rs->getInt64(1);
            a.name = rs->getString(2);
            a.mimeType = rs->getString(3);
            a.size = rs->getInt64(4);
            a.externalPath = rs->getString(5);
            a.createdAt = rs->getInt64(6);
            out.push_back(a);
        }
        return out;
    }

    if(!dbConnection) return out;
    const char* sql = "SELECT ID, ItemID, Name, MimeType, Size, ExternalPath, CreatedAt FROM Attachments WHERE ItemID = ? ORDER BY ID ASC;";
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(stmt, 1, itemID);
        while(sqlite3_step(stmt) == SQLITE_ROW){
            Attachment a;
            a.id = sqlite3_column_int64(stmt, 0);
            a.itemID = sqlite3_column_int64(stmt, 1);
            const unsigned char* t0 = sqlite3_column_text(stmt, 2); if(t0) a.name = reinterpret_cast<const char*>(t0);
            const unsigned char* t1 = sqlite3_column_text(stmt, 3); if(t1) a.mimeType = reinterpret_cast<const char*>(t1);
            a.size = sqlite3_column_int64(stmt, 4);
            const unsigned char* t2 = sqlite3_column_text(stmt, 5); if(t2) a.externalPath = reinterpret_cast<const char*>(t2);
            a.createdAt = sqlite3_column_int64(stmt, 6);
            out.push_back(a);
        }
    }
    if(stmt) sqlite3_finalize(stmt);
    return out;
}

inline std::vector<uint8_t> Vault::getAttachmentData(int64_t attachmentID){
    std::vector<uint8_t> out;
    if(dbBackend && dbBackend->isOpen()){
        std::string err;
        auto stmt = dbBackend->prepare("SELECT Data FROM Attachments WHERE ID = ? LIMIT 1;", &err);
        if(!stmt){ PLOGE << "getAttachmentData prepare failed: " << err; return out; }
        stmt->bindInt(1, attachmentID);
        auto rs = stmt->executeQuery();
        if(rs && rs->next()){
            out = rs->getBlob(0);
        }
        return out;
    }

    if(!dbConnection) return out;
    const char* sql = "SELECT Data FROM Attachments WHERE ID = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(stmt, 1, attachmentID);
        if(sqlite3_step(stmt) == SQLITE_ROW){
            const void* blob = sqlite3_column_blob(stmt, 0);
            int bsize = sqlite3_column_bytes(stmt, 0);
            if(blob && bsize > 0){ out.resize(static_cast<size_t>(bsize)); memcpy(out.data(), blob, static_cast<size_t>(bsize)); }
        }
    }
    if(stmt) sqlite3_finalize(stmt);
    return out;
}

inline bool Vault::removeAttachment(int64_t attachmentID){
    if(dbBackend && dbBackend->isOpen()){
        std::string err;
        auto stmt = dbBackend->prepare("DELETE FROM Attachments WHERE ID = ?;", &err);
        if(!stmt){ PLOGE << "removeAttachment prepare failed: " << err; return false; }
        stmt->bindInt(1, attachmentID);
        return stmt->execute();
    }

    if(!dbConnection) return false;
    const char* sql = "DELETE FROM Attachments WHERE ID = ?;";
    sqlite3_stmt* stmt = nullptr;
    bool ok = false;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(stmt, 1, attachmentID);
        if(sqlite3_step(stmt) == SQLITE_DONE) ok = true;
    }
    if(stmt) sqlite3_finalize(stmt);
    return ok;
}

inline bool Vault::setAttachmentDisplaySize(int64_t attachmentID, int width, int height){
    if(dbBackend && dbBackend->isOpen()){
        std::string err;
        auto stmt = dbBackend->prepare("UPDATE Attachments SET DisplayWidth = ?, DisplayHeight = ? WHERE ID = ?;", &err);
        if(!stmt){ PLOGE << "setAttachmentDisplaySize prepare failed: " << err; return false; }
        if(width >= 0) stmt->bindInt(1, width); else stmt->bindNull(1);
        if(height >= 0) stmt->bindInt(2, height); else stmt->bindNull(2);
        stmt->bindInt(3, attachmentID);
        bool ok = stmt->execute();
        if(ok) { statusMessage = "Display settings saved"; statusTime = ImGui::GetTime(); }
        return ok;
    }

    if(!dbConnection) return false;
    const char* sql = "UPDATE Attachments SET DisplayWidth = ?, DisplayHeight = ? WHERE ID = ?;";
    sqlite3_stmt* stmt = nullptr;
    bool ok = false;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        if(width >= 0) sqlite3_bind_int(stmt, 1, width); else sqlite3_bind_null(stmt,1);
        if(height >= 0) sqlite3_bind_int(stmt, 2, height); else sqlite3_bind_null(stmt,2);
        sqlite3_bind_int64(stmt, 3, attachmentID);
        if(sqlite3_step(stmt) == SQLITE_DONE) ok = true;
    }
    if(stmt) sqlite3_finalize(stmt);
    if(ok) { statusMessage = "Display settings saved"; statusTime = ImGui::GetTime(); }
    return ok;
}

inline void Vault::openPreviewFromSrc(const std::string& src){
    previewIsRaw = false; previewRawData.clear(); previewMime.clear(); previewName.clear();
    // vault://attachment/<id>
    const std::string prefix = "vault://attachment/";
    if(src.rfind(prefix, 0) == 0){
        std::string idstr = src.substr(prefix.size());
        try{
            int64_t aid = std::stoll(idstr);
            auto meta = getAttachmentMeta(aid);
            if(meta.size > 0){
                // Read data in worker thread then show preview on main thread
                std::thread([this, aid, meta](){
                    auto data = this->getAttachmentData(aid);
                    if(!data.empty()){
                        auto dataPtr = std::make_shared<std::vector<uint8_t>>(std::move(data));
                        this->enqueueMainThreadTask([this, dataPtr, meta, aid](){
                            this->previewRawData = std::move(*dataPtr);
                            this->previewMime = meta.mimeType;
                            this->previewName = meta.name;
                            this->previewIsRaw = true;
                            this->previewAttachmentID = aid; this->previewDisplayWidth = meta.displayWidth; this->previewDisplayHeight = meta.displayHeight;
                            this->showAttachmentPreview = true;
                            ImGui::OpenPopup("Attachment Preview");
                        });
                    } else {
                        if(!meta.externalPath.empty()) this->asyncFetchAndStoreAttachment(meta.id, meta.externalPath);
                    }
                }).detach();
                return;
            } else {
                // Data empty — try async fetch if ExternalPath present
                if(!meta.externalPath.empty()){
                    asyncFetchAndStoreAttachment(meta.id, meta.externalPath);
                    statusMessage = "Fetching attachment..."; statusTime = ImGui::GetTime();
                }
            }
            statusMessage = "Attachment empty"; statusTime = ImGui::GetTime();
        } catch(...) { statusMessage = "Invalid attachment id"; statusTime = ImGui::GetTime(); }
        return;
    }

    // vault://Assets/<name>
    const std::string assetsPrefix = "vault://Assets/";
    if(src.rfind(assetsPrefix,0) == 0){
        int64_t aid = findAttachmentByExternalPath(src);
        if(aid != -1){
            auto meta = getAttachmentMeta(aid);
            if(meta.size > 0){
                std::thread([this, aid, meta](){
                    auto data = this->getAttachmentData(aid);
                    if(!data.empty()){
                        auto dataPtr = std::make_shared<std::vector<uint8_t>>(std::move(data));
                        this->enqueueMainThreadTask([this, dataPtr, meta, aid](){
                            this->previewRawData = std::move(*dataPtr);
                            this->previewMime = meta.mimeType;
                            this->previewName = meta.name;
                            this->previewIsRaw = true;
                            this->previewAttachmentID = aid; this->previewDisplayWidth = meta.displayWidth; this->previewDisplayHeight = meta.displayHeight;
                            this->showAttachmentPreview = true;
                            ImGui::OpenPopup("Attachment Preview");
                        });
                    } else {
                        if(!meta.externalPath.empty()) this->asyncFetchAndStoreAttachment(meta.id, meta.externalPath);
                    }
                }).detach();
                return;
            } else {
                if(!meta.externalPath.empty()){
                    asyncFetchAndStoreAttachment(meta.id, meta.externalPath);
                    statusMessage = "Fetching asset..."; statusTime = ImGui::GetTime();
                    return;
                } else { statusMessage = "Asset not found"; statusTime = ImGui::GetTime(); return; }
            }
        } else { statusMessage = "Asset not found"; statusTime = ImGui::GetTime(); return; }
    }

    // file:// or plain path or http(s)
    std::string path = src;
    if(src.rfind("file://", 0) == 0) path = src.substr(7);
    if(src.rfind("http://", 0) == 0 || src.rfind("https://", 0) == 0){
        // Add to vault cache if not already present, and start async fetch
        int64_t aid = findAttachmentByExternalPath(src);
        if(aid == -1) aid = addAttachmentFromURL(src);
        if(aid != -1){
            auto meta = getAttachmentMeta(aid);
            if(meta.size > 0){
                std::thread([this, aid, meta, src](){
                    auto data = this->getAttachmentData(aid);
                    if(!data.empty()){
                        auto dataPtr = std::make_shared<std::vector<uint8_t>>(std::move(data));
                        this->enqueueMainThreadTask([this, dataPtr, meta, aid](){
                            this->previewRawData = std::move(*dataPtr);
                            this->previewMime = meta.mimeType;
                            this->previewName = meta.name;
                            this->previewIsRaw = true;
                            this->previewAttachmentID = aid;
                            this->showAttachmentPreview = true; ImGui::OpenPopup("Attachment Preview");
                        });
                    } else {
                        this->statusMessage = "Fetching web resource..."; this->statusTime = ImGui::GetTime();
                        this->asyncFetchAndStoreAttachment(aid, src);
                    }
                }).detach();
                return;
            } else {
                statusMessage = "Fetching web resource..."; statusTime = ImGui::GetTime();
                asyncFetchAndStoreAttachment(aid, src);
                return;
            }
        } else {
            statusMessage = "Failed to create cache record"; statusTime = ImGui::GetTime(); return;
        }
    }
    try{
        if(std::filesystem::exists(path) && std::filesystem::is_regular_file(path)){
            std::ifstream in(path, std::ios::binary);
            if(in){
                previewRawData.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
                previewName = std::filesystem::path(path).filename().string();
                std::string ext = std::filesystem::path(path).extension().string(); std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                if(ext==".png" || ext==".jpg" || ext==".jpeg" || ext==".bmp" || ext==".gif") previewMime = std::string("image/") + (ext.size()>1? ext.substr(1) : "");
                else previewMime = "application/octet-stream";
                previewIsRaw = true; showAttachmentPreview = true; previewAttachmentID = -1; ImGui::OpenPopup("Attachment Preview"); return;
            }
        }
    } catch(...){}
    statusMessage = "Unsupported preview source"; statusTime = ImGui::GetTime();
}

inline void Vault::openModelFromSrc(const std::string& src){
    // Try vault attachment first
    const std::string prefix = "vault://attachment/";
    if(src.rfind(prefix,0)==0){
        std::string idstr = src.substr(prefix.size());
        try{
            int64_t aid = std::stoll(idstr);
            auto meta = getAttachmentMeta(aid);
            if(meta.size>0){
                if(!modelViewer) modelViewer = std::make_unique<ModelViewer>();
                auto mvPtr = modelViewer.get();
                statusMessage = "Loading model..."; statusTime = ImGui::GetTime();
                std::thread([this, aid, mvPtr, metaId = meta.id, metaName = meta.name](){
                    auto data = getAttachmentData(aid);
                    if(!data.empty()){
                        auto dataPtr = std::make_shared<std::vector<uint8_t>>(std::move(data));
                        mvPtr->loadFromMemoryAsync(*dataPtr, metaName);
                        this->enqueueMainThreadTask([this](){ this->showModelViewer = true; });
                    } else {
                        auto m = this->getAttachmentMeta(aid);
                        if(!m.externalPath.empty()) this->asyncFetchAndStoreAttachment(m.id, m.externalPath);
                        else this->enqueueMainThreadTask([this](){ this->statusMessage = "Model data missing"; this->statusTime = ImGui::GetTime(); });
                    }
                }).detach();
                return;
            } else { if(!meta.externalPath.empty()) asyncFetchAndStoreAttachment(meta.id, meta.externalPath); }
        } catch(...){}
        return;
    }

    // http(s)
    if(src.rfind("http://",0)==0 || src.rfind("https://",0)==0){
        int64_t aid = findAttachmentByExternalPath(src);
        if(aid == -1) aid = addAttachmentFromURL(src);
        if(aid != -1){ auto meta = getAttachmentMeta(aid); if(meta.size>0){
                    if(!modelViewer) modelViewer = std::make_unique<ModelViewer>();
                    auto mvPtr = modelViewer.get();
                    statusMessage = "Loading model..."; statusTime = ImGui::GetTime();
                    std::thread([this, aid, mvPtr, metaName = meta.name, src](){
                        auto data = getAttachmentData(aid);
                        if(!data.empty()){
                            auto dataPtr = std::make_shared<std::vector<uint8_t>>(std::move(data));
                            mvPtr->loadFromMemoryAsync(*dataPtr, metaName);
                            this->enqueueMainThreadTask([this](){ this->showModelViewer = true; });
                            return;
                        } else {
                            this->asyncFetchAndStoreAttachment(aid, src);
                        }
                    }).detach();
                    return;
                } else { asyncFetchAndStoreAttachment(aid, src); statusMessage = "Fetching model..."; statusTime = ImGui::GetTime(); return; } }
        statusMessage = "Failed to prepare remote model"; statusTime = ImGui::GetTime(); return;
    }

    // file path
    std::string path = src;
    if(src.rfind("file://",0)==0) path = src.substr(7);
    try{
        if(std::filesystem::exists(path) && std::filesystem::is_regular_file(path)){
            if(!modelViewer) modelViewer = std::make_unique<ModelViewer>();
            auto mvPtr = modelViewer.get();
            statusMessage = "Loading model..."; statusTime = ImGui::GetTime();
            std::thread([this, path, mvPtr](){
                try{ std::ifstream in(path, std::ios::binary); if(in){ auto d = std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()); if(!d.empty()){ auto dataPtr = std::make_shared<std::vector<uint8_t>>(std::move(d)); mvPtr->loadFromMemoryAsync(*dataPtr, path); this->enqueueMainThreadTask([this](){ this->showModelViewer = true; }); return; } } } catch(...){ }
                this->enqueueMainThreadTask([this](){ this->statusMessage = "Unsupported model source"; this->statusTime = ImGui::GetTime(); });
            }).detach();
            return;
        }
    } catch(...){}
    statusMessage = "Unsupported model source"; statusTime = ImGui::GetTime();
}

// Find an attachment by ExternalPath (URL or path), returns -1 if not found
// This routine attempts multiple normalizations: exact match, strip "vault://" prefix, and basename-like matches
inline int64_t Vault::findAttachmentByExternalPath(const std::string& path){
    if(!dbConnection) return -1;
    int64_t out = -1;
    // Exact match first
    const char* exactSQL = "SELECT ID FROM Attachments WHERE ExternalPath = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(dbConnection, exactSQL, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_text(stmt, 1, path.c_str(), -1, SQLITE_TRANSIENT);
        if(sqlite3_step(stmt) == SQLITE_ROW) out = sqlite3_column_int64(stmt, 0);
    }
    if(stmt) sqlite3_finalize(stmt);
    if(out != -1) return out;

    // Try stripping vault:// prefix
    std::string p = path;
    const std::string vpre = "vault://";
    if(p.rfind(vpre, 0) == 0) p = p.substr(vpre.size());

    // Try exact without prefix and with leading slash
    const char* altSQL = "SELECT ID FROM Attachments WHERE ExternalPath = ? OR ExternalPath = ? OR ExternalPath LIKE '%' || ? LIMIT 1;";
    stmt = nullptr;
    try{
        std::string p_slash = std::string("/") + p;
        std::string basename = std::filesystem::path(p).filename().string();
        if(sqlite3_prepare_v2(dbConnection, altSQL, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_text(stmt, 1, p.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, p_slash.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 3, basename.c_str(), -1, SQLITE_TRANSIENT);
            if(sqlite3_step(stmt) == SQLITE_ROW) out = sqlite3_column_int64(stmt, 0);
        }
    } catch(...){ }
    if(stmt) sqlite3_finalize(stmt);
    if(out != -1) return out;

    // Additional: try sanitized path/basename matches to handle uploaded virtual paths that are normalized
    try{
        // If path contains 'Assets/', sanitize the trailing relative path and try exact match
        std::string assetsKey = "Assets/";
        size_t pos = p.find(assetsKey);
        if(pos != std::string::npos){
            std::string rel = p.substr(pos + assetsKey.size());
            std::string srel = sanitizeExternalPath(rel);
            std::string candidate = std::string("vault://Assets/") + srel;
            const char* ssql = "SELECT ID FROM Attachments WHERE ExternalPath = ? LIMIT 1;";
            sqlite3_stmt* sstmt = nullptr;
            if(sqlite3_prepare_v2(dbConnection, ssql, -1, &sstmt, nullptr) == SQLITE_OK){
                sqlite3_bind_text(sstmt, 1, candidate.c_str(), -1, SQLITE_TRANSIENT);
                if(sqlite3_step(sstmt) == SQLITE_ROW) out = sqlite3_column_int64(sstmt, 0);
            }
            if(sstmt) sqlite3_finalize(sstmt);
            if(out != -1) return out;
        }
        // Also try matching sanitized basename anywhere in ExternalPath
        std::string base = std::filesystem::path(p).filename().string();
        std::string sbase = sanitizeExternalName(base);
        if(!sbase.empty()){
            const char* likeSQL = "SELECT ID FROM Attachments WHERE ExternalPath LIKE '%' || ? LIMIT 1;";
            sqlite3_stmt* lstmt = nullptr;
            if(sqlite3_prepare_v2(dbConnection, likeSQL, -1, &lstmt, nullptr) == SQLITE_OK){
                sqlite3_bind_text(lstmt, 1, sbase.c_str(), -1, SQLITE_TRANSIENT);
                if(sqlite3_step(lstmt) == SQLITE_ROW) out = sqlite3_column_int64(lstmt, 0);
            }
            if(lstmt) sqlite3_finalize(lstmt);
            if(out != -1) return out;
        }
    } catch(...){ }
    PLOGV << "vault:findAttachment -> " << out;

    return out;
}

// Get or create an inline ModelViewer for a markdown src
inline ModelViewer* Vault::getOrCreateModelViewerForSrc(const std::string& src){
    PLOGI << "vault:getOrCreateModelViewerForSrc src='" << src << "'";
    auto it = modelViewerCache.find(src);
    if(it != modelViewerCache.end()) return it->second.get();

    auto mv = std::make_unique<ModelViewer>();
    // texture resolver tries relative -> exact -> basename against attachments
    mv->setTextureLoader([this, src](const std::string& path)->std::vector<uint8_t>{
        std::vector<uint8_t> empty;
        try{
            int64_t aid = findAttachmentByExternalPath(src);
            if(aid != -1){ auto meta = getAttachmentMeta(aid); if(!meta.externalPath.empty()){
                try{ auto baseDir = std::filesystem::path(meta.externalPath).parent_path(); if(!baseDir.empty()){ auto cand = (baseDir / path).string(); int64_t aid2 = findAttachmentByExternalPath(cand); if(aid2 != -1) return getAttachmentData(aid2); } }catch(...){}
            } }
            int64_t aid3 = findAttachmentByExternalPath(path);
            if(aid3 != -1) return getAttachmentData(aid3);
            std::string base = std::filesystem::path(path).filename().string(); if(!base.empty()){ int64_t aid4 = findAttachmentByExternalPath(base); if(aid4 != -1) return getAttachmentData(aid4); }
        } catch(...){ }
        return empty;
    });

    // Try to pre-load model if already cached
    // First, try resolving any ExternalPath entries (covers vault://Assets/ and other vault URLs)
    try{
        int64_t aid_pre = findAttachmentByExternalPath(src);
        if(aid_pre != -1){
            auto meta = getAttachmentMeta(aid_pre);
            if(meta.size>0){
                // Fetch data off the UI thread and enqueue the GL upload on the main thread
                statusMessage = "Loading model..."; statusTime = ImGui::GetTime();
                std::thread([this, aid_pre, mvPtr = mv.get(), metaName = meta.name](){
                    std::shared_ptr<std::vector<uint8_t>> dataPtr;
                    {
                        std::lock_guard<std::mutex> l(dbMutex);
                        auto d = getAttachmentData(aid_pre);
                        if(!d.empty()) dataPtr = std::make_shared<std::vector<uint8_t>>(std::move(d));
                    }
                    if(dataPtr && !dataPtr->empty()){
                        mvPtr->loadFromMemoryAsync(*dataPtr, metaName);
                        PLOGI << "vault:async parse queued model aid=" << aid_pre;
                    }
                }).detach();
            } else { if(!meta.externalPath.empty()) asyncFetchAndStoreAttachment(meta.id, meta.externalPath); }
        }
    } catch(...){}
    const std::string prefix = "vault://attachment/";
    if(src.rfind(prefix,0) == 0){
        try{ int64_t aid = std::stoll(src.substr(prefix.size())); auto meta = getAttachmentMeta(aid); if(meta.size>0){
                statusMessage = "Loading model..."; statusTime = ImGui::GetTime();
                std::thread([this, aid, mvPtr = mv.get(), metaName = meta.name](){
                    std::shared_ptr<std::vector<uint8_t>> dataPtr;
                    {
                        std::lock_guard<std::mutex> l(dbMutex);
                        auto d = getAttachmentData(aid);
                        if(!d.empty()) dataPtr = std::make_shared<std::vector<uint8_t>>(std::move(d));
                    }
                    if(dataPtr && !dataPtr->empty()){
                        mvPtr->loadFromMemoryAsync(*dataPtr, metaName);
                        PLOGI << "vault:async parse queued model aid=" << aid;
                    }
                }).detach();
            } else { if(!meta.externalPath.empty()) asyncFetchAndStoreAttachment(meta.id, meta.externalPath); } } catch(...){}
    } else if(src.rfind("http://",0)==0 || src.rfind("https://",0)==0){
        int64_t aid = findAttachmentByExternalPath(src);
        if(aid != -1){ auto meta = getAttachmentMeta(aid); if(meta.size>0){
                statusMessage = "Loading model..."; statusTime = ImGui::GetTime();
                std::thread([this, aid, mvPtr = mv.get(), metaName = meta.name](){
                    std::shared_ptr<std::vector<uint8_t>> dataPtr;
                    {
                        std::lock_guard<std::mutex> l(dbMutex);
                        auto d = getAttachmentData(aid);
                        if(!d.empty()) dataPtr = std::make_shared<std::vector<uint8_t>>(std::move(d));
                    }
                    if(dataPtr && !dataPtr->empty()){
                        mvPtr->loadFromMemoryAsync(*dataPtr, metaName);
                        PLOGI << "vault:async parse queued model aid=" << aid;
                    }
                }).detach();
            } else { asyncFetchAndStoreAttachment(aid, src); } } else { addAttachmentFromURL(src); }
    } else {
        std::string path = src; if(src.rfind("file://",0)==0) path = src.substr(7);
        try{ if(std::filesystem::exists(path) && std::filesystem::is_regular_file(path)){
            // Read file off the UI thread and enqueue load on main thread
            statusMessage = "Loading model..."; statusTime = ImGui::GetTime();
            std::thread([this, path, mvPtr = mv.get()](){
                std::shared_ptr<std::vector<uint8_t>> dataPtr;
                try{ std::ifstream in(path, std::ios::binary); if(in){ auto d = std::vector<uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()); if(!d.empty()) dataPtr = std::make_shared<std::vector<uint8_t>>(std::move(d)); } } catch(...){}
                if(dataPtr && !dataPtr->empty()){
                    mvPtr->loadFromMemoryAsync(*dataPtr, path);
                    PLOGI << "vault:async parse queued local file '" << path << "'";
                }
            }).detach();
        } } catch(...){ }
    }

    ModelViewer* out = mv.get(); modelViewerCache[src] = std::move(mv); return out;
}

// Add an attachment record referencing the external URL (creates placeholder and spawns async fetch), returns attachment id
inline int64_t Vault::addAttachmentFromURL(const std::string& url, const std::string& name){
    if(!dbConnection) return -1;
    // check existing
    int64_t existing = findAttachmentByExternalPath(url);
    if(existing != -1) return existing;

    std::string filename = name;
    if(filename.empty()){
        try{
            auto pos = url.find_last_of('/');
            if(pos != std::string::npos) filename = url.substr(pos+1);
            auto q = filename.find_first_of('?'); if(q!=std::string::npos) filename = filename.substr(0,q);
        } catch(...){}
        if(filename.empty()) filename = "remote";
    }

    const char* insSQL = "INSERT INTO Attachments (ItemID, Name, MimeType, Data, ExternalPath, Size) VALUES (NULL, ?, ?, NULL, ?, 0);";
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(dbConnection, insSQL, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_text(stmt, 1, filename.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, "", -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, url.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
    }
    if(stmt) sqlite3_finalize(stmt);
    int64_t id = sqlite3_last_insert_rowid(dbConnection);
    if(id != -1){
        // async fetch
        asyncFetchAndStoreAttachment(id, url);
    }
    return id;
}

// Synchronous fetch (blocking) that updates the attachment data and mime type
inline bool Vault::fetchAttachmentNow(int64_t attachmentID, const std::string& url){
    if(!dbConnection) return false;
    // Curl fetch
    std::vector<uint8_t> out;
    std::string contentType;
    CURL* curl = curl_easy_init();
    if(!curl) return false;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "LoreBook/1.0");
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, +[](char* ptr, size_t size, size_t nmemb, void* userdata)->size_t{
        auto* vec = static_cast<std::vector<uint8_t>*>(userdata);
        size_t n = size * nmemb;
        vec->insert(vec->end(), ptr, ptr + n);
        return n;
    });
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &out);
    // header callback to capture content-type
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, +[](char* buffer, size_t size, size_t nitems, void* userdata)->size_t{
        std::string s(buffer, size * nitems);
        auto* ct = static_cast<std::string*>(userdata);
        std::string key = "content-type:";
        std::string low = s; std::transform(low.begin(), low.end(), low.begin(), ::tolower);
        auto pos = low.find(key);
        if(pos != std::string::npos){
            auto val = s.substr(pos + key.size());
            // trim
            while(!val.empty() && (val.front()==' ' || val.front()=='\t' || val.front()=='\r' || val.front()=='\n')) val.erase(val.begin());
            while(!val.empty() && (val.back()=='\r' || val.back()=='\n')) val.pop_back();
            *ct = val;
        }
        return size * nitems;
    });
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &contentType);
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    if(res != CURLE_OK) return false;

    // Determine mime
    std::string mime = contentType;
    if(mime.empty()){
        // fallback based on extension
        try{ auto p = std::filesystem::path(url); auto ext = p.extension().string(); std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower); if(!ext.empty()) mime = (ext==".png"||ext==".jpg"||ext==".jpeg"? std::string("image/") + (ext.size()>1? ext.substr(1):"") : std::string("application/octet-stream")); }catch(...){}
    }

    // Update DB with blob and mime and size
    {
        std::lock_guard<std::mutex> l(dbMutex);
        const char* upd = "UPDATE Attachments SET Data = ?, Size = ?, MimeType = ? WHERE ID = ?;";
        sqlite3_stmt* stmt = nullptr;
        if(sqlite3_prepare_v2(dbConnection, upd, -1, &stmt, nullptr) == SQLITE_OK){
            if(!out.empty()) sqlite3_bind_blob(stmt, 1, reinterpret_cast<const void*>(out.data()), static_cast<int>(out.size()), SQLITE_TRANSIENT);
            else sqlite3_bind_null(stmt, 1);
            sqlite3_bind_int64(stmt, 2, static_cast<int64_t>(out.size()));
            sqlite3_bind_text(stmt, 3, mime.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(stmt, 4, attachmentID);
            sqlite3_step(stmt);
        }
        if(stmt) sqlite3_finalize(stmt);
    }
    return true;
}

// Start async fetch to populate an existing attachment
inline void Vault::asyncFetchAndStoreAttachment(int64_t attachmentID, const std::string& url){
    // detach thread, do not block UI
    std::thread([this, attachmentID, url](){
        bool ok = fetchAttachmentNow(attachmentID, url);
        if(ok){
            statusMessage = "Attachment fetched"; statusTime = ImGui::GetTime();
            // enqueue reload for any inline model viewers that reference this external path or attachment id
            std::lock_guard<std::mutex> l(dbMutex);
            try{
                auto meta = getAttachmentMeta(attachmentID);
                if(!meta.externalPath.empty()) pendingViewerReloads.push_back(meta.externalPath);
                // also add vault://attachment/<id> key so viewers keyed by attachment uri reload as well
                pendingViewerReloads.push_back(std::string("vault://attachment/") + std::to_string(attachmentID));
            } catch(...){}
        }
        else { statusMessage = "Failed to fetch attachment"; statusTime = ImGui::GetTime(); }
    }).detach();
}

// Sanitize a filename into a safe ExternalPath segment (lowercase, alnum, dash, underscore, dot allowed)
inline std::string Vault::sanitizeExternalName(const std::string& name){
    std::string n = std::filesystem::path(name).filename().string();
    std::string out;
    for(char c : n){
        unsigned char uc = static_cast<unsigned char>(c);
        if(std::isalnum(uc) || c=='-' || c=='_' || c=='.') out += static_cast<char>(std::tolower(uc));
        else out += '-';
    }
    // collapse multiple dashes
    std::string res;
    bool lastDash = false;
    for(char c : out){
        if(c == '-'){
            if(!lastDash) res.push_back(c);
            lastDash = true;
        } else {
            res.push_back(c);
            lastDash = false;
        }
    }
    // trim dashes
    while(!res.empty() && res.front() == '-') res.erase(res.begin());
    while(!res.empty() && res.back() == '-') res.pop_back();
    if(res.empty()) res = "asset";
    return res;
}

// Sanitize a virtual path like "sub/folder/file.png" into safe components joined by '/'
inline std::string Vault::sanitizeExternalPath(const std::string& path){
    std::string p = path;
    // normalize backslashes to slashes
    std::replace(p.begin(), p.end(), '\\', '/');
    std::vector<std::string> comps;
    std::string cur;
    for(char c : p){
        if(c == '/'){
            if(!cur.empty()){
                if(cur == "." || cur == ".."){ cur.clear(); continue; }
                std::string s = sanitizeExternalName(cur);
                if(s.empty()) s = "asset";
                comps.push_back(s);
                cur.clear();
            }
        } else cur.push_back(c);
    }
    if(!cur.empty()){
        if(cur != "." && cur != ".."){ std::string s = sanitizeExternalName(cur); if(s.empty()) s = "asset"; comps.push_back(s); }
    }
    if(comps.empty()) return std::string("asset");
    std::string out;
    for(size_t i=0;i<comps.size();++i){ if(i) out.push_back('/'); out += comps[i]; }
    return out;
}

// Add an asset file into the vault Assets namespace; returns attachment id or -1
inline int64_t Vault::addAssetFromFile(const std::string& filepath, const std::string& desiredName){
    if(!dbConnection) return -1;
    std::ifstream in(filepath, std::ios::binary);
    if(!in) return -1;
    std::vector<uint8_t> data((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());

    // desiredName may include virtual path components (e.g. "folder/file.png")
    std::string name = desiredName.empty() ? std::filesystem::path(filepath).filename().string() : desiredName;
    // sanitize virtual path and use that for ExternalPath. Determine extension from final component
    std::string safeRel = sanitizeExternalPath(name);
    std::string finalComponent = std::filesystem::path(safeRel).filename().string();
    std::string ext = std::filesystem::path(finalComponent).extension().string(); std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    std::string candidate = std::string("vault://Assets/") + safeRel;

    // Exact-path collision check: if an attachment already exists at this exact virtual path, prompt the user
    int64_t existing = findAttachmentByExternalPath(candidate);
    if(existing != -1){
        // schedule user decision via modal
        overwritePendingLocalFile = filepath;
        overwriteTargetExternalPath = candidate;
        overwriteExistingAttachmentID = existing;
        showOverwriteConfirmModal = true;
        statusMessage = "Asset exists: confirm action"; statusTime = ImGui::GetTime();
        return -1;
    }

    std::string mime = "application/octet-stream";
    if(ext==".png" || ext==".jpg" || ext==".jpeg" || ext==".bmp" || ext==".gif") mime = std::string("image/") + (ext.size()>1? ext.substr(1) : "");

    int64_t aid = addAttachment(-1, std::filesystem::path(finalComponent).string(), mime, data, candidate);
    if(aid != -1){ statusMessage = "Asset added"; statusTime = ImGui::GetTime(); }
    return aid;
}  

// ------------------------- User & Auth implementations -------------------------
inline bool Vault::hasUsers() const{
    if(dbBackend && dbBackend->isOpen()){
        std::string err;
        auto stmt = dbBackend->prepare("SELECT 1 FROM Users LIMIT 1;", &err);
        if(!stmt){ PLOGE << "hasUsers prepare failed: " << err; return false; }
        auto rs = stmt->executeQuery();
        return rs && rs->next();
    }

    if(!dbConnection) return false;
    const char* sql = "SELECT 1 FROM Users LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    bool any = false;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        if(sqlite3_step(stmt) == SQLITE_ROW) any = true;
    }
    if(stmt) sqlite3_finalize(stmt);
    return any;
}

inline int64_t Vault::createUser(const std::string& username, const std::string& displayName, const std::string& passwordPlain, bool isAdmin){
    // generate salt and derive hash
    const int iterations = 100000;
    std::string saltB64 = CryptoHelpers::generateSaltBase64(16);
    std::string hashB64 = CryptoHelpers::derivePBKDF2_Base64(passwordPlain, saltB64, iterations);

    if(dbBackend && dbBackend->isOpen()){
        std::string err;
        auto stmt = dbBackend->prepare("INSERT INTO Users (Username, DisplayName, PasswordHash, Salt, Iterations, IsAdmin) VALUES (?, ?, ?, ?, ?, ?);", &err);
        if(!stmt){ PLOGE << "createUser prepare failed: " << err; return -1; }
        stmt->bindString(1, username);
        stmt->bindString(2, displayName);
        stmt->bindString(3, hashB64);
        stmt->bindString(4, saltB64);
        stmt->bindInt(5, iterations);
        stmt->bindInt(6, isAdmin ? 1 : 0);
        if(!stmt->execute()) return -1;
        return dbBackend->lastInsertId();
    }

    if(!dbConnection) return -1;
    const char* insSQL = "INSERT INTO Users (Username, DisplayName, PasswordHash, Salt, Iterations, IsAdmin) VALUES (?, ?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(dbConnection, insSQL, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, displayName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, hashB64.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 4, saltB64.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 5, iterations);
        sqlite3_bind_int(stmt, 6, isAdmin ? 1 : 0);
        if(sqlite3_step(stmt) != SQLITE_DONE){ sqlite3_finalize(stmt); return -1; }
    }
    if(stmt) sqlite3_finalize(stmt);
    return sqlite3_last_insert_rowid(dbConnection);
}

inline int64_t Vault::authenticateUser(const std::string& username, const std::string& passwordPlain){
    // Trim username input to avoid accidental whitespace errors
    std::string uname = username;
    while(!uname.empty() && std::isspace((unsigned char)uname.front())) uname.erase(uname.begin());
    while(!uname.empty() && std::isspace((unsigned char)uname.back())) uname.pop_back();

    if(dbBackend && dbBackend->isOpen()){
        std::string err;
        auto stmt = dbBackend->prepare("SELECT ID, PasswordHash, Salt, Iterations, IsAdmin, DisplayName FROM Users WHERE Username = ? LIMIT 1;", &err);
        if(!stmt){ PLOGE << "authenticateUser prepare failed: " << err; return -1; }
        stmt->bindString(1, uname);
        auto rs = stmt->executeQuery();
        if(rs && rs->next()){
            int64_t id = rs->getInt64(0);
            std::string phs = rs->getString(1);
            std::string salts = rs->getString(2);
            int iters = rs->getInt(3);
            PLOGI << "authenticateUser: found user id=" << id << " username='" << uname << "' iters=" << iters << " ph_len=" << phs.size() << " salt_len=" << salts.size();
            if(phs.empty() || salts.empty()){
                PLOGW << "authenticateUser: missing hash/salt for user '" << uname << "' id=" << id;
                return -1;
            }
            if(CryptoHelpers::pbkdf2_verify(passwordPlain, salts, iters, phs)) return id;
            PLOGW << "authenticateUser: password verification failed for user '" << uname << "' id=" << id << " iters=" << iters;
        } else {
            PLOGW << "authenticateUser: user not found: '" << uname << "'";
        }
        return -1;
    }

    if(!dbConnection) return -1;
    const char* sql = "SELECT ID, PasswordHash, Salt, Iterations, IsAdmin, DisplayName FROM Users WHERE Username = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        // Trim username before lookup
        std::string uname = username;
        while(!uname.empty() && std::isspace((unsigned char)uname.front())) uname.erase(uname.begin());
        while(!uname.empty() && std::isspace((unsigned char)uname.back())) uname.pop_back();
        sqlite3_bind_text(stmt, 1, uname.c_str(), -1, SQLITE_TRANSIENT);
        if(sqlite3_step(stmt) == SQLITE_ROW){
            int64_t id = sqlite3_column_int64(stmt, 0);
            const unsigned char* ph = sqlite3_column_text(stmt, 1);
            const unsigned char* salt = sqlite3_column_text(stmt, 2);
            int iters = sqlite3_column_int(stmt, 3);
            const unsigned char* dname = sqlite3_column_text(stmt, 5);
            std::string phs = ph ? reinterpret_cast<const char*>(ph) : std::string();
            std::string salts = salt ? reinterpret_cast<const char*>(salt) : std::string();
            PLOGI << "authenticateUser (sqlite): found user id=" << id << " username='" << uname << "' iters=" << iters << " ph_len=" << phs.size() << " salt_len=" << salts.size();
            if(phs.empty() || salts.empty()){
                PLOGW << "authenticateUser: missing hash/salt for user '" << uname << "' id=" << id;
                sqlite3_finalize(stmt);
                return -1;
            }
            if(CryptoHelpers::pbkdf2_verify(passwordPlain, salts, iters, phs)){
                sqlite3_finalize(stmt);
                return id;
            }
            PLOGW << "authenticateUser: password verification failed for user '" << uname << "' id=" << id << " iters=" << iters;
        }
    }
    if(stmt) sqlite3_finalize(stmt);
    return -1;
}

inline int64_t Vault::getCurrentUserID() const { return currentUserID; }
inline std::string Vault::getCurrentUserDisplayName() const { return currentUserDisplayName; }

inline void Vault::setCurrentUser(int64_t userID){
    if(userID <= 0){ currentUserID = -1; currentUserDisplayName.clear(); return; }
    if(dbBackend && dbBackend->isOpen()){
        std::string err;
        auto stmt = dbBackend->prepare("SELECT DisplayName FROM Users WHERE ID = ? LIMIT 1;", &err);
        if(!stmt){ PLOGE << "setCurrentUser prepare failed: " << err; return; }
        stmt->bindInt(1, userID);
        auto rs = stmt->executeQuery();
        if(rs && rs->next()){
            currentUserDisplayName = rs->getString(0);
            currentUserID = userID;
        }
        return;
    }

    const char* sql = "SELECT DisplayName FROM Users WHERE ID = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(stmt, 1, userID);
        if(sqlite3_step(stmt) == SQLITE_ROW){
            const unsigned char* d = sqlite3_column_text(stmt, 0);
            currentUserDisplayName = d ? reinterpret_cast<const char*>(d) : std::string();
            currentUserID = userID;
        }
    }
    if(stmt) sqlite3_finalize(stmt);
}

inline void Vault::clearCurrentUser(){ currentUserID = -1; currentUserDisplayName.clear(); }

inline std::vector<Vault::User> Vault::listUsers() const{
    std::vector<User> out;
    if(dbBackend && dbBackend->isOpen()){
        std::string err;
        auto stmt = dbBackend->prepare("SELECT ID, Username, DisplayName, IsAdmin FROM Users ORDER BY Username;", &err);
        if(!stmt){ PLOGE << "listUsers prepare failed: " << err; return out; }
        auto rs = stmt->executeQuery();
        while(rs && rs->next()){
            User u;
            u.id = rs->getInt64(0);
            u.username = rs->getString(1);
            u.displayName = rs->getString(2);
            u.isAdmin = rs->getInt(3) != 0;
            out.push_back(u);
        }
        return out;
    }

    if(!dbConnection) return out;
    const char* sql = "SELECT ID, Username, DisplayName, IsAdmin FROM Users ORDER BY Username;";
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        while(sqlite3_step(stmt) == SQLITE_ROW){
            User u;
            u.id = sqlite3_column_int64(stmt, 0);
            const unsigned char* un = sqlite3_column_text(stmt, 1);
            const unsigned char* dn = sqlite3_column_text(stmt, 2);
            u.username = un ? reinterpret_cast<const char*>(un) : std::string();
            u.displayName = dn ? reinterpret_cast<const char*>(dn) : std::string();
            u.isAdmin = sqlite3_column_int(stmt, 3) != 0;
            out.push_back(u);
        }
    }
    if(stmt) sqlite3_finalize(stmt);
    return out;
}

inline bool Vault::isUserAdmin(int64_t userID) const{
    if(userID <= 0) return false;
    if(dbBackend && dbBackend->isOpen()){
        std::string err;
        auto stmt = dbBackend->prepare("SELECT IsAdmin FROM Users WHERE ID = ? LIMIT 1;", &err);
        if(!stmt){ PLOGE << "isUserAdmin prepare failed: " << err; return false; }
        stmt->bindInt(1, userID);
        auto rs = stmt->executeQuery();
        if(rs && rs->next()) return rs->getInt(0) != 0;
        return false;
    }

    if(!dbConnection || userID <= 0) return false;
    const char* sql = "SELECT IsAdmin FROM Users WHERE ID = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    bool isAdmin = false;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(stmt, 1, userID);
        if(sqlite3_step(stmt) == SQLITE_ROW) isAdmin = (sqlite3_column_int(stmt,0) != 0);
    }
    if(stmt) sqlite3_finalize(stmt);
    return isAdmin;
}

inline bool Vault::updateUserDisplayName(int64_t userID, const std::string& newDisplayName){
    if(!dbConnection) return false;
    const char* sql = "UPDATE Users SET DisplayName = ? WHERE ID = ?;";
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_text(stmt, 1, newDisplayName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(stmt, 2, userID);
        if(sqlite3_step(stmt) != SQLITE_DONE){ sqlite3_finalize(stmt); return false; }
    }
    if(stmt) sqlite3_finalize(stmt);
    return true;
}

inline bool Vault::changeUserPassword(int64_t userID, const std::string& newPasswordPlain){
    if(!dbConnection) return false;
    const int iterations = 100000;
    std::string saltB64 = CryptoHelpers::generateSaltBase64(16);
    std::string hashB64 = CryptoHelpers::derivePBKDF2_Base64(newPasswordPlain, saltB64, iterations);
    const char* sql = "UPDATE Users SET PasswordHash = ?, Salt = ?, Iterations = ? WHERE ID = ?;";
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_text(stmt, 1, hashB64.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, saltB64.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 3, iterations);
        sqlite3_bind_int64(stmt, 4, userID);
        if(sqlite3_step(stmt) != SQLITE_DONE){ sqlite3_finalize(stmt); return false; }
    }
    if(stmt) sqlite3_finalize(stmt);
    return true;
}

inline bool Vault::setUserAdminFlag(int64_t userID, bool isAdmin){
    if(!dbConnection) return false;
    // Prevent downgrading the currently logged-in admin to avoid lockout
    if(userID == currentUserID && !isAdmin) return false;
    // If clearing admin, ensure at least one other admin will remain
    if(!isAdmin){
        const char* countSql = "SELECT COUNT(1) FROM Users WHERE IsAdmin = 1;";
        sqlite3_stmt* stmt = nullptr;
        int cnt = 0;
        if(sqlite3_prepare_v2(dbConnection, countSql, -1, &stmt, nullptr) == SQLITE_OK){
            if(sqlite3_step(stmt) == SQLITE_ROW) cnt = sqlite3_column_int(stmt,0);
        }
        if(stmt) sqlite3_finalize(stmt);
        if(cnt <= 1) return false;
    }
    const char* sql = "UPDATE Users SET IsAdmin = ? WHERE ID = ?;";
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_int(stmt, 1, isAdmin ? 1 : 0);
        sqlite3_bind_int64(stmt, 2, userID);
        if(sqlite3_step(stmt) != SQLITE_DONE){ sqlite3_finalize(stmt); return false; }
    }
    if(stmt) sqlite3_finalize(stmt);
    return true;
}

inline bool Vault::deleteUser(int64_t userID){
    if(!dbConnection) return false;
    // Prevent deleting the currently logged-in user
    if(userID == currentUserID) return false;
    // If target is admin ensure another admin remains
    sqlite3_stmt* stmt = nullptr;
    const char* adminCheck = "SELECT IsAdmin FROM Users WHERE ID = ? LIMIT 1;";
    bool targetIsAdmin = false;
    if(sqlite3_prepare_v2(dbConnection, adminCheck, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(stmt,1,userID);
        if(sqlite3_step(stmt) == SQLITE_ROW) targetIsAdmin = sqlite3_column_int(stmt,0) != 0;
    }
    if(stmt) sqlite3_finalize(stmt);
    if(targetIsAdmin){
        const char* countSql = "SELECT COUNT(1) FROM Users WHERE IsAdmin = 1;";
        sqlite3_stmt* cstmt = nullptr;
        int cnt = 0;
        if(sqlite3_prepare_v2(dbConnection, countSql, -1, &cstmt, nullptr) == SQLITE_OK){
            if(sqlite3_step(cstmt) == SQLITE_ROW) cnt = sqlite3_column_int(cstmt,0);
        }
        if(cstmt) sqlite3_finalize(cstmt);
        if(cnt <= 1) return false;
    }
    // Remove any permissions references first
    const char* deletePerms = "DELETE FROM ItemPermissions WHERE UserID = ?;";
    if(sqlite3_prepare_v2(dbConnection, deletePerms, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(stmt, 1, userID);
        sqlite3_step(stmt);
    }
    if(stmt) sqlite3_finalize(stmt);
    const char* deleteUserSql = "DELETE FROM Users WHERE ID = ?;";
    if(sqlite3_prepare_v2(dbConnection, deleteUserSql, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(stmt, 1, userID);
        if(sqlite3_step(stmt) != SQLITE_DONE){ sqlite3_finalize(stmt); return false; }
    }
    if(stmt) sqlite3_finalize(stmt);
    return true;
}

inline bool Vault::setItemPermission(int64_t itemID, int64_t userID, int level){
    if(!dbConnection) return false;
    // If target user is admin, always ensure EDIT level (prevent downgrading own/other admin permissions)
    sqlite3_stmt* astmt = nullptr;
    const char* aSql = "SELECT IsAdmin FROM Users WHERE ID = ? LIMIT 1;";
    bool targetIsAdmin = false;
    if(sqlite3_prepare_v2(dbConnection, aSql, -1, &astmt, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(astmt, 1, userID);
        if(sqlite3_step(astmt) == SQLITE_ROW) targetIsAdmin = (sqlite3_column_int(astmt,0) != 0);
    }
    if(astmt) sqlite3_finalize(astmt);
    if(targetIsAdmin && level < 2) level = 2; // force EDIT for admins

    const char* sql = "INSERT OR REPLACE INTO ItemPermissions (ItemID, UserID, Level) VALUES (?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(stmt, 1, itemID);
        sqlite3_bind_int64(stmt, 2, userID);
        sqlite3_bind_int(stmt, 3, level);
        if(sqlite3_step(stmt) != SQLITE_DONE){ sqlite3_finalize(stmt); return false; }
    }
    if(stmt) sqlite3_finalize(stmt);
    return true;
}

inline bool Vault::removeItemPermission(int64_t itemID, int64_t userID){
    if(!dbConnection) return false;
    // Prevent removal of permissions for admin users to avoid accidental lockout
    sqlite3_stmt* astmt = nullptr;
    const char* aSql = "SELECT IsAdmin FROM Users WHERE ID = ? LIMIT 1;";
    bool targetIsAdmin = false;
    if(sqlite3_prepare_v2(dbConnection, aSql, -1, &astmt, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(astmt, 1, userID);
        if(sqlite3_step(astmt) == SQLITE_ROW) targetIsAdmin = (sqlite3_column_int(astmt,0) != 0);
    }
    if(astmt) sqlite3_finalize(astmt);
    if(targetIsAdmin) return false; // disallow removal

    const char* sql = "DELETE FROM ItemPermissions WHERE ItemID = ? AND UserID = ?;";
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(stmt, 1, itemID);
        sqlite3_bind_int64(stmt, 2, userID);
        if(sqlite3_step(stmt) != SQLITE_DONE){ sqlite3_finalize(stmt); return false; }
    }
    if(stmt) sqlite3_finalize(stmt);
    return true;
}

inline std::vector<Vault::ItemPermission> Vault::listItemPermissions(int64_t itemID) const{
    std::vector<ItemPermission> out;
    if(!dbConnection) return out;
    const char* sql = "SELECT ID, ItemID, UserID, Level, CreatedAt FROM ItemPermissions WHERE ItemID = ? ORDER BY UserID;";
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(stmt, 1, itemID);
        while(sqlite3_step(stmt) == SQLITE_ROW){
            ItemPermission p;
            p.id = sqlite3_column_int64(stmt, 0);
            p.itemID = sqlite3_column_int64(stmt, 1);
            p.userID = sqlite3_column_int64(stmt, 2);
            p.level = sqlite3_column_int(stmt, 3);
            p.createdAt = sqlite3_column_int64(stmt, 4);
            out.push_back(p);
        }
    }
    if(stmt) sqlite3_finalize(stmt);
    return out;
}

inline bool Vault::isItemVisibleToUser(int64_t itemID, int64_t userID) const{
    // Default: if no DB or invalid user id treat as visible (backwards compatible)
    if(!dbConnection || userID <= 0) return true;
    // Admin override
    const char* adminSql = "SELECT IsAdmin FROM Users WHERE ID = ? LIMIT 1;";
    sqlite3_stmt* s = nullptr;
    if(sqlite3_prepare_v2(dbConnection, adminSql, -1, &s, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(s, 1, userID);
        if(sqlite3_step(s) == SQLITE_ROW){ if(sqlite3_column_int(s,0) != 0){ if(s) sqlite3_finalize(s); return true; } }
    }
    if(s) sqlite3_finalize(s);
    // Check explicit permission
    const char* sql = "SELECT Level FROM ItemPermissions WHERE ItemID = ? AND UserID = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(stmt, 1, itemID);
        sqlite3_bind_int64(stmt, 2, userID);
        if(sqlite3_step(stmt) == SQLITE_ROW){
            int lvl = sqlite3_column_int(stmt, 0);
            if(stmt) sqlite3_finalize(stmt);
            return lvl >= 1; // VIEW or EDIT
        }
    }
    if(stmt) sqlite3_finalize(stmt);
    // No explicit permission -> default visible
    return true;
}

inline bool Vault::isItemEditableByUser(int64_t itemID, int64_t userID) const{
    if(!dbConnection || userID <= 0) return true; // no auth = editable
    // Admin override
    const char* adminSql = "SELECT IsAdmin FROM Users WHERE ID = ? LIMIT 1;";
    sqlite3_stmt* s = nullptr;
    if(sqlite3_prepare_v2(dbConnection, adminSql, -1, &s, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(s, 1, userID);
        if(sqlite3_step(s) == SQLITE_ROW){ if(sqlite3_column_int(s,0) != 0){ if(s) sqlite3_finalize(s); return true; } }
    }
    if(s) sqlite3_finalize(s);
    const char* sql = "SELECT Level FROM ItemPermissions WHERE ItemID = ? AND UserID = ? LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(stmt, 1, itemID);
        sqlite3_bind_int64(stmt, 2, userID);
        if(sqlite3_step(stmt) == SQLITE_ROW){
            int lvl = sqlite3_column_int(stmt, 0);
            if(stmt) sqlite3_finalize(stmt);
            return lvl >= 2; // EDIT
        }
    }
    if(stmt) sqlite3_finalize(stmt);
    // No explicit permission: default editable only for no-auth; otherwise not editable
    return false;
}

inline std::vector<std::pair<int64_t,std::string>> Vault::getAllItemsForUser(int64_t userID){
    auto all = getAllItems();
    if(userID <= 0) return all;
    std::vector<std::pair<int64_t,std::string>> out;
    for(auto &p : all){ if(isItemVisibleToUser(p.first, userID)) out.push_back(p); }
    return out;
}
