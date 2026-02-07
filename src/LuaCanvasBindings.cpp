#include "LuaCanvasBindings.hpp"
#include "LuaBindingDocs.hpp"
#include "LuaBindingDocsUtil.hpp"
extern "C" {
#include <lauxlib.h>
}
#include <plog/Log.h>
#include <GL/glew.h>
#include <chrono>
#include <vector>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <memory>
#include <string>
#include <imgui.h>

/*
how canvas bindings work:

function Config()
    return {
        type = "canvas",
        width = 400,
        height = 400,
        title = "Circle"
    }
end

camPosition = { x=0, y=0, z=0 }
camUp = { x=0, y=1, z=0 }
target = { x=0, y=0, z=0 }

fov = 60
aspectRatio = 1.0
nearPlane = 0.1
farPlane = 100.0

-- For canvas scripts: called every frame
function Render(dt)
    canvas.clear({.35,.35,.35,1})
    canvas.viewport(0,0,canvas.width, canvas.height)
    canvas.viewMatrix(canvas.lookAt(camPosition, target, camUp))
    canvas.projectionMatrix(canvas.ortho(0, 0, canvas.width, canvas.height, nearPlane, farPlane))
    --[
        or for 3D mode:
        canvas.projectionMatrix(canvas.perspective(fov, aspectRatio, nearPlane, farPlane))
    ]--

    --appends a circle draw call to the canvas batch for this embed; actual GL draw happens in flushLuaCanvasForEmbed after Render returns
    canvas.circle(circle.x, circle.y, circle.radius, circle.color, circle.filled, circle.thickness)

end

-- for more complex drawing we support vbo, texture, and shader management in Lua with a simple GL wrapper. Example usage:

vert_source = [[
#version 330 core
layout(location=0) in vec2 in_pos;
layout(location=1) in vec4 in_color;
out vec4 v_color;
uniform vec2 uSize;
void main(){
    v_color = in_color;
    vec2 p = in_pos / uSize * 2.0 - 1.0; // convert from [0,size] to [-1,1]
    gl_Position = vec4(p,0,1);
}
]]

frag_source = [[
#version 330 core
in vec4 v_color;
out vec4 out_color;
void main(){
    out_color = v_color;
}
]]

shader = canvas.createShader(vert_source, frag_source)

verts = {
    -- x, y, r, g, b, a
    100,100, 1,0,0,1,
    200,100, 0,1,0,1,
    200,200, 0,0,1,1,
    100,100, 1,0,0,1,
    200,200, 0,0,1,1,
    100,200, 1,1,0,1
}
vbo = canvas.createVertexBuffer(verts)


function Render(dt)
    local w,h = canvas.width, canvas.height
    canvas.viewport(0,0,w,h)
    canvas.clear({.1,.1,.1,1})

    canvas.useShader(shader)
    canvas.setShaderUniformVec2("uSize", {w,h})
    canvas.bindVertexBuffer(vbo)
    canvas.drawArrays("triangles", 0, 6)
end
*/


// Forward declarations
static GLuint compileShaderProgram(const char *vsSrc, const char *fsSrc);


// Per-embed canvas batching state
struct CanvasBatch {
    GLenum mode;
    int start; // start vertex index
    int count; // vertex count
};

struct CanvasState {
    std::string embedID;
    int width = 0, height = 0;
    GLuint program = 0;
    GLuint vbo = 0;
    GLuint vao = 0;
    unsigned int textureId = 0;
    int vertexStride = 6; // x,y, r,g,b,a
    std::vector<float> cpuVerts;
    std::vector<CanvasBatch> batches;
};

static std::unordered_map<std::string, std::unique_ptr<CanvasState>> s_canvasStates;

static CanvasState *ensureCanvasState(const std::string &embedID, unsigned int tex, int w, int h)
{
    auto it = s_canvasStates.find(embedID);
    if (it != s_canvasStates.end()){
        CanvasState *cs = it->second.get();
        cs->width = w; cs->height = h; cs->textureId = tex;
        return cs;
    }
    auto csu = std::make_unique<CanvasState>();
    CanvasState *cs = csu.get();
    cs->embedID = embedID;
    cs->width = w; cs->height = h; cs->textureId = tex;
    cs->program = 0; cs->vbo = 0; cs->vao = 0;
    s_canvasStates.emplace(embedID, std::move(csu));
    return s_canvasStates[embedID].get();
}




// Flush pending draws for the given embed: upload VBO and draw recorded batches.
void flushLuaCanvasForEmbed(lua_State *L, const std::string &embedID)
{
    (void)L;
    auto it = s_canvasStates.find(embedID);
    if (it == s_canvasStates.end()) return;
    CanvasState *cs = it->second.get();
    if (!cs) return;
    if (cs->cpuVerts.empty() || cs->batches.empty()) return;

    // Ensure VAO/VBO
    if (cs->vao == 0) glGenVertexArrays(1, &cs->vao);
    if (cs->vbo == 0) glGenBuffers(1, &cs->vbo);

    glBindVertexArray(cs->vao);
    glBindBuffer(GL_ARRAY_BUFFER, cs->vbo);
    glBufferData(GL_ARRAY_BUFFER, cs->cpuVerts.size() * sizeof(float), cs->cpuVerts.data(), GL_DYNAMIC_DRAW);

    // vertex format: vec2 pos; vec4 color
    GLsizei stride = cs->vertexStride * sizeof(float);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)(0));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));

    GLuint prog = cs->program;
    if (prog) glUseProgram(prog);
    GLint locSize = prog ? glGetUniformLocation(prog, "uSize") : -1;
    if (locSize >= 0) glUniform2f(locSize, (float)cs->width, (float)cs->height);

    // enable alpha blending for canvas draws
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    for (auto &b : cs->batches){
        if (b.count <= 0) continue;
        glDrawArrays(b.mode, b.start, b.count);
    }

    glDisable(GL_BLEND);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    if (prog) glUseProgram(0);

    // clear recorded verts/batches
    cs->cpuVerts.clear();
    cs->batches.clear();
}

// Common simple solid-color shader
static const char *s_shape_vs_src = "#version 330 core\n"
"layout(location=0) in vec2 in_pos;\n"
"layout(location=1) in vec4 in_color;\n"
"out vec4 v_color;\n"
"uniform vec2 uSize;\n"
"void main(){ vec2 p = in_pos / uSize * 2.0 - 1.0; p.y = -p.y; v_color = in_color; gl_Position = vec4(p,0.0,1.0); }\n";
static const char *s_shape_fs_src = "#version 330 core\n"
"in vec4 v_color; out vec4 out_color; void main(){ out_color = v_color; }\n";

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

// Append a stroke as a triangle strip along the given point list (pts contains x,y pairs)
static void appendStrokeStrip(CanvasState *cs, const std::vector<float> &pts, float thickness, const ImVec4 &cc)
{
    if (!cs) return;
    int n = (int)(pts.size() / 2);
    if (n < 2) return;
    size_t start = cs->cpuVerts.size() / cs->vertexStride;

    std::vector<std::pair<float,float>> segNormals(n);
    for (int i = 0; i < n-1; ++i){
        float dx = pts[(i+1)*2] - pts[i*2];
        float dy = pts[(i+1)*2+1] - pts[i*2+1];
        float len = sqrtf(dx*dx + dy*dy);
        if (len == 0.0f) { segNormals[i] = {0.0f, 0.0f}; }
        else { segNormals[i].first = -dy / len; segNormals[i].second = dx / len; }
    }
    segNormals[n-1] = segNormals[n-2];

    std::vector<std::pair<float,float>> normals(n);
    for (int i = 0; i < n; ++i){
        if (i == 0) normals[i] = segNormals[0];
        else if (i == n-1) normals[i] = segNormals[n-2];
        else {
            float nx = segNormals[i-1].first + segNormals[i].first;
            float ny = segNormals[i-1].second + segNormals[i].second;
            float l = sqrtf(nx*nx + ny*ny);
            if (l == 0.0f) normals[i] = segNormals[i];
            else { normals[i].first = nx / l; normals[i].second = ny / l; }
        }
    }

    float half = thickness * 0.5f;
    for (int i = 0; i < n; ++i){
        float x = pts[i*2], y = pts[i*2+1];
        float nx = normals[i].first * half;
        float ny = normals[i].second * half;
        // top
        cs->cpuVerts.push_back(x + nx); cs->cpuVerts.push_back(y + ny);
        cs->cpuVerts.push_back(cc.x); cs->cpuVerts.push_back(cc.y); cs->cpuVerts.push_back(cc.z); cs->cpuVerts.push_back(cc.w);
        // bottom
        cs->cpuVerts.push_back(x - nx); cs->cpuVerts.push_back(y - ny);
        cs->cpuVerts.push_back(cc.x); cs->cpuVerts.push_back(cc.y); cs->cpuVerts.push_back(cc.z); cs->cpuVerts.push_back(cc.w);
    }
    CanvasBatch b; b.mode = GL_TRIANGLE_STRIP; b.start = (int)start; b.count = n*2;
    cs->batches.push_back(b);
}


static int l_canvas_circle(lua_State *L)
{
    float cx = (float)luaL_checknumber(L, 1);
    float cy = (float)luaL_checknumber(L, 2);
    float r = (float)luaL_checknumber(L, 3);
    ImU32 col = parseColor(L, 4);
    bool filled = lua_toboolean(L, 5);
    float thickness = lua_isnumber(L, 6) ? (float)lua_tonumber(L, 6) : 1.0f;
    int segments = lua_isinteger(L, 7) ? (int)lua_tointeger(L, 7) : 32;

    GLuint prog = (GLuint)lua_tointeger(L, lua_upvalueindex(4));
    void *cup = lua_touserdata(L, lua_upvalueindex(5));
    CanvasState *cs = (CanvasState*)cup;
    std::vector<float> verts;
    if (filled){
        verts.reserve((segments+2)*2);
        verts.push_back(cx); verts.push_back(cy);
        for (int i=0;i<=segments;++i){ float a = (float)i/(float)segments * 2.0f * 3.14159265f; verts.push_back(cx + cosf(a)*r); verts.push_back(cy + sinf(a)*r); }
    } else {
        verts.reserve((segments+1)*2);
        for (int i=0;i<=segments;++i){ float a = (float)i/(float)segments * 2.0f * 3.14159265f; verts.push_back(cx + cosf(a)*r); verts.push_back(cy + sinf(a)*r); }
    }

    if (cs) {
        ImVec4 cc = ImGui::ColorConvertU32ToFloat4(col);
        if (filled) {
            size_t start = cs->cpuVerts.size() / cs->vertexStride;
            for (size_t i=0;i<verts.size(); i+=2){
                float x = verts[i]; float y = verts[i+1];
                cs->cpuVerts.push_back(x);
                cs->cpuVerts.push_back(y);
                cs->cpuVerts.push_back(cc.x); cs->cpuVerts.push_back(cc.y); cs->cpuVerts.push_back(cc.z); cs->cpuVerts.push_back(cc.w);
            }
            CanvasBatch b; b.mode = GL_TRIANGLE_FAN; b.start = (int)start; b.count = (int)(verts.size()/2);
            cs->batches.push_back(b);
        } else {
            appendStrokeStrip(cs, verts, thickness, cc);
        }
        lua_getglobal(L, "canvas"); if (lua_istable(L,-1)){ lua_getfield(L,-1,"draw_count"); int cc=(int)lua_tointeger(L,-1); lua_pop(L,1); cc++; lua_pushinteger(L,cc); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
        return 0;
    }

    // batching-only mode: do not perform immediate GL draws
    PLOGW << "lua:canvas immediate draw requested but batching-only mode; skipping draw";
    return 0;
}

static int l_canvas_text(lua_State *L)
{
    // Text rendering not supported in GL canvas bindings; return as no-op and estimate extents in text_size
    // Consume args and increment draw_count
    (void)luaL_optinteger(L,1,0); (void)luaL_optinteger(L,2,0); (void)luaL_optstring(L,3,NULL); (void)parseColor(L,4);
    lua_getglobal(L, "canvas"); if (lua_istable(L, -1)){ lua_getfield(L,-1,"draw_count"); int c=(int)lua_tointeger(L,-1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
    return 0;
}

static int l_canvas_rect(lua_State *L)
{
    // GL implementation: draws into currently bound FBO using pixel coordinates
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float w = (float)luaL_checknumber(L, 3);
    float h = (float)luaL_checknumber(L, 4);
    ImU32 col = parseColor(L, 5);
    bool filled = lua_toboolean(L, 6);
    float thickness = lua_isnumber(L, 7) ? (float)lua_tonumber(L, 7) : 1.0f;
    // attempt buffered path
    void *cup = lua_touserdata(L, lua_upvalueindex(5));
    CanvasState *cs = (CanvasState*)cup;
    GLuint prog = (GLuint)lua_tointeger(L, lua_upvalueindex(4));
    if (cs) {
        size_t start = cs->cpuVerts.size() / cs->vertexStride;
        ImVec4 cc = ImGui::ColorConvertU32ToFloat4(col);
        if (filled) {
            // two triangles (6 vertices)
            float vx[12] = { x, y, x + w, y, x + w, y + h, x, y, x + w, y + h, x, y + h };
            for (int i=0;i<6;i++){
                cs->cpuVerts.push_back(vx[i*2]); cs->cpuVerts.push_back(vx[i*2+1]);
                cs->cpuVerts.push_back(cc.x); cs->cpuVerts.push_back(cc.y); cs->cpuVerts.push_back(cc.z); cs->cpuVerts.push_back(cc.w);
            }
            CanvasBatch b; b.mode = GL_TRIANGLES; b.start = (int)start; b.count = 6; cs->batches.push_back(b);
        } else {
            std::vector<float> pts = { x, y, x + w, y, x + w, y + h, x, y + h };
            appendStrokeStrip(cs, pts, thickness, cc);
        }
        lua_getglobal(L, "canvas"); if (lua_istable(L, -1)){ lua_getfield(L,-1,"draw_count"); int c=(int)lua_tointeger(L,-1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
        return 0;
    }

    // batching-only mode: do not perform immediate GL draws
    PLOGW << "lua:canvas immediate draw requested but batching-only mode; skipping draw";
    return 0;
}



static int l_canvas_line(lua_State *L)
{
    float x1 = (float)luaL_checknumber(L, 1);
    float y1 = (float)luaL_checknumber(L, 2);
    float x2 = (float)luaL_checknumber(L, 3);
    float y2 = (float)luaL_checknumber(L, 4);
    ImU32 col = parseColor(L, 5);
    float thickness = lua_isnumber(L, 6) ? (float)lua_tonumber(L, 6) : 1.0f;
    // attempt to append into CanvasState buffer
    void *cup = lua_touserdata(L, lua_upvalueindex(5));
    CanvasState *cs = (CanvasState*)cup;
    GLuint prog = (GLuint)lua_tointeger(L, lua_upvalueindex(4));
    if (cs) {
        ImVec4 cc = ImGui::ColorConvertU32ToFloat4(col);
        std::vector<float> pts = { x1, y1, x2, y2 };
        appendStrokeStrip(cs, pts, thickness, cc);
        lua_getglobal(L, "canvas"); if (lua_istable(L, -1)){ lua_getfield(L,-1,"draw_count"); int c=(int)lua_tointeger(L,-1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
        return 0;
    }

    // batching-only mode: do not perform immediate GL draws
    PLOGW << "lua:canvas immediate draw requested but batching-only mode; skipping draw";
    return 0;
}

static int l_canvas_poly(lua_State *L)
{
    if (!lua_istable(L, 1)) return 0;
    ImU32 col = parseColor(L, 2);
    bool filled = lua_toboolean(L, 3);

    // build point list
    std::vector<float> pts;
    lua_pushnil(L);
    while (lua_next(L, 1) != 0){
        // value at -1 should be a pair table {x,y}
        if (lua_istable(L, -1)){
            lua_rawgeti(L, -1, 1); float px = (float)lua_tonumber(L, -1); lua_pop(L,1);
            lua_rawgeti(L, -1, 2); float py = (float)lua_tonumber(L, -1); lua_pop(L,1);
            pts.push_back(px); pts.push_back(py);
        }
        lua_pop(L,1); // pop value, keep key
    }

    // attempt buffered path
    void *cup = lua_touserdata(L, lua_upvalueindex(5));
    CanvasState *cs = (CanvasState*)cup;
    GLuint prog = (GLuint)lua_tointeger(L, lua_upvalueindex(4));
    float thickness = lua_isnumber(L,4) ? (float)lua_tonumber(L,4) : 1.0f;
    if (cs) {
        ImVec4 cc = ImGui::ColorConvertU32ToFloat4(col);
        if (filled) {
            size_t start = cs->cpuVerts.size() / cs->vertexStride;
            for (size_t i=0;i<pts.size(); i+=2){
                cs->cpuVerts.push_back(pts[i]); cs->cpuVerts.push_back(pts[i+1]);
                cs->cpuVerts.push_back(cc.x); cs->cpuVerts.push_back(cc.y); cs->cpuVerts.push_back(cc.z); cs->cpuVerts.push_back(cc.w);
            }
            CanvasBatch b; b.mode = GL_TRIANGLE_FAN; b.start = (int)start; b.count = (int)(pts.size()/2); cs->batches.push_back(b);
        } else {
            appendStrokeStrip(cs, pts, thickness, cc);
        }
        lua_getglobal(L, "canvas"); if (lua_istable(L, -1)){ lua_getfield(L,-1,"draw_count"); int c=(int)lua_tointeger(L,-1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
        return 0;
    }

    // batching-only mode: do not perform immediate GL draws
    PLOGW << "lua:canvas immediate draw requested but batching-only mode; skipping draw";
    return 0;


}

static int l_canvas_triangle(lua_State *L)
{
    float x1 = (float)luaL_checknumber(L, 1);
    float y1 = (float)luaL_checknumber(L, 2);
    float x2 = (float)luaL_checknumber(L, 3);
    float y2 = (float)luaL_checknumber(L, 4);
    float x3 = (float)luaL_checknumber(L, 5);
    float y3 = (float)luaL_checknumber(L, 6);
    ImU32 col = parseColor(L, 7);
    bool filled = lua_toboolean(L, 8);
    float thickness = lua_isnumber(L, 9) ? (float)lua_tonumber(L, 9) : 1.0f;
    // attempt buffered path
    void *cup = lua_touserdata(L, lua_upvalueindex(5));
    CanvasState *cs = (CanvasState*)cup;
    GLuint prog = (GLuint)lua_tointeger(L, lua_upvalueindex(4));
    if (cs) {
        ImVec4 cc = ImGui::ColorConvertU32ToFloat4(col);
        if (filled) {
            size_t start = cs->cpuVerts.size() / cs->vertexStride;
            cs->cpuVerts.push_back(x1); cs->cpuVerts.push_back(y1); cs->cpuVerts.push_back(cc.x); cs->cpuVerts.push_back(cc.y); cs->cpuVerts.push_back(cc.z); cs->cpuVerts.push_back(cc.w);
            cs->cpuVerts.push_back(x2); cs->cpuVerts.push_back(y2); cs->cpuVerts.push_back(cc.x); cs->cpuVerts.push_back(cc.y); cs->cpuVerts.push_back(cc.z); cs->cpuVerts.push_back(cc.w);
            cs->cpuVerts.push_back(x3); cs->cpuVerts.push_back(y3); cs->cpuVerts.push_back(cc.x); cs->cpuVerts.push_back(cc.y); cs->cpuVerts.push_back(cc.z); cs->cpuVerts.push_back(cc.w);
            CanvasBatch b; b.mode = GL_TRIANGLES; b.start = (int)start; b.count = 3; cs->batches.push_back(b);
        } else {
            std::vector<float> pts = { x1, y1, x2, y2, x3, y3 };
            appendStrokeStrip(cs, pts, thickness, cc);
        }
        lua_getglobal(L, "canvas"); if (lua_istable(L, -1)){ lua_getfield(L,-1,"draw_count"); int c=(int)lua_tointeger(L,-1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
        return 0;
    }

    // batching-only mode: do not perform immediate GL draws
    PLOGW << "lua:canvas immediate draw requested but batching-only mode; skipping draw";
    return 0;
}

static int l_canvas_rounded_rect(lua_State *L)
{
    // Approximate rounded rect as plain rect for GL-backed canvas (rounded corners not implemented)
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float w = (float)luaL_checknumber(L, 3);
    float h = (float)luaL_checknumber(L, 4);
    ImU32 col = parseColor(L, 6);
    bool filled = lua_toboolean(L, 7);
    float thickness = lua_isnumber(L, 8) ? (float)lua_tonumber(L, 8) : 1.0f;
    // reuse rect implementation
    lua_pushnumber(L, x); lua_pushnumber(L, y); lua_pushnumber(L, w); lua_pushnumber(L, h); lua_pushinteger(L, col); lua_pushboolean(L, filled); lua_pushnumber(L, thickness);
    int ret = l_canvas_rect(L);
    // l_canvas_rect already increments draw_count
    return ret;
}

static int l_canvas_bezier(lua_State *L)
{
    // Evaluate cubic Bezier into line segments and draw as polyline
    float x1 = (float)luaL_checknumber(L, 1);
    float y1 = (float)luaL_checknumber(L, 2);
    float x2 = (float)luaL_checknumber(L, 3);
    float y2 = (float)luaL_checknumber(L, 4);
    float x3 = (float)luaL_checknumber(L, 5);
    float y3 = (float)luaL_checknumber(L, 6);
    float x4 = (float)luaL_checknumber(L, 7);
    float y4 = (float)luaL_checknumber(L, 8);
    ImU32 col = parseColor(L, 9);
    float thickness = lua_isnumber(L, 10) ? (float)lua_tonumber(L, 10) : 1.0f;
    int segments = lua_isinteger(L, 11) ? (int)lua_tointeger(L, 11) : 32;

    std::vector<float> pts; pts.reserve((segments+1)*2);
    for (int i=0;i<=segments;++i){ float t=(float)i/(float)segments; float u=1.0f-t; float bx = u*u*u*x1 + 3*u*u*t*x2 + 3*u*t*t*x3 + t*t*t*x4; float by = u*u*u*y1 + 3*u*u*t*y2 + 3*u*t*t*y3 + t*t*t*y4; pts.push_back(bx); pts.push_back(by); }

    // attempt buffered path
    void *cup = lua_touserdata(L, lua_upvalueindex(5));
    CanvasState *cs = (CanvasState*)cup;
    GLuint prog = (GLuint)lua_tointeger(L, lua_upvalueindex(4));
    if (cs) {
        ImVec4 cc = ImGui::ColorConvertU32ToFloat4(col);
        appendStrokeStrip(cs, pts, thickness, cc);
        lua_getglobal(L, "canvas"); if (lua_istable(L,-1)){ lua_getfield(L,-1,"draw_count"); int c=(int)lua_tointeger(L,-1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
        return 0;
    }

    // batching-only mode: do not perform immediate GL draws
    PLOGW << "lua:canvas immediate draw requested but batching-only mode; skipping draw";
    return 0;


}

static int l_canvas_text_size(lua_State *L)
{
    const char *s = lua_tostring(L, 1);
    if (!s) { lua_pushnumber(L, 0); lua_pushnumber(L, 0); return 2; }
    // Approximate text extents: width ~ 8px per char, height ~ 14px
    int len = (int)strlen(s);
    lua_pushnumber(L, (double)(len * 8));
    lua_pushnumber(L, 14.0);
    return 2;
}

static int l_canvas_push_clip(lua_State *L)
{
    float x = (float)luaL_checknumber(L, 1);
    float y = (float)luaL_checknumber(L, 2);
    float w = (float)luaL_checknumber(L, 3);
    float h = (float)luaL_checknumber(L, 4);
    // Convert top-left y to GL scissor bottom-left origin
    int fbH = (int)lua_tointeger(L, lua_upvalueindex(3));
    int sy = fbH - (int)(y + h);
    glEnable(GL_SCISSOR_TEST);
    glScissor((int)x, sy, (int)w, (int)h);
    return 0;
}

static int l_canvas_pop_clip(lua_State *L)
{
    glDisable(GL_SCISSOR_TEST);
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
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0.1, 0.1, 0.1, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    canvas.circle(160, 120, 40, {0, 1, 0, 1}, true)
    canvas.rect(10, 10, 80, 40, {1, 0, 0, 1}, true)
    print("draw calls this frame: " .. canvas.draw_count)
end
    )", __FILE__);

    // width
    lua_pushinteger(L, width);
    lua_pushcclosure(L, l_canvas_width, 1);
    lua_setfield(L, -2, "width");
        LuaBindingDocs::get().registerDoc("canvas.width", "width -> int", "Get canvas width in pixels", R"(
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0.1,0.1,0.1,1})
    canvas.viewport(0,0,canvas.width,canvas.height)
    local w = canvas.width; local h = canvas.height
    canvas.rect(0,0,w,h, {0.1,0.1,0.1,1}, true)
end
)", __FILE__);

    // height
    lua_pushinteger(L, height);
    lua_pushcclosure(L, l_canvas_height, 1);
    lua_setfield(L, -2, "height");
        LuaBindingDocs::get().registerDoc("canvas.height", "height -> int", "Get canvas height in pixels", R"(
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0.15,0.15,0.15,1})
    canvas.viewport(0,0,canvas.width,canvas.height)
    local w = canvas.width; local h = canvas.height
    canvas.rect(0,0,w,h, {0.15,0.15,0.15,1}, true)
end
)", __FILE__);

    // circle (captures origin.x and origin.y)
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushcclosure(L, l_canvas_circle, 2);
    lua_setfield(L, -2, "circle");
    // doc
    LuaBindingDocs::get().registerDoc("canvas.circle", "circle(cx, cy, r, color, filled, thickness)", "Draw a circle at local canvas coordinates", R"(
-- Full canvas example: stroked and filled circles
function Config()
  return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
  canvas.clear({0,0,0,1})
  canvas.viewport(0,0,canvas.width,canvas.height)
  canvas.circle(canvas.width/2, canvas.height/2, 48, {0,1,0,1}, false, 4)
  canvas.circle(canvas.width/4, canvas.height/4, 20, {1,0,0,1}, true)
end
)", __FILE__);

    // text (captures origin.x and origin.y)
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushcclosure(L, l_canvas_text, 2);
    lua_setfield(L, -2, "text");
    LuaBindingDocs::get().registerDoc("canvas.text", "text(x, y, string, color)", "Draw text at local canvas coordinates (text rendering is a no-op in this environment)", R"(
-- Full canvas example: measure text using text_size (canvas.text is a no-op here)
function Config()
  return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
  canvas.clear({0.15,0.15,0.15,1})
  canvas.viewport(0,0,canvas.width,canvas.height)
  local tw, th = canvas.text_size("Hello World")
  canvas.rect((320-tw)/2, (240-th)/2, tw+10, th+6, {0,0,0,0.6}, true)
end
)", __FILE__);

    // rect(x,y,w,h,color,filled)
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushcclosure(L, l_canvas_rect, 2);
    lua_setfield(L, -2, "rect");
    LuaBindingDocs::get().registerDoc("canvas.rect", "rect(x, y, w, h, color, filled, thickness)", "Draw a rectangle", R"(
-- Full canvas example: filled and framed rectangles
function Config()
  return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
  canvas.clear({0.05,0.05,0.05,1})
  canvas.viewport(0,0,canvas.width,canvas.height)
  local w = canvas.width; local h = canvas.height
  canvas.rect(w/2 - 50, h/2 - 20, 100, 40, {0,0,1,1}, false, 2)
  canvas.rect(10,10, 80, 40, {0.2,0.6,1,1}, true)
end
)", __FILE__);
    // line(x1,y1,x2,y2,color,thickness)
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushcclosure(L, l_canvas_line, 2);
    lua_setfield(L, -2, "line");
    LuaBindingDocs::get().registerDoc("canvas.line", "line(x1, y1, x2, y2, color, thickness)", "Draw a line between two points", R"(
-- Full canvas example: draw an X using stroked lines
function Config()
  return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
  canvas.clear({0,0,0,1})
  canvas.viewport(0,0,canvas.width,canvas.height)
  local w = canvas.width; local h = canvas.height
  canvas.line(0,0, w, h, {1,0,0,1}, 3)
  canvas.line(0,h, w, 0, {1,0,0,1}, 3)
end
)", __FILE__);


    // poly(points_table, color, filled)
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushcclosure(L, l_canvas_poly, 2);
    lua_setfield(L, -2, "poly");
    LuaBindingDocs::get().registerDoc("canvas.poly", "poly(points_table, color, filled)", "Draw polygon from point table", R"(
-- Full canvas example: filled and stroked polygon
function Config()
  return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
  canvas.clear({0.08,0.08,0.08,1})
  canvas.viewport(0,0,canvas.width,canvas.height)
  local cx, cy = canvas.width/2, canvas.height/2
  canvas.poly({{cx-30,cy+20},{cx,cy-30},{cx+30,cy+20}}, {0.5,0.8,0.2,1}, true)
  canvas.poly({{20,20},{80,40},{60,100},{10,80}}, {1,0.5,0,1}, false)
end
)", __FILE__);

        // triangle(x1,y1,x2,y2,x3,y3,color,filled,thickness)
        lua_pushnumber(L, origin.x);
        lua_pushnumber(L, origin.y);
        lua_pushcclosure(L, l_canvas_triangle, 2);
        lua_setfield(L, -2, "triangle");
        LuaBindingDocs::get().registerDoc("canvas.triangle", "triangle(x1,y1,x2,y2,x3,y3,color,filled,thickness)", "Draw a triangle (filled or stroked)", R"(
-- Full canvas example: filled and stroked triangle
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0.12,0.12,0.12,1})
    canvas.viewport(0,0,canvas.width,canvas.height)
    local w = canvas.width; local h = canvas.height
    local cx, cy = w/2, h/2
    canvas.triangle(cx-40, cy+30, cx, cy-30, cx+40, cy+30, {0.2,0.6,1,1}, true)
    canvas.triangle(10,10, 60,20, 40,70, {1,1,0,1}, false, 3)
end
)", __FILE__);

        // rounded_rect(x,y,w,h,rounding,color,filled,thickness)
        lua_pushnumber(L, origin.x);
        lua_pushnumber(L, origin.y);
        lua_pushcclosure(L, l_canvas_rounded_rect, 2);
        lua_setfield(L, -2, "rounded_rect");
        LuaBindingDocs::get().registerDoc("canvas.rounded_rect", "rounded_rect(x,y,w,h,rounding,color,filled,thickness)", "Draw a rectangle with rounded corners (rounded corners approximated)", R"(
-- Full canvas example: rounded frame (approximate)
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0.1,0.1,0.1,1})
    canvas.viewport(0,0,canvas.width,canvas.height)
    canvas.rounded_rect(10,10, canvas.width-20, canvas.height-20, 8, {0,0,0,0.6}, false, 3)
end
)", __FILE__);

        // bezier(p1x,p1y,p2x,p2y,p3x,p3y,p4x,p4y,color,thickness,segments)
        lua_pushnumber(L, origin.x);
        lua_pushnumber(L, origin.y);
        lua_pushcclosure(L, l_canvas_bezier, 2);
        lua_setfield(L, -2, "bezier");
        LuaBindingDocs::get().registerDoc("canvas.bezier", "bezier(p1x,p1y,p2x,p2y,p3x,p3y,p4x,p4y,color,thickness,segments)", "Draw a cubic Bezier curve", R"(
-- Full canvas example: stroked Bezier curve
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0.12,0.12,0.12,1})
    canvas.viewport(0,0,canvas.width,canvas.height)
    canvas.bezier(20,canvas.height-20, 120,20, 200,canvas.height-20, 300,20, {1,0.5,0,1}, 3, 48)
end
)", __FILE__);

    // text_size(s)
    lua_pushcfunction(L, l_canvas_text_size);
    lua_setfield(L, -2, "text_size");
        LuaBindingDocs::get().registerDoc("canvas.text_size", "text_size(s) -> width, height", "Measure text extents in canvas-local units", R"(
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0.1,0.1,0.1,1})
    canvas.viewport(0,0,canvas.width,canvas.height)
    local tw, th = canvas.text_size("Hello World")
    canvas.rect(5,5, tw+10, th+6, {0,0,0,0.5}, true)
end
)", __FILE__);

    // clip push/pop
    lua_pushnumber(L, origin.x);
    lua_pushnumber(L, origin.y);
    lua_pushcclosure(L, l_canvas_push_clip, 2);
    lua_setfield(L, -2, "push_clip");
    lua_pushcfunction(L, l_canvas_pop_clip);
    lua_setfield(L, -2, "pop_clip");
    LuaBindingDocs::get().registerDoc("canvas.push_clip", "push_clip(x, y, w, h)", "Push a scissor clip region relative to the canvas", R"(
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0.1, 0.1, 0.1, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    canvas.push_clip(50, 50, 220, 140)
    canvas.rect(0, 0, canvas.width, canvas.height, {0, 1, 0, 1}, true)
    canvas.pop_clip()
end
)", __FILE__);
    LuaBindingDocs::get().registerDoc("canvas.pop_clip", "pop_clip()", "Pop last scissor clip", R"(
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0.1, 0.1, 0.1, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    canvas.push_clip(50, 50, 220, 140)
    canvas.circle(160, 120, 80, {1, 0, 0, 1}, true)
    canvas.pop_clip()
end
)", __FILE__);

    // image (stub)
    lua_pushcfunction(L, l_canvas_image);
    lua_setfield(L, -2, "image");
    LuaBindingDocs::get().registerDoc("canvas.image", "image(keyOrUrl, x, y, w, h)", "Render an image by key or URL (stubbed in this environment)", R"(
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0.15, 0.15, 0.15, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    canvas.image('my_image_key', 10, 10, 64, 64)
    canvas.rect(10, 10, 64, 64, {1, 1, 1, 0.3}, false, 1)
end
)", __FILE__);

    lua_setglobal(L, "canvas");

    // Enforce that all exported functions on the 'canvas' table have docs. This will throw if any are missing.
    LuaBindingDocsUtil::enforceTableHasDocs(L, "canvas");
}

static GLuint compileShaderProgram(const char *vsSrc, const char *fsSrc)
{
    GLuint vs = glCreateShader(GL_VERTEX_SHADER);
    glShaderSource(vs, 1, &vsSrc, nullptr);
    glCompileShader(vs);
    GLint ok = 0; glGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) { char buf[1024]; glGetShaderInfoLog(vs, 1024, nullptr, buf); PLOGE << "VS compile: " << buf; glDeleteShader(vs); return 0; }

    GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
    glShaderSource(fs, 1, &fsSrc, nullptr);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) { char buf[1024]; glGetShaderInfoLog(fs, 1024, nullptr, buf); PLOGE << "FS compile: " << buf; glDeleteShader(vs); glDeleteShader(fs); return 0; }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, vs);
    glAttachShader(prog, fs);
    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) { char buf[1024]; glGetProgramInfoLog(prog, 1024, nullptr, buf); PLOGE << "Link: " << buf; glDeleteProgram(prog); prog = 0; }
    glDeleteShader(vs);
    glDeleteShader(fs);
    return prog;
}

    // (moved later)

// Simple fullscreen quad for draw_fullscreen
static GLuint s_quadVAO = 0;
static void ensureQuad()
{
    if (s_quadVAO != 0) return;
    GLuint vbo;
    GLfloat verts[] = {
        // pos   // uv
        -1.0f, -1.0f, 0.0f, 0.0f,
         1.0f, -1.0f, 1.0f, 0.0f,
        -1.0f,  1.0f, 0.0f, 1.0f,
         1.0f,  1.0f, 1.0f, 1.0f,
    };
    glGenVertexArrays(1, &s_quadVAO);
    glGenBuffers(1, &vbo);
    glBindVertexArray(s_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}



// canvas.clear({r,g,b,a}) — accepts a color table
static int l_canvas_clear_gl(lua_State *L)
{
    float r = 0.0f, g = 0.0f, b = 0.0f, a = 1.0f;
    if (lua_istable(L, 1)) {
        lua_rawgeti(L, 1, 1); r = (float)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_rawgeti(L, 1, 2); g = (float)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_rawgeti(L, 1, 3); b = (float)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_rawgeti(L, 1, 4); a = (float)lua_tonumber(L, -1); lua_pop(L, 1);
    }
    glClearColor(r,g,b,a);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    // increment draw_count
    lua_getglobal(L, "canvas");
    if (lua_istable(L, -1)){
        lua_getfield(L, -1, "draw_count"); int c = (int)lua_tointeger(L, -1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count");
    }
    lua_pop(L,1);
    return 0;
}

// canvas.viewport(x,y,w,h) — set OpenGL viewport
static int l_canvas_viewport_gl(lua_State *L)
{
    int x = (int)luaL_checkinteger(L, 1);
    int y = (int)luaL_checkinteger(L, 2);
    int w = (int)luaL_checkinteger(L, 3);
    int h = (int)luaL_checkinteger(L, 4);
    glViewport(x, y, w, h);
    return 0;
}

// Helper: push a 4x4 matrix (16 floats) as a flat Lua table
static void pushMatrix4(lua_State *L, const float m[16])
{
    lua_createtable(L, 16, 0);
    for (int i = 0; i < 16; ++i) {
        lua_pushnumber(L, m[i]);
        lua_rawseti(L, -2, i + 1);
    }
}

// Helper: read a flat 16-element Lua table into a float[16]
static bool readMatrix4(lua_State *L, int idx, float out[16])
{
    if (!lua_istable(L, idx)) return false;
    for (int i = 0; i < 16; ++i) {
        lua_rawgeti(L, idx, i + 1);
        out[i] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    return true;
}

// canvas.viewMatrix(mat) — set the view matrix uniform on the currently bound shader
static int l_canvas_view_matrix(lua_State *L)
{
    float m[16];
    if (!readMatrix4(L, 1, m)) return luaL_error(L, "viewMatrix expects a table of 16 floats");
    // Find uniform in currently bound program
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog) {
        GLint loc = glGetUniformLocation((GLuint)prog, "uView");
        if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, m);
    }
    return 0;
}

// canvas.projectionMatrix(mat) — set the projection matrix uniform on the currently bound shader
static int l_canvas_projection_matrix(lua_State *L)
{
    float m[16];
    if (!readMatrix4(L, 1, m)) return luaL_error(L, "projectionMatrix expects a table of 16 floats");
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog) {
        GLint loc = glGetUniformLocation((GLuint)prog, "uProjection");
        if (loc >= 0) glUniformMatrix4fv(loc, 1, GL_FALSE, m);
    }
    return 0;
}

// canvas.lookAt(eye, target, up) — returns a 4x4 view matrix table
static int l_canvas_look_at(lua_State *L)
{
    // Each argument is a table {x,y,z}
    auto readVec3 = [](lua_State *L, int idx, float out[3]) {
        lua_rawgeti(L, idx, 1); out[0] = (float)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_rawgeti(L, idx, 2); out[1] = (float)lua_tonumber(L, -1); lua_pop(L, 1);
        lua_rawgeti(L, idx, 3); out[2] = (float)lua_tonumber(L, -1); lua_pop(L, 1);
    };
    float eye[3], target[3], up[3];
    readVec3(L, 1, eye);
    readVec3(L, 2, target);
    readVec3(L, 3, up);

    // Compute lookAt matrix (column-major, OpenGL convention)
    float f[3] = { target[0]-eye[0], target[1]-eye[1], target[2]-eye[2] };
    float flen = sqrtf(f[0]*f[0]+f[1]*f[1]+f[2]*f[2]);
    if (flen > 1e-8f) { f[0]/=flen; f[1]/=flen; f[2]/=flen; }

    // s = f x up
    float s[3] = { f[1]*up[2]-f[2]*up[1], f[2]*up[0]-f[0]*up[2], f[0]*up[1]-f[1]*up[0] };
    float slen = sqrtf(s[0]*s[0]+s[1]*s[1]+s[2]*s[2]);
    if (slen > 1e-8f) { s[0]/=slen; s[1]/=slen; s[2]/=slen; }

    // u = s x f
    float u[3] = { s[1]*f[2]-s[2]*f[1], s[2]*f[0]-s[0]*f[2], s[0]*f[1]-s[1]*f[0] };

    float m[16] = {
        s[0],  u[0], -f[0], 0,
        s[1],  u[1], -f[1], 0,
        s[2],  u[2], -f[2], 0,
        -(s[0]*eye[0]+s[1]*eye[1]+s[2]*eye[2]),
        -(u[0]*eye[0]+u[1]*eye[1]+u[2]*eye[2]),
         (f[0]*eye[0]+f[1]*eye[1]+f[2]*eye[2]),
        1
    };
    pushMatrix4(L, m);
    return 1;
}

// canvas.ortho(left, right, bottom, top, near, far) — returns a 4x4 orthographic projection matrix
static int l_canvas_ortho(lua_State *L)
{
    float l = (float)luaL_checknumber(L, 1);
    float r = (float)luaL_checknumber(L, 2);
    float b = (float)luaL_checknumber(L, 3);
    float t = (float)luaL_checknumber(L, 4);
    float n = (float)luaL_checknumber(L, 5);
    float f = (float)luaL_checknumber(L, 6);

    float m[16] = {};
    m[0]  = 2.0f / (r - l);
    m[5]  = 2.0f / (t - b);
    m[10] = -2.0f / (f - n);
    m[12] = -(r + l) / (r - l);
    m[13] = -(t + b) / (t - b);
    m[14] = -(f + n) / (f - n);
    m[15] = 1.0f;
    pushMatrix4(L, m);
    return 1;
}

// canvas.perspective(fovDeg, aspect, near, far) — returns a 4x4 perspective projection matrix
static int l_canvas_perspective(lua_State *L)
{
    float fovDeg = (float)luaL_checknumber(L, 1);
    float aspect = (float)luaL_checknumber(L, 2);
    float nearP  = (float)luaL_checknumber(L, 3);
    float farP   = (float)luaL_checknumber(L, 4);

    float fovRad = fovDeg * (float)M_PI / 180.0f;
    float tanHalf = tanf(fovRad / 2.0f);

    float m[16] = {};
    m[0]  = 1.0f / (aspect * tanHalf);
    m[5]  = 1.0f / tanHalf;
    m[10] = -(farP + nearP) / (farP - nearP);
    m[11] = -1.0f;
    m[14] = -(2.0f * farP * nearP) / (farP - nearP);
    pushMatrix4(L, m);
    return 1;
}

// canvas.createVertexBuffer(verts_table) — create a GL VBO from a flat table of floats, returns VBO id
static int l_canvas_create_vertex_buffer(lua_State *L)
{
    luaL_checktype(L, 1, LUA_TTABLE);
    int n = (int)lua_rawlen(L, 1);
    std::vector<float> data(n);
    for (int i = 0; i < n; ++i) {
        lua_rawgeti(L, 1, i + 1);
        data[i] = (float)lua_tonumber(L, -1);
        lua_pop(L, 1);
    }
    GLuint vbo;
    glGenBuffers(1, &vbo);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, n * sizeof(float), data.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    lua_pushinteger(L, (lua_Integer)vbo);
    return 1;
}

// canvas.bindVertexBuffer(vbo) — bind a VBO and set up standard vertex attribs (vec2 pos + vec4 color, stride 6)
static int l_canvas_bind_vertex_buffer(lua_State *L)
{
    GLuint vbo = (GLuint)luaL_checkinteger(L, 1);
    // Ensure a VAO is bound — use a static shared VAO for user VBOs
    static GLuint s_userVAO = 0;
    if (s_userVAO == 0) glGenVertexArrays(1, &s_userVAO);
    glBindVertexArray(s_userVAO);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    GLsizei stride = 6 * sizeof(float); // vec2 pos + vec4 color
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, stride, (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, stride, (void*)(2 * sizeof(float)));
    return 0;
}

// canvas.drawArrays(mode, first, count) — draw from the currently bound VBO
static int l_canvas_draw_arrays(lua_State *L)
{
    const char *modeStr = luaL_checkstring(L, 1);
    int first = (int)luaL_checkinteger(L, 2);
    int count = (int)luaL_checkinteger(L, 3);
    GLenum mode = GL_TRIANGLES;
    if (strcmp(modeStr, "triangles") == 0) mode = GL_TRIANGLES;
    else if (strcmp(modeStr, "triangle_strip") == 0) mode = GL_TRIANGLE_STRIP;
    else if (strcmp(modeStr, "triangle_fan") == 0) mode = GL_TRIANGLE_FAN;
    else if (strcmp(modeStr, "lines") == 0) mode = GL_LINES;
    else if (strcmp(modeStr, "line_strip") == 0) mode = GL_LINE_STRIP;
    else if (strcmp(modeStr, "points") == 0) mode = GL_POINTS;

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDrawArrays(mode, first, count);
    glDisable(GL_BLEND);

    // increment draw_count
    lua_getglobal(L, "canvas");
    if (lua_istable(L, -1)){
        lua_getfield(L, -1, "draw_count"); int c = (int)lua_tointeger(L, -1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count");
    }
    lua_pop(L,1);
    return 0;
}

// canvas.setShaderUniformVec2(name, {x,y}) — set a vec2 uniform on the currently bound shader
static int l_canvas_set_uniform_vec2(lua_State *L)
{
    const char *name = luaL_checkstring(L, 1);
    luaL_checktype(L, 2, LUA_TTABLE);
    lua_rawgeti(L, 2, 1); float x = (float)lua_tonumber(L, -1); lua_pop(L, 1);
    lua_rawgeti(L, 2, 2); float y = (float)lua_tonumber(L, -1); lua_pop(L, 1);
    GLint prog = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &prog);
    if (prog) {
        GLint loc = glGetUniformLocation((GLuint)prog, name);
        if (loc >= 0) glUniform2f(loc, x, y);
    }
    return 0;
}

static int l_canvas_upload_shader(lua_State *L)
{
    const char *vs = luaL_checkstring(L,1);
    const char *fs = luaL_checkstring(L,2);
    GLuint prog = compileShaderProgram(vs, fs);
    lua_pushinteger(L, (lua_Integer)prog);
    return 1;
}

static int l_canvas_use_shader(lua_State *L)
{
    GLuint prog = (GLuint)luaL_checkinteger(L,1);
    glUseProgram(prog);
    return 0;
}

static int l_canvas_set_uniform(lua_State *L)
{
    GLuint prog = (GLuint)luaL_checkinteger(L,1);
    const char *name = luaL_checkstring(L,2);
    float v = (float)luaL_checknumber(L,3);
    GLint loc = glGetUniformLocation(prog, name);
    if (loc >= 0) glUniform1f(loc, v);
    return 0;
}

static int l_canvas_create_texture(lua_State *L)
{
    // expects a string with raw RGBA bytes followed by width and height
    size_t len = 0; const char *data = luaL_checklstring(L,1,&len);
    int w = (int)luaL_checkinteger(L,2);
    int h = (int)luaL_checkinteger(L,3);
    GLuint tex; glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, (const void*)data);
    glBindTexture(GL_TEXTURE_2D, 0);
    lua_pushinteger(L, (lua_Integer)tex);
    return 1;
}

static int l_canvas_bind_texture(lua_State *L)
{
    int unit = (int)luaL_checkinteger(L,1);
    GLuint tex = (GLuint)luaL_checkinteger(L,2);
    glActiveTexture(GL_TEXTURE0 + unit);
    glBindTexture(GL_TEXTURE_2D, tex);
    return 0;
}

static int l_canvas_draw_fullscreen(lua_State *L)
{
    GLuint tex = (GLuint)luaL_checkinteger(L,1);
    GLuint shader = (GLuint)luaL_optinteger(L,2,0);
    ensureQuad();
    if (shader) glUseProgram(shader);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, tex);
    glBindVertexArray(s_quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);
    if (shader) glUseProgram(0);

    // increment draw_count
    lua_getglobal(L, "canvas");
    if (lua_istable(L, -1)){
        lua_getfield(L, -1, "draw_count"); int c = (int)lua_tointeger(L, -1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count");
    }
    lua_pop(L,1);
    return 0;
}

void registerLuaGLCanvasBindings(lua_State *L, const std::string &embedID, unsigned int textureId, int width, int height)
{
    // Create a new canvas table
    lua_newtable(L);
    lua_pushinteger(L, 0);
    lua_setfield(L, -2, "draw_count");

    // clear
    lua_pushcfunction(L, l_canvas_clear_gl);
    lua_setfield(L, -2, "clear");
    LuaBindingDocs::get().registerDoc("canvas.clear", "clear({r,g,b,a})", "Clear the canvas FBO with the given color table.", R"(
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0.2, 0.1, 0.3, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    canvas.rect(60, 80, 200, 80, {1, 1, 1, 1}, true)
end
)", __FILE__);

    // viewport
    lua_pushcfunction(L, l_canvas_viewport_gl);
    lua_setfield(L, -2, "viewport");
    LuaBindingDocs::get().registerDoc("canvas.viewport", "viewport(x,y,w,h)", "Set the GL viewport rectangle.", R"(
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0, 0, 0, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    canvas.rect(10, 10, 100, 60, {0, 1, 0, 1}, true)
end
)", __FILE__);

    // view/projection matrix
    lua_pushcfunction(L, l_canvas_view_matrix);
    lua_setfield(L, -2, "viewMatrix");
    LuaBindingDocs::get().registerDoc("canvas.viewMatrix", "viewMatrix(mat)", "Set the view matrix uniform (uView) on the current shader. mat is a flat table of 16 floats (column-major).", R"(
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0.1, 0.1, 0.1, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    local view = canvas.lookAt({0,0,5}, {0,0,0}, {0,1,0})
    canvas.viewMatrix(view)
    canvas.rect(100, 80, 120, 80, {0.4, 0.7, 1, 1}, true)
end
)", __FILE__);
    lua_pushcfunction(L, l_canvas_projection_matrix);
    lua_setfield(L, -2, "projectionMatrix");
    LuaBindingDocs::get().registerDoc("canvas.projectionMatrix", "projectionMatrix(mat)", "Set the projection matrix uniform (uProjection) on the current shader. mat is a flat table of 16 floats (column-major).", R"(
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0.1, 0.1, 0.1, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    local proj = canvas.ortho(0, canvas.width, 0, canvas.height, 0.1, 100)
    canvas.projectionMatrix(proj)
    canvas.rect(40, 40, 240, 160, {0.8, 0.2, 0.4, 1}, true)
end
)", __FILE__);

    // lookAt / ortho / perspective helpers
    lua_pushcfunction(L, l_canvas_look_at);
    lua_setfield(L, -2, "lookAt");
    LuaBindingDocs::get().registerDoc("canvas.lookAt", "lookAt(eye, target, up) -> mat", "Compute a 4x4 lookAt view matrix. Arguments are {x,y,z} tables. Returns a flat table of 16 floats.", R"(
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0.05, 0.05, 0.1, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    local view = canvas.lookAt({0,0,5}, {0,0,0}, {0,1,0})
    canvas.viewMatrix(view)
    canvas.circle(160, 120, 40, {0, 1, 0.5, 1}, true)
end
)", __FILE__);
    lua_pushcfunction(L, l_canvas_ortho);
    lua_setfield(L, -2, "ortho");
    LuaBindingDocs::get().registerDoc("canvas.ortho", "ortho(left,right,bottom,top,near,far) -> mat", "Compute a 4x4 orthographic projection matrix. Returns a flat table of 16 floats.", R"(
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0.1, 0.1, 0.1, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    local proj = canvas.ortho(0, 320, 0, 240, 0.1, 100)
    canvas.projectionMatrix(proj)
    canvas.rect(60, 60, 200, 120, {0.2, 0.6, 1, 1}, true)
end
)", __FILE__);
    lua_pushcfunction(L, l_canvas_perspective);
    lua_setfield(L, -2, "perspective");
    LuaBindingDocs::get().registerDoc("canvas.perspective", "perspective(fovDeg,aspect,near,far) -> mat", "Compute a 4x4 perspective projection matrix. Returns a flat table of 16 floats.", R"(
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0.08, 0.08, 0.12, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    local proj = canvas.perspective(60, canvas.width / canvas.height, 0.1, 100)
    canvas.projectionMatrix(proj)
    canvas.triangle(160, 40, 60, 200, 260, 200, {1, 0.5, 0, 1}, true)
end
)", __FILE__);

    // shader management
    lua_pushcfunction(L, l_canvas_upload_shader);
    lua_setfield(L, -2, "createShader");
    LuaBindingDocs::get().registerDoc("canvas.createShader", "createShader(vs, fs) -> prog", "Compile and link a shader program from vertex/fragment source strings.", R"(
local vs_src = [[
#version 330 core
layout(location=0) in vec2 in_pos;
layout(location=1) in vec4 in_color;
out vec4 v_color;
uniform vec2 uSize;
void main(){
    v_color = in_color;
    vec2 p = in_pos / uSize * 2.0 - 1.0;
    gl_Position = vec4(p, 0, 1);
}
]]

local fs_src = [[
#version 330 core
in vec4 v_color;
out vec4 out_color;
void main(){ out_color = v_color; }
]]

shader = nil
vbo = nil

function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    if not shader then
        shader = canvas.createShader(vs_src, fs_src)
        vbo = canvas.createVertexBuffer({
            100,80,  1,0,0,1,
            220,80,  0,1,0,1,
            160,180, 0,0,1,1
        })
    end
    canvas.clear({0.1, 0.1, 0.1, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    canvas.useShader(shader)
    canvas.setShaderUniformVec2("uSize", {canvas.width, canvas.height})
    canvas.bindVertexBuffer(vbo)
    canvas.drawArrays("triangles", 0, 3)
end
)", __FILE__);
    lua_pushcfunction(L, l_canvas_use_shader);
    lua_setfield(L, -2, "useShader");
    LuaBindingDocs::get().registerDoc("canvas.useShader", "useShader(prog)", "Bind a shader program for subsequent draws.", R"(
local vs = [[
#version 330 core
layout(location=0) in vec2 in_pos;
layout(location=1) in vec4 in_color;
out vec4 v_color;
uniform vec2 uSize;
void main(){
    v_color = in_color;
    vec2 p = in_pos / uSize * 2.0 - 1.0;
    gl_Position = vec4(p, 0, 1);
}
]]

local fs = [[
#version 330 core
in vec4 v_color;
out vec4 out_color;
void main(){ out_color = v_color; }
]]

shader = nil
vbo = nil

function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    if not shader then
        shader = canvas.createShader(vs, fs)
        vbo = canvas.createVertexBuffer({
            60,40,   0.2,0.5,1,1,
            260,40,  0.2,0.5,1,1,
            260,200, 0.2,0.5,1,1,
            60,40,   0.2,0.5,1,1,
            260,200, 0.2,0.5,1,1,
            60,200,  0.2,0.5,1,1
        })
    end
    canvas.clear({0.05, 0.05, 0.05, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    canvas.useShader(shader)
    canvas.setShaderUniformVec2("uSize", {canvas.width, canvas.height})
    canvas.bindVertexBuffer(vbo)
    canvas.drawArrays("triangles", 0, 6)
end
)", __FILE__);
    lua_pushcfunction(L, l_canvas_set_uniform);
    lua_setfield(L, -2, "set_uniform");
    LuaBindingDocs::get().registerDoc("canvas.set_uniform", "set_uniform(prog, name, value)", "Set a float uniform on the given program.", R"(
local vs = [[
#version 330 core
layout(location=0) in vec2 in_pos;
layout(location=1) in vec4 in_color;
out vec4 v_color;
uniform vec2 uSize;
void main(){
    v_color = in_color;
    vec2 p = in_pos / uSize * 2.0 - 1.0;
    gl_Position = vec4(p, 0, 1);
}
]]

local fs = [[
#version 330 core
in vec4 v_color;
out vec4 out_color;
uniform float uBrightness;
void main(){
    out_color = v_color * uBrightness;
}
]]

shader = nil
vbo = nil
totalTime = 0

function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    totalTime = totalTime + dt
    if not shader then
        shader = canvas.createShader(vs, fs)
        vbo = canvas.createVertexBuffer({
            100,60,  1,0.8,0.2,1,
            220,60,  1,0.8,0.2,1,
            160,180, 1,0.8,0.2,1
        })
    end
    canvas.clear({0.1, 0.1, 0.1, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    canvas.useShader(shader)
    canvas.setShaderUniformVec2("uSize", {canvas.width, canvas.height})
    -- Pulse brightness between 0.3 and 1.0 using set_uniform
    local brightness = 0.65 + 0.35 * math.sin(totalTime * 3)
    canvas.set_uniform(shader, "uBrightness", brightness)
    canvas.bindVertexBuffer(vbo)
    canvas.drawArrays("triangles", 0, 3)
end
)", __FILE__);
    lua_pushcfunction(L, l_canvas_set_uniform_vec2);
    lua_setfield(L, -2, "setShaderUniformVec2");
    LuaBindingDocs::get().registerDoc("canvas.setShaderUniformVec2", "setShaderUniformVec2(name, {x,y})", "Set a vec2 uniform on the currently bound shader.", R"(
local vs = [[
#version 330 core
layout(location=0) in vec2 in_pos;
layout(location=1) in vec4 in_color;
out vec4 v_color;
uniform vec2 uSize;
void main(){
    v_color = in_color;
    vec2 p = in_pos / uSize * 2.0 - 1.0;
    gl_Position = vec4(p, 0, 1);
}
]]

local fs = [[
#version 330 core
in vec4 v_color;
out vec4 out_color;
void main(){ out_color = v_color; }
]]

shader = nil
vbo = nil

function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    if not shader then
        shader = canvas.createShader(vs, fs)
        -- Diamond shape via 4 triangles
        local cx, cy = 160, 120
        local sz = 60
        vbo = canvas.createVertexBuffer({
            cx,cy-sz,     0.5,1,0.5,1,
            cx+sz,cy,     0.3,0.8,0.3,1,
            cx,cy,        0.4,0.9,0.4,1,
            cx+sz,cy,     0.3,0.8,0.3,1,
            cx,cy+sz,     0.5,1,0.5,1,
            cx,cy,        0.4,0.9,0.4,1,
            cx,cy+sz,     0.5,1,0.5,1,
            cx-sz,cy,     0.3,0.8,0.3,1,
            cx,cy,        0.4,0.9,0.4,1,
            cx-sz,cy,     0.3,0.8,0.3,1,
            cx,cy-sz,     0.5,1,0.5,1,
            cx,cy,        0.4,0.9,0.4,1
        })
    end
    canvas.clear({0.05, 0.05, 0.08, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    canvas.useShader(shader)
    -- setShaderUniformVec2 passes canvas size so the shader can map pixel coords to NDC
    canvas.setShaderUniformVec2("uSize", {canvas.width, canvas.height})
    canvas.bindVertexBuffer(vbo)
    canvas.drawArrays("triangles", 0, 12)
end
)", __FILE__);

    // VBO management
    lua_pushcfunction(L, l_canvas_create_vertex_buffer);
    lua_setfield(L, -2, "createVertexBuffer");
    LuaBindingDocs::get().registerDoc("canvas.createVertexBuffer", "createVertexBuffer(verts) -> vbo", "Create a GL VBO from a flat table of floats (x,y,r,g,b,a per vertex). Returns VBO id.", R"(
local vs = [[
#version 330 core
layout(location=0) in vec2 in_pos;
layout(location=1) in vec4 in_color;
out vec4 v_color;
uniform vec2 uSize;
void main(){
    v_color = in_color;
    vec2 p = in_pos / uSize * 2.0 - 1.0;
    gl_Position = vec4(p, 0, 1);
}
]]

local fs = [[
#version 330 core
in vec4 v_color;
out vec4 out_color;
void main(){ out_color = v_color; }
]]

shader = nil
vbo = nil

function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    if not vbo then
        shader = canvas.createShader(vs, fs)
        -- Two triangles forming a color-gradient rectangle
        vbo = canvas.createVertexBuffer({
            80,60,   1,0,0,1,   -- red
            240,60,  0,1,0,1,   -- green
            240,180, 0,0,1,1,   -- blue
            80,60,   1,0,0,1,   -- red
            240,180, 0,0,1,1,   -- blue
            80,180,  1,1,0,1    -- yellow
        })
    end
    canvas.clear({0.1, 0.1, 0.1, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    canvas.useShader(shader)
    canvas.setShaderUniformVec2("uSize", {canvas.width, canvas.height})
    canvas.bindVertexBuffer(vbo)
    canvas.drawArrays("triangles", 0, 6)
end
)", __FILE__);
    lua_pushcfunction(L, l_canvas_bind_vertex_buffer);
    lua_setfield(L, -2, "bindVertexBuffer");
    LuaBindingDocs::get().registerDoc("canvas.bindVertexBuffer", "bindVertexBuffer(vbo)", "Bind a VBO and set standard vertex attribs (vec2 pos + vec4 color, stride 6).", R"(
local vs = [[
#version 330 core
layout(location=0) in vec2 in_pos;
layout(location=1) in vec4 in_color;
out vec4 v_color;
uniform vec2 uSize;
void main(){
    v_color = in_color;
    vec2 p = in_pos / uSize * 2.0 - 1.0;
    gl_Position = vec4(p, 0, 1);
}
]]

local fs = [[
#version 330 core
in vec4 v_color;
out vec4 out_color;
void main(){ out_color = v_color; }
]]

shader = nil
vbo1 = nil
vbo2 = nil

function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    if not shader then
        shader = canvas.createShader(vs, fs)
        vbo1 = canvas.createVertexBuffer({
            40,40,   1,0,0,1,
            160,40,  1,0.5,0,1,
            100,140, 1,0,0.5,1
        })
        vbo2 = canvas.createVertexBuffer({
            160,100, 0,0.5,1,1,
            280,100, 0,1,0.5,1,
            220,200, 0.5,0,1,1
        })
    end
    canvas.clear({0.08, 0.08, 0.08, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    canvas.useShader(shader)
    canvas.setShaderUniformVec2("uSize", {canvas.width, canvas.height})
    -- bindVertexBuffer switches which VBO is active for drawing
    canvas.bindVertexBuffer(vbo1)
    canvas.drawArrays("triangles", 0, 3)
    canvas.bindVertexBuffer(vbo2)
    canvas.drawArrays("triangles", 0, 3)
end
)", __FILE__);
    lua_pushcfunction(L, l_canvas_draw_arrays);
    lua_setfield(L, -2, "drawArrays");
    LuaBindingDocs::get().registerDoc("canvas.drawArrays", "drawArrays(mode, first, count)", "Draw primitives from the currently bound VBO. mode: 'triangles','triangle_strip','lines','line_strip','points'.", R"(
shader = nil
vbo = nil

function Config()
    return { type = "canvas", width = 320, height = 240 }
end

local vs = [[
#version 330 core
layout(location=0) in vec2 in_pos;
layout(location=1) in vec4 in_color;
out vec4 v_color;
uniform vec2 uSize;
void main(){
    v_color = in_color;
    vec2 p = in_pos / uSize * 2.0 - 1.0;
    gl_Position = vec4(p, 0, 1);
}
]]

local fs = [[
#version 330 core
in vec4 v_color;
out vec4 out_color;
void main(){ out_color = v_color; }
]]

function Render(dt)
    if not shader then
        shader = canvas.createShader(vs, fs)
        vbo = canvas.createVertexBuffer({
            60,60,   1,0,0,1,
            260,60,  0,1,0,1,
            260,180, 0,0,1,1,
            60,60,   1,0,0,1,
            260,180, 0,0,1,1,
            60,180,  1,1,0,1
        })
    end
    canvas.clear({0.05, 0.05, 0.05, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    canvas.useShader(shader)
    canvas.setShaderUniformVec2("uSize", {canvas.width, canvas.height})
    canvas.bindVertexBuffer(vbo)
    canvas.drawArrays("triangles", 0, 6)
end
)", __FILE__);

    // texture helpers
    lua_pushcfunction(L, l_canvas_create_texture);
    lua_setfield(L, -2, "create_texture");
    LuaBindingDocs::get().registerDoc("canvas.create_texture", "create_texture(bytes, w, h) -> tex", "Create an RGBA texture from raw bytes.", R"(
local tex_vs = [[
#version 330 core
layout(location=0) in vec2 in_pos;
layout(location=1) in vec4 in_color;
out vec2 v_uv;
uniform vec2 uSize;
void main(){
    v_uv = in_color.xy;
    vec2 p = in_pos / uSize * 2.0 - 1.0;
    gl_Position = vec4(p, 0, 1);
}
]]

local tex_fs = [[
#version 330 core
in vec2 v_uv;
out vec4 out_color;
uniform sampler2D uTex;
void main(){ out_color = texture(uTex, v_uv); }
]]

shader = nil
vbo = nil
tex = nil

function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    if not tex then
        -- Build a 4x4 checkerboard RGBA texture
        local w, h = 4, 4
        local bytes = {}
        for y = 0, h-1 do
            for x = 0, w-1 do
                if (x + y) % 2 == 0 then
                    bytes[#bytes+1] = string.char(255,100,50,255)
                else
                    bytes[#bytes+1] = string.char(50,100,255,255)
                end
            end
        end
        tex = canvas.create_texture(table.concat(bytes), w, h)
        shader = canvas.createShader(tex_vs, tex_fs)
        -- Quad using UV in color channels
        vbo = canvas.createVertexBuffer({
            60,40,   0,0,0,0,
            260,40,  1,0,0,0,
            260,200, 1,1,0,0,
            60,40,   0,0,0,0,
            260,200, 1,1,0,0,
            60,200,  0,1,0,0
        })
    end
    canvas.clear({0.1, 0.1, 0.1, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    canvas.useShader(shader)
    canvas.setShaderUniformVec2("uSize", {canvas.width, canvas.height})
    canvas.bind_texture(0, tex)
    canvas.bindVertexBuffer(vbo)
    canvas.drawArrays("triangles", 0, 6)
end
)", __FILE__);
    lua_pushcfunction(L, l_canvas_bind_texture);
    lua_setfield(L, -2, "bind_texture");
    LuaBindingDocs::get().registerDoc("canvas.bind_texture", "bind_texture(unit, tex)", "Bind texture to texture unit.", R"(
local tex_vs = [[
#version 330 core
layout(location=0) in vec2 in_pos;
layout(location=1) in vec4 in_color;
out vec2 v_uv;
uniform vec2 uSize;
void main(){
    v_uv = in_color.xy;
    vec2 p = in_pos / uSize * 2.0 - 1.0;
    gl_Position = vec4(p, 0, 1);
}
]]

local tex_fs = [[
#version 330 core
in vec2 v_uv;
out vec4 out_color;
uniform sampler2D uTex;
void main(){ out_color = texture(uTex, v_uv); }
]]

shader = nil
vbo = nil
tex = nil

function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    if not tex then
        -- 8x8 gradient texture
        local w, h = 8, 8
        local bytes = {}
        for y = 0, h-1 do
            for x = 0, w-1 do
                local r = math.floor(x / (w-1) * 255)
                local g = math.floor(y / (h-1) * 255)
                bytes[#bytes+1] = string.char(r, g, 128, 255)
            end
        end
        tex = canvas.create_texture(table.concat(bytes), w, h)
        shader = canvas.createShader(tex_vs, tex_fs)
        vbo = canvas.createVertexBuffer({
            80,50,   0,0,0,0,
            240,50,  1,0,0,0,
            240,190, 1,1,0,0,
            80,50,   0,0,0,0,
            240,190, 1,1,0,0,
            80,190,  0,1,0,0
        })
    end
    canvas.clear({0.08, 0.08, 0.08, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    canvas.useShader(shader)
    canvas.setShaderUniformVec2("uSize", {canvas.width, canvas.height})
    -- bind_texture attaches a texture to the given texture unit
    canvas.bind_texture(0, tex)
    canvas.bindVertexBuffer(vbo)
    canvas.drawArrays("triangles", 0, 6)
end
)", __FILE__);

    // draw fullscreen helper
    lua_pushcfunction(L, l_canvas_draw_fullscreen);
    lua_setfield(L, -2, "draw_fullscreen");
    LuaBindingDocs::get().registerDoc("canvas.draw_fullscreen", "draw_fullscreen(tex, shader)", "Draw a fullscreen textured quad (optionally supplying a shader).", R"(
local fs_shader_src = [[
#version 330 core
in vec2 v_uv;
out vec4 out_color;
uniform sampler2D uTex;
void main(){
    vec4 c = texture(uTex, v_uv);
    out_color = vec4(c.rgb * 1.2, 1.0);
}
]]

local fs_vs_src = [[
#version 330 core
layout(location=0) in vec2 in_pos;
layout(location=1) in vec2 in_uv;
out vec2 v_uv;
void main(){
    v_uv = in_uv;
    gl_Position = vec4(in_pos, 0, 1);
}
]]

tex = nil
shader = nil

function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    if not tex then
        -- Build a small colored gradient texture
        local w, h = 8, 8
        local bytes = {}
        for y = 0, h-1 do
            for x = 0, w-1 do
                local r = math.floor(x / (w-1) * 200 + 55)
                local b = math.floor(y / (h-1) * 200 + 55)
                bytes[#bytes+1] = string.char(r, 80, b, 255)
            end
        end
        tex = canvas.create_texture(table.concat(bytes), w, h)
        shader = canvas.createShader(fs_vs_src, fs_shader_src)
    end
    canvas.clear({0, 0, 0, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    -- draw_fullscreen renders a fullscreen quad with the texture and optional shader
    canvas.draw_fullscreen(tex, shader)
end
)", __FILE__);

    // expose size as plain integer properties (canvas.width, canvas.height)
    lua_pushinteger(L, width);
    lua_setfield(L, -2, "width");
    LuaBindingDocs::get().registerDoc("canvas.width", "width -> int", "Canvas width in pixels (read-only property).", R"(
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0.1, 0.1, 0.1, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    local w = canvas.width
    local h = canvas.height
    canvas.rect(0, 0, w, h, {0.15, 0.15, 0.15, 1}, true)
    canvas.circle(w/2, h/2, 40, {1, 0.5, 0, 1}, true)
end
)", __FILE__);
    lua_pushinteger(L, height);
    lua_setfield(L, -2, "height");
    LuaBindingDocs::get().registerDoc("canvas.height", "height -> int", "Canvas height in pixels (read-only property).", R"(
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    canvas.clear({0.1, 0.1, 0.1, 1})
    canvas.viewport(0, 0, canvas.width, canvas.height)
    local w = canvas.width
    local h = canvas.height
    canvas.rect(10, 10, w - 20, h - 20, {0.3, 0.7, 0.3, 1}, false, 2)
end
)", __FILE__);

    // compile a default solid shader only once per embed (cached in CanvasState)
    CanvasState *cs = ensureCanvasState(embedID, textureId, width, height);
    if (cs->program == 0) {
        GLuint default_prog = compileShaderProgram(s_shape_vs_src, s_shape_fs_src);
        if (default_prog)
            PLOGI << "lua:canvas compiled default_prog=" << default_prog << " for embed=" << embedID;
        else
            PLOGW << "lua:canvas failed to compile default shader for embed=" << embedID;
        cs->program = default_prog;
    }
    GLuint default_prog = cs->program;

    // register GL-backed shape functions. upvalues: textureId, width, height, program, canvasState
    lua_pushinteger(L, (lua_Integer)textureId);
    lua_pushinteger(L, width);
    lua_pushinteger(L, height);
    lua_pushinteger(L, (lua_Integer)default_prog);
    lua_pushlightuserdata(L, (void*)cs);
    lua_pushcclosure(L, l_canvas_rect, 5);
    lua_setfield(L, -2, "rect");

    lua_pushinteger(L, (lua_Integer)textureId);
    lua_pushinteger(L, width);
    lua_pushinteger(L, height);
    lua_pushinteger(L, (lua_Integer)default_prog);
    lua_pushlightuserdata(L, (void*)cs);
    lua_pushcclosure(L, l_canvas_line, 5);
    lua_setfield(L, -2, "line");

    lua_pushinteger(L, (lua_Integer)textureId);
    lua_pushinteger(L, width);
    lua_pushinteger(L, height);
    lua_pushinteger(L, (lua_Integer)default_prog);
    lua_pushlightuserdata(L, (void*)cs);
    lua_pushcclosure(L, l_canvas_poly, 5);
    lua_setfield(L, -2, "poly");

    lua_pushinteger(L, (lua_Integer)textureId);
    lua_pushinteger(L, width);
    lua_pushinteger(L, height);
    lua_pushinteger(L, (lua_Integer)default_prog);
    lua_pushlightuserdata(L, (void*)cs);
    lua_pushcclosure(L, l_canvas_triangle, 5);
    lua_setfield(L, -2, "triangle");

    lua_pushinteger(L, (lua_Integer)textureId);
    lua_pushinteger(L, width);
    lua_pushinteger(L, height);
    lua_pushinteger(L, (lua_Integer)default_prog);
    lua_pushlightuserdata(L, (void*)cs);
    lua_pushcclosure(L, l_canvas_rounded_rect, 5);
    lua_setfield(L, -2, "rounded_rect");

    lua_pushinteger(L, (lua_Integer)textureId);
    lua_pushinteger(L, width);
    lua_pushinteger(L, height);
    lua_pushinteger(L, (lua_Integer)default_prog);
    lua_pushlightuserdata(L, (void*)cs);
    lua_pushcclosure(L, l_canvas_bezier, 5);
    lua_setfield(L, -2, "bezier");

    lua_pushinteger(L, (lua_Integer)textureId);
    lua_pushinteger(L, width);
    lua_pushinteger(L, height);
    lua_pushinteger(L, (lua_Integer)default_prog);
    lua_pushlightuserdata(L, (void*)cs);
    lua_pushcclosure(L, l_canvas_circle, 5);
    lua_setfield(L, -2, "circle");

    // clipping helpers
    lua_pushinteger(L, (lua_Integer)textureId);
    lua_pushinteger(L, width);
    lua_pushinteger(L, height);
    lua_pushinteger(L, (lua_Integer)default_prog);
    lua_pushlightuserdata(L, (void*)cs);
    lua_pushcclosure(L, l_canvas_push_clip, 5);
    lua_setfield(L, -2, "push_clip");
    lua_pushcfunction(L, l_canvas_pop_clip);
    lua_setfield(L, -2, "pop_clip");

    // text helpers (no-op for now)
    lua_pushinteger(L, (lua_Integer)textureId);
    lua_pushinteger(L, width);
    lua_pushinteger(L, height);
    lua_pushlightuserdata(L, (void*)cs);
    lua_pushcclosure(L, l_canvas_text, 4);
    lua_setfield(L, -2, "text");
    lua_pushcfunction(L, l_canvas_text_size);
    lua_setfield(L, -2, "text_size");

    // image (stub)
    lua_pushcfunction(L, l_canvas_image);
    lua_setfield(L, -2, "image");

    lua_setglobal(L, "canvas");
    LuaBindingDocsUtil::enforceTableHasDocs(L, "canvas");
}

