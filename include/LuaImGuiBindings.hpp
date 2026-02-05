#pragma once

extern "C" {
#include <lua.h>
}

// Register minimal ImGui UI bindings into Lua state
void registerLuaImGuiBindings(lua_State *L);
