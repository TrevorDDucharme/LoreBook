#include "LuaImGuiBindings.hpp"
#include <imgui.h>
#include <plog/Log.h>

static int l_ui_text(lua_State *L)
{
    const char *s = lua_tostring(L, 1);
    if (s) ImGui::TextUnformatted(s);
    return 0;
}

static int l_ui_button(lua_State *L)
{
    const char *label = lua_tostring(L, 1);
    bool clicked = false;
    if (label)
        clicked = ImGui::Button(label);
    lua_pushboolean(L, clicked);
    return 1;
}

void registerLuaImGuiBindings(lua_State *L)
{
    lua_newtable(L);
    lua_pushcfunction(L, l_ui_text); lua_setfield(L, -2, "text");
    lua_pushcfunction(L, l_ui_button); lua_setfield(L, -2, "button");
    lua_setglobal(L, "ui");
}
