#include "db/SQLiteBackend.hpp"
#include <plog/Log.h>
#include <cstring>

namespace LoreBook {

SQLiteBackend::SQLiteBackend() = default;
SQLiteBackend::~SQLiteBackend(){ close(); }

bool SQLiteBackend::open(const DBConnectionInfo &info, std::string *outError){
    if(db) close();
    std::string path;
    if(!info.sqlite_dir.empty()) path = info.sqlite_dir + "/" + info.sqlite_filename;
    else path = info.sqlite_filename;
    if(sqlite3_open(path.c_str(), &db) != SQLITE_OK){
        if(outError) *outError = sqlite3_errmsg(db ? db : nullptr);
        if(db){ sqlite3_close(db); db = nullptr; }
        return false;
    }
    // set WAL journal and busy timeout recommended for multi-thread usage
    sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    sqlite3_busy_timeout(db, 5000);
    PLOGI << "SQLiteBackend: opened " << path;
    return true;
}

void SQLiteBackend::close(){ if(db) { sqlite3_close(db); db = nullptr; } }

bool SQLiteBackend::isOpen() const{ return db != nullptr; }

bool SQLiteBackend::execute(const std::string &sql, std::string *outError){
    if(!db){ if(outError) *outError = "DB not open"; return false; }
    char* err = nullptr;
    if(sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &err) != SQLITE_OK){ if(outError && err) *outError = err; if(err) sqlite3_free(err); return false; }
    return true;
}

class SQLiteStmtWrapper : public IStatement {
public:
    SQLiteStmtWrapper(sqlite3* db_, sqlite3_stmt* s_) : db(db_), stmt(s_) {}
    ~SQLiteStmtWrapper(){ if(stmt) sqlite3_finalize(stmt); }
    void bindInt(int idx, int64_t v) override { sqlite3_bind_int64(stmt, idx, v); }
    void bindInt32(int idx, int32_t v) override { sqlite3_bind_int(stmt, idx, v); }
    void bindString(int idx, const std::string &s) override { sqlite3_bind_text(stmt, idx, s.c_str(), -1, SQLITE_TRANSIENT); }
    void bindBlob(int idx, const void* data, size_t size) override { sqlite3_bind_blob(stmt, idx, data, static_cast<int>(size), SQLITE_TRANSIENT); }
    void bindNull(int idx) override { sqlite3_bind_null(stmt, idx); }
    bool execute() override { int r = sqlite3_step(stmt); if(r == SQLITE_DONE) return true; return false; }
    std::unique_ptr<IResultSet> executeQuery() override;
private:
    sqlite3* db;
    sqlite3_stmt* stmt;
};

class SQLiteResultSetImpl : public IResultSet {
public:
    SQLiteResultSetImpl(sqlite3_stmt* s) : stmt(s) {}
    ~SQLiteResultSetImpl(){ if(stmt) sqlite3_reset(stmt); /* do not finalize here, statement owns it */ }
    bool next() override { int r = sqlite3_step(stmt); return r == SQLITE_ROW; }
    int64_t getInt64(int idx) override { return sqlite3_column_int64(stmt, idx); }
    int getInt(int idx) override { return sqlite3_column_int(stmt, idx); }
    std::string getString(int idx) override { const unsigned char* t = sqlite3_column_text(stmt, idx); return t ? reinterpret_cast<const char*>(t) : std::string(); }
    std::vector<uint8_t> getBlob(int idx) override { const void* b = sqlite3_column_blob(stmt, idx); int sz = sqlite3_column_bytes(stmt, idx); if(!b || sz<=0) return {}; return std::vector<uint8_t>((uint8_t*)b, (uint8_t*)b + sz); }
    bool isNull(int idx) override { return sqlite3_column_type(stmt, idx) == SQLITE_NULL; }
private:
    sqlite3_stmt* stmt;
};

std::unique_ptr<IResultSet> SQLiteStmtWrapper::executeQuery(){ return std::make_unique<SQLiteResultSetImpl>(stmt); }

std::unique_ptr<IStatement> SQLiteBackend::prepare(const std::string &sql, std::string *outError){
    if(!db){ if(outError) *outError = "DB not open"; return nullptr; }
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK){ if(outError) *outError = sqlite3_errmsg(db); if(stmt) sqlite3_finalize(stmt); return nullptr; }
    return std::make_unique<SQLiteStmtWrapper>(db, stmt);
}

void SQLiteBackend::beginTransaction(){ if(db) sqlite3_exec(db, "BEGIN TRANSACTION;", nullptr, nullptr, nullptr); }
void SQLiteBackend::commit(){ if(db) sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr); }
void SQLiteBackend::rollback(){ if(db) sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr); }

int64_t SQLiteBackend::lastInsertId(){ if(!db) return -1; return sqlite3_last_insert_rowid(db); }

bool SQLiteBackend::hasColumn(const std::string &table, const std::string &column){
    if(!db) return false;
    std::string q = "PRAGMA table_info('" + table + "');";
    sqlite3_stmt* stmt = nullptr;
    bool found = false;
    if(sqlite3_prepare_v2(db, q.c_str(), -1, &stmt, nullptr) == SQLITE_OK){
        while(sqlite3_step(stmt) == SQLITE_ROW){
            const unsigned char* name = sqlite3_column_text(stmt,1);
            if(name){ std::string cname = reinterpret_cast<const char*>(name); if(cname == column){ found = true; break; } }
        }
    }
    if(stmt) sqlite3_finalize(stmt);
    return found;
}

bool SQLiteBackend::supportsFullText() const { return true; }

bool SQLiteBackend::ensureFullTextIndex(const std::string &table, const std::string &column, std::string *outError){
    if(!db){ if(outError) *outError = "DB not open"; return false; }
    // Special-case VaultItems to create combined FTS table matching existing behavior
    if(table == "VaultItems"){
        const char* createFTS = "CREATE VIRTUAL TABLE IF NOT EXISTS VaultItemsFTS USING fts5(Name, Content, Tags);";
        char* err = nullptr;
        if(sqlite3_exec(db, createFTS, nullptr, nullptr, &err) != SQLITE_OK){ if(err){ if(outError) *outError = err; sqlite3_free(err); } return false; }
        const char* clear = "DELETE FROM VaultItemsFTS;";
        if(sqlite3_exec(db, clear, nullptr, nullptr, &err) != SQLITE_OK){ if(err){ if(outError) *outError = err; sqlite3_free(err); } return false; }
        const char* insertSQL = "INSERT INTO VaultItemsFTS(rowid, Name, Content, Tags) SELECT ID, Name, Content, Tags FROM VaultItems;";
        if(sqlite3_exec(db, insertSQL, nullptr, nullptr, &err) != SQLITE_OK){ if(err){ if(outError) *outError = err; sqlite3_free(err); } return false; }
        return true;
    }
    // Generic: create a simple FTS table for the column
    if(!hasColumn(table, column)){ if(outError) *outError = "Column does not exist"; return false; }
    std::string ftsName = table + "_fts_" + column;
    std::string create = "CREATE VIRTUAL TABLE IF NOT EXISTS '" + ftsName + "' USING fts5('" + column + "');";
    char* err = nullptr;
    if(sqlite3_exec(db, create.c_str(), nullptr, nullptr, &err) != SQLITE_OK){ if(err){ if(outError) *outError = err; sqlite3_free(err); } return false; }
    std::string clear = "DELETE FROM '" + ftsName + "';";
    if(sqlite3_exec(db, clear.c_str(), nullptr, nullptr, &err) != SQLITE_OK){ /* ignore clear errors */ if(err){ sqlite3_free(err); } }
    std::string insertSQL = "INSERT INTO '" + ftsName + "'(rowid, '" + column + "') SELECT ID, '" + column + "' FROM '" + table + "';";
    if(sqlite3_exec(db, insertSQL.c_str(), nullptr, nullptr, &err) != SQLITE_OK){ if(err){ if(outError) *outError = err; sqlite3_free(err); } return false; }
    return true;
}

std::vector<int64_t> SQLiteBackend::fullTextSearch(const std::string &query, int limit){
    // Basic fallback: use LIKE against Name/Content/Tags
    std::vector<int64_t> out;
    if(!db) return out;
    std::string q = "SELECT ID FROM VaultItems WHERE lower(Name) LIKE '%' || ? || '%' OR lower(Content) LIKE '%' || ? || '%' OR lower(Tags) LIKE '%' || ? || '%' LIMIT ";
    q += std::to_string(limit);
    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(db, q.c_str(), -1, &stmt, nullptr) == SQLITE_OK){
        sqlite3_bind_text(stmt,1,query.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,2,query.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,3,query.c_str(),-1,SQLITE_TRANSIENT);
        while(sqlite3_step(stmt) == SQLITE_ROW){ out.push_back(sqlite3_column_int64(stmt,0)); }
    }
    if(stmt) sqlite3_finalize(stmt);
    return out;
}

} // namespace LoreBook
