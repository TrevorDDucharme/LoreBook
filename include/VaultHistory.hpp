#pragma once
#include <string>
#include <map>
#include <vector>
#include <cstdint>
#include <sqlite3.h>
#include "DBBackend.hpp"

namespace LoreBook {

struct RevisionField {
    std::string fieldName;
    std::string oldValue;
    std::string newValue;
    std::string fieldDiff;
};

struct ConflictRecord {
    std::string ConflictID;
    int64_t ItemID;
    std::string FieldName;
    std::string BaseRevisionID;
    std::string LocalRevisionID;
    std::string RemoteRevisionID;
    int64_t OriginatorUserID;
    int64_t CreatedAt;
    std::string Status;
    // Resolved metadata
    int64_t ResolvedByAdminUserID = 0;
    int64_t ResolvedAt = 0;
    std::string ResolutionPayload;
};

class VaultHistory {
public:
    explicit VaultHistory(sqlite3* dbConnection);
    explicit VaultHistory(LoreBook::IDBBackend* backend);

    bool ensureSchema(std::string* outError = nullptr);

    // recordRevision returns the created RevisionID (UUID) or empty on failure
    std::string recordRevision(int64_t itemID, int64_t authorUserID, const std::string &revisionType,
                               const std::map<std::string, std::pair<std::string,std::string>> &fieldChanges,
                               const std::string &baseRevisionID = "");

    // After inserting a local revision, detect concurrent head mismatches and try to auto-merge or enqueue Conflicts
    // Returns a list of conflict IDs created (empty if auto-merged or fast-forward)
    std::vector<std::string> detectAndEnqueueConflicts(int64_t itemID, const std::string &localRevisionID, const std::string &baseRevisionID, const std::string &remoteRevisionID, int64_t originatorUserID);

    // List open conflicts
    std::vector<ConflictRecord> listOpenConflicts(int64_t originatorFilter = -1);

    // Get conflict details like base/local/remote field values for the given conflict
    ConflictRecord getConflictDetail(const std::string &conflictID);

    // Admin resolves a conflict by providing merged values per field; createMergeRevision will create a merge commit and apply it
    bool adminResolveConflict(const std::string &conflictID, int64_t adminUserID, const std::map<std::string,std::string> &mergedValues, const std::string &resolutionSummary, bool createMergeRevision = true);

    // Expose a helper to read a value for UI
    std::string getFieldValue(const std::string &revID, int64_t itemID, const std::string &fieldName);

private:
    sqlite3* db = nullptr;
    LoreBook::IDBBackend* backend = nullptr;
    std::string generateUUID();
};

} // namespace LoreBook
