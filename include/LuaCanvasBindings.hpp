#pragma once

extern "C" {
#include <lua.h>
}
#include <imgui.h>

// Bind canvas drawing helpers into the given Lua state for a canvas region positioned at 'origin' with size
void registerLuaCanvasBindings(lua_State *L, ImVec2 origin, int width, int height);