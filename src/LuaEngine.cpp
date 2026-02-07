#include "LuaEngine.hpp"
#include "LuaCanvasBindings.hpp"
#include <plog/Log.h>
#include <sstream>
#include <GL/glew.h>

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
    // Clean up FBO resources
    if (m_fbo)  { glDeleteFramebuffers(1, &m_fbo);  m_fbo = 0; }
    if (m_fboTex) { glDeleteTextures(1, &m_fboTex); m_fboTex = 0; }
    if (m_fboRbo) { glDeleteRenderbuffers(1, &m_fboRbo); m_fboRbo = 0; }

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

// ── Canvas FBO rendering ─────────────────────────────────────────────────

unsigned int LuaEngine::renderCanvasFrame(const std::string &embedID, int width, int height, float dt)
{
    if (!m_L) return 0;

    // Create or resize FBO if dimensions changed
    if (m_fboTex == 0 || m_fboW != width || m_fboH != height)
    {
        if (m_fbo)    { glDeleteFramebuffers(1, &m_fbo);    m_fbo = 0; }
        if (m_fboTex) { glDeleteTextures(1, &m_fboTex);     m_fboTex = 0; }
        if (m_fboRbo) { glDeleteRenderbuffers(1, &m_fboRbo); m_fboRbo = 0; }

        m_fboW = width;
        m_fboH = height;

        glGenFramebuffers(1, &m_fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

        glGenTextures(1, &m_fboTex);
        glBindTexture(GL_TEXTURE_2D, m_fboTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_fboTex, 0);

        glGenRenderbuffers(1, &m_fboRbo);
        glBindRenderbuffer(GL_RENDERBUFFER, m_fboRbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_fboRbo);

        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if (status != GL_FRAMEBUFFER_COMPLETE)
            PLOGW << "LuaEngine FBO incomplete for embed=" << embedID << " status=" << status;

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    // Bind FBO
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);

    // Save GL state that ImGui / other renderers may have set
    GLint lastViewport[4]; glGetIntegerv(GL_VIEWPORT, lastViewport);
    GLboolean wasDepthTest  = glIsEnabled(GL_DEPTH_TEST);
    GLboolean wasCullFace   = glIsEnabled(GL_CULL_FACE);
    GLboolean wasScissor    = glIsEnabled(GL_SCISSOR_TEST);
    GLboolean wasStencil    = glIsEnabled(GL_STENCIL_TEST);

    // Set clean GL state for canvas rendering
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_STENCIL_TEST);

    // Register GL-backed canvas bindings (sets up canvas table with width/height/clear/viewport etc.)
    registerLuaGLCanvasBindings(m_L, embedID, m_fboTex, width, height);

    // Call Render(dt)
    callRender(dt);

    // Flush batched canvas draws into the FBO
    flushLuaCanvasForEmbed(m_L, embedID);

    // Restore GL state
    if (wasDepthTest)  glEnable(GL_DEPTH_TEST);
    if (wasCullFace)   glEnable(GL_CULL_FACE);
    if (wasScissor)    glEnable(GL_SCISSOR_TEST);
    if (wasStencil)    glEnable(GL_STENCIL_TEST);
    glViewport(lastViewport[0], lastViewport[1], lastViewport[2], lastViewport[3]);

    // Unbind FBO
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    return m_fboTex;
}

int LuaEngine::canvasDrawCount() const
{
    if (!m_L) return 0;
    int count = 0;
    lua_getglobal(m_L, "canvas");
    if (lua_istable(m_L, -1))
    {
        lua_getfield(m_L, -1, "draw_count");
        count = (int)lua_tointeger(m_L, -1);
        lua_pop(m_L, 1);
    }
    lua_pop(m_L, 1);
    return count;
}
