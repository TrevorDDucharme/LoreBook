#pragma once

#include <string>
#include <memory>
#include <vector>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

struct ScriptConfig {
    enum class Type { None, Canvas, UI };
    Type type = Type::None;
    int width = 300;
    int height = 200;
    std::string title;
};

class LuaEngine {
public:
    LuaEngine();
    ~LuaEngine();

    // Load and compile lua source (returns false on error)
    bool loadScript(const std::string &code);

    // Call optional Config() function and return parsed config
    ScriptConfig callConfig();

    // Call Render(dt) for canvas scripts
    void callRender(float dt);

    // Call UI() for ui scripts
    void callUI();

    // Last error message captured from Lua calls
    const std::string &lastError() const { return m_error; }

    lua_State *L() { return m_L; }

private:
    lua_State *m_L = nullptr;
    std::string m_error;

    void setupSandbox();
    void captureLuaError(const char *msg);
};