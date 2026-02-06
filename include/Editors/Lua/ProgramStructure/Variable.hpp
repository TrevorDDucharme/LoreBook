#pragma once
#include <string>
#include "Type.hpp"

namespace Lua {

struct Variable
{
    std::string name;
    Type type;
};

} // namespace Lua
