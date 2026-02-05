#include "Vault.hpp"
#include "LuaScriptManager.hpp"
#include <plog/Log.h>

LuaScriptManager* Vault::getScriptManager()
{
    return scriptManager.get();
}

void Vault::initScriptManager()
{
    try {
        scriptManager = std::make_unique<LuaScriptManager>(this);
    } catch (...) {
        PLOGW << "Failed to create LuaScriptManager";
    }
}

int64_t Vault::storeScript(const std::string &name, const std::string &code)
{
    std::string ext = "vault://Scripts/" + sanitizeExternalPath(name);
    std::vector<uint8_t> data(code.begin(), code.end());
    return addAttachment(-1, name, "text/x-lua", data, ext);
}

std::string Vault::getScript(const std::string &name)
{
    std::string ext = "vault://Scripts/" + sanitizeExternalPath(name);
    int64_t aid = findAttachmentByExternalPath(ext);
    if (aid <= 0)
        return std::string();
    auto d = getAttachmentData(aid);
    return std::string(d.begin(), d.end());
}

std::vector<std::string> Vault::listScripts()
{
    std::vector<std::string> out;
    if (dbBackend && dbBackend->isOpen())
    {
        std::string err;
        auto stmt = dbBackend->prepare("SELECT ExternalPath FROM Attachments WHERE ExternalPath LIKE 'vault://Scripts/%';", &err);
        if (!stmt)
        {
            PLOGW << "listScripts prepare failed: " << err;
            return out;
        }
        auto rs = stmt->executeQuery();
        while (rs && rs->next())
        {
            out.push_back(rs->getString(0));
        }
        return out;
    }
    if (!dbConnection) return out;
    const char *q = "SELECT ExternalPath FROM Attachments WHERE ExternalPath LIKE 'vault://Scripts/%';";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(dbConnection, q, -1, &stmt, nullptr) == SQLITE_OK)
    {
        while (sqlite3_step(stmt) == SQLITE_ROW)
        {
            const unsigned char *t = sqlite3_column_text(stmt, 0);
            if (t) out.push_back(reinterpret_cast<const char *>(t));
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return out;
}