#pragma once

#include "../DBBackend.hpp"
#include <string>

namespace LoreBook {

// Skeleton MySQL backend - real implementation pending

struct MySQLImpl;

class MySQLBackend : public IDBBackend {
public:
    MySQLBackend();
    ~MySQLBackend() override;

    bool open(const DBConnectionInfo &info, std::string *outError) override;
    void close() override;
    bool isOpen() const override;

    bool execute(const std::string &sql, std::string *outError) override;
    std::unique_ptr<IStatement> prepare(const std::string &sql, std::string *outError) override;

    void beginTransaction() override;
    void commit() override;
    void rollback() override;

    int64_t lastInsertId() override;
    std::vector<int64_t> fullTextSearch(const std::string &query, int limit = 50) override;

    // Introspection & FTS support (added to IDBBackend)
    bool hasColumn(const std::string &table, const std::string &column) override;
    bool supportsFullText() const override;
    bool ensureFullTextIndex(const std::string &table, const std::string &column, std::string *outError = nullptr) override;

private:
    bool connected = false;
    std::unique_ptr<MySQLImpl> impl;
    // placeholder for connector-specific objects
};

} // namespace LoreBook
