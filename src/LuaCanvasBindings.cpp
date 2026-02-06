#include "LuaCanvasBindings.hpp"
#include "LuaBindingDocs.hpp"
#include "LuaBindingDocsUtil.hpp"
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
        dl->AddCircle(p, r, col, 20, thickness);

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

void registerLuaCanvasBindings(lua_State *L, ImVec2 origin, int width, int height)
{
    // Create a new canvas table with small closures capturing the required upvalues.
    lua_newtable(L);

    // initialize a draw_count field we can increment to detect whether Render() actually drew anything
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "draw_count");
    LuaBindingDocs::get().registerDoc("canvas.draw_count", "draw_count -> int", "Number of draw calls issued to canvas in this frame (for diagnostics)", 
    R"(
        
        print(canvas.draw_count)
    )", __FILE__);

    // width
    lua_pushinteger(L, width);
    lua_pushcclosure(L, l_canvas_width, 1);
    lua_setfield(L, -2, "width");
    LuaBindingDocs::get().registerDoc("canvas.width", "width() -> int", "Get canvas width in pixels", "w = canvas.width()", __FILE__);

    // height
    lua_pushinteger(L, height);
    lua_pushcclosure(L, l_canvas_height, 1);
    lua_setfield(L, -2, "height");
    LuaBindingDocs::get().registerDoc("canvas.height", "height() -> int", "Get canvas height in pixels", "h = canvas.height()", __FILE__);

    // circle (captures origin.x and origin.y)
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushcclosure(L, l_canvas_circle, 2);
    lua_setfield(L, -2, "circle");
    // doc
    LuaBindingDocs::get().registerDoc("canvas.circle", "circle(cx, cy, r, color, filled, thickness)", "Draw a circle at local canvas coordinates", R"(
-- Full canvas example: center a circle
function Config()
  return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
  local w = canvas.width()
  local h = canvas.height()
  local r = math.min(w,h) * 0.25
  canvas.circle(w/2, h/2, r, {0,1,0,1}, true)
end
)", __FILE__);

    // text (captures origin.x and origin.y)
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushcclosure(L, l_canvas_text, 2);
    lua_setfield(L, -2, "text");
    LuaBindingDocs::get().registerDoc("canvas.text", "text(x, y, string, color)", "Draw text at local canvas coordinates", R"(
-- Full canvas example: centered text
function Config()
  return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
  local w = canvas.width()
  local h = canvas.height()
  local tw, th = canvas.text_size("Hello World")
  canvas.text((w-tw)/2, (h-th)/2, "Hello World", {1,1,1,1})
end
)", __FILE__);

    // rect(x,y,w,h,color,filled)
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushcclosure(L, l_canvas_rect, 2);
    lua_setfield(L, -2, "rect");
    LuaBindingDocs::get().registerDoc("canvas.rect", "rect(x, y, w, h, color, filled, thickness)", "Draw a rectangle", R"(
-- Full canvas example: framed rectangle at center
function Config()
  return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
  local w = canvas.width()
  local h = canvas.height()
  canvas.rect(w/2 - 50, h/2 - 20, 100, 40, {0,0,1,1}, false, 2)
end
)", __FILE__);
    // line(x1,y1,x2,y2,color,thickness)
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushcclosure(L, l_canvas_line, 2);
    lua_setfield(L, -2, "line");
    LuaBindingDocs::get().registerDoc("canvas.line", "line(x1, y1, x2, y2, color, thickness)", "Draw a line between two points", R"(
-- Full canvas example: draw an X
function Config()
  return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
  local w = canvas.width()
  local h = canvas.height()
  canvas.line(0,0, w, h, {1,0,0,1}, 2)
  canvas.line(0,h, w, 0, {1,0,0,1}, 2)
end
)", __FILE__);


    // poly(points_table, color, filled)
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushcclosure(L, l_canvas_poly, 2);
    lua_setfield(L, -2, "poly");
    LuaBindingDocs::get().registerDoc("canvas.poly", "poly(points_table, color, filled)", "Draw polygon from point table", R"(
-- Full canvas example: triangle at center
function Config()
  return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
  local w = canvas.width()
  local h = canvas.height()
  local cx, cy = w/2, h/2
  canvas.poly({{cx-30,cy+20},{cx,cy-30},{cx+30,cy+20}}, {0.5,0.8,0.2,1}, true)
end
)", __FILE__);

    // text_size(s)
    lua_pushcfunction(L, l_canvas_text_size);
    lua_setfield(L, -2, "text_size");
    LuaBindingDocs::get().registerDoc("canvas.text_size", "text_size(s) -> width, height", "Measure text extents in canvas-local units", "-- Measure and use text for placement\nlocal w,h = canvas.text_size('Hello')\ncanvas.text(10,10,'Hello', {1,1,1,1})", __FILE__);

    // clip push/pop
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushcclosure(L, l_canvas_push_clip, 2);
    lua_setfield(L, -2, "push_clip");
    lua_pushcfunction(L, l_canvas_pop_clip);
    lua_setfield(L, -2, "pop_clip");
    LuaBindingDocs::get().registerDoc("canvas.push_clip", "push_clip(x, y, w, h)", "Push a scissor clip region relative to the canvas", "canvas.push_clip(0,0,100,100)", __FILE__);
    LuaBindingDocs::get().registerDoc("canvas.pop_clip", "pop_clip()", "Pop last scissor clip", "canvas.pop_clip()", __FILE__);

    // image (stub)
    lua_pushcfunction(L, l_canvas_image);
    lua_setfield(L, -2, "image");
    LuaBindingDocs::get().registerDoc("canvas.image", "image(keyOrUrl, x, y, w, h)", "Render an image by key or URL (stubbed in this environment)", "canvas.image('some_key', 10, 10, 64, 64)", __FILE__);

    lua_setglobal(L, "canvas");

    // Enforce that all exported functions on the 'canvas' table have docs. This will throw if any are missing.
    LuaBindingDocsUtil::enforceTableHasDocs(L, "canvas");
}
