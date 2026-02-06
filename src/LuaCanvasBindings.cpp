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
    float thickness = lua_isnumber(L, 6) ? (float)lua_tonumber(L, 6) : 1.0f;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p(ox + cx, oy + cy);
    if (filled)
        dl->AddCircleFilled(p, r, col);
    else
        dl->AddCircle(p, r, col, 12, thickness);

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

static int l_canvas_rect(lua_State *L)
{
    float ox = (float)lua_tonumber(L, lua_upvalueindex(1));
    float oy = (float)lua_tonumber(L, lua_upvalueindex(2));
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float w = (float)luaL_checknumber(L, 3);
    float h = (float)luaL_checknumber(L, 4);
    ImU32 col = parseColor(L, 5);
    bool filled = lua_toboolean(L, 6);
    float thickness = lua_isnumber(L, 7) ? (float)lua_tonumber(L, 7) : 1.0f;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    ImVec2 p1(ox + x, oy + y);
    ImVec2 p2(ox + x + w, oy + y + h);
    if (filled)
        dl->AddRectFilled(p1, p2, col);
    else
        dl->AddRect(p1, p2, col, 0.0f, ~0u, thickness);

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

static int l_canvas_line(lua_State *L)
{
    float ox = (float)lua_tonumber(L, lua_upvalueindex(1));
    float oy = (float)lua_tonumber(L, lua_upvalueindex(2));
    float x1 = (float)luaL_checknumber(L, 1);
    float y1 = (float)luaL_checknumber(L, 2);
    float x2 = (float)luaL_checknumber(L, 3);
    float y2 = (float)luaL_checknumber(L, 4);
    ImU32 col = parseColor(L, 5);
    float thickness = lua_isnumber(L, 6) ? (float)lua_tonumber(L, 6) : 1.0f;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->AddLine(ImVec2(ox + x1, oy + y1), ImVec2(ox + x2, oy + y2), col, thickness);

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

static int l_canvas_poly(lua_State *L)
{
    // upvalues: origin.x, origin.y
    float ox = (float)lua_tonumber(L, lua_upvalueindex(1));
    float oy = (float)lua_tonumber(L, lua_upvalueindex(2));
    if (!lua_istable(L, 1)) return 0;
    ImU32 col = parseColor(L, 2);
    bool filled = lua_toboolean(L, 3);
    float thickness = lua_isnumber(L, 4) ? (float)lua_tonumber(L, 4) : 1.0f;
    ImDrawList *dl = ImGui::GetWindowDrawList();
    int n = (int)lua_rawlen(L, 1);
    if (n < 2) return 0;
    std::vector<ImVec2> pts; pts.reserve(n);
    for (int i = 1; i <= n; ++i)
    {
        lua_rawgeti(L, 1, i);
        if (lua_istable(L, -1))
        {
            lua_rawgeti(L, -1, 1); float x = (float)lua_tonumber(L, -1); lua_pop(L, 1);
            lua_rawgeti(L, -1, 2); float y = (float)lua_tonumber(L, -1); lua_pop(L, 1);
            pts.emplace_back(ox + x, oy + y);
        }
        lua_pop(L, 1);
    }
    if (filled)
        dl->AddConvexPolyFilled(pts.data(), (int)pts.size(), col);
    else
        dl->AddPolyline(pts.data(), (int)pts.size(), col, true, thickness);

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

static int l_canvas_text_size(lua_State *L)
{
    const char *s = lua_tostring(L, 1);
    if (!s) { lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 2; }
    ImVec2 sz = ImGui::CalcTextSize(s);
    lua_pushnumber(L, sz.x);
    lua_pushnumber(L, sz.y);
    return 2;
}

static int l_canvas_push_clip(lua_State *L)
{
    float ox = (float)lua_tonumber(L, lua_upvalueindex(1));
    float oy = (float)lua_tonumber(L, lua_upvalueindex(2));
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float w = (float)luaL_checknumber(L, 3);
    float h = (float)luaL_checknumber(L, 4);
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(ImVec2(ox + x, oy + y), ImVec2(ox + x + w, oy + y + h), true);
    return 0;
}

static int l_canvas_pop_clip(lua_State *L)
{
    ImDrawList *dl = ImGui::GetWindowDrawList();
    dl->PopClipRect();
    return 0;
}

static int l_canvas_image(lua_State *L)
{
    // image binding is a no-op here; real image rendering requires texture ids
    // Accept (key|string, x, y, w, h)
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

// Register a Lua callback: canvas.onMouseClick(fn) stores fn in canvas.__onMouseClick
static int l_canvas_register_on_mouse_click(lua_State *L)
{
    if (!lua_isfunction(L, 1) && !lua_isnil(L, 1))
        return luaL_error(L, "expected function or nil");

    lua_getglobal(L, "canvas");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return luaL_error(L, "canvas not initialized"); }

    if (lua_isnil(L, 1))
    {
        lua_pushnil(L);
        lua_setfield(L, -2, "__onMouseClick");
    }
    else
    {
        lua_pushvalue(L, 1);
        lua_setfield(L, -2, "__onMouseClick");
    }

    lua_pop(L, 1);
    return 0;
}

// Register a Lua callback: canvas.onMouseDrag(fn) stores fn in canvas.__onMouseDrag
static int l_canvas_register_on_mouse_drag(lua_State *L)
{
    if (!lua_isfunction(L, 1) && !lua_isnil(L, 1))
        return luaL_error(L, "expected function or nil");

    lua_getglobal(L, "canvas");
    if (!lua_istable(L, -1)) { lua_pop(L, 1); return luaL_error(L, "canvas not initialized"); }

    if (lua_isnil(L, 1))
    {
        lua_pushnil(L);
        lua_setfield(L, -2, "__onMouseDrag");
    }
    else
    {
        lua_pushvalue(L, 1);
        lua_setfield(L, -2, "__onMouseDrag");
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

    // rect(x,y,w,h,color,filled)
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushcclosure(L, l_canvas_rect, 2);
    lua_setfield(L, -2, "rect");

    // line(x1,y1,x2,y2,color,thickness)
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushcclosure(L, l_canvas_line, 2);
    lua_setfield(L, -2, "line");


    // poly(points_table, color, filled)
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushcclosure(L, l_canvas_poly, 2);
    lua_setfield(L, -2, "poly");

    // text_size(s)
    lua_pushcfunction(L, l_canvas_text_size);
    lua_setfield(L, -2, "text_size");

    // clip push/pop
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushcclosure(L, l_canvas_push_clip, 2);
    lua_setfield(L, -2, "push_clip");
    lua_pushcfunction(L, l_canvas_pop_clip);
    lua_setfield(L, -2, "pop_clip");

    // image (stub)
    lua_pushcfunction(L, l_canvas_image);
    lua_setfield(L, -2, "image");

    // onMouseClick(fn) and onMouseDrag(fn) -> register/clear handlers in canvas.__onMouseClick / __onMouseDrag
    lua_pushcfunction(L, l_canvas_register_on_mouse_click);
    lua_setfield(L, -2, "registerOnMouseClick");
    lua_pushcfunction(L, l_canvas_register_on_mouse_drag);
    lua_setfield(L, -2, "registerOnMouseDrag");

    lua_setglobal(L, "canvas");
}
