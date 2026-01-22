#include "VaultSync.hpp"
#include "Vault.hpp"
#include <thread>
#include <future>
#include <plog/Log.h>

namespace LoreBook {

void VaultSync::startUpload(Vault* localVault, const DBConnectionInfo &remoteCI, bool dryRun, int64_t uploaderUserID, std::function<void(int,const std::string&)> progressCb){
    // run in background thread
    std::thread([localVault, remoteCI, dryRun, uploaderUserID, progressCb](){
        try{
            if(progressCb) progressCb(0, "Opening remote vault...");
            VaultConfig cfg; cfg.connInfo = remoteCI; cfg.createIfMissing = true;
            std::string err;
            auto remoteVault = Vault::Open(cfg, &err);
            if(!remoteVault){
                if(progressCb) progressCb(-1, std::string("Failed to open remote vault: ") + err);
                return;
            }
            if(progressCb) progressCb(5, "Remote vault opened and schema ensured");
            if(dryRun){
                if(progressCb) progressCb(100, "Dry run complete (no changes made)");
                return;
            }

            // Implement staged copy: 1) ensure items exist remotely (insert missing), 2) create revisions on remote representing local changes which will enqueue conflicts if needed
            auto localItems = localVault->getAllItemsPublic();
            size_t total = localItems.size(); size_t idx = 0;
            auto remoteHistory = remoteVault->getHistoryPublic();
            LoreBook::IDBBackend* rdb = remoteVault->getDBBackendPublic();
            if(!remoteHistory){ if(progressCb) progressCb(-1, "Remote vault has no history helper"); return; }

            // Use a transaction on remote if supported
            if(rdb) rdb->beginTransaction();
            try{
                for(auto &p : localItems){
                    ++idx;
                    int64_t itemID = p.first;
                    if(progressCb) progressCb(static_cast<int>((idx*90)/ (total>0?total:1)), std::string("Processing item ") + std::to_string(itemID));
                    // Read local item
                    auto localRec = localVault->getItemPublic(itemID);
                    if(localRec.id <= 0) continue;
                    // Read remote item
                    auto remoteRec = remoteVault->getItemPublic(itemID);
                    if(remoteRec.id <= 0){
                        // insert missing remote item with same ID
                        if(rdb){
                            std::string err;
                            auto ins = rdb->prepare("INSERT INTO VaultItems (ID, Name, Content, Tags, IsRoot) VALUES (?, ?, ?, ?, ?);", &err);
                            if(ins){ ins->bindInt(1, localRec.id); ins->bindString(2, localRec.name); ins->bindString(3, localRec.content); ins->bindString(4, localRec.tags); ins->bindInt(5, localRec.isRoot); ins->execute(); }
                        } else {
                            // sqlite path - insert with explicit ID
                            sqlite3_stmt* ins = nullptr;
                            const char* sql = "INSERT INTO VaultItems (ID, Name, Content, Tags, IsRoot) VALUES (?, ?, ?, ?, ?);";
                            if(localVault->getDBPublic()){
                                if(sqlite3_prepare_v2(localVault->getDBPublic(), sql, -1, &ins, nullptr) == SQLITE_OK){
                                    sqlite3_bind_int64(ins,1, localRec.id);
                                    sqlite3_bind_text(ins,2, localRec.name.c_str(), -1, SQLITE_TRANSIENT);
                                    sqlite3_bind_text(ins,3, localRec.content.c_str(), -1, SQLITE_TRANSIENT);
                                    sqlite3_bind_text(ins,4, localRec.tags.c_str(), -1, SQLITE_TRANSIENT);
                                    sqlite3_bind_int(ins,5, localRec.isRoot);
                                    sqlite3_step(ins);
                                }
                                if(ins) sqlite3_finalize(ins);
                            }
                        }
                    }

                    // Determine changes: compare remote values to local
                    std::map<std::string,std::pair<std::string,std::string>> changes;
                    if(remoteRec.name != localRec.name) changes["Name"] = std::make_pair(remoteRec.name, localRec.name);
                    if(remoteRec.content != localRec.content) changes["Content"] = std::make_pair(remoteRec.content, localRec.content);
                    if(remoteRec.tags != localRec.tags) changes["Tags"] = std::make_pair(remoteRec.tags, localRec.tags);

                    if(!changes.empty()){
                        // baseRevision is remote current HeadRevision
                        std::string baseRev = remoteRec.headRevision;
                        // record revision on remote; this will enqueue conflicts with OriginatorUserID = uploaderUserID when concurrent
                        std::string newRev = remoteHistory->recordRevision(itemID, uploaderUserID, std::string("upload"), changes, baseRev);
                        if(newRev.empty()){
                            if(progressCb) progressCb(-1, std::string("Failed to record revision for item ") + std::to_string(itemID));
                        } else {
                            if(progressCb) progressCb(static_cast<int>(90 + (idx*9)/(total>0?total:1)), std::string("Recorded revision ") + newRev + " for item " + std::to_string(itemID));
                        }
                    }
                }
                if(rdb) rdb->commit();
            } catch(...){ if(rdb) rdb->rollback(); throw; }

            if(progressCb) progressCb(100, "Upload completed (items & revisions). Please verify conflicts as needed.");
        }catch(const std::exception &ex){
            if(progressCb) progressCb(-1, std::string("Upload failed: ") + ex.what());
        }
    }).detach();
}

} // namespace LoreBook
