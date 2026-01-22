#include "VaultHistory.hpp"
#include <plog/Log.h>

namespace LoreBook {

std::vector<ConflictRecord> VaultHistory::listOpenConflicts(int64_t originatorFilter){
    std::vector<ConflictRecord> out;
    if(!db && !backend) return out;

    if(backend){
        std::string q = "SELECT ConflictID, ItemID, FieldName, BaseRevisionID, LocalRevisionID, RemoteRevisionID, OriginatorUserID, CreatedAt, Status FROM Conflicts WHERE Status='open'";
        if(originatorFilter >= 0) q += " AND OriginatorUserID = ?";
        q += " ORDER BY CreatedAt DESC;";
        auto stmt = backend->prepare(q);
        if(!stmt) return out;
        if(originatorFilter >= 0) stmt->bindInt(1, originatorFilter);
        auto rs = stmt->executeQuery();
        while(rs && rs->next()){
            ConflictRecord r;
            r.ConflictID = rs->getString(0);
            r.ItemID = rs->getInt64(1);
            r.FieldName = rs->getString(2);
            r.BaseRevisionID = rs->isNull(3) ? std::string() : rs->getString(3);
            r.LocalRevisionID = rs->isNull(4) ? std::string() : rs->getString(4);
            r.RemoteRevisionID = rs->isNull(5) ? std::string() : rs->getString(5);
            r.OriginatorUserID = rs->getInt64(6);
            r.CreatedAt = rs->getInt64(7);
            r.Status = rs->getString(8);
            out.push_back(r);
        }
        return out;
    }

    sqlite3_stmt* s = nullptr;
    std::string q = "SELECT ConflictID, ItemID, FieldName, BaseRevisionID, LocalRevisionID, RemoteRevisionID, OriginatorUserID, CreatedAt, Status FROM Conflicts WHERE Status='open'";
    if(originatorFilter >= 0) q += " AND OriginatorUserID = ?";
    q += " ORDER BY CreatedAt DESC;";
    if(sqlite3_prepare_v2(db, q.c_str(), -1, &s, nullptr) != SQLITE_OK) return out;
    if(originatorFilter >= 0) sqlite3_bind_int64(s,1,originatorFilter);
    while(sqlite3_step(s) == SQLITE_ROW){
        ConflictRecord r;
        const unsigned char* cid = sqlite3_column_text(s,0); if(cid) r.ConflictID = reinterpret_cast<const char*>(cid);
        r.ItemID = sqlite3_column_int64(s,1);
        const unsigned char* fn = sqlite3_column_text(s,2); if(fn) r.FieldName = reinterpret_cast<const char*>(fn);
        const unsigned char* br = sqlite3_column_text(s,3); if(br) r.BaseRevisionID = reinterpret_cast<const char*>(br);
        const unsigned char* lr = sqlite3_column_text(s,4); if(lr) r.LocalRevisionID = reinterpret_cast<const char*>(lr);
        const unsigned char* rr = sqlite3_column_text(s,5); if(rr) r.RemoteRevisionID = reinterpret_cast<const char*>(rr);
        r.OriginatorUserID = sqlite3_column_int64(s,6);
        r.CreatedAt = sqlite3_column_int64(s,7);
        const unsigned char* st = sqlite3_column_text(s,8); if(st) r.Status = reinterpret_cast<const char*>(st);
        out.push_back(r);
    }
    if(s) sqlite3_finalize(s);
    return out;
}

ConflictRecord VaultHistory::getConflictDetail(const std::string &conflictID){
    ConflictRecord r;
    if(!db && !backend) return r;
    if(backend){
        auto stmt = backend->prepare("SELECT ConflictID, ItemID, FieldName, BaseRevisionID, LocalRevisionID, RemoteRevisionID, OriginatorUserID, CreatedAt, Status, ResolvedByAdminUserID, ResolvedAt, ResolutionPayload FROM Conflicts WHERE ConflictID = ? LIMIT 1;");
        if(!stmt) return r;
        stmt->bindString(1, conflictID);
        auto rs = stmt->executeQuery();
        if(rs && rs->next()){
            r.ConflictID = rs->getString(0);
            r.ItemID = rs->getInt64(1);
            r.FieldName = rs->getString(2);
            r.BaseRevisionID = rs->isNull(3) ? std::string() : rs->getString(3);
            r.LocalRevisionID = rs->isNull(4) ? std::string() : rs->getString(4);
            r.RemoteRevisionID = rs->isNull(5) ? std::string() : rs->getString(5);
            r.OriginatorUserID = rs->getInt64(6);
            r.CreatedAt = rs->getInt64(7);
            r.Status = rs->getString(8);
            if(!rs->isNull(9)) r.ResolvedByAdminUserID = rs->getInt64(9);
            if(!rs->isNull(10)) r.ResolvedAt = rs->getInt64(10);
            r.ResolutionPayload = rs->isNull(11) ? std::string() : rs->getString(11);
        }
        return r;
    }

    sqlite3_stmt* s = nullptr;
    const char* q = "SELECT ConflictID, ItemID, FieldName, BaseRevisionID, LocalRevisionID, RemoteRevisionID, OriginatorUserID, CreatedAt, Status, ResolvedByAdminUserID, ResolvedAt, ResolutionPayload FROM Conflicts WHERE ConflictID = ? LIMIT 1;";
    if(sqlite3_prepare_v2(db, q, -1, &s, nullptr) != SQLITE_OK) return r;
    sqlite3_bind_text(s,1,conflictID.c_str(), -1, SQLITE_TRANSIENT);
    if(sqlite3_step(s) == SQLITE_ROW){
        const unsigned char* cid = sqlite3_column_text(s,0); if(cid) r.ConflictID = reinterpret_cast<const char*>(cid);
        r.ItemID = sqlite3_column_int64(s,1);
        const unsigned char* fn = sqlite3_column_text(s,2); if(fn) r.FieldName = reinterpret_cast<const char*>(fn);
        const unsigned char* br = sqlite3_column_text(s,3); if(br) r.BaseRevisionID = reinterpret_cast<const char*>(br);
        const unsigned char* lr = sqlite3_column_text(s,4); if(lr) r.LocalRevisionID = reinterpret_cast<const char*>(lr);
        const unsigned char* rr = sqlite3_column_text(s,5); if(rr) r.RemoteRevisionID = reinterpret_cast<const char*>(rr);
        r.OriginatorUserID = sqlite3_column_int64(s,6);
        r.CreatedAt = sqlite3_column_int64(s,7);
        const unsigned char* st = sqlite3_column_text(s,8); if(st) r.Status = reinterpret_cast<const char*>(st);
        if(sqlite3_column_type(s,9) != SQLITE_NULL) r.ResolvedByAdminUserID = sqlite3_column_int64(s,9);
        if(sqlite3_column_type(s,10) != SQLITE_NULL) r.ResolvedAt = sqlite3_column_int64(s,10);
        const unsigned char* rp = sqlite3_column_text(s,11); if(rp) r.ResolutionPayload = reinterpret_cast<const char*>(rp);
    }
    if(s) sqlite3_finalize(s);
    return r;
}

std::string VaultHistory::getFieldValue(const std::string &revID, int64_t itemID, const std::string &fieldName){
    if(!db && !backend) return std::string();
    if(backend){
        if(!revID.empty()){
            auto stmt = backend->prepare("SELECT NewValue FROM RevisionFields WHERE RevisionID = ? AND FieldName = ? LIMIT 1;");
            if(!stmt) return std::string();
            stmt->bindString(1, revID);
            stmt->bindString(2, fieldName);
            auto rs = stmt->executeQuery();
            if(rs && rs->next()){ if(!rs->isNull(0)) return rs->getString(0); }
        }
        if(fieldName == "Name" || fieldName == "Content" || fieldName == "Tags" || fieldName == "HeadRevision"){
            std::string sql = std::string("SELECT ") + fieldName + " FROM VaultItems WHERE ID = ? LIMIT 1;";
            auto stmt = backend->prepare(sql);
            if(!stmt) return std::string();
            stmt->bindInt(1, itemID);
            auto rs = stmt->executeQuery();
            if(rs && rs->next()){ if(rs->isNull(0)) return std::string(); return rs->getString(0); }
            return std::string();
        }
        return std::string();
    }
    // Try RevisionFields first
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
    // Fallback to current value in VaultItems
    sqlite3_stmt* s2 = nullptr;
    const char* ik = nullptr;
    if(fieldName == "Name") ik = "SELECT Name FROM VaultItems WHERE ID = ? LIMIT 1;";
    else if(fieldName == "Content") ik = "SELECT Content FROM VaultItems WHERE ID = ? LIMIT 1;";
    else if(fieldName == "Tags") ik = "SELECT Tags FROM VaultItems WHERE ID = ? LIMIT 1;";
    else if(fieldName == "HeadRevision") ik = "SELECT HeadRevision FROM VaultItems WHERE ID = ? LIMIT 1;";
    std::string out;
    if(ik && sqlite3_prepare_v2(db, ik, -1, &s2, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(s2,1,itemID);
        if(sqlite3_step(s2) == SQLITE_ROW){ const unsigned char* t = sqlite3_column_text(s2,0); if(t) out = reinterpret_cast<const char*>(t); }
    }
    if(s2) sqlite3_finalize(s2);
    return out;
}

bool VaultHistory::adminResolveConflict(const std::string &conflictID, int64_t adminUserID, const std::map<std::string,std::string> &mergedValues, const std::string &resolutionSummary, bool createMergeRevision){
    if(!db && !backend) return false;
    ConflictRecord cr = getConflictDetail(conflictID);
    if(cr.ConflictID.empty()) return false;

    if(backend){
        // apply merged values, optionally creating a merge revision
        if(createMergeRevision){
            std::string mergeRevID = generateUUID();
            auto insM = backend->prepare("INSERT INTO Revisions (RevisionID, ItemID, AuthorUserID, CreatedAt, BaseRevisionID, RevisionType, ChangeSummary, UnifiedDiff) VALUES (?, ?, ?, ?, ?, ?, ?, ?);");
            if(insM){ insM->bindString(1, mergeRevID); insM->bindInt(2, cr.ItemID); insM->bindInt(3, adminUserID); insM->bindInt(4, static_cast<int64_t>(std::time(nullptr))); if(cr.RemoteRevisionID.empty()) insM->bindNull(5); else insM->bindString(5, cr.RemoteRevisionID); insM->bindString(6, "merge"); insM->bindString(7, resolutionSummary); insM->bindNull(8); insM->execute(); }

            auto insField = backend->prepare("INSERT INTO RevisionFields (RevisionID, FieldName, OldValue, NewValue, FieldDiff) VALUES (?, ?, ?, ?, ?);");
            for(auto &p : mergedValues){ std::string oldVal = getFieldValue(cr.RemoteRevisionID, cr.ItemID, p.first); if(insField){ insField->bindString(1, mergeRevID); insField->bindString(2, p.first); insField->bindString(3, oldVal); insField->bindString(4, p.second); insField->bindNull(5); insField->execute(); } }

            if(!cr.RemoteRevisionID.empty()){ auto ip = backend->prepare("INSERT IGNORE INTO RevisionParents (RevisionID, ParentRevisionID) VALUES (?, ?);"); if(ip){ ip->bindString(1, mergeRevID); ip->bindString(2, cr.RemoteRevisionID); ip->execute(); } }
            if(!cr.LocalRevisionID.empty()){ auto ip2 = backend->prepare("INSERT IGNORE INTO RevisionParents (RevisionID, ParentRevisionID) VALUES (?, ?);"); if(ip2){ ip2->bindString(1, mergeRevID); ip2->bindString(2, cr.LocalRevisionID); ip2->execute(); } }

            std::string upd = "UPDATE VaultItems SET "; bool first=true; for(auto &p : mergedValues){ if(!first) upd += ", "; first=false; upd += p.first + " = ?"; } upd += " WHERE ID = ?;";
            auto upst = backend->prepare(upd);
            if(upst){ int idx=1; for(auto &p : mergedValues){ upst->bindString(idx++, p.second); } upst->bindInt(idx, cr.ItemID); upst->execute(); }

            // update ItemVersions & head
            auto maxStmt = backend->prepare("SELECT MAX(VersionSeq) FROM ItemVersions WHERE ItemID = ?;"); int64_t nextSeq = 1; if(maxStmt){ maxStmt->bindInt(1, cr.ItemID); auto r = maxStmt->executeQuery(); if(r && r->next()){ if(!r->isNull(0)) nextSeq = r->getInt64(0) + 1; } }
            auto insV = backend->prepare("INSERT INTO ItemVersions (ItemID, VersionSeq, RevisionID, CreatedAt) VALUES (?, ?, ?, ?);"); if(insV){ insV->bindInt(1, cr.ItemID); insV->bindInt(2, nextSeq); insV->bindString(3, mergeRevID); insV->bindInt(4, static_cast<int64_t>(std::time(nullptr))); insV->execute(); }
            auto upHead = backend->prepare("UPDATE VaultItems SET VersionSeq = ?, HeadRevision = ? WHERE ID = ?;"); if(upHead){ upHead->bindInt(1, nextSeq); upHead->bindString(2, mergeRevID); upHead->bindInt(3, cr.ItemID); upHead->execute(); }
        } else {
            std::string upd = "UPDATE VaultItems SET "; bool first=true; for(auto &p : mergedValues){ if(!first) upd += ", "; first=false; upd += p.first + " = ?"; } upd += " WHERE ID = ?;";
            auto upst = backend->prepare(upd);
            if(upst){ int idx=1; for(auto &p : mergedValues){ upst->bindString(idx++, p.second); } upst->bindInt(idx, cr.ItemID); upst->execute(); }
        }

        // mark conflict resolved
        auto res = backend->prepare("UPDATE Conflicts SET Status = 'resolved', ResolvedByAdminUserID = ?, ResolvedAt = ?, ResolutionPayload = ? WHERE ConflictID = ?;");
        if(res){ res->bindInt(1, adminUserID); res->bindInt(2, static_cast<int64_t>(std::time(nullptr))); res->bindString(3, resolutionSummary); res->bindString(4, conflictID); res->execute(); }
        return true;
    }

    if(cr.ConflictID.empty()) return false;
    // apply merged values
    if(createMergeRevision){
        // create a merge revision using remoteRevision as base
        std::string mergeRevID = generateUUID();
        sqlite3_stmt* insM = nullptr;
        const char* insMerge = "INSERT INTO Revisions (RevisionID, ItemID, AuthorUserID, CreatedAt, BaseRevisionID, RevisionType, ChangeSummary, UnifiedDiff) VALUES (?, ?, ?, ?, ?, ?, ?, ?);";
        if(sqlite3_prepare_v2(db, insMerge, -1, &insM, nullptr) == SQLITE_OK){
            sqlite3_bind_text(insM,1,mergeRevID.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int64(insM,2,cr.ItemID);
            sqlite3_bind_int64(insM,3,adminUserID);
            sqlite3_bind_int64(insM,4, static_cast<sqlite3_int64>(std::time(nullptr)));
            if(!cr.RemoteRevisionID.empty()) sqlite3_bind_text(insM,5,cr.RemoteRevisionID.c_str(), -1, SQLITE_TRANSIENT); else sqlite3_bind_null(insM,5);
            sqlite3_bind_text(insM,6,"merge", -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(insM,7,resolutionSummary.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_null(insM,8);
            sqlite3_step(insM);
        }
        if(insM) sqlite3_finalize(insM);

        const char* insField = "INSERT INTO RevisionFields (RevisionID, FieldName, OldValue, NewValue, FieldDiff) VALUES (?, ?, ?, ?, ?);";
        for(auto &p : mergedValues){
            std::string oldVal = getFieldValue(cr.RemoteRevisionID, cr.ItemID, p.first);
            sqlite3_stmt* inf = nullptr;
            if(sqlite3_prepare_v2(db, insField, -1, &inf, nullptr) == SQLITE_OK){
                sqlite3_bind_text(inf,1,mergeRevID.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(inf,2,p.first.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(inf,3,oldVal.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(inf,4,p.second.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_null(inf,5);
                sqlite3_step(inf);
            }
            if(inf) sqlite3_finalize(inf);
        }
        // link parents
        if(!cr.RemoteRevisionID.empty()){
            sqlite3_stmt* ip = nullptr;
            const char* insParent = "INSERT OR IGNORE INTO RevisionParents (RevisionID, ParentRevisionID) VALUES (?, ?);";
            if(sqlite3_prepare_v2(db, insParent, -1, &ip, nullptr) == SQLITE_OK){
                sqlite3_bind_text(ip,1,mergeRevID.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ip,2,cr.RemoteRevisionID.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_step(ip);
            }
            if(ip) sqlite3_finalize(ip);
        }
        if(!cr.LocalRevisionID.empty()){
            sqlite3_stmt* ip2 = nullptr;
            const char* insParent = "INSERT OR IGNORE INTO RevisionParents (RevisionID, ParentRevisionID) VALUES (?, ?);";
            if(sqlite3_prepare_v2(db, insParent, -1, &ip2, nullptr) == SQLITE_OK){
                sqlite3_bind_text(ip2,1,mergeRevID.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(ip2,2,cr.LocalRevisionID.c_str(), -1, SQLITE_TRANSIENT);
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
            sqlite3_bind_int64(upst, idx, cr.ItemID);
            sqlite3_step(upst);
        }
        if(upst) sqlite3_finalize(upst);
        // update head & ItemVersions
        sqlite3_stmt* selMax = nullptr; int64_t nextSeq = 1;
        const char* selMaxSQL = "SELECT MAX(VersionSeq) FROM ItemVersions WHERE ItemID = ?;";
        if(sqlite3_prepare_v2(db, selMaxSQL, -1, &selMax, nullptr) == SQLITE_OK){ sqlite3_bind_int64(selMax,1,cr.ItemID); if(sqlite3_step(selMax) == SQLITE_ROW){ if(sqlite3_column_type(selMax,0) != SQLITE_NULL) nextSeq = sqlite3_column_int64(selMax,0) + 1; } }
        if(selMax) sqlite3_finalize(selMax);
        sqlite3_stmt* insV = nullptr; const char* insVSQL = "INSERT INTO ItemVersions (ItemID, VersionSeq, RevisionID, CreatedAt) VALUES (?, ?, ?, ?);";
        if(sqlite3_prepare_v2(db, insVSQL, -1, &insV, nullptr) == SQLITE_OK){ sqlite3_bind_int64(insV,1,cr.ItemID); sqlite3_bind_int64(insV,2,nextSeq); sqlite3_bind_text(insV,3,mergeRevID.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_int64(insV,4, static_cast<sqlite3_int64>(std::time(nullptr))); sqlite3_step(insV); }
        if(insV) sqlite3_finalize(insV);
        sqlite3_stmt* upHead = nullptr; const char* upHeadSQL = "UPDATE VaultItems SET VersionSeq = ?, HeadRevision = ? WHERE ID = ?;";
        if(sqlite3_prepare_v2(db, upHeadSQL, -1, &upHead, nullptr) == SQLITE_OK){ sqlite3_bind_int64(upHead,1,nextSeq); sqlite3_bind_text(upHead,2,mergeRevID.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_int64(upHead,3,cr.ItemID); sqlite3_step(upHead); }
        if(upHead) sqlite3_finalize(upHead);
    } else {
        // just apply merged values directly
        std::string upd = "UPDATE VaultItems SET ";
        bool first = true;
        for(auto &p : mergedValues){ if(!first) upd += ", "; first = false; upd += p.first + " = ?"; }
        upd += " WHERE ID = ?;";
        sqlite3_stmt* upst = nullptr;
        if(sqlite3_prepare_v2(db, upd.c_str(), -1, &upst, nullptr) == SQLITE_OK){
            int idx = 1; for(auto &p : mergedValues){ sqlite3_bind_text(upst, idx++, p.second.c_str(), -1, SQLITE_TRANSIENT); }
            sqlite3_bind_int64(upst, idx, cr.ItemID);
            sqlite3_step(upst);
        }
        if(upst) sqlite3_finalize(upst);
    }

    // mark conflict resolved
    sqlite3_stmt* res = nullptr;
    const char* resSQL = "UPDATE Conflicts SET Status = 'resolved', ResolvedByAdminUserID = ?, ResolvedAt = ?, ResolutionPayload = ? WHERE ConflictID = ?;";
    if(sqlite3_prepare_v2(db, resSQL, -1, &res, nullptr) == SQLITE_OK){
        sqlite3_bind_int64(res,1,adminUserID);
        sqlite3_bind_int64(res,2, static_cast<sqlite3_int64>(std::time(nullptr)));
        sqlite3_bind_text(res,3,resolutionSummary.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(res,4,conflictID.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(res);
    }
    if(res) sqlite3_finalize(res);

    return true;
}

} // namespace LoreBook

