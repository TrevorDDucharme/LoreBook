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

    for (auto &b : cs->batches){
        if (b.count <= 0) continue;
        glDrawArrays(b.mode, b.start, b.count);
    }

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
"uniform vec2 uSize;\n"
"void main(){ vec2 p = in_pos / uSize * 2.0 - 1.0; p.y = -p.y; gl_Position = vec4(p,0.0,1.0); }\n";
static const char *s_shape_fs_src = "#version 330 core\n"
"uniform vec4 uColor; out vec4 out_color; void main(){ out_color = uColor; }\n";

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
    float cx = (float)luaL_checknumber(L, 1);
    float cy = (float)luaL_checknumber(L, 2);
    float r = (float)luaL_checknumber(L, 3);
    ImU32 col = parseColor(L, 4);
    bool filled = lua_toboolean(L, 5);
    int segments = lua_isinteger(L, 6) ? (int)lua_tointeger(L, 6) : 32;

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
        size_t start = cs->cpuVerts.size() / cs->vertexStride;
        for (size_t i=0;i<verts.size(); i+=2){
            float x = verts[i]; float y = verts[i+1];
            cs->cpuVerts.push_back(x);
            cs->cpuVerts.push_back(y);
            ImVec4 cc = ImGui::ColorConvertU32ToFloat4(col);
            cs->cpuVerts.push_back(cc.x); cs->cpuVerts.push_back(cc.y); cs->cpuVerts.push_back(cc.z); cs->cpuVerts.push_back(cc.w);
        }
        CanvasBatch b; b.mode = filled ? GL_TRIANGLE_FAN : GL_LINE_STRIP; b.start = (int)start; b.count = (int)(verts.size()/2);
        cs->batches.push_back(b);
        lua_getglobal(L, "canvas"); if (lua_istable(L,-1)){ lua_getfield(L,-1,"draw_count"); int cc=(int)lua_tointeger(L,-1); lua_pop(L,1); cc++; lua_pushinteger(L,cc); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
        return 0;
    }

    if (!prog) return 0;
    glUseProgram(prog);
    GLint loc = glGetUniformLocation(prog, "uSize"); if (loc>=0) glUniform2f(loc, (float)lua_tointeger(L, lua_upvalueindex(2)), (float)lua_tointeger(L, lua_upvalueindex(3)));
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
    loc = glGetUniformLocation(prog, "uColor"); if (loc>=0) glUniform4f(loc, c.x, c.y, c.z, c.w);

    GLuint vbo=0, vao=0; glGenBuffers(1,&vbo); glGenVertexArrays(1,&vao);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, verts.size()*sizeof(float), verts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);
    if (filled) glDrawArrays(GL_TRIANGLE_FAN, 0, (GLsizei)(verts.size()/2)); else { glLineWidth(1.0f); glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)(verts.size()/2)); }
    glBindBuffer(GL_ARRAY_BUFFER,0); glBindVertexArray(0); glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao);

    lua_getglobal(L, "canvas"); if (lua_istable(L,-1)){ lua_getfield(L,-1,"draw_count"); int cc=(int)lua_tointeger(L,-1); lua_pop(L,1); cc++; lua_pushinteger(L,cc); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
    glUseProgram(0);
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
            // line loop: 4 vertices
            float vx[8] = { x, y, x + w, y, x + w, y + h, x, y + h };
            for (int i=0;i<4;i++){
                cs->cpuVerts.push_back(vx[i*2]); cs->cpuVerts.push_back(vx[i*2+1]);
                cs->cpuVerts.push_back(cc.x); cs->cpuVerts.push_back(cc.y); cs->cpuVerts.push_back(cc.z); cs->cpuVerts.push_back(cc.w);
            }
            CanvasBatch b; b.mode = GL_LINE_LOOP; b.start = (int)start; b.count = 4; cs->batches.push_back(b);
        }
        lua_getglobal(L, "canvas"); if (lua_istable(L, -1)){ lua_getfield(L,-1,"draw_count"); int c=(int)lua_tointeger(L,-1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
        return 0;
    }

    // fallback immediate path
    if (!prog) return 0;
    float verts[12];
    verts[0] = x; verts[1] = y;
    verts[2] = x + w; verts[3] = y;
    verts[4] = x + w; verts[5] = y + h;
    verts[6] = x; verts[7] = y;
    verts[8] = x + w; verts[9] = y + h;
    verts[10] = x; verts[11] = y + h;

    glUseProgram(prog);
    GLint loc = glGetUniformLocation(prog, "uSize"); if (loc>=0) glUniform2f(loc, (float)lua_tointeger(L, lua_upvalueindex(2)), (float)lua_tointeger(L, lua_upvalueindex(3)));
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
    loc = glGetUniformLocation(prog, "uColor"); if (loc>=0) glUniform4f(loc, c.x, c.y, c.z, c.w);

    GLuint vbo=0, vao=0; glGenBuffers(1,&vbo); glGenVertexArrays(1,&vao);
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);
    if (filled){ glDrawArrays(GL_TRIANGLES, 0, 6); }
    else { glLineWidth(thickness); glDrawArrays(GL_LINE_LOOP, 0, 4); }
    glBindBuffer(GL_ARRAY_BUFFER, 0); glBindVertexArray(0);
    glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao);

    // increment draw_count
    lua_getglobal(L, "canvas"); if (lua_istable(L, -1)){ lua_getfield(L,-1,"draw_count"); int c=(int)lua_tointeger(L,-1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
    glUseProgram(0);
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
        size_t start = cs->cpuVerts.size() / cs->vertexStride;
        ImVec4 cc = ImGui::ColorConvertU32ToFloat4(col);
        cs->cpuVerts.push_back(x1); cs->cpuVerts.push_back(y1); cs->cpuVerts.push_back(cc.x); cs->cpuVerts.push_back(cc.y); cs->cpuVerts.push_back(cc.z); cs->cpuVerts.push_back(cc.w);
        cs->cpuVerts.push_back(x2); cs->cpuVerts.push_back(y2); cs->cpuVerts.push_back(cc.x); cs->cpuVerts.push_back(cc.y); cs->cpuVerts.push_back(cc.z); cs->cpuVerts.push_back(cc.w);
        CanvasBatch b; b.mode = GL_LINES; b.start = (int)start; b.count = 2; cs->batches.push_back(b);
        lua_getglobal(L, "canvas"); if (lua_istable(L, -1)){ lua_getfield(L,-1,"draw_count"); int c=(int)lua_tointeger(L,-1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
        return 0;
    }
    // fallback to immediate draw
    if (!prog) return 0;
    float verts[4] = { x1, y1, x2, y2 };
    glUseProgram(prog);
    GLint loc = glGetUniformLocation(prog, "uSize"); if (loc>=0) glUniform2f(loc, (float)lua_tointeger(L, lua_upvalueindex(2)), (float)lua_tointeger(L, lua_upvalueindex(3)));
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
    loc = glGetUniformLocation(prog, "uColor"); if (loc>=0) glUniform4f(loc, c.x, c.y, c.z, c.w);
    GLuint vbo=0, vao=0; glGenBuffers(1,&vbo); glGenVertexArrays(1,&vao);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);
    glLineWidth(thickness); glDrawArrays(GL_LINES, 0, 2);
    glBindBuffer(GL_ARRAY_BUFFER,0); glBindVertexArray(0); glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao);
    lua_getglobal(L, "canvas"); if (lua_istable(L, -1)){ lua_getfield(L,-1,"draw_count"); int c=(int)lua_tointeger(L,-1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
    glUseProgram(0);
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
    if (cs) {
        size_t start = cs->cpuVerts.size() / cs->vertexStride;
        ImVec4 cc = ImGui::ColorConvertU32ToFloat4(col);
        for (size_t i=0;i<pts.size(); i+=2){
            cs->cpuVerts.push_back(pts[i]); cs->cpuVerts.push_back(pts[i+1]);
            cs->cpuVerts.push_back(cc.x); cs->cpuVerts.push_back(cc.y); cs->cpuVerts.push_back(cc.z); cs->cpuVerts.push_back(cc.w);
        }
        CanvasBatch b; b.mode = filled ? GL_TRIANGLE_FAN : GL_LINE_STRIP; b.start = (int)start; b.count = (int)(pts.size()/2); cs->batches.push_back(b);
        lua_getglobal(L, "canvas"); if (lua_istable(L, -1)){ lua_getfield(L,-1,"draw_count"); int c=(int)lua_tointeger(L,-1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
        return 0;
    }

    // fallback immediate draw
    if (!prog) return 0;
    glUseProgram(prog);
    GLint loc = glGetUniformLocation(prog, "uSize"); if (loc>=0) glUniform2f(loc, (float)lua_tointeger(L, lua_upvalueindex(2)), (float)lua_tointeger(L, lua_upvalueindex(3)));
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
    loc = glGetUniformLocation(prog, "uColor"); if (loc>=0) glUniform4f(loc, c.x, c.y, c.z, c.w);
    GLuint vbo=0, vao=0; glGenBuffers(1,&vbo); glGenVertexArrays(1,&vao);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, pts.size()*sizeof(float), pts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);
    if (filled) glDrawArrays(GL_TRIANGLE_FAN, 0, (GLsizei)(pts.size()/2)); else glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)(pts.size()/2));
    glBindBuffer(GL_ARRAY_BUFFER,0); glBindVertexArray(0); glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao);
    lua_getglobal(L, "canvas"); if (lua_istable(L,-1)){ lua_getfield(L,-1,"draw_count"); int c=(int)lua_tointeger(L,-1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
    glUseProgram(0);
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
        size_t start = cs->cpuVerts.size() / cs->vertexStride;
        ImVec4 cc = ImGui::ColorConvertU32ToFloat4(col);
        cs->cpuVerts.push_back(x1); cs->cpuVerts.push_back(y1); cs->cpuVerts.push_back(cc.x); cs->cpuVerts.push_back(cc.y); cs->cpuVerts.push_back(cc.z); cs->cpuVerts.push_back(cc.w);
        cs->cpuVerts.push_back(x2); cs->cpuVerts.push_back(y2); cs->cpuVerts.push_back(cc.x); cs->cpuVerts.push_back(cc.y); cs->cpuVerts.push_back(cc.z); cs->cpuVerts.push_back(cc.w);
        cs->cpuVerts.push_back(x3); cs->cpuVerts.push_back(y3); cs->cpuVerts.push_back(cc.x); cs->cpuVerts.push_back(cc.y); cs->cpuVerts.push_back(cc.z); cs->cpuVerts.push_back(cc.w);
        CanvasBatch b; b.mode = filled ? GL_TRIANGLES : GL_LINE_LOOP; b.start = (int)start; b.count = filled ? 3 : 3; cs->batches.push_back(b);
        lua_getglobal(L, "canvas"); if (lua_istable(L, -1)){ lua_getfield(L,-1,"draw_count"); int c=(int)lua_tointeger(L,-1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
        return 0;
    }

    // fallback immediate path
    if (!prog) return 0;
    float verts[6] = { x1,y1, x2,y2, x3,y3 };
    glUseProgram(prog);
    GLint loc = glGetUniformLocation(prog, "uSize"); if (loc>=0) glUniform2f(loc, (float)lua_tointeger(L, lua_upvalueindex(2)), (float)lua_tointeger(L, lua_upvalueindex(3)));
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
    loc = glGetUniformLocation(prog, "uColor"); if (loc>=0) glUniform4f(loc, c.x, c.y, c.z, c.w);
    GLuint vbo=0, vao=0; glGenBuffers(1,&vbo); glGenVertexArrays(1,&vao);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);
    if (filled) glDrawArrays(GL_TRIANGLES, 0, 3); else { glLineWidth(thickness); glDrawArrays(GL_LINE_LOOP, 0, 3); }
    glBindBuffer(GL_ARRAY_BUFFER,0); glBindVertexArray(0); glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao);
    lua_getglobal(L, "canvas"); if (lua_istable(L,-1)){ lua_getfield(L,-1,"draw_count"); int c=(int)lua_tointeger(L,-1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
    glUseProgram(0);
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
        size_t start = cs->cpuVerts.size() / cs->vertexStride;
        ImVec4 cc = ImGui::ColorConvertU32ToFloat4(col);
        for (size_t i=0;i<pts.size(); i+=2){
            cs->cpuVerts.push_back(pts[i]); cs->cpuVerts.push_back(pts[i+1]);
            cs->cpuVerts.push_back(cc.x); cs->cpuVerts.push_back(cc.y); cs->cpuVerts.push_back(cc.z); cs->cpuVerts.push_back(cc.w);
        }
        CanvasBatch b; b.mode = GL_LINE_STRIP; b.start = (int)start; b.count = (int)(pts.size()/2); cs->batches.push_back(b);
        lua_getglobal(L, "canvas"); if (lua_istable(L,-1)){ lua_getfield(L,-1,"draw_count"); int c=(int)lua_tointeger(L,-1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
        return 0;
    }

    // fallback immediate path
    if (!prog) return 0;
    glUseProgram(prog);
    GLint loc = glGetUniformLocation(prog, "uSize"); if (loc>=0) glUniform2f(loc, (float)lua_tointeger(L, lua_upvalueindex(2)), (float)lua_tointeger(L, lua_upvalueindex(3)));
    ImVec4 c = ImGui::ColorConvertU32ToFloat4(col);
    loc = glGetUniformLocation(prog, "uColor"); if (loc>=0) glUniform4f(loc, c.x, c.y, c.z, c.w);
    GLuint vbo=0, vao=0; glGenBuffers(1,&vbo); glGenVertexArrays(1,&vao);
    glBindVertexArray(vao); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, pts.size()*sizeof(float), pts.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);
    glLineWidth(thickness); glDrawArrays(GL_LINE_STRIP, 0, (GLsizei)(pts.size()/2));
    glBindBuffer(GL_ARRAY_BUFFER,0); glBindVertexArray(0); glDeleteBuffers(1,&vbo); glDeleteVertexArrays(1,&vao);
    lua_getglobal(L, "canvas"); if (lua_istable(L,-1)){ lua_getfield(L,-1,"draw_count"); int c=(int)lua_tointeger(L,-1); lua_pop(L,1); c++; lua_pushinteger(L,c); lua_setfield(L,-2,"draw_count"); } lua_pop(L,1);
    glUseProgram(0);
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
        
        print(canvas.draw_count)
    )", __FILE__);

    // width
    lua_pushinteger(L, width);
    lua_pushcclosure(L, l_canvas_width, 1);
    lua_setfield(L, -2, "width");
        LuaBindingDocs::get().registerDoc("canvas.width", "width() -> int", "Get canvas width in pixels", R"(
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    local w = canvas.width()
    local h = canvas.height()
    ui = string.format("Canvas: %dx%d", w, h)
    -- draw a background so we can see size
    canvas.rect(0,0,w,h, {0.1,0.1,0.1,1}, true)
    canvas.text(10,10, ui, {1,1,1,1})
end
)", __FILE__);

    // height
    lua_pushinteger(L, height);
    lua_pushcclosure(L, l_canvas_height, 1);
    lua_setfield(L, -2, "height");
        LuaBindingDocs::get().registerDoc("canvas.height", "height() -> int", "Get canvas height in pixels", R"(
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    local w = canvas.width()
    local h = canvas.height()
    canvas.rect(0,0,w,h, {0.15,0.15,0.15,1}, true)
    canvas.text(10,10, string.format("H: %d", h), {1,1,1,1})
end
)", __FILE__);

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

        // triangle(x1,y1,x2,y2,x3,y3,color,filled,thickness)
        lua_pushnumber(L, origin.x);
        lua_pushnumber(L, origin.y);
        lua_pushcclosure(L, l_canvas_triangle, 2);
        lua_setfield(L, -2, "triangle");
        LuaBindingDocs::get().registerDoc("canvas.triangle", "triangle(x1,y1,x2,y2,x3,y3,color,filled,thickness)", "Draw a triangle (filled or stroked)", R"(
-- Full canvas example: centered triangle
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    local w = canvas.width()
    local h = canvas.height()
    local cx, cy = w/2, h/2
    canvas.triangle(cx-40, cy+30, cx, cy-30, cx+40, cy+30, {0.2,0.6,1,1}, true)
end
)", __FILE__);

        // rounded_rect(x,y,w,h,rounding,color,filled,thickness)
        lua_pushnumber(L, origin.x);
        lua_pushnumber(L, origin.y);
        lua_pushcclosure(L, l_canvas_rounded_rect, 2);
        lua_setfield(L, -2, "rounded_rect");
        LuaBindingDocs::get().registerDoc("canvas.rounded_rect", "rounded_rect(x,y,w,h,rounding,color,filled,thickness)", "Draw a rectangle with rounded corners", R"(
-- Full canvas example: rounded frame
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    local w = canvas.width()
    local h = canvas.height()
    canvas.rounded_rect(10,10, w-20, h-20, 8, {0,0,0,0.6}, false, 3)
end
)", __FILE__);

        // bezier(p1x,p1y,p2x,p2y,p3x,p3y,p4x,p4y,color,thickness,segments)
        lua_pushnumber(L, origin.x);
        lua_pushnumber(L, origin.y);
        lua_pushcclosure(L, l_canvas_bezier, 2);
        lua_setfield(L, -2, "bezier");
        LuaBindingDocs::get().registerDoc("canvas.bezier", "bezier(p1x,p1y,p2x,p2y,p3x,p3y,p4x,p4y,color,thickness,segments)", "Draw a cubic Bezier curve", R"(
-- Full canvas example: bezier curve
function Config()
    return { type = "canvas", width = 320, height = 240 }
end

function Render(dt)
    local w = canvas.width()
    local h = canvas.height()
    canvas.bezier(20,h-20, 120,20, 200,h-20, 300,20, {1,0.5,0,1}, 2, 32)
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
    local tw, th = canvas.text_size("Hello World")
    canvas.rect(5,5, tw+10, th+6, {0,0,0,0.5}, true)
    canvas.text(10,8, "Hello World", {1,1,1,1})
end
)", __FILE__);

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



static int l_canvas_begin_gl(lua_State *L)
{
    // upvalues: textureId, width, height
    GLuint tex = (GLuint)lua_tointeger(L, lua_upvalueindex(1));
    int w = (int)lua_tointeger(L, lua_upvalueindex(2));
    int h = (int)lua_tointeger(L, lua_upvalueindex(3));
    // Bind the texture-backed framebuffer by binding the texture to the framebuffer 0 via glBindTexture
    // The caller (MarkdownText) binds the FBO before calling Render; nothing to do here currently.
    // Provide viewport convenience
    glViewport(0, 0, w, h);
    return 0;
}

static int l_canvas_end_gl(lua_State *L)
{
    // restore default viewport is left to the caller; nothing to do here
    return 0;
}

static int l_canvas_clear_gl(lua_State *L)
{
    float r = (float)luaL_optnumber(L,1,0.0);
    float g = (float)luaL_optnumber(L,2,0.0);
    float b = (float)luaL_optnumber(L,3,0.0);
    float a = (float)luaL_optnumber(L,4,1.0);
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

    // begin/end (no-op here, viewport handled by caller)
    lua_pushinteger(L, (lua_Integer)textureId);
    lua_pushinteger(L, width);
    lua_pushinteger(L, height);
    lua_pushcclosure(L, l_canvas_begin_gl, 3);
    lua_setfield(L, -2, "begin");
    lua_pushcfunction(L, l_canvas_end_gl);
    lua_setfield(L, -2, "end");

    // clear
    lua_pushcfunction(L, l_canvas_clear_gl);
    lua_setfield(L, -2, "clear");

    // shader/upload/use/set_uniform
    lua_pushcfunction(L, l_canvas_upload_shader);
    lua_setfield(L, -2, "upload_shader");
    lua_pushcfunction(L, l_canvas_use_shader);
    lua_setfield(L, -2, "use_shader");
    lua_pushcfunction(L, l_canvas_set_uniform);
    lua_setfield(L, -2, "set_uniform");

    // texture helpers
    lua_pushcfunction(L, l_canvas_create_texture);
    lua_setfield(L, -2, "create_texture");
    lua_pushcfunction(L, l_canvas_bind_texture);
    lua_setfield(L, -2, "bind_texture");

    // draw fullscreen helper
    lua_pushcfunction(L, l_canvas_draw_fullscreen);
    lua_setfield(L, -2, "draw_fullscreen");

    // expose size helpers (width/height)
    lua_pushinteger(L, width);
    lua_pushcclosure(L, l_canvas_width, 1);
    lua_setfield(L, -2, "width");
    lua_pushinteger(L, height);
    lua_pushcclosure(L, l_canvas_height, 1);
    lua_setfield(L, -2, "height");

    // compile a default solid shader for this canvas and push as last upvalue
    GLuint default_prog = compileShaderProgram(s_shape_vs_src, s_shape_fs_src);
    if (default_prog)
        PLOGI << "lua:canvas compiled default_prog=" << default_prog << " for embed=" << embedID;
    else
        PLOGW << "lua:canvas failed to compile default shader for embed=" << embedID;

    // create per-embed canvas state and register GL-backed shape functions.
    CanvasState *cs = ensureCanvasState(embedID, textureId, width, height);
    cs->program = default_prog;

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
