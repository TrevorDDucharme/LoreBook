#include "LuaCanvasBindings.hpp"
extern "C" {
#include <lauxlib.h>
}
#include <plog/Log.h>

// Helper: read color table {r,g,b,a} (0..1) or accept hex integer as first arg
static ImU32 parseColor(lua_State *L, int idx)
{
    if (lua_isnumber(L, idx))
    {
        unsigned int hex = (unsigned int)lua_tointeger(L, idx);
        return ImColor(((hex >> 16) & 0xFF)/255.0f, ((hex >> 8) & 0xFF)/255.0f, (hex & 0xFF)/255.0f, ((hex >> 24) & 0xFF)/255.0f);
    }
    if (lua_istable(L, idx))
    {
        lua_rawgeti(L, idx, 1); float r = (float)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_rawgeti(L, idx, 2); float g = (float)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_rawgeti(L, idx, 3); float b = (float)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_rawgeti(L, idx, 4); float a = 1.0f; if (lua_isnumber(L, -1)) a = (float)lua_tonumber(L, -1); lua_pop(L, 1);
        return ImColor(r,g,b,a);
    }
    return ImColor(1.0f,1.0f,1.0f,1.0f);
}

static int l_canvas_width(lua_State *L)
{
    int w = (int)lua_tointeger(L, lua_upvalueindex(2));
    lua_pushinteger(L, w);
    return 1;
}
static int l_canvas_height(lua_State *L)
{
    int h = (int)lua_tointeger(L, lua_upvalueindex(3));
    lua_pushinteger(L, h);
    return 1;
}

static int l_canvas_circle(lua_State *L)
{
    // upvalues: origin.x, origin.y, width, height
    float ox = (float)lua_tonumber(L, lua_upvalueindex(1));
    float oy = (float)lua_tonumber(L, lua_upvalueindex(2));
    float cx = (float)luaL_checknumber(L, 1);
    float cy = (float)luaL_checknumber(L, 2);
    float r = (float)luaL_checknumber(L, 3);
    ImU32 col = parseColor(L, 4);
    bool filled = lua_toboolean(L, 5);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p(ox + cx, oy + cy);
    if (filled)
        dl->AddCircleFilled(p, r, col);
    else
        dl->AddCircle(p, r, col);
    return 0;
}

static int l_canvas_text(lua_State *L)
{
    float ox = (float)lua_tonumber(L, lua_upvalueindex(1));
    float oy = (float)lua_tonumber(L, lua_upvalueindex(2));
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    const char *s = lua_tostring(L, 3);
    ImU32 col = parseColor(L, 4);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (s)
        dl->AddText(ImGui::GetFont(), ImGui::GetFontSize(), ImVec2(ox + x, oy + y), col, s);
    return 0;
}

void registerLuaCanvasBindings(lua_State *L, ImVec2 origin, int width, int height)
{
    // push upvalues: origin.x, origin.y, width, height
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushinteger(L, width);
    lua_pushinteger(L, height);

    lua_newtable(L);
    // width
    lua_pushvalue(L, -4); // origin.x (dummy for alignment)
    lua_pushvalue(L, -3); // origin.y (dummy)
    lua_pushvalue(L, -2); // width
    lua_pushcclosure(L, l_canvas_width, 1);
    lua_setfield(L, -2, "width");

    // height
    lua_pushvalue(L, -3); // origin.y (dummy)
    lua_pushvalue(L, -2); // width
    lua_pushvalue(L, -1); // height
    lua_pushcclosure(L, l_canvas_height, 1);
    lua_setfield(L, -2, "height");

    // circle
    // for circle we'll capture origin.x and origin.y as upvalues (1 & 2)
    lua_pushvalue(L, -6); // origin.x
    lua_pushvalue(L, -6); // origin.y (they get disrupted by previous pushes; push again reliably)
    lua_pushcclosure(L, l_canvas_circle, 2);
    lua_setfield(L, -2, "circle");

    // text
    lua_pushvalue(L, -8); // origin.x
    lua_pushvalue(L, -8); // origin.y
    lua_pushcclosure(L, l_canvas_text, 2);
    lua_setfield(L, -2, "text");

    lua_setglobal(L, "canvas");

    // cleanup leftover upvalues on stack
    // After setglobal, the table is popped. Remove the upvalues we pushed earlier
    lua_pop(L, 4);
}
