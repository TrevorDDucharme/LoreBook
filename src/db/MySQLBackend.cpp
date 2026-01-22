#include "db/MySQLBackend.hpp"
#include <plog/Log.h>
#include <sstream>
#include <iomanip>
#include <vector>
#include <algorithm>

#if __has_include(<mysqlx/xdevapi.h>)
#include <mysqlx/xdevapi.h>
#define HAVE_MYSQLX 1
#else
#warning "mysqlx/xdevapi.h not found; MySQL/MariaDB backend will be built as a stub"
#define HAVE_MYSQLX 0
#endif

namespace LoreBook {

#if HAVE_MYSQLX
struct MySQLImpl {
    std::unique_ptr<mysqlx::Session> sess;
    std::string dbName; // store DB name for INFORMATION_SCHEMA queries
};

MySQLBackend::MySQLBackend(){ impl = std::make_unique<MySQLImpl>(); }
MySQLBackend::~MySQLBackend(){ close(); }

bool MySQLBackend::open(const DBConnectionInfo &info, std::string *outError){
    try{
        // Use the Session initializer with options
        impl->sess = std::make_unique<mysqlx::Session>(
            mysqlx::SessionOption::HOST, info.mysql_host,
            mysqlx::SessionOption::PORT, info.mysql_port,
            mysqlx::SessionOption::USER, info.mysql_user,
            mysqlx::SessionOption::PWD, info.mysql_password,
            mysqlx::SessionOption::DB, info.mysql_db
        );
        impl->dbName = info.mysql_db;
        // quick health check
        auto sess = impl->sess.get();
        mysqlx::SqlResult r = sess->sql("SELECT 1").execute();
        auto row = r.fetchOne();
        if(!row.isNull()) {
            connected = true; 
            PLOGI << "MySQLBackend: connected to " << info.mysql_host << ":" << info.mysql_port << ", db=" << info.mysql_db;
            if(outError) outError->clear();
            return true;
        }
        if(outError) *outError = "MySQL: health query returned no rows";
        return false;
    } catch(const mysqlx::Error &e){
        std::string msg = e.what();
        if(msg.find("unexpected message") != std::string::npos || msg.find("Unexpected message") != std::string::npos){
            msg += " -- This often means the server is not speaking the X Protocol on this port (are you connecting to 3306 instead of the X plugin port 33060?), or the mysqlx plugin is not enabled on the server.";
        }
        if(outError) *outError = msg; return false;
    } catch(const std::exception &ex){
        std::string msg = ex.what();
        if(msg.find("unexpected message") != std::string::npos){
            msg += " -- This often means the server is not speaking the X Protocol on this port (are you connecting to 3306 instead of the X plugin port 33060?), or the mysqlx plugin is not enabled on the server.";
        }
        if(outError) *outError = msg; return false;
    }
}
#else
struct MySQLImpl {
    // stub
};

MySQLBackend::MySQLBackend(){ impl = std::make_unique<MySQLImpl>(); }
MySQLBackend::~MySQLBackend(){ close(); }

bool MySQLBackend::open(const DBConnectionInfo &info, std::string *outError){
    if(outError) *outError = "MySQL/MariaDB connector headers not available at build time";
    return false;
}
#endif

#if HAVE_MYSQLX
// Introspection: check if a column exists in the connected database
bool MySQLBackend::hasColumn(const std::string &table, const std::string &column){
    if(!isOpen()) return false;
    try{
        std::string q = "SELECT COUNT(*) FROM INFORMATION_SCHEMA.COLUMNS WHERE TABLE_SCHEMA = '" + impl->dbName + "' AND TABLE_NAME = '" + table + "' AND COLUMN_NAME = '" + column + "'";
        auto r = impl->sess->sql(q).execute();
        auto row = r.fetchOne();
        if(!row.isNull()){
            int64_t cnt = row[0];
            return cnt > 0;
        }
    } catch(...){ }
    return false;
}

bool MySQLBackend::supportsFullText() const { return true; }

bool MySQLBackend::ensureFullTextIndex(const std::string &table, const std::string &column, std::string *outError){
    if(!isOpen()){ if(outError) *outError = "Not connected"; return false; }
    if(!hasColumn(table, column)){ if(outError) *outError = "Column does not exist"; return false; }
    try{
        std::string qCheck = "SELECT COUNT(*) FROM INFORMATION_SCHEMA.STATISTICS WHERE TABLE_SCHEMA = '" + impl->dbName + "' AND TABLE_NAME = '" + table + "' AND INDEX_TYPE = 'FULLTEXT' AND COLUMN_NAME = '" + column + "'";
        auto r = impl->sess->sql(qCheck).execute();
        auto row = r.fetchOne();
        if(!row.isNull()){
            int64_t cnt = row[0];
            if(cnt > 0) return true; // already has index
        }
        std::string idxName = "ft_" + table + "_" + column;
        std::string qCreate = "CREATE FULLTEXT INDEX `" + idxName + "` ON `" + table + "`(`" + column + "`)";
        impl->sess->sql(qCreate).execute();
        return true;
    } catch(const mysqlx::Error &e){ if(outError) *outError = e.what(); return false; }
    catch(const std::exception &ex){ if(outError) *outError = ex.what(); return false; }
}

int64_t MySQLBackend::lastInsertId(){
    if(!isOpen()) return -1;
    try{
        auto r = impl->sess->sql("SELECT LAST_INSERT_ID()").execute();
        auto row = r.fetchOne();
        if(!row.isNull()) return static_cast<int64_t>(row[0]);
    } catch(...){ }
    return -1;
}

void MySQLBackend::close(){ if(impl && impl->sess) { try { impl->sess->close(); } catch(...){} impl->sess.reset(); } connected = false; }

bool MySQLBackend::isOpen() const { return connected && impl && impl->sess; }

namespace {
// Escape string for use as single-quoted SQL literal (basic escaping)
static std::string escapeSqlString(const std::string &s){ std::string out; out.reserve(s.size()+2); out.push_back('\''); for(char c: s){ if(c == '\\') { out.append("\\\\"); } else if(c == '\'') { out.append("\\\'"); } else { out.push_back(c); } } out.push_back('\''); return out; }
// Convert binary blob to MySQL hex literal: x'deadbeef'
static std::string blobToHex(const void* data, size_t size){ const unsigned char* p = reinterpret_cast<const unsigned char*>(data); std::ostringstream oss; oss << "x'" << std::hex << std::setfill('0'); for(size_t i=0;i<size;++i){ oss << std::setw(2) << static_cast<int>(p[i]); } oss << "'"; return oss.str(); }
// Escape for LIKE pattern (escape %, _ and \\) and wrap in quotes
static std::string escapeLikePattern(const std::string &s){ std::string tmp; tmp.reserve(s.size()); for(char c: s){ if(c == '\\' || c == '%' || c == '_') { tmp.push_back('\\'); tmp.push_back(c); } else tmp.push_back(c); } return std::string("'%") + tmp + "%'"; }

// Simple ResultSet wrapper for mysqlx
class MySQLResultSetImpl : public IResultSet {
public:
    MySQLResultSetImpl(mysqlx::SqlResult &&r) : result(std::move(r)) {}
    bool next() override {
        try{
            row = result.fetchOne();
            if(row.isNull()) return false;
            return true;
        }catch(...){ return false; }
    }
    int64_t getInt64(int idx) override {
        try{
            if(row.isNull()) return 0;
            // Prefer numeric get if available
            try{ return row[idx].get<int64_t>(); } catch(...){ }
            try{ std::string s = row[idx].get<std::string>(); if(s.empty()) return 0; return std::stoll(s); } catch(...){ return 0; }
        }catch(...){ return 0; }
    }
    int getInt(int idx) override {
        try{
            if(row.isNull()) return 0;
            try{ return static_cast<int>(row[idx].get<int64_t>()); } catch(...){ }
            try{ std::string s = row[idx].get<std::string>(); if(s.empty()) return 0; return std::stoi(s); } catch(...){ return 0; }
        }catch(...){ return 0; }
    }
    std::string getString(int idx) override { try{ if(row.isNull()) return std::string(); return row[idx].get<std::string>(); } catch(...){ try{ auto v = row[idx].get<int64_t>(); return std::to_string(v); } catch(...){ return std::string(); } } }
    std::vector<uint8_t> getBlob(int idx) override { try{ if(row.isNull()) return {}; std::string s = row[idx].get<std::string>(); return std::vector<uint8_t>(s.begin(), s.end()); } catch(...){ return {}; } }
    bool isNull(int idx) override { try{ return row[idx].isNull(); } catch(...){ return true; } }
private:
    mysqlx::SqlResult result;
    mysqlx::Row row;
};

// Statement wrapper: use mysqlx prepared-style binding instead of textual substitution
class MySQLStmtWrapper : public IStatement {
public:
    MySQLStmtWrapper(MySQLImpl* impl_, const std::string &sql_) : impl(impl_), sql(sql_) {
        // count placeholders
        size_t cnt = std::count(sql.begin(), sql.end(), '?');
        bindVals.resize(cnt); // default-initialized mysqlx::Value (null)
    }
    void bindInt(int idx, int64_t v) override { if(idx>=1 && (size_t)idx<=bindVals.size()) bindVals[idx-1] = mysqlx::Value(v); }
    void bindInt32(int idx, int32_t v) override { if(idx>=1 && (size_t)idx<=bindVals.size()) bindVals[idx-1] = mysqlx::Value(static_cast<int64_t>(v)); }
    void bindString(int idx, const std::string &s) override { if(idx>=1 && (size_t)idx<=bindVals.size()) bindVals[idx-1] = mysqlx::Value(s); }
    void bindBlob(int idx, const void* data, size_t size) override { if(idx>=1 && (size_t)idx<=bindVals.size()) bindVals[idx-1] = mysqlx::Value(std::string(reinterpret_cast<const char*>(data), size)); }
    void bindNull(int idx) override { if(idx>=1 && (size_t)idx<=bindVals.size()) bindVals[idx-1] = mysqlx::Value(); }
    bool execute() override {
        try{
            mysqlx::SqlStatement st = impl->sess->sql(sql);
            for(auto &v : bindVals) st.bind(v);
            st.execute();
            return true;
        } catch(const mysqlx::Error &e){ PLOGE << "MySQLStmt execute error: " << e.what(); return false; }
    }
    std::unique_ptr<IResultSet> executeQuery() override {
        try{
            mysqlx::SqlStatement st = impl->sess->sql(sql);
            for(auto &v : bindVals) st.bind(v);
            auto r = st.execute();
            return std::make_unique<MySQLResultSetImpl>(std::move(r));
        }catch(const mysqlx::Error &e){ PLOGE << "MySQLStmt executeQuery error: " << e.what(); return nullptr; }
    }
private:
    MySQLImpl* impl;
    std::string sql;
    std::vector<mysqlx::Value> bindVals;
};
} // anonymous namespace

bool MySQLBackend::execute(const std::string &sql, std::string *outError){ if(!isOpen()){ if(outError) *outError = "Not connected"; return false; } try{ impl->sess->sql(sql).execute(); return true; } catch(const mysqlx::Error &e){ if(outError) *outError = e.what(); return false; } }
std::unique_ptr<IStatement> MySQLBackend::prepare(const std::string &sql, std::string *outError){ if(!isOpen()){ if(outError) *outError = "Not connected"; return nullptr; } try{ return std::make_unique<MySQLStmtWrapper>(impl.get(), sql); } catch(const std::exception &ex){ if(outError) *outError = ex.what(); return nullptr; } }

void MySQLBackend::beginTransaction(){ if(isOpen()){ try{ impl->sess->startTransaction(); } catch(...){} } }
void MySQLBackend::commit(){ if(isOpen()){ try{ impl->sess->commit(); } catch(...){} } }
void MySQLBackend::rollback(){ if(isOpen()){ try{ impl->sess->rollback(); } catch(...){} } }

std::vector<int64_t> MySQLBackend::fullTextSearch(const std::string &query, int limit){ std::vector<int64_t> out; if(!isOpen()) return out; try{ std::string q = "SELECT ID FROM VaultItems WHERE lower(Name) LIKE " + escapeLikePattern(query) + " OR lower(Content) LIKE " + escapeLikePattern(query) + " OR lower(Tags) LIKE " + escapeLikePattern(query) + " LIMIT " + std::to_string(limit); auto r = impl->sess->sql(q).execute(); while(true){ auto row = r.fetchOne(); if(row.isNull()) break; out.push_back(static_cast<int64_t>(row[0])); } }catch(...){ } return out; }

#else
// Stubs when connector headers are absent
bool MySQLBackend::hasColumn(const std::string &table, const std::string &column){ return false; }
bool MySQLBackend::supportsFullText() const { return false; }
bool MySQLBackend::ensureFullTextIndex(const std::string &table, const std::string &column, std::string *outError){ if(outError) *outError = "Connector not available"; return false; }
int64_t MySQLBackend::lastInsertId(){ return -1; }
void MySQLBackend::close(){ connected = false; }
bool MySQLBackend::isOpen() const { return false; }
bool MySQLBackend::execute(const std::string &sql, std::string *outError){ if(outError) *outError = "Connector not available"; return false; }
std::unique_ptr<IStatement> MySQLBackend::prepare(const std::string &sql, std::string *outError){ if(outError) *outError = "Connector not available"; return nullptr; }
void MySQLBackend::beginTransaction(){}
void MySQLBackend::commit(){}
void MySQLBackend::rollback(){}
std::vector<int64_t> MySQLBackend::fullTextSearch(const std::string &query, int limit){ return {}; }
#endif

} // namespace LoreBook
