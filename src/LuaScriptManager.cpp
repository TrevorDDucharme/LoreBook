#include "LuaScriptManager.hpp"
#include "LuaEngine.hpp"
#include "Vault.hpp"
#include "LuaVaultBindings.hpp"
#include "LuaImGuiBindings.hpp"
#include <plog/Log.h>
#include <chrono>

using namespace std::chrono;

LuaScriptManager::LuaScriptManager(Vault* vault) : m_vault(vault)
{
}

LuaScriptManager::~LuaScriptManager()
{
}

LuaEngine* LuaScriptManager::getOrCreateEngine(const std::string &scriptPath, const std::string &embedID, int64_t /*contextNodeID*/)
{
    std::string key = scriptPath + "::" + embedID;
    std::lock_guard<std::mutex> l(m_mutex);
    auto it = m_engines.find(key);
    if (it != m_engines.end())
    {
        it->second.lastUsed = (float)steady_clock::now().time_since_epoch().count();
        return it->second.engine.get();
    }

    // Load script text from vault
    std::string code = m_vault->getScript(scriptPath);
    if (code.empty())
    {
        PLOGW << "LuaScriptManager: script not found: " << scriptPath;
        return nullptr;
    }

    auto eng = std::make_unique<LuaEngine>();
    if (!eng->loadScript(code))
    {
        std::string err = eng->lastError();
        PLOGW << "LuaScriptManager: failed to load script: " << scriptPath << " err=" << err;
        // record last error for the caller/UI to inspect
        m_lastErrors[key] = err;
        return nullptr;
    }
    // Register basic bindings
    registerLuaVaultBindings(eng->L(), m_vault);
    registerLuaImGuiBindings(eng->L());

    LuaEngine* ePtr = eng.get();
    m_engines[key] = EngineInstance{std::move(eng), (float)steady_clock::now().time_since_epoch().count()};
    return ePtr;
}

void LuaScriptManager::invalidateScript(const std::string &scriptPath)
{
    std::lock_guard<std::mutex> l(m_mutex);
    for (auto it = m_engines.begin(); it != m_engines.end();) {
        if (it->first.find(scriptPath + "::") == 0) it = m_engines.erase(it);
        else ++it;
    }
    // Also clear any recorded load errors for this script
    for (auto it = m_lastErrors.begin(); it != m_lastErrors.end();) {
        if (it->first.find(scriptPath + "::") == 0) it = m_lastErrors.erase(it);
        else ++it;
    }
}

std::string LuaScriptManager::getLastError(const std::string &scriptPath, const std::string &embedID)
{
    std::string key = scriptPath + "::" + embedID;
    std::lock_guard<std::mutex> l(m_mutex);
    auto it = m_lastErrors.find(key);
    if (it != m_lastErrors.end())
        return it->second;
    return std::string();
}
