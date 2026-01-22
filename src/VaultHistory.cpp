#include "VaultHistory.hpp"
#include <plog/Log.h>
#include <ctime>
#include <random>
#include <sstream>
#include <iomanip>

#include <git2.h>

namespace LoreBook {

VaultHistory::VaultHistory(sqlite3* dbConnection) : db(dbConnection) {}

static int execNoErr(sqlite3* db, const char* sql){
    char* err = nullptr;
    int r = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if(r != SQLITE_OK){ if(err) sqlite3_free(err); }
    return r;
}

bool VaultHistory::ensureSchema(std::string* outError){
    if(!db){ if(outError) *outError = "DB not open"; return false; }

    const char* createRevisions = "CREATE TABLE IF NOT EXISTS Revisions ("
                                   "RevisionID TEXT PRIMARY KEY,"
                                   "ItemID INTEGER,"
                                   "AuthorUserID INTEGER,"
                                   "CreatedAt INTEGER,"
                                   "BaseRevisionID TEXT,"
                                   "RevisionType TEXT,"
                                   "ChangeSummary TEXT,"
                                   "UnifiedDiff TEXT"
                                   ");";
    if(execNoErr(db, createRevisions) != SQLITE_OK){ if(outError) *outError = "failed to create Revisions"; return false; }

    const char* createRevisionFields = "CREATE TABLE IF NOT EXISTS RevisionFields ("
                                       "RevisionID TEXT,"
                                       "FieldName TEXT,"
                                       "OldValue TEXT,"
                                       "NewValue TEXT,"
                                       "FieldDiff TEXT,"
                                       "PRIMARY KEY (RevisionID, FieldName),"
                                       "FOREIGN KEY (RevisionID) REFERENCES Revisions(RevisionID) ON DELETE CASCADE"
                                       ");";
    if(execNoErr(db, createRevisionFields) != SQLITE_OK){ if(outError) *outError = "failed to create RevisionFields"; return false; }

    const char* createItemVersions = "CREATE TABLE IF NOT EXISTS ItemVersions ("
                                     "ItemID INTEGER,"
                                     "VersionSeq INTEGER,"
                                     "RevisionID TEXT,"
                                     "CreatedAt INTEGER,"
                                     "PRIMARY KEY (ItemID, VersionSeq),"
                                     "FOREIGN KEY (RevisionID) REFERENCES Revisions(RevisionID) ON DELETE SET NULL"
                                     ");";
    if(execNoErr(db, createItemVersions) != SQLITE_OK){ if(outError) *outError = "failed to create ItemVersions"; return false; }

    const char* createRevisionParents = "CREATE TABLE IF NOT EXISTS RevisionParents ("
                                        "RevisionID TEXT,"
                                        "ParentRevisionID TEXT,"
                                        "PRIMARY KEY (RevisionID, ParentRevisionID)"
                                        ");";
    if(execNoErr(db, createRevisionParents) != SQLITE_OK){ if(outError) *outError = "failed to create RevisionParents"; return false; }

    const char* createConflicts = "CREATE TABLE IF NOT EXISTS Conflicts ("
                                  "ConflictID TEXT PRIMARY KEY,"
                                  "ItemID INTEGER,"
                                  "FieldName TEXT,"
                                  "BaseRevisionID TEXT,"
                                  "LocalRevisionID TEXT,"
                                  "RemoteRevisionID TEXT,"
                                  "OriginatorUserID INTEGER,"
                                  "CreatedAt INTEGER,"
                                  "Status TEXT DEFAULT 'open',"
                                  "ResolvedByAdminUserID INTEGER,"
                                  "ResolvedAt INTEGER,"
                                  "ResolutionPayload TEXT"
                                  ");";
    if(execNoErr(db, createConflicts) != SQLITE_OK){ if(outError) *outError = "failed to create Conflicts"; return false; }

    // ensure VaultItems has HeadRevision and VersionSeq - this is a no-op if columns already exist
    sqlite3_stmt* pragma2 = nullptr;
    bool hasVersionSeq = false, hasHeadRevision = false;
    const char* pragmaSQL2 = "PRAGMA table_info(VaultItems);";
    if(sqlite3_prepare_v2(db, pragmaSQL2, -1, &pragma2, nullptr) == SQLITE_OK){
        while(sqlite3_step(pragma2) == SQLITE_ROW){
            const unsigned char* colName = sqlite3_column_text(pragma2, 1);
            if(colName){
                std::string cname = reinterpret_cast<const char*>(colName);
                if(cname == "VersionSeq") hasVersionSeq = true;
                if(cname == "HeadRevision") hasHeadRevision = true;
            }
        }
    }
    if(pragma2) sqlite3_finalize(pragma2);
    if(!hasVersionSeq){ execNoErr(db, "ALTER TABLE VaultItems ADD COLUMN VersionSeq INTEGER DEFAULT 0;"); }
    if(!hasHeadRevision){ execNoErr(db, "ALTER TABLE VaultItems ADD COLUMN HeadRevision TEXT DEFAULT NULL;"); }

    return true;
}

static std::string nowEpoch(){
    std::time_t t = std::time(nullptr);
    return std::to_string(static_cast<long long>(t));
}

// Helper: read single text column
static std::string readSingleText(sqlite3* db, const char* sql, int64_t param){
    sqlite3_stmt* s = nullptr; std::string out;
    if(sqlite3_prepare_v2(db, sql, -1, &s, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(s,1,param);
        if(sqlite3_step(s) == SQLITE_ROW){
            const unsigned char* t = sqlite3_column_text(s,0);
            if(t) out = reinterpret_cast<const char*>(t);
        }
    }
    if(s) sqlite3_finalize(s);
    return out;
}

// Read a field value from a revision (RevisionFields.NewValue) if present, otherwise fallback to current VaultItems value
static std::string getFieldValueFromRevisionOrItem(sqlite3* db, const std::string &revID, int64_t itemID, const std::string &fieldName);

// Forward-declare threeWayMerge so it can be used before its definition
static bool threeWayMerge(const std::string &base, const std::string &local, const std::string &remote, std::string &merged, bool &hasConflict);

static std::string getFieldValueFromRevisionOrItem(sqlite3* db, const std::string &revID, int64_t itemID, const std::string &fieldName){
    // Special-case to return HeadRevision if requested
    if(fieldName == "HeadRevision"){
        return readSingleText(db, "SELECT HeadRevision FROM VaultItems WHERE ID = ? LIMIT 1;", itemID);
    }
    if(!revID.empty()){
        sqlite3_stmt* s = nullptr;
        const char* q = "SELECT NewValue FROM RevisionFields WHERE RevisionID = ? AND FieldName = ? LIMIT 1;";
        if(sqlite3_prepare_v2(db, q, -1, &s, nullptr) == SQLITE_OK){
            sqlite3_bind_text(s,1,revID.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(s,2,fieldName.c_str(), -1, SQLITE_TRANSIENT);
            if(sqlite3_step(s) == SQLITE_ROW){ const unsigned char* t = sqlite3_column_text(s,0); if(t){ std::string v = reinterpret_cast<const char*>(t); sqlite3_finalize(s); return v; } }
        }
        if(s) sqlite3_finalize(s);
    }
    // fallback to current value in VaultItems
    const char* ik = nullptr;
    if(fieldName == "Name") ik = "SELECT Name FROM VaultItems WHERE ID = ? LIMIT 1;";
    else if(fieldName == "Content") ik = "SELECT Content FROM VaultItems WHERE ID = ? LIMIT 1;";
    else if(fieldName == "Tags") ik = "SELECT Tags FROM VaultItems WHERE ID = ? LIMIT 1;";
    if(ik) return readSingleText(db, ik, itemID);
    return std::string();
}

// Try to three-way merge fields between base/local/remote for a given item. If auto-merge succeeds (no conflicts), returns mergedValues and empty conflicts vector.
// If any field has an automatic conflict or libgit2 fails to produce a result, it will create Conflict rows and return their IDs in the conflicts vector.
std::vector<std::string> VaultHistory::detectAndEnqueueConflicts(int64_t itemID, const std::string &localRevisionID, const std::string &baseRevisionID, const std::string &remoteRevisionID, int64_t originatorUserID){
    // Ensure inputs are reasonable
    if(itemID <= 0 || localRevisionID.empty() || remoteRevisionID.empty()) return {};
    std::vector<std::string> createdConflicts;
    if(!db) return createdConflicts;
    // Fetch all fields changed by local revision
    sqlite3_stmt* s = nullptr;
    const char* selFields = "SELECT FieldName, NewValue FROM RevisionFields WHERE RevisionID = ?;";
    if(sqlite3_prepare_v2(db, selFields, -1, &s, nullptr) != SQLITE_OK) return createdConflicts;
    sqlite3_bind_text(s,1,localRevisionID.c_str(), -1, SQLITE_TRANSIENT);
    std::map<std::string,std::string> mergedValues;
    std::map<std::string,std::string> localValues;
    std::vector<std::string> fields;
    while(sqlite3_step(s) == SQLITE_ROW){
        const unsigned char* fn = sqlite3_column_text(s,0);
        const unsigned char* nv = sqlite3_column_text(s,1);
        if(!fn) continue;
        std::string field = reinterpret_cast<const char*>(fn);
        std::string local = nv ? reinterpret_cast<const char*>(nv) : std::string();
        localValues[field] = local;
        fields.push_back(field);
    }
    sqlite3_finalize(s);

    // For each field attempt three-way merge
    for(auto &f : fields){
        std::string base = getFieldValueFromRevisionOrItem(db, baseRevisionID, itemID, f);
        std::string remote = getFieldValueFromRevisionOrItem(db, remoteRevisionID, itemID, f);
        std::string local = localValues[f];
        std::string merged;
        bool hasConflict = false;
        bool ok = threeWayMerge(base, local, remote, merged, hasConflict);
        if(!ok){
            // libgit2 merge failed - enqueue a conflict
            std::string cid = generateUUID();
            sqlite3_stmt* ins = nullptr;
            const char* insSQL = "INSERT INTO Conflicts (ConflictID, ItemID, FieldName, BaseRevisionID, LocalRevisionID, RemoteRevisionID, OriginatorUserID, CreatedAt, Status) VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'open');";
            if(sqlite3_prepare_v2(db, insSQL, -1, &ins, nullptr) == SQLITE_OK){
                sqlite3_bind_text(ins,1,cid.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(ins,2,itemID);
                sqlite3_bind_text(ins,3,f.c_str(), -1, SQLITE_TRANSIENT);
                if(!base.empty()) sqlite3_bind_text(ins,4,baseRevisionID.c_str(), -1, SQLITE_TRANSIENT); else sqlite3_bind_null(ins,4);
                sqlite3_bind_text(ins,5,localRevisionID.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins,6,remoteRevisionID.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(ins,7,originatorUserID);
                sqlite3_bind_int64(ins,8, static_cast<sqlite3_int64>(std::time(nullptr)));
                sqlite3_step(ins);
            }
            if(ins) sqlite3_finalize(ins);
            createdConflicts.push_back(cid);
            continue;
        }
        if(hasConflict){
            // enqueue conflict row - admin must resolve
            std::string cid = generateUUID();
            sqlite3_stmt* ins = nullptr;
            const char* insSQL = "INSERT INTO Conflicts (ConflictID, ItemID, FieldName, BaseRevisionID, LocalRevisionID, RemoteRevisionID, OriginatorUserID, CreatedAt, Status) VALUES (?, ?, ?, ?, ?, ?, ?, ?, 'open');";
            if(sqlite3_prepare_v2(db, insSQL, -1, &ins, nullptr) == SQLITE_OK){
                sqlite3_bind_text(ins,1,cid.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(ins,2,itemID);
                sqlite3_bind_text(ins,3,f.c_str(), -1, SQLITE_TRANSIENT);
                if(!base.empty()) sqlite3_bind_text(ins,4,baseRevisionID.c_str(), -1, SQLITE_TRANSIENT); else sqlite3_bind_null(ins,4);
                sqlite3_bind_text(ins,5,localRevisionID.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ins,6,remoteRevisionID.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int64(ins,7,originatorUserID);
                sqlite3_bind_int64(ins,8, static_cast<sqlite3_int64>(std::time(nullptr)));
                sqlite3_step(ins);
            }
            if(ins) sqlite3_finalize(ins);
            createdConflicts.push_back(cid);
            continue;
        }
        // auto-merged
        mergedValues[f] = merged;
    }

    // If some fields auto-merged and no conflicts at all, create merge revision and apply
    if(createdConflicts.empty() && !mergedValues.empty()){
        // create merge revision with parents remoteRevisionID and localRevisionID
        std::map<std::string,std::pair<std::string,std::string>> fieldsForCommit;
        for(auto &p : mergedValues){
            std::string oldVal = getFieldValueFromRevisionOrItem(db, remoteRevisionID, itemID, p.first);
            fieldsForCommit[p.first] = std::make_pair(oldVal, p.second);
        }
        std::string mergeRevID = generateUUID();
        sqlite3_stmt* insM = nullptr;
        const char* insMerge = "INSERT INTO Revisions (RevisionID, ItemID, AuthorUserID, CreatedAt, BaseRevisionID, RevisionType, ChangeSummary, UnifiedDiff) VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
        if(sqlite3_prepare_v2(db, insMerge, -1, &insM, nullptr) == SQLITE_OK){
            sqlite3_bind_text(insM,1,mergeRevID.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(insM,2,itemID);
            sqlite3_bind_int64(insM,3,originatorUserID);
            sqlite3_bind_int64(insM,4, static_cast<sqlite3_int64>(std::time(nullptr)));
            if(!remoteRevisionID.empty()) sqlite3_bind_text(insM,5,remoteRevisionID.c_str(), -1, SQLITE_TRANSIENT); else sqlite3_bind_null(insM,5);
            sqlite3_bind_text(insM,6,"merge", -1, SQLITE_TRANSIENT);
            sqlite3_bind_null(insM,7);
            sqlite3_bind_null(insM,8);
            sqlite3_step(insM);
        }
        if(insM) sqlite3_finalize(insM);
        const char* insField = "INSERT INTO RevisionFields (RevisionID, FieldName, OldValue, NewValue, FieldDiff) VALUES (?, ?, ?, ?, ?);";
        for(auto &p : fieldsForCommit){
            sqlite3_stmt* inf = nullptr;
            if(sqlite3_prepare_v2(db, insField, -1, &inf, nullptr) == SQLITE_OK){
                sqlite3_bind_text(inf,1,mergeRevID.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(inf,2,p.first.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(inf,3,p.second.first.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(inf,4,p.second.second.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_null(inf,5);
                sqlite3_step(inf);
            }
            if(inf) sqlite3_finalize(inf);
        }
        // add parent relations
        if(!remoteRevisionID.empty()){
            sqlite3_stmt* ip = nullptr;
            const char* insParent = "INSERT OR IGNORE INTO RevisionParents (RevisionID, ParentRevisionID) VALUES (?, ?);";
            if(sqlite3_prepare_v2(db, insParent, -1, &ip, nullptr) == SQLITE_OK){
                sqlite3_bind_text(ip,1,mergeRevID.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ip,2,remoteRevisionID.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(ip);
            }
            if(ip) sqlite3_finalize(ip);
        }
        if(!localRevisionID.empty()){
            sqlite3_stmt* ip2 = nullptr;
            const char* insParent = "INSERT OR IGNORE INTO RevisionParents (RevisionID, ParentRevisionID) VALUES (?, ?);";
            if(sqlite3_prepare_v2(db, insParent, -1, &ip2, nullptr) == SQLITE_OK){
                sqlite3_bind_text(ip2,1,mergeRevID.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ip2,2,localRevisionID.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(ip2);
            }
            if(ip2) sqlite3_finalize(ip2);
        }
        // apply merged values to VaultItems
        std::string upd = "UPDATE VaultItems SET ";
        bool first = true;
        for(auto &p : mergedValues){ if(!first) upd += ", "; first = false; upd += p.first + " = ?"; }
        upd += " WHERE ID = ?;";
        sqlite3_stmt* upst = nullptr;
        if(sqlite3_prepare_v2(db, upd.c_str(), -1, &upst, nullptr) == SQLITE_OK){
            int idx = 1; for(auto &p : mergedValues){ sqlite3_bind_text(upst, idx++, p.second.c_str(), -1, SQLITE_TRANSIENT); }
            sqlite3_bind_int64(upst, idx, itemID);
            sqlite3_step(upst);
        }
        if(upst) sqlite3_finalize(upst);
        // update head & ItemVersions
        sqlite3_stmt* selMax = nullptr; int64_t nextSeq = 1;
        const char* selMaxSQL = "SELECT MAX(VersionSeq) FROM ItemVersions WHERE ItemID = ?;";
        if(sqlite3_prepare_v2(db, selMaxSQL, -1, &selMax, nullptr) == SQLITE_OK){ sqlite3_bind_int64(selMax,1,itemID); if(sqlite3_step(selMax) == SQLITE_ROW){ if(sqlite3_column_type(selMax,0) != SQLITE_NULL) nextSeq = sqlite3_column_int64(selMax,0) + 1; } }
        if(selMax) sqlite3_finalize(selMax);
        sqlite3_stmt* insV = nullptr; const char* insVSQL = "INSERT INTO ItemVersions (ItemID, VersionSeq, RevisionID, CreatedAt) VALUES (?, ?, ?, ?);";
        if(sqlite3_prepare_v2(db, insVSQL, -1, &insV, nullptr) == SQLITE_OK){ sqlite3_bind_int64(insV,1,itemID); sqlite3_bind_int64(insV,2,nextSeq); sqlite3_bind_text(insV,3,mergeRevID.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_int64(insV,4, static_cast<sqlite3_int64>(std::time(nullptr))); sqlite3_step(insV); }
        if(insV) sqlite3_finalize(insV);
        sqlite3_stmt* upHead = nullptr; const char* upHeadSQL = "UPDATE VaultItems SET VersionSeq = ?, HeadRevision = ? WHERE ID = ?;";
        if(sqlite3_prepare_v2(db, upHeadSQL, -1, &upHead, nullptr) == SQLITE_OK){ sqlite3_bind_int64(upHead,1,nextSeq); sqlite3_bind_text(upHead,2,mergeRevID.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_int64(upHead,3,itemID); sqlite3_step(upHead); }
        if(upHead) sqlite3_finalize(upHead);
    }

    return createdConflicts;
}


std::string VaultHistory::generateUUID(){
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dis;
    uint64_t a = dis(gen);
    uint64_t b = dis(gen);
    std::stringstream ss;
    ss << std::hex << std::setfill('0')
       << std::setw(8) << static_cast<uint32_t>((a >> 32) & 0xffffffff) << "-"
       << std::setw(4) << static_cast<uint16_t>((a >> 16) & 0xffff) << "-"
       << std::setw(4) << static_cast<uint16_t>(a & 0xffff) << "-"
       << std::setw(4) << static_cast<uint16_t>((b >> 48) & 0xffff) << "-"
       << std::setw(12) << (b & 0xffffffffffffULL);
    return ss.str();
}

static bool threeWayMerge(const std::string &base, const std::string &local, const std::string &remote, std::string &merged, bool &hasConflict){
    git_merge_file_input ancestor = GIT_MERGE_FILE_INPUT_INIT;
    ancestor.ptr = base.c_str(); ancestor.size = base.size();
    git_merge_file_input ours = GIT_MERGE_FILE_INPUT_INIT;
    ours.ptr = local.c_str(); ours.size = local.size();
    git_merge_file_input theirs = GIT_MERGE_FILE_INPUT_INIT;
    theirs.ptr = remote.c_str(); theirs.size = remote.size();
    git_merge_file_result result;
    int r = git_merge_file(&result, &ancestor, &ours, &theirs, nullptr);
    if(r == 0){
        merged.assign(result.ptr, result.ptr + result.len);
        hasConflict = (result.automergeable == 0);
        git_merge_file_result_free(&result);
        return true;
    }
    const git_error* e = git_error_last();
    if(e) PLOGW << "threeWayMerge: libgit2 merge failed: " << (e->message ? e->message : "(no message)");
    else PLOGW << "threeWayMerge: libgit2 merge failed with code " << r;
    // No fallback: signal failure to caller so the conflict can be enqueued for admin resolution
    hasConflict = true;
    return false;
}

std::string VaultHistory::recordRevision(int64_t itemID, int64_t authorUserID, const std::string &revisionType,
                                         const std::map<std::string, std::pair<std::string,std::string>> &fieldChanges,
                                         const std::string &baseRevisionID){
    if(!db) return std::string();
    std::string revID = generateUUID();
    sqlite3_stmt* ins = nullptr;
    const char* insertRev = "INSERT INTO Revisions (RevisionID, ItemID, AuthorUserID, CreatedAt, BaseRevisionID, RevisionType, ChangeSummary, UnifiedDiff) VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
    if(sqlite3_prepare_v2(db, insertRev, -1, &ins, nullptr) != SQLITE_OK){ PLOGW << "VaultHistory: prepare insertRev failed"; return std::string(); }
    sqlite3_bind_text(ins, 1, revID.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(ins, 2, itemID);
    sqlite3_bind_int64(ins, 3, authorUserID);
    sqlite3_bind_int64(ins, 4, static_cast<sqlite3_int64>(std::time(nullptr)));
    if(baseRevisionID.empty()) sqlite3_bind_null(ins, 5); else sqlite3_bind_text(ins,5,baseRevisionID.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(ins, 6, revisionType.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_null(ins, 7); // ChangeSummary - optional for now
    sqlite3_bind_null(ins, 8); // UnifiedDiff - optional for now
    if(sqlite3_step(ins) != SQLITE_DONE){ PLOGW << "VaultHistory: failed to insert Revisions"; sqlite3_finalize(ins); return std::string(); }
    sqlite3_finalize(ins);

    const char* insertField = "INSERT INTO RevisionFields (RevisionID, FieldName, OldValue, NewValue, FieldDiff) VALUES (?, ?, ?, ?, ?);";
    for(const auto &p : fieldChanges){
        sqlite3_stmt* fin = nullptr;
        if(sqlite3_prepare_v2(db, insertField, -1, &fin, nullptr) != SQLITE_OK){ PLOGW << "VaultHistory: prepare insertField failed"; continue; }
        sqlite3_bind_text(fin,1,revID.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(fin,2,p.first.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(fin,3,p.second.first.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(fin,4,p.second.second.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_null(fin,5); // FieldDiff - compute later
        if(sqlite3_step(fin) != SQLITE_DONE){ PLOGW << "VaultHistory: failed to insert RevisionFields"; }
        sqlite3_finalize(fin);
    }

    // Append to ItemVersions with next sequence
    sqlite3_stmt* verStmt = nullptr;
    const char* selMax = "SELECT MAX(VersionSeq) FROM ItemVersions WHERE ItemID = ?;";
    int64_t nextSeq = 1;
    if(sqlite3_prepare_v2(db, selMax, -1, &verStmt, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(verStmt, 1, itemID);
        if(sqlite3_step(verStmt) == SQLITE_ROW){
            if(sqlite3_column_type(verStmt,0) != SQLITE_NULL){ nextSeq = sqlite3_column_int64(verStmt,0) + 1; }
        }
    }
    if(verStmt) sqlite3_finalize(verStmt);

    sqlite3_stmt* insVer = nullptr;
    const char* insertVer = "INSERT INTO ItemVersions (ItemID, VersionSeq, RevisionID, CreatedAt) VALUES (?, ?, ?, ?);";
    if(sqlite3_prepare_v2(db, insertVer, -1, &insVer, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(insVer,1,itemID);
        sqlite3_bind_int64(insVer,2,nextSeq);
        sqlite3_bind_text(insVer,3,revID.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int64(insVer,4, static_cast<sqlite3_int64>(std::time(nullptr)));
        sqlite3_step(insVer);
    }
    if(insVer) sqlite3_finalize(insVer);

    // Check current head in VaultItems
    std::string curHead = getFieldValueFromRevisionOrItem(db, std::string(), itemID, "HeadRevision");
    // If baseRevisionID matches current head (fast-forward), apply as new head
    if(baseRevisionID.empty() || baseRevisionID == curHead){
        sqlite3_stmt* upd = nullptr;
        const char* updSQL = "UPDATE VaultItems SET VersionSeq = ?, HeadRevision = ? WHERE ID = ?;";
        if(sqlite3_prepare_v2(db, updSQL, -1, &upd, nullptr) == SQLITE_OK){
            sqlite3_bind_int64(upd,1,nextSeq);
            sqlite3_bind_text(upd,2,revID.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(upd,3,itemID);
            sqlite3_step(upd);
        }
        if(upd) sqlite3_finalize(upd);
    } else {
        // concurrent update - try three-way merge or enqueue conflicts
        std::vector<std::string> conflicts = detectAndEnqueueConflicts(itemID, revID, baseRevisionID, curHead, authorUserID);
        if(!conflicts.empty()){
            PLOGI << "recordRevision: enqueued " << conflicts.size() << " conflicts for item " << itemID;
            // do not update head
        }
    }

    return revID;
}

} // namespace LoreBook
