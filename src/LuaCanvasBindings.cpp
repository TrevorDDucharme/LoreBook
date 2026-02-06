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
    int w = (int)lua_tointeger(L, lua_upvalueindex(1));
    lua_pushinteger(L, w);
    return 1;
}
static int l_canvas_height(lua_State *L)
{
    int h = (int)lua_tointeger(L, lua_upvalueindex(1));
    lua_pushinteger(L, h);
    return 1;
}

static int l_canvas_circle(lua_State *L)
{
    // upvalues: origin.x, origin.y
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

    // increment canvas.draw_count
    lua_getglobal(L, "canvas");
    if (lua_istable(L, -1))
    {
        lua_getfield(L, -1, "draw_count");
        int c = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        c++;
        lua_pushinteger(L, c);
        lua_setfield(L, -2, "draw_count");
    }
    lua_pop(L, 1);
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

    // increment canvas.draw_count
    lua_getglobal(L, "canvas");
    if (lua_istable(L, -1))
    {
        lua_getfield(L, -1, "draw_count");
        int c = (int)lua_tointeger(L, -1);
        lua_pop(L, 1);
        c++;
        lua_pushinteger(L, c);
        lua_setfield(L, -2, "draw_count");
    }
    lua_pop(L, 1);
    return 0;
}

void registerLuaCanvasBindings(lua_State *L, ImVec2 origin, int width, int height)
{
    // Create a new canvas table with small closures capturing the required upvalues.
    lua_newtable(L);

    // initialize a draw_count field we can increment to detect whether Render() actually drew anything
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "draw_count");

    // width
    lua_pushinteger(L, width);
    lua_pushcclosure(L, l_canvas_width, 1);
    lua_setfield(L, -2, "width");

    // height
    lua_pushinteger(L, height);
    lua_pushcclosure(L, l_canvas_height, 1);
    lua_setfield(L, -2, "height");

    // circle (captures origin.x and origin.y)
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushcclosure(L, l_canvas_circle, 2);
    lua_setfield(L, -2, "circle");

    // text (captures origin.x and origin.y)
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushcclosure(L, l_canvas_text, 2);
    lua_setfield(L, -2, "text");

    lua_setglobal(L, "canvas");
}
