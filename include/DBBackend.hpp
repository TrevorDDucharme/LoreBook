#pragma once
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

// Minimal DB backend abstraction used by Vault
namespace LoreBook {

struct DBConnectionInfo {
    enum class Backend { SQLite, MySQL, MariaDB };
    Backend backend = Backend::SQLite;
    // SQLite
    std::string sqlite_dir;
    std::string sqlite_filename;
    // MySQL / MariaDB (shared fields, both supported)
    std::string mysql_host;
    int mysql_port = 3306;
    std::string mysql_db;
    std::string mysql_user;
    std::string mysql_password;
    bool mysql_use_ssl = false;
    std::string mysql_ca_file;
};

struct IResultSet {
    virtual ~IResultSet() = default;
    virtual bool next() = 0;
    virtual int64_t getInt64(int idx) = 0;
    virtual int getInt(int idx) = 0;
    virtual std::string getString(int idx) = 0;
    virtual std::vector<uint8_t> getBlob(int idx) = 0;
    virtual bool isNull(int idx) = 0;
};

struct IStatement {
    virtual ~IStatement() = default;
    virtual void bindInt(int idx, int64_t v) = 0;
    virtual void bindInt32(int idx, int32_t v) = 0;
    virtual void bindString(int idx, const std::string &s) = 0;
    virtual void bindBlob(int idx, const void* data, size_t size) = 0;
    virtual void bindNull(int idx) = 0;
    virtual bool execute() = 0; // for INSERT/UPDATE/DELETE
    virtual std::unique_ptr<IResultSet> executeQuery() = 0; // for SELECT
};

struct IDBBackend {
    virtual ~IDBBackend() = default;
    virtual bool open(const DBConnectionInfo &info, std::string *outError = nullptr) = 0;
    virtual void close() = 0;
    virtual bool isOpen() const = 0;
    virtual bool execute(const std::string &sql, std::string *outError = nullptr) = 0; // DDL / simple exec
    virtual std::unique_ptr<IStatement> prepare(const std::string &sql, std::string *outError = nullptr) = 0;
    virtual void beginTransaction() = 0;
    virtual void commit() = 0;
    virtual void rollback() = 0;
    virtual int64_t lastInsertId() = 0;

    // Introspection helpers for migrations and portability
    virtual bool hasColumn(const std::string &table, const std::string &column) = 0;

    // Full-text search support
    virtual bool supportsFullText() const = 0;
    // Ensure that the backend has a full-text index on `table`.`column`. Returns true on success.
    virtual bool ensureFullTextIndex(const std::string &table, const std::string &column, std::string *outError = nullptr) = 0;

    // Perform a full-text search and return matching row ids (backend-specific semantics)
    virtual std::vector<int64_t> fullTextSearch(const std::string &query, int limit = 50) = 0;
};

} // namespace LoreBook
