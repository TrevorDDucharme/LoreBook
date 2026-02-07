#pragma once

extern "C" {
#include <lua.h>
}
#include <imgui.h>
#include <string>

// Bind canvas drawing helpers into the given Lua state for a canvas region positioned at 'origin' with size
void registerLuaCanvasBindings(lua_State *L, ImVec2 origin, int width, int height);
// Register OpenGL-backed canvas bindings. The embedID identifies the canvas instance
void registerLuaGLCanvasBindings(lua_State *L, const std::string &embedID, unsigned int textureId, int width, int height);
// Flush any pending canvas draws for an embed (uploads VBO and issues GL draws).
void flushLuaCanvasForEmbed(lua_State *L, const std::string &embedID);