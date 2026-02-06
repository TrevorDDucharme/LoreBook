#include "LuaEngine.hpp"
#include <plog/Log.h>
#include <sstream>

LuaEngine::LuaEngine()
{
    m_L = luaL_newstate();
    if (!m_L) {
        PLOGE << "Lua: failed to create state";
        return;
    }
    luaL_openlibs(m_L);
    setupSandbox();
}

LuaEngine::~LuaEngine()
{
    if (m_L)
        lua_close(m_L);
}

void LuaEngine::captureLuaError(const char *msg)
{
    if (msg)
        m_error = msg;
    else
        m_error = "unknown lua error";
    PLOGW << "Lua error: " << m_error;
}

void LuaEngine::setupSandbox()
{
    // Remove dangerous globals: io, os, loadfile, dofile, load
    lua_pushnil(m_L);
    lua_setglobal(m_L, "io");
    lua_pushnil(m_L);
    lua_setglobal(m_L, "os");
    lua_pushnil(m_L);
    lua_setglobal(m_L, "loadfile");
    lua_pushnil(m_L);
    lua_setglobal(m_L, "dofile");
    lua_pushnil(m_L);
    lua_setglobal(m_L, "load");

    // Replace print with a logger function that forwards to plog and captures output
    // push 'this' as lightuserdata upvalue for the closure
    lua_pushlightuserdata(m_L, this);
    lua_pushcclosure(m_L, &LuaEngine::l_print, 1);
    lua_setglobal(m_L, "print");
}

int LuaEngine::l_print(lua_State *L)
{
    // upvalue 1 is our LuaEngine* pointer
    LuaEngine *eng = reinterpret_cast<LuaEngine*>(lua_touserdata(L, lua_upvalueindex(1)));
    int nargs = lua_gettop(L);
    std::ostringstream ss;
    for (int i = 1; i <= nargs; ++i)
    {
        if (lua_isstring(L, i))
            ss << lua_tostring(L, i);
        else
            ss << luaL_tolstring(L, i, nullptr);
        if (i < nargs)
            ss << "\t";
    }
    std::string s = ss.str();
    PLOGI << "lua: " << s;
    if (eng)
    {
        eng->m_stdout += s;
        eng->m_stdout.push_back('\n');
    }
    return 0;
}

bool LuaEngine::loadScript(const std::string &code)
{
    m_error.clear();
    m_stdout.clear();
    if (!m_L)
    {
        captureLuaError("lua state not initialized");
        return false;
    }
    int r = luaL_loadbuffer(m_L, code.c_str(), (int)code.size(), "@embedded_script");
    if (r != LUA_OK)
    {
        captureLuaError(lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
        return false;
    }
    // run chunk
    r = lua_pcall(m_L, 0, 0, 0);
    if (r != LUA_OK)
    {
        captureLuaError(lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
        return false;
    }
    return true;
}

std::string LuaEngine::takeStdout()
{
    std::string s = m_stdout;
    m_stdout.clear();
    return s;
}

ScriptConfig LuaEngine::callConfig()
{
    ScriptConfig out;
    m_error.clear();
    if (!m_L)
        return out;
    lua_getglobal(m_L, "Config");
    if (lua_isfunction(m_L, -1))
    {
        if (lua_pcall(m_L, 0, 1, 0) != LUA_OK)
        {
            captureLuaError(lua_tostring(m_L, -1));
            lua_pop(m_L, 1);
            return out;
        }
        if (lua_istable(m_L, -1))
        {
            // read fields
            lua_getfield(m_L, -1, "type");
            if (lua_isstring(m_L, -1))
            {
                std::string t = lua_tostring(m_L, -1);
                if (t == "canvas") out.type = ScriptConfig::Type::Canvas;
                else if (t == "ui") out.type = ScriptConfig::Type::UI;
            }
            lua_pop(m_L, 1);
            lua_getfield(m_L, -1, "width");
            if (lua_isnumber(m_L, -1)) out.width = (int)lua_tointeger(m_L, -1);
            lua_pop(m_L, 1);
            lua_getfield(m_L, -1, "height");
            if (lua_isnumber(m_L, -1)) out.height = (int)lua_tointeger(m_L, -1);
            lua_pop(m_L, 1);
            lua_getfield(m_L, -1, "title");
            if (lua_isstring(m_L, -1)) out.title = lua_tostring(m_L, -1);
            lua_pop(m_L, 1);
        }
        lua_pop(m_L, 1); // pop return
    }
    else
    {
        lua_pop(m_L, 1); // pop non-function
    }
    return out;
}

void LuaEngine::callRender(float dt)
{
    if (!m_L) return;
    m_error.clear();
    lua_getglobal(m_L, "Render");
    if (!lua_isfunction(m_L, -1)) { lua_pop(m_L, 1); return; }
    lua_pushnumber(m_L, dt);
    if (lua_pcall(m_L, 1, 0, 0) != LUA_OK)
    {
        captureLuaError(lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
    }
}

void LuaEngine::callUI()
{
    if (!m_L) return;
    m_error.clear();
    lua_getglobal(m_L, "UI");
    if (!lua_isfunction(m_L, -1)) { lua_pop(m_L, 1); return; }
    if (lua_pcall(m_L, 0, 0, 0) != LUA_OK)
    {
        captureLuaError(lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
    }
}

void LuaEngine::callOnCanvasEvent(const LuaEngine::CanvasEvent &event)
{
    if (!m_L) return;
    m_error.clear();
    lua_getglobal(m_L, "OnCanvasEvent");
    if (!lua_isfunction(m_L, -1)) { lua_pop(m_L, 1); return; }
    // push event table
    lua_newtable(m_L);
    lua_pushstring(m_L, event.type.c_str());
    lua_setfield(m_L, -2, "type");
    // data subtable
    lua_newtable(m_L);
    for (const auto &[k, v] : event.data)
    {
        lua_pushstring(m_L, v.c_str());
        lua_setfield(m_L, -2, k.c_str());
    }
    lua_setfield(m_L, -2, "data");
    if (lua_pcall(m_L, 1, 0, 0) != LUA_OK)
    {
        captureLuaError(lua_tostring(m_L, -1));
        lua_pop(m_L, 1);
    }
}
