#pragma once

extern "C" {
#include <lua.h>
#include <lauxlib.h>
}

#include <string>
#include <vector>

namespace LuaBindingDocsUtil {
    std::vector<std::string> listMissingDocs(lua_State *L, const std::string &tableName);
    bool verifyTableHasDocs(lua_State *L, const std::string &tableName);
    void enforceTableHasDocs(lua_State *L, const std::string &tableName);
}
