#include "Vault.hpp"
#include "db/SQLiteBackend.hpp"
#include "db/MySQLBackend.hpp"
#include <memory>

std::unique_ptr<Vault> Vault::Open(const VaultConfig& cfg, std::string* outError){
    using LoreBook::DBConnectionInfo;
    const DBConnectionInfo &ci = cfg.connInfo;
    if(ci.backend == DBConnectionInfo::Backend::SQLite){
        // Use existing filename/dir behavior - construct a Vault backed by SQLite on disk
        std::filesystem::path dir = ci.sqlite_dir.empty() ? std::filesystem::current_path() : std::filesystem::path(ci.sqlite_dir);
        std::string filename = ci.sqlite_filename.empty() ? "vault.db" : ci.sqlite_filename;
        try{
            auto v = std::make_unique<Vault>(dir, filename);
            if(outError) outError->clear();
            return v;
        } catch(const std::exception &ex){ if(outError) *outError = ex.what(); return nullptr; }
    } else if(ci.backend == DBConnectionInfo::Backend::MySQL || ci.backend == DBConnectionInfo::Backend::MariaDB){
        // Attempt to open a MySQL/MariaDB backend and return a Vault that wraps it
        auto mb = std::make_unique<LoreBook::MySQLBackend>();
        if(!mb->open(ci, outError)) return nullptr;

        // If requested, create / ensure schema exists on remote backend
        if(cfg.createIfMissing){
            std::string err;
            // VaultItems
            const char* createVaultItems = R"SQL(CREATE TABLE IF NOT EXISTS VaultItems (
                ID BIGINT AUTO_INCREMENT PRIMARY KEY,
                Name TEXT NOT NULL,
                Content MEDIUMTEXT,
                Tags TEXT,
                IsRoot TINYINT DEFAULT 0
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;)SQL";
            if(!mb->execute(createVaultItems, &err)) PLOGW << "MySQL: failed creating VaultItems: " << err;

            // VaultItemChildren
            const char* createChildren = R"SQL(CREATE TABLE IF NOT EXISTS VaultItemChildren (
                ParentID BIGINT,
                ChildID BIGINT,
                INDEX idx_parent (ParentID),
                INDEX idx_child (ChildID),
                FOREIGN KEY (ParentID) REFERENCES VaultItems(ID),
                FOREIGN KEY (ChildID) REFERENCES VaultItems(ID)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;)SQL";
            if(!mb->execute(createChildren, &err)) PLOGW << "MySQL: failed creating VaultItemChildren: " << err;

            // VaultNodeFilters
            const char* createFilters = R"SQL(CREATE TABLE IF NOT EXISTS VaultNodeFilters (
                NodeID BIGINT PRIMARY KEY,
                Mode VARCHAR(32),
                Tags TEXT,
                Expr TEXT
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;)SQL";
            if(!mb->execute(createFilters, &err)) PLOGW << "MySQL: failed creating VaultNodeFilters: " << err;

            // VaultMeta
            const char* createMeta = R"SQL(CREATE TABLE IF NOT EXISTS VaultMeta (
                `Key` VARCHAR(255) PRIMARY KEY,
                `Value` TEXT
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;)SQL";
            if(!mb->execute(createMeta, &err)) PLOGW << "MySQL: failed creating VaultMeta: " << err;

            // Attachments
            // Use a prefix index on ExternalPath to avoid exceeding InnoDB index key size when using utf8mb4
            const char* createAttachments = R"SQL(CREATE TABLE IF NOT EXISTS Attachments (
                ID BIGINT AUTO_INCREMENT PRIMARY KEY,
                ItemID BIGINT,
                Name VARCHAR(512),
                MimeType VARCHAR(128),
                Data LONGBLOB,
                ExternalPath VARCHAR(1024),
                Size BIGINT,
                CreatedAt BIGINT DEFAULT (UNIX_TIMESTAMP()),
                DisplayWidth INT,
                DisplayHeight INT,
                INDEX idx_Attachments_ExternalPath (ExternalPath(255)),
                FOREIGN KEY (ItemID) REFERENCES VaultItems(ID)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;)SQL";
            if(!mb->execute(createAttachments, &err)) PLOGW << "MySQL: failed creating Attachments: " << err;

            // Users
            const char* createUsers = R"SQL(CREATE TABLE IF NOT EXISTS Users (
                ID BIGINT AUTO_INCREMENT PRIMARY KEY,
                Username VARCHAR(256) NOT NULL UNIQUE,
                DisplayName VARCHAR(256),
                PasswordHash TEXT,
                Salt TEXT,
                Iterations INT DEFAULT 100000,
                IsAdmin TINYINT DEFAULT 0,
                CreatedAt BIGINT DEFAULT (UNIX_TIMESTAMP())
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;)SQL";
            if(!mb->execute(createUsers, &err)) PLOGW << "MySQL: failed creating Users: " << err;

            // ItemPermissions
            const char* createPerms = R"SQL(CREATE TABLE IF NOT EXISTS ItemPermissions (
                ID BIGINT AUTO_INCREMENT PRIMARY KEY,
                ItemID BIGINT NOT NULL,
                UserID BIGINT NOT NULL,
                Level INT NOT NULL DEFAULT 0,
                CreatedAt BIGINT DEFAULT (UNIX_TIMESTAMP()),
                UNIQUE KEY ux_item_user (ItemID, UserID),
                INDEX idx_ItemPermissions_ItemUser (ItemID, UserID),
                FOREIGN KEY (ItemID) REFERENCES VaultItems(ID),
                FOREIGN KEY (UserID) REFERENCES Users(ID)
            ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;)SQL";
            if(!mb->execute(createPerms, &err)) PLOGW << "MySQL: failed creating ItemPermissions: " << err;

            // Ensure schema version entry
            const char* insertSchemaVer = "INSERT INTO VaultMeta (`Key`,`Value`) VALUES ('SchemaVersion','3') ON DUPLICATE KEY UPDATE `Value`='3';";
            if(!mb->execute(insertSchemaVer, &err)) PLOGW << "MySQL: failed inserting SchemaVersion: " << err;
        }

        auto v = std::make_unique<Vault>(std::move(mb), ci.mysql_db.empty() ? std::string("remote") : ci.mysql_db);
        if(outError) outError->clear();
        return v;
    }
    if(outError) *outError = "Unknown backend type";
    return nullptr;
}
