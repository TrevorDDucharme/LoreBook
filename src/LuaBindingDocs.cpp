#include "LuaBindingDocs.hpp"
#include <plog/Log.h>
#include <sstream>
#include <stdexcept>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}


LuaBindingDocs &LuaBindingDocs::get()
{
    static LuaBindingDocs inst;
    return inst;
}

void LuaBindingDocs::registerDoc(const std::string &name, const std::string &signature, const std::string &summary, const std::string &example, const std::string &sourceFile)
{
    std::lock_guard<std::mutex> l(m_mutex);
    LuaBindingDocEntry e{ name, signature, summary, example, sourceFile };
    m_docs[name] = std::move(e);
}

bool LuaBindingDocs::hasDoc(const std::string &name) const
{
    std::lock_guard<std::mutex> l(m_mutex);
    return m_docs.find(name) != m_docs.end();
}

const LuaBindingDocEntry *LuaBindingDocs::getDoc(const std::string &name) const
{
    std::lock_guard<std::mutex> l(m_mutex);
    auto it = m_docs.find(name);
    if (it == m_docs.end()) return nullptr;
    return &it->second;
}

std::vector<LuaBindingDocEntry> LuaBindingDocs::listAll() const
{
    std::vector<LuaBindingDocEntry> out;
    std::lock_guard<std::mutex> l(m_mutex);
    out.reserve(m_docs.size());
    for (const auto &kv : m_docs) out.push_back(kv.second);
    return out;
}

std::vector<LuaBindingDocEntry> LuaBindingDocs::listWithPrefix(const std::string &prefix) const
{
    std::vector<LuaBindingDocEntry> out;
    std::lock_guard<std::mutex> l(m_mutex);
    for (const auto &kv : m_docs)
    {
        if (kv.first.rfind(prefix, 0) == 0)
            out.push_back(kv.second);
    }
    return out;
}

