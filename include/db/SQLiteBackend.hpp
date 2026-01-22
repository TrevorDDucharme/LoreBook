#pragma once
#include "DBBackend.hpp"
#include <sqlite3.h>
#include <string>
#include <memory>

namespace LoreBook {

class SQLiteResultSet;
class SQLiteStatement;

class SQLiteBackend : public IDBBackend {
public:
    SQLiteBackend();
    ~SQLiteBackend() override;

    bool open(const DBConnectionInfo &info, std::string *outError = nullptr) override;
    void close() override;
    bool isOpen() const override;
    bool execute(const std::string &sql, std::string *outError = nullptr) override;
    std::unique_ptr<IStatement> prepare(const std::string &sql, std::string *outError = nullptr) override;
    void beginTransaction() override;
    void commit() override;
    void rollback() override;
    int64_t lastInsertId() override;
    std::vector<int64_t> fullTextSearch(const std::string &query, int limit = 50) override;

    // Introspection & FTS support (added to IDBBackend)
    bool hasColumn(const std::string &table, const std::string &column) override;
    bool supportsFullText() const override;
    bool ensureFullTextIndex(const std::string &table, const std::string &column, std::string *outError = nullptr) override;

    // Expose raw sqlite pointer for compatibility while refactoring
    sqlite3* getRawDb() const { return db; }

private:
    sqlite3* db = nullptr;
};

} // namespace LoreBook
