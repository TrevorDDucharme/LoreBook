#include "LuaDocsTest.hpp"
#include "LuaBindingDocsUtil.hpp"
#include "LuaImGuiBindings.hpp"
#include "LuaCanvasBindings.hpp"
#include "LuaVaultBindings.hpp"
#include <plog/Log.h>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

#include <stdexcept>
#include <imgui.h> // for ImVec2

void RunLuaDocChecks()
{
    // Create a temporary lua state and register the bindings used in the app.
    lua_State *L = luaL_newstate();
    if (!L)
    {
        PLOGE << "Lua docs test: failed to create lua state";
        throw std::runtime_error("failed to create lua state");
    }
    luaL_openlibs(L);

    try
    {
        // Register bindings (use harmless dummy args)
        registerLuaImGuiBindings(L);
        registerLuaCanvasBindings(L, ImVec2(0,0), 320, 240);
        // For vault bindings we pass nullptr (they store userdata but avoid calling into it at registration)
        registerLuaVaultBindings(L, nullptr);

        // Now verify / enforce docs for these public tables
        LuaBindingDocsUtil::enforceTableHasDocs(L, "ui");
        LuaBindingDocsUtil::enforceTableHasDocs(L, "canvas");
        LuaBindingDocsUtil::enforceTableHasDocs(L, "vault");
    }
    catch (const std::exception &e)
    {
        lua_close(L);
        PLOGE << "Lua docs test failed: " << e.what();
        throw;
    }

    lua_close(L);
    PLOGI << "Lua docs test passed";
}
