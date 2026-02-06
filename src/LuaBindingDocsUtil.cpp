#include "LuaBindingDocsUtil.hpp"
#include "LuaBindingDocs.hpp"
#include <plog/Log.h>
#include <sstream>
#include <stdexcept>

namespace LuaBindingDocsUtil {

std::vector<std::string> listMissingDocs(lua_State *L, const std::string &tableName)
{
    std::vector<std::string> missing;
    if (!L) return missing;
    lua_getglobal(L, tableName.c_str());
    if (!lua_istable(L, -1)) { lua_pop(L, 1); PLOGW << "listMissingDocs: " << tableName << " is not a table"; return missing; }
    lua_pushnil(L);
    while (lua_next(L, -2) != 0)
    {
        // key at -2, value at -1
        if (lua_type(L, -2) == LUA_TSTRING)
        {
            const char *name = lua_tostring(L, -2);
            if (name && name[0] != '_') {
                if (lua_isfunction(L, -1))
                {
                    std::string full = tableName + "." + name;
                    if (!LuaBindingDocs::get().hasDoc(full)) missing.push_back(full);
                }
            }
        }
        lua_pop(L, 1); // pop value, keep key
    }
    lua_pop(L, 1); // pop table
    return missing;
}

bool verifyTableHasDocs(lua_State *L, const std::string &tableName)
{
    auto missing = listMissingDocs(L, tableName);
    if (missing.empty()) return true;
    for (const auto &m : missing) PLOGE << "Lua binding missing documentation: " << m;
    return false;
}

void enforceTableHasDocs(lua_State *L, const std::string &tableName)
{
    auto missing = listMissingDocs(L, tableName);
    if (missing.empty()) return;
    std::ostringstream ss;
    ss << "Missing Lua docs for table '" << tableName << "': ";
    for (size_t i = 0; i < missing.size(); ++i) { if (i) ss << ", "; ss << missing[i]; }
    throw std::runtime_error(ss.str());
}

} // namespace LuaBindingDocsUtil
