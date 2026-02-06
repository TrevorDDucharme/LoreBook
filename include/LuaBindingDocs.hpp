#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>



struct LuaBindingDocEntry {
    std::string name;        // fully-qualified, e.g., "canvas.circle"
    std::string signature;   // e.g., "circle(x,y,r,color,filled,thickness)"
    std::string summary;     // short description
    std::string example;     // optional example snippet
    std::string sourceFile;  // optional binding source file path, if known
};


class LuaBindingDocs {
public:
    static LuaBindingDocs &get();

    // Register a documentation entry for a binding
    void registerDoc(const std::string &name, const std::string &signature, const std::string &summary, const std::string &example = "", const std::string &sourceFile = "");

    // Query
    bool hasDoc(const std::string &name) const;
    const LuaBindingDocEntry *getDoc(const std::string &name) const;
    std::vector<LuaBindingDocEntry> listAll() const;
    std::vector<LuaBindingDocEntry> listWithPrefix(const std::string &prefix) const;

    // Runtime verification helpers (implemented in LuaBindingDocsUtil.hpp/.cpp)
    // Use LuaBindingDocs::get().registerDoc(...) to add docs.


private:
    mutable std::mutex m_mutex;
    std::unordered_map<std::string, LuaBindingDocEntry> m_docs;
};