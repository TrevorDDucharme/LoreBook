#pragma once
#include <string>
#include <memory>
#include <unordered_map>
#include <mutex>

class Vault;
class LuaEngine;

class LuaScriptManager {
public:
    LuaScriptManager(Vault* vault);
    ~LuaScriptManager();

    // key: scriptPath + "::" + embedID
    LuaEngine* getOrCreateEngine(const std::string &scriptPath, const std::string &embedID, int64_t contextNodeID);
    void invalidateScript(const std::string &scriptPath);

    // If a script failed to load, a diagnostic error message is recorded and can
    // be retrieved via this API (useful for presenting load errors in previews).
    std::string getLastError(const std::string &scriptPath, const std::string &embedID);

private:
    Vault* m_vault;
    mutable std::mutex m_mutex;
    struct EngineInstance { std::unique_ptr<LuaEngine> engine; float lastUsed = 0.0f; };
    std::unordered_map<std::string, EngineInstance> m_engines;

    // Map of last load errors for scripts (keyed by scriptPath + "::" + embedID)
    mutable std::unordered_map<std::string, std::string> m_lastErrors;
};