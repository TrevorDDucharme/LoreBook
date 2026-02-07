#include "LuaImGuiBindings.hpp"
#include "LuaBindingDocs.hpp"
#include "LuaBindingDocsUtil.hpp"
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

    // Documentation entries for ui bindings
        LuaBindingDocs::get().registerDoc("ui.text", "text(s)", "Render a line of text in the UI", R"(
function Config()
    return { type = "ui" }
end

function UI()
    ui.text('Hello from UI docs')
    if ui.button('Press me') then
        print('button pressed')
    end
end
)", __FILE__);
        LuaBindingDocs::get().registerDoc("ui.button", "button(label) -> bool", "Render a button and return true if clicked", R"(
function Config()
    return { type = "ui" }
end

function UI()
    if ui.button('Click me') then
        print('clicked')
    end
end
)", __FILE__);

    lua_setglobal(L, "ui");

    // Enforce docs presence
    LuaBindingDocsUtil::enforceTableHasDocs(L, "ui");
}
