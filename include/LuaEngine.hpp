#pragma once

#include <string>
#include <memory>
#include <vector>
#include <string>
#include <map>

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

    struct CanvasEvent {
        std::string type;
        std::map<std::string, std::string> data;
    };

    // Call optional OnEvent(event) for ui scripts
    void callOnCanvasEvent(const CanvasEvent &event);

    // Last error message captured from Lua calls
    const std::string &lastError() const { return m_error; }

    lua_State *L() { return m_L; }

    // Take captured stdout from the Lua 'print' binding. Returns and clears the buffer.
    std::string takeStdout();

    // ── Canvas FBO rendering ──────────────────────────────────────────────
    // Renders a canvas frame: creates/resizes FBO if needed, binds it,
    // registers GL canvas bindings, calls Render(dt), flushes batched
    // draws, unbinds FBO.  Returns the GL texture ID that can be displayed
    // via ImGui::Image.  Both MarkdownText and LuaEditor call this.
    unsigned int renderCanvasFrame(const std::string &embedID, int width, int height, float dt);

    // Returns the texture ID from the last renderCanvasFrame call (0 if none).
    unsigned int canvasTextureID() const { return m_fboTex; }

    // Returns the number of canvas draw calls issued in the last frame.
    int canvasDrawCount() const;

private:
    lua_State *m_L = nullptr;
    std::string m_error;

    // captured output from print() calls
    std::string m_stdout;

    // FBO state owned by this engine instance (one per engine)
    unsigned int m_fbo = 0;
    unsigned int m_fboTex = 0;
    unsigned int m_fboRbo = 0;
    int m_fboW = 0, m_fboH = 0;

    void setupSandbox();
    void captureLuaError(const char *msg);

    // C-function used as the 'print' binding (added as a closure with 'this' as upvalue)
    static int l_print(lua_State *L);
};