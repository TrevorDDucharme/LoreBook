#pragma once
#include <string>
#include <filesystem>
#include <vector>
#include <algorithm>
#include <utility>
#include <imgui.h>
#include <imgui_stdlib.h>
#include "MarkdownText.hpp"
#include <sqlite3.h>
#include <unordered_set>
#include <unordered_map>
#include <sstream>
#include <cctype>
#include <fstream>

class Vault{
    // forward declaration for chat render helper
    friend void RenderVaultChat(Vault* vault);
    int64_t id;
    std::string name;
    int64_t selectedItemID = -1;
    sqlite3* dbConnection = nullptr;

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

    // Per-node child filters (display-only). Keys are node IDs; values are a filter spec.
    struct ChildFilterSpec { std::string mode = "AND"; std::vector<std::string> tags; std::string expr; };
    std::unordered_map<int64_t, ChildFilterSpec> nodeChildFilters;
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
    bool isOpen() const { return dbConnection != nullptr; }

    // Expose raw DB connection for helpers (use carefully)
    sqlite3* getDBPublic() const { return dbConnection; }

    // Public API for GraphView and other UI integrations
    std::vector<std::pair<int64_t,std::string>> getAllItemsPublic(){ return getAllItems(); }
    std::vector<int64_t> getParentsOfPublic(int64_t id){ return getParentsOf(id); }
    void selectItemByID(int64_t id){ selectedItemID = id; }
    int64_t getSelectedItemID() const { return selectedItemID; }

    // Tags helpers for UI
    std::vector<std::string> getTagsOfPublic(int64_t id){ return parseTags(getTagsOf(id)); }
    std::vector<std::string> getAllTagsPublic(){
        std::unordered_set<std::string> s;
        const char* sql = "SELECT Tags FROM VaultItems;";
        sqlite3_stmt* stmt = nullptr;
        if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
            while(sqlite3_step(stmt) == SQLITE_ROW){
                const unsigned char* text = sqlite3_column_text(stmt, 0);
                if(text){
                    auto tags = parseTags(reinterpret_cast<const char*>(text));
                    for(auto &t : tags) s.insert(t);
                }
            }
        }
        if(stmt) sqlite3_finalize(stmt);
        std::vector<std::string> out; out.reserve(s.size());
        for(auto &t : s) out.push_back(t);
        std::sort(out.begin(), out.end());
        return out;
    }

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
    void loadNodeFiltersFromDB(){
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
        // Prefer explicit IsRoot flag (so the root is identified by ID, not by mutable Name)
        const char* findByFlag = "SELECT ID FROM VaultItems WHERE IsRoot = 1 LIMIT 1;";
        sqlite3_stmt* stmt = nullptr;
        int64_t id = -1;
        if(sqlite3_prepare_v2(dbConnection, findByFlag, -1, &stmt, nullptr) == SQLITE_OK){
            if(sqlite3_step(stmt) == SQLITE_ROW){
                id = sqlite3_column_int64(stmt, 0);
            }
        }
        if(stmt) sqlite3_finalize(stmt);
        if(id != -1) return id;

        // Fall back to legacy name-based lookup (for older DBs), and mark it IsRoot for future runs
        const char* findByName = "SELECT ID FROM VaultItems WHERE Name = ? LIMIT 1;";
        if(sqlite3_prepare_v2(dbConnection, findByName, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            if(sqlite3_step(stmt) == SQLITE_ROW){
                id = sqlite3_column_int64(stmt, 0);
            }
        }
        if(stmt) sqlite3_finalize(stmt);
        if(id != -1){
            const char* setFlag = "UPDATE VaultItems SET IsRoot = 1 WHERE ID = ?;";
            if(sqlite3_prepare_v2(dbConnection, setFlag, -1, &stmt, nullptr) == SQLITE_OK){
                sqlite3_bind_int64(stmt, 1, id);
                sqlite3_step(stmt);
            }
            if(stmt) sqlite3_finalize(stmt);
            return id;
        }

        // Another fallback: find an item that has no parents (candidate for root)
        const char* findNoParents = "SELECT ID FROM VaultItems WHERE ID NOT IN (SELECT ChildID FROM VaultItemChildren) LIMIT 1;";
        if(sqlite3_prepare_v2(dbConnection, findNoParents, -1, &stmt, nullptr) == SQLITE_OK){
            if(sqlite3_step(stmt) == SQLITE_ROW){
                id = sqlite3_column_int64(stmt, 0);
            }
        }
        if(stmt) sqlite3_finalize(stmt);
        if(id != -1){
            const char* setFlag = "UPDATE VaultItems SET IsRoot = 1 WHERE ID = ?;";
            if(sqlite3_prepare_v2(dbConnection, setFlag, -1, &stmt, nullptr) == SQLITE_OK){
                sqlite3_bind_int64(stmt, 1, id);
                sqlite3_step(stmt);
            }
            if(stmt) sqlite3_finalize(stmt);
            return id;
        }

        // Otherwise, create a new root record and mark it IsRoot
        const char* insertSQL = "INSERT INTO VaultItems (Name, Content, Tags, IsRoot) VALUES (?, ?, ?, 1);";
        if(sqlite3_prepare_v2(dbConnection, insertSQL, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_text(stmt, 1, name.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(stmt, 2, "", -1, SQLITE_STATIC);
            sqlite3_bind_text(stmt, 3, "", -1, SQLITE_STATIC);
            sqlite3_step(stmt);
        }
        if(stmt) sqlite3_finalize(stmt);
        return sqlite3_last_insert_rowid(dbConnection);
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
                int64_t nid = createItem(nameStr, parent);
                if(nid != -1){
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
                            if(nid != -1) selectedItemID = nid;
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
                        int64_t id = createItemWithContent(title, content, {}, importParentID);
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
                        int64_t id = createItemWithContent(title, content, {}, importParentID);
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
            if(contentDirty && loadedItemID >= 0 && dbConnection){
                const char* updateSQL = "UPDATE VaultItems SET Content = ? WHERE ID = ?;";
                sqlite3_stmt* stmt = nullptr;
                if(sqlite3_prepare_v2(dbConnection, updateSQL, -1, &stmt, nullptr) == SQLITE_OK){
                    sqlite3_bind_text(stmt, 1, currentContent.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(stmt, 2, loadedItemID);
                    sqlite3_step(stmt);
                }
                if(stmt) sqlite3_finalize(stmt);
                contentDirty = false;
                lastSaveTime = ImGui::GetTime();
            }

            const char* sql = "SELECT Name, Content, Tags FROM VaultItems WHERE ID = ?;";
            sqlite3_stmt* stmt = nullptr;
            currentContent.clear();
            currentTitle.clear();
            currentTags.clear();
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
            loadedItemID = selectedItemID;
            contentDirty = false;
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
        if(ImGui::InputText("Title", titleBuf, sizeof(titleBuf))){
            std::string newTitle = std::string(titleBuf);
            if(dbConnection && loadedItemID >= 0){
                const char* updateSQL = "UPDATE VaultItems SET Name = ? WHERE ID = ?;";
                sqlite3_stmt* stmt = nullptr;
                if(sqlite3_prepare_v2(dbConnection, updateSQL, -1, &stmt, nullptr) == SQLITE_OK){
                    sqlite3_bind_text(stmt, 1, newTitle.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(stmt, 2, loadedItemID);
                    sqlite3_step(stmt);
                }
                if(stmt) sqlite3_finalize(stmt);
                currentTitle = newTitle;
                statusMessage = "Title saved";
                statusTime = ImGui::GetTime();
            } else {
                currentTitle = newTitle;
            }
        }

        ImGui::Separator();

        // Editor content
        ImVec2 editorSize = ImVec2(leftWidth - 16, mainHeight - 40);
        if(ImGui::InputTextMultiline("##md_editor", &currentContent, editorSize, ImGuiInputTextFlags_AllowTabInput)){
            // Immediate save on modification
            if(dbConnection && loadedItemID >= 0){
                const char* updateSQL = "UPDATE VaultItems SET Content = ? WHERE ID = ?;";
                sqlite3_stmt* stmt = nullptr;
                if(sqlite3_prepare_v2(dbConnection, updateSQL, -1, &stmt, nullptr) == SQLITE_OK){
                    sqlite3_bind_text(stmt, 1, currentContent.c_str(), -1, SQLITE_TRANSIENT);
                    sqlite3_bind_int64(stmt, 2, loadedItemID);
                    sqlite3_step(stmt);
                }
                if(stmt) sqlite3_finalize(stmt);
                contentDirty = false;
                lastSaveTime = ImGui::GetTime();
            } else {
                contentDirty = true;
            }
        }

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
        // Render markdown using our MarkdownText helper
        ImGui::MarkdownText(currentContent.c_str());
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
        ImGui::BeginChild("VaultMeta", ImVec2(0, bottomMetaHeight), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // Tags UI as chips with remove X
        ImGui::TextDisabled("Tags:");
        ImGui::SameLine();
        std::vector<std::string> tags = parseTags(currentTags);
        for(size_t i = 0; i < tags.size(); ++i){
            ImGui::PushID(static_cast<int>(i));
            ImGui::Button(tags[i].c_str()); ImGui::SameLine();
            if(ImGui::SmallButton((std::string("x##tag") + std::to_string(i)).c_str())){
                if(removeTagFromItem(loadedItemID, tags[i])){
                    currentTags = getTagsOf(loadedItemID);
                    statusMessage = "Tag removed";
                    statusTime = ImGui::GetTime();
                }
            }
            ImGui::SameLine();
            ImGui::PopID();
        }
        ImGui::NewLine();
        static char addTagBuf[64] = "";
        ImGui::SetNextItemWidth(200);
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
                if(ImGui::Selectable(selLabel.c_str())){
                    if(addParentRelation(id, loadedItemID)){
                        statusMessage = "Parent added";
                    } else {
                        statusMessage = "Failed to add parent";
                    }
                    statusTime = ImGui::GetTime();
                }
            }
            ImGui::EndChild();
        }

        // Show status message briefly
        if(!statusMessage.empty() && (ImGui::GetTime() - statusTime) < 3.0f){
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0.8f,0.8f,0.2f,1.0f), "%s", statusMessage.c_str());
        }

        ImGui::EndChild();

        ImGui::End();
    }

    // Manage DB lifetime safely and prevent accidental copies
    ~Vault(){
        if(dbConnection) sqlite3_close(dbConnection);
    }
    Vault(const Vault&) = delete;
    Vault& operator=(const Vault&) = delete;
    Vault(Vault&& other) noexcept : id(other.id), name(std::move(other.name)), selectedItemID(other.selectedItemID), dbConnection(other.dbConnection){
        other.dbConnection = nullptr;
    }
    Vault& operator=(Vault&& other) noexcept{
        if(this != &other){
            if(dbConnection) sqlite3_close(dbConnection);
            id = other.id;
            name = std::move(other.name);
            selectedItemID = other.selectedItemID;
            dbConnection = other.dbConnection;
            other.dbConnection = nullptr;
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
        const char* updateSQL = "UPDATE VaultItems SET Content = ?, Tags = ? WHERE ID = ?;";
        sqlite3_stmt* stmt = nullptr;
        std::string joined = joinTags(tags);
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
        const char* sql = "SELECT Name FROM VaultItems WHERE ID = ?;";
        sqlite3_stmt* stmt;
        std::string name = "<unknown>";
        if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_int64(stmt, 1, id);
            if(sqlite3_step(stmt) == SQLITE_ROW){
                const unsigned char* text = sqlite3_column_text(stmt, 0);
                if(text) name = reinterpret_cast<const char*>(text);
            }
        }
        sqlite3_finalize(stmt);
        return name;
    }

    void getChildren(int64_t parentID, std::vector<int64_t>& outChildren){
        const char* sql = "SELECT ChildID FROM VaultItemChildren WHERE ParentID = ?;";
        sqlite3_stmt* stmt;
        if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_int64(stmt, 1, parentID);
            while(sqlite3_step(stmt) == SQLITE_ROW){
                outChildren.push_back(sqlite3_column_int64(stmt, 0));
            }
        }
        sqlite3_finalize(stmt);
    }

    void drawVaultNode(int64_t parentID, int64_t nodeID, std::vector<int64_t>& path){
        // If filtering is active, skip subtrees that have no matching nodes
        if(!activeTagFilter.empty()){
            std::unordered_set<int64_t> visited;
            if(!nodeOrDescendantMatches(nodeID, visited)) return;
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
                    int64_t nid = createItem("New Note", nodeID);
                    if(nid != -1) selectedItemID = nid;
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
                    int64_t nid = createItem("New Note", nodeID);
                    if(nid != -1) selectedItemID = nid;
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
        const char* sql = "SELECT ID, Name FROM VaultItems ORDER BY Name;";
        sqlite3_stmt* stmt = nullptr;
        if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
            while(sqlite3_step(stmt) == SQLITE_ROW){
                int64_t id = sqlite3_column_int64(stmt, 0);
                const unsigned char* text = sqlite3_column_text(stmt, 1);
                std::string name = text ? reinterpret_cast<const char*>(text) : std::string();
                out.emplace_back(id, name);
            }
        }
        sqlite3_finalize(stmt);
        return out;
    }

    // Get parents of a child
    std::vector<int64_t> getParentsOf(int64_t childID){
        std::vector<int64_t> out;
        const char* sql = "SELECT ParentID FROM VaultItemChildren WHERE ChildID = ?;";
        sqlite3_stmt* stmt = nullptr;
        if(sqlite3_prepare_v2(dbConnection, sql, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_int64(stmt, 1, childID);
            while(sqlite3_step(stmt) == SQLITE_ROW){
                int64_t pid = sqlite3_column_int64(stmt, 0);
                // Skip self-parent relations (defensive fix) and remove them from DB if found
                if(pid == childID){
                    // remove invalid self-relation
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
        // prevent duplicate
        const char* exists = "SELECT 1 FROM VaultItemChildren WHERE ParentID = ? AND ChildID = ?;";
        sqlite3_stmt* stmt = nullptr;
        if(sqlite3_prepare_v2(dbConnection, exists, -1, &stmt, nullptr) == SQLITE_OK){
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
        if(sqlite3_prepare_v2(dbConnection, insert, -1, &stmt, nullptr) == SQLITE_OK){
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

        // Count current parents
        int parentCount = 0;
        const char* countSQL = "SELECT COUNT(*) FROM VaultItemChildren WHERE ChildID = ?;";
        sqlite3_stmt* stmt = nullptr;
        if(sqlite3_prepare_v2(dbConnection, countSQL, -1, &stmt, nullptr) == SQLITE_OK){
            sqlite3_bind_int64(stmt, 1, childID);
            if(sqlite3_step(stmt) == SQLITE_ROW) parentCount = sqlite3_column_int(stmt, 0);
        }
        if(stmt) sqlite3_finalize(stmt);

        // Disallow removing the last remaining parent (never leave a node parentless)
        if(parentCount <= 1) return false;

        // Otherwise, removal is allowed (including removing the root if there are other parents)
        const char* del = "DELETE FROM VaultItemChildren WHERE ParentID = ? AND ChildID = ?;";
        stmt = nullptr;
        if(sqlite3_prepare_v2(dbConnection, del, -1, &stmt, nullptr) == SQLITE_OK){
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