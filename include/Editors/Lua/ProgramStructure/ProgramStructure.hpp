#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include "Class.hpp"
#include "Method.hpp"

namespace Lua {

class ProgramStructure
{
public:
    ProgramStructure() = default;
    ~ProgramStructure() = default;

    void startWatching(const std::filesystem::path &p) { (void)p; }
    void stopWatching() {}

    // Minimal lookup for table/module
    const Table *findTable(const std::string &name) const
    {
        for (const auto &t : tables)
        {
            if (t.name == name) return &t;
        }
        return nullptr;
    }

    std::vector<Table> tables;
};

} // namespace Lua
#pragma once

