#pragma once
#include <string>
#include <vector>
#include "Method.hpp"
#include "Variable.hpp"

namespace Lua {

// Lua uses tables/modules instead of classical classes. Represent common
// table-like structures that contain functions and fields.
struct Table
{
    std::string name;
    std::vector<Function> functions; // functions stored on the table
    std::vector<Variable> fields;
};

} // namespace Lua
