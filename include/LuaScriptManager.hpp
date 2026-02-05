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

private:
    Vault* m_vault;
    std::mutex m_mutex;
    struct EngineInstance { std::unique_ptr<LuaEngine> engine; float lastUsed = 0.0f; };
    std::unordered_map<std::string, EngineInstance> m_engines;
};