#pragma once
#include <string>
#include <vector>
#include "Variable.hpp"
#include "Type.hpp"

namespace Lua {

// Represent a Lua function (free function or table entry)
struct Function
{
    std::string name;
    std::vector<Variable> parameters;
    Type returnType;
};

} // namespace Lua
