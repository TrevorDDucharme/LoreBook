#pragma once

extern "C" {
#include <lua.h>
}

class Vault;

// Register minimal vault bindings into the provided Lua state and bind to a specific Vault instance
void registerLuaVaultBindings(lua_State *L, Vault *vault);