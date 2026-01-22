#include "GraphView.hpp"
#include "Vault.hpp"
#include <imgui.h>
#include <cmath>
#include <algorithm>
#include <random>
#include <stdio.h>
#include <GL/glew.h>

// Minimal GL helper for compiling shaders
static GLuint compileShader(GLenum type, const char* src){
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok){
        char buf[1024]; glGetShaderInfoLog(s, sizeof(buf), nullptr, buf);
        fprintf(stderr, "Shader compile error: %s\n", buf);
    }
    return s;
}
static GLuint linkProgram(GLuint vs, GLuint fs){
    GLuint p = glCreateProgram(); glAttachShader(p, vs); glAttachShader(p, fs); glLinkProgram(p);
    GLint ok=0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if(!ok){ char buf[1024]; glGetProgramInfoLog(p, sizeof(buf), nullptr, buf); fprintf(stderr, "Program link error: %s\n", buf); }
    return p;
}

// Shader sources for simple line and textured quad rendering
static const char *vs_line_src = "#version 330 core\nlayout(location=0) in vec2 aPos; uniform vec2 uScreen; void main(){ vec2 ndc = (aPos / uScreen)*2.0 - 1.0; ndc.y = -ndc.y; gl_Position = vec4(ndc, 0.0, 1.0); }";
static const char *fs_line_src = "#version 330 core\nuniform vec4 uColor; out vec4 FragColor; void main(){ FragColor = uColor; }";
static const char *vs_quad_src = "#version 330 core\nlayout(location=0) in vec2 aPos; layout(location=1) in vec2 aUV; uniform vec2 uScreen; out vec2 vUV; void main(){ vec2 ndc = (aPos / uScreen)*2.0 - 1.0; ndc.y = -ndc.y; gl_Position = vec4(ndc, 0.0, 1.0); vUV = aUV; }";
static const char *fs_quad_src = "#version 330 core\nin vec2 vUV; uniform sampler2D uTex; uniform vec4 uColor; out vec4 FragColor; void main(){ vec4 c = texture(uTex, vUV); FragColor = vec4(uColor.rgb, c.a * uColor.a); if(FragColor.a < 0.01) discard; }";

GraphView::GraphView() : fbo(0), fboTex(0), rbo(0), fbWidth(0), fbHeight(0) {}
GraphView::~GraphView(){ cleanupGLResources(); }

void GraphView::setVault(Vault* v){
    if(vault != v){
        vault = v;
        needsRebuild = true;
    }
}

void GraphView::rebuildFromVault(){
    nodes.clear(); edges.clear(); nodeIndexById.clear();
    if(!vault) return;

    auto items = (vault && vault->getCurrentUserID() > 0) ? vault->getAllItemsForUser(vault->getCurrentUserID()) : vault->getAllItemsPublic();
    // Layout nodes in a circle initially and ensure uniqueness
    const float centerX = 0.0f, centerY = 0.0f;
    const float radius = 200.0f;
    size_t n = items.size();
    if(n == 0) return;
    std::unordered_set<int64_t> seenIds;
    size_t idx = 0;
    for(size_t i=0;i<n;++i){
        int64_t id = items[i].first;
        if(seenIds.find(id) != seenIds.end()) continue; // skip duplicates
        seenIds.insert(id);
        Node nd;
        nd.id = id;
        nd.label = items[i].second;
        float ang = (float)idx / (float)n * 2.0f * 3.1415926f;
        nd.pos = ImVec2(centerX + cosf(ang) * radius, centerY + sinf(ang) * radius);
        nd.vel = ImVec2(0,0);
        nd.pinned = false;
        nd.radius = 8.0f + std::min(12.0f, (float)(nd.label.size() * 0.15f));
        // Populate tags for this node
        if(vault) nd.tags = vault->getTagsOfPublic(nd.id);
        nodeIndexById[nd.id] = (int)nodes.size();
        nodes.push_back(std::move(nd));
        idx++;
    }

    // Build edges from parent relations, deduplicate edges
    std::unordered_set<uint64_t> seenEdges;
    for(auto &it : items){
        int64_t id = it.first;
        auto parents = vault->getParentsOfPublic(id);
        for(auto p : parents){
            // skip self
            if(p == id) continue;
            if(nodeIndexById.find(p) == nodeIndexById.end() || nodeIndexById.find(id) == nodeIndexById.end()) continue;
            uint64_t key = (uint64_t(p) << 32) | (uint64_t(id) & 0xffffffffULL);
            if(seenEdges.find(key) != seenEdges.end()) continue;
            seenEdges.insert(key);
            Edge e; e.a = p; e.b = id; e.rest = springRest;
            edges.push_back(e);
        }
    }

    needsRebuild = false;
}

void GraphView::updateAndDraw(float dt){
    if(needsRebuild) rebuildFromVault();

    ImGui::Begin("Vault Graph", nullptr, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    // UI controls
    if(ImGui::CollapsingHeader("Physics", ImGuiTreeNodeFlags_DefaultOpen)){
        ImGui::SliderFloat("Spring K", &springK, 0.0f, 0.5f);
        ImGui::SliderFloat("Rest Length", &springRest, 10.0f, 400.0f);
        ImGui::SliderFloat("Repulsion", &repulsion, 100.0f, 20000.0f);
        ImGui::SliderFloat("Damping", &damping, 0.0f, 1.0f);
        ImGui::SliderInt("Iterations", &physicsSteps, 1, 16);
        if(ImGui::Button(paused? "Resume" : "Pause")) paused = !paused;
        ImGui::SameLine();
        if(ImGui::Button("Recenter")){
            pan = ImVec2(0,0);
            scale = 1.0f;
        }
        ImGui::SameLine();
        if(ImGui::Button("Reset Layout")){
            needsRebuild = true;
        }
    }
    ImGui::Separator();
    ImGui::Checkbox("Show Labels", &showLabels);
    ImGui::Checkbox("Show Edges", &showEdges);

    // Tags UI (from Vault) — synced with Vault's active filter
    if(ImGui::CollapsingHeader("Tags", ImGuiTreeNodeFlags_DefaultOpen)){
        // refresh tags
        allTags = vault ? vault->getAllTagsPublic() : std::vector<std::string>();

        // Sync toggles from Vault's active filter so changes in Tree view are reflected here
        std::vector<std::string> vaultActive = vault ? vault->getActiveTagFilterPublic() : std::vector<std::string>();
        std::unordered_set<std::string> activeSet(vaultActive.begin(), vaultActive.end());
        for(auto &t : allTags){
            tagToggles[t] = (activeSet.find(t) != activeSet.end());
        }
        // Sync mode from Vault
        if(vault) tagModeAll = vault->getTagFilterModeAllPublic();

        ImGui::Text("Mode:"); ImGui::SameLine();
        if(ImGui::RadioButton("All (AND)", tagModeAll)) tagModeAll = true; ImGui::SameLine();
        if(ImGui::RadioButton("Any (OR)", !tagModeAll)) tagModeAll = false;
        ImGui::NewLine();

        bool changed = false;
        for(auto &t : allTags){
            // color swatch
            ImU32 col = 0xFF000000;
            // compute deterministic color by hashing
            std::hash<std::string> hasher; uint64_t h = hasher(t);
            float hue = (h % 360);
            float r,g,b;
            // simple HSV->RGB for hue only
            float s = 0.65f, v = 0.9f;
            float hh = hue/60.0f; int i = (int)floor(hh); float f = hh - i; float p = v*(1-s); float q = v*(1 - s*f); float tcol = v*(1 - s*(1-f));
            switch(i%6){ case 0: r=v;g=tcol;b=p; break; case 1: r=q;g=v;b=p; break; case 2: r=p;g=v;b=tcol; break; case 3: r=p;g=q;b=v; break; case 4: r=tcol;g=p;b=v; break; case 5: default: r=v;g=p;b=q; break; }
            ImGui::SameLine();
            ImGui::ColorButton((std::string("##tagcol") + t).c_str(), ImVec4(r,g,b,1.0f), ImGuiColorEditFlags_NoTooltip, ImVec2(16,16));
            ImGui::SameLine();
            bool prev = tagToggles[t];
            if(ImGui::Checkbox((std::string(" ") + t).c_str(), &tagToggles[t])){
                changed = true;
            }
        }
        if(changed && vault){
            std::vector<std::string> active;
            for(auto &kv : tagToggles) if(kv.second) active.push_back(kv.first);
            vault->setTagFilter(active, tagModeAll);
        }
    }

    // Canvas area
    ImVec2 canvasSize = ImGui::GetContentRegionAvail();
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();

    // Draw background (we'll render into an FBO and show it as an image)
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(canvasPos, ImVec2(canvasPos.x+canvasSize.x, canvasPos.y+canvasSize.y), IM_COL32(20,20,20,200));

    // Create an invisible button to capture mouse interactions over the canvas (prevents the underlying window from being dragged)
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::InvisibleButton("GraphCanvas", canvasSize);
    ImGuiIO &io = ImGui::GetIO();
    ImVec2 mousePos = io.MousePos;
    bool hovered = ImGui::IsItemHovered();

    // handle zoom with wheel
    if(hovered && io.MouseWheel != 0.0f){
        ImVec2 mpWorldBefore = screenToWorld(io.MousePos, canvasPos, (int)canvasSize.x, (int)canvasSize.y);
        if(io.MouseWheel > 0) scale *= 1.1f; else scale /= 1.1f;
        // keep within bounds
        scale = std::clamp(scale, 0.2f, 4.0f);
        ImVec2 mpWorldAfter = screenToWorld(io.MousePos, canvasPos, (int)canvasSize.x, (int)canvasSize.y);
        pan.x += (mpWorldAfter.x - mpWorldBefore.x);
        pan.y += (mpWorldAfter.y - mpWorldBefore.y);
    }

    // Dragging canvas (middle mouse)
    if(hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)){
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        pan.x += delta.x / scale;
        pan.y += delta.y / scale;
    }

    // Ensure GL resources and FBO size
    initGLResources();
    ensureFBOSize((int)canvasSize.x, (int)canvasSize.y);

    // Physics stepping
    if(!paused) {
        float subDt = dt / (float)std::max(1, physicsSteps);
        for(int i=0;i<physicsSteps;++i) stepPhysics(subDt);
    }

    // Render graph into FBO
    drawGraphGL((int)canvasSize.x, (int)canvasSize.y);

    // Display the FBO texture in ImGui (flip vertically to match GL framebuffer orientation)
    ImGui::SetCursorScreenPos(canvasPos);
    ImGui::Image((ImTextureID)(intptr_t)fboTex, canvasSize, ImVec2(0,1), ImVec2(1,0));


    // Node interactions (left click) - map mouse into world coordinates using canvas dims
    if(hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)){
        ImVec2 world = screenToWorld(mousePos, canvasPos, (int)canvasSize.x, (int)canvasSize.y);
        int idx = findNodeAt(world);
        if(idx >= 0){
            // select in Vault
            if(vault) vault->selectItemByID(nodes[idx].id);
            draggingNodeIdx = idx;
            nodes[idx].pinned = true;
        }
    }
    if(draggingNodeIdx >= 0){
        if(ImGui::IsMouseDown(ImGuiMouseButton_Left)){
            ImVec2 world = screenToWorld(mousePos, canvasPos, (int)canvasSize.x, (int)canvasSize.y);
            nodes[draggingNodeIdx].pos = world;
            nodes[draggingNodeIdx].vel = ImVec2(0,0);
        } else {
            // release
            nodes[draggingNodeIdx].pinned = false;
            draggingNodeIdx = -1;
        }
    }

    ImGui::Dummy(canvasSize);
    ImGui::End();
}

void GraphView::stepPhysics(float dt){
    if(nodes.empty()) return;

    // Reset forces (we use velocities directly)
    // Edge springs
    for(auto &e : edges){
        int ai = nodeIndexById.count(e.a) ? nodeIndexById[e.a] : -1;
        int bi = nodeIndexById.count(e.b) ? nodeIndexById[e.b] : -1;
        if(ai < 0 || bi < 0) continue;
        Node &A = nodes[ai];
        Node &B = nodes[bi];
        ImVec2 d = ImVec2(B.pos.x - A.pos.x, B.pos.y - A.pos.y);
        float dist = sqrtf(d.x*d.x + d.y*d.y) + 1e-6f;
        ImVec2 dir = ImVec2(d.x/dist, d.y/dist);
        float k = springK;
        float rest = springRest;
        float force = k * (dist - rest);
        // Apply half to each (unless pinned)
        if(!A.pinned) A.vel.x += force * dir.x * dt;
        if(!A.pinned) A.vel.y += force * dir.y * dt;
        if(!B.pinned) B.vel.x -= force * dir.x * dt;
        if(!B.pinned) B.vel.y -= force * dir.y * dt;
    }

    // Repulsion (n^2) - only for small graphs so naive is OK
    for(size_t i=0;i<nodes.size();++i){
        for(size_t j=i+1;j<nodes.size();++j){
            Node &A = nodes[i]; Node &B = nodes[j];
            ImVec2 d = ImVec2(B.pos.x - A.pos.x, B.pos.y - A.pos.y);
            float dist2 = d.x*d.x + d.y*d.y + 1e-6f;
            float dist = sqrtf(dist2);
            ImVec2 dir = ImVec2(d.x/dist, d.y/dist);
            float f = repulsion / dist2;
            if(!A.pinned){ A.vel.x -= f * dir.x * dt; A.vel.y -= f * dir.y * dt; }
            if(!B.pinned){ B.vel.x += f * dir.x * dt; B.vel.y += f * dir.y * dt; }
        }
    }

    // Integrate and apply damping
    for(auto &n : nodes){
        if(n.pinned) { n.vel = ImVec2(0,0); continue; }
        n.vel.x *= (1.0f - damping);
        n.vel.y *= (1.0f - damping);
        n.pos.x += n.vel.x * dt * 60.0f; // scale to feel stable across dt
        n.pos.y += n.vel.y * dt * 60.0f;
    }
}

ImVec2 GraphView::worldToScreen(const ImVec2 &w, const ImVec2 &origin, int canvasW, int canvasH){
    // screen = origin + (w + pan) * scale + (canvasW/2, canvasH/2)
    ImVec2 screen = ImVec2(origin.x + (w.x + pan.x) * scale + (float)canvasW * 0.5f,
                           origin.y + (w.y + pan.y) * scale + (float)canvasH * 0.5f);
    return screen;
}

ImVec2 GraphView::screenToWorld(const ImVec2 &s, const ImVec2 &origin, int canvasW, int canvasH){
    ImVec2 world;
    world.x = (s.x - origin.x - (float)canvasW*0.5f) / scale - pan.x;
    world.y = (s.y - origin.y - (float)canvasH*0.5f) / scale - pan.y;
    return world;
}

int GraphView::findNodeAt(const ImVec2 &worldPos){
    for(size_t i=0;i<nodes.size();++i){
        float r = nodes[i].radius;
        ImVec2 d = ImVec2(nodes[i].pos.x - worldPos.x, nodes[i].pos.y - worldPos.y);
        float dist2 = d.x*d.x + d.y*d.y;
        if(dist2 <= (r * r)) return (int)i;
    }
    return -1;
}

// GL resource initialization / cleanup
void GraphView::initGLResources(){
    if(lineProg && quadProg) return; // already initialized

    // Compile/link programs
    GLuint vs = compileShader(GL_VERTEX_SHADER, vs_line_src);
    GLuint fs = compileShader(GL_FRAGMENT_SHADER, fs_line_src);
    lineProg = linkProgram(vs, fs);
    glDeleteShader(vs); glDeleteShader(fs);

    vs = compileShader(GL_VERTEX_SHADER, vs_quad_src);
    fs = compileShader(GL_FRAGMENT_SHADER, fs_quad_src);
    quadProg = linkProgram(vs, fs);
    glDeleteShader(vs); glDeleteShader(fs);

    // Quad VAO/VBO
    glGenVertexArrays(1, &quadVAO);
    glGenBuffers(1, &quadVBO);
    glBindVertexArray(quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(float)*6*4, nullptr, GL_STREAM_DRAW); // will stream per-node
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)0);
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,2,GL_FLOAT,GL_FALSE,4*sizeof(float),(void*)(2*sizeof(float)));
    glBindVertexArray(0);

    // Line VBO + VAO
    glGenBuffers(1, &lineVBO);
    glGenVertexArrays(1, &lineVAO);
    glBindVertexArray(lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,2*sizeof(float),(void*)0);
    glBindVertexArray(0);

    // Circle texture
    const int texS = 64;
    std::vector<unsigned char> img(texS*texS*4);
    for(int y=0;y<texS;++y){
        for(int x=0;x<texS;++x){
            float dx = (x + 0.5f - texS*0.5f)/ (texS*0.5f);
            float dy = (y + 0.5f - texS*0.5f)/ (texS*0.5f);
            float d = sqrtf(dx*dx + dy*dy);
            float a = d<=1.0f? 1.0f : 0.0f;
            int idx = (y*texS + x)*4;
            img[idx+0] = 255; img[idx+1]=255; img[idx+2]=255; img[idx+3] = (unsigned char)(a*255.0f);
        }
    }
    glGenTextures(1, &circleTex);
    glBindTexture(GL_TEXTURE_2D, circleTex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,texS,texS,0,GL_RGBA,GL_UNSIGNED_BYTE,img.data());
    glBindTexture(GL_TEXTURE_2D,0);
}

void GraphView::cleanupGLResources(){
    if(quadVBO){ glDeleteBuffers(1, &quadVBO); quadVBO = 0; }
    if(quadVAO){ glDeleteVertexArrays(1, &quadVAO); quadVAO = 0; }
    if(lineVBO){ glDeleteBuffers(1, &lineVBO); lineVBO = 0; }
    if(lineVAO){ glDeleteVertexArrays(1, &lineVAO); lineVAO = 0; }
    if(lineProg){ glDeleteProgram(lineProg); lineProg = 0; }
    if(quadProg){ glDeleteProgram(quadProg); quadProg = 0; }
    if(circleTex){ glDeleteTextures(1, &circleTex); circleTex = 0; }
    if(fbo){ glDeleteFramebuffers(1, &fbo); fbo = 0; }
    if(fboTex){ glDeleteTextures(1, &fboTex); fboTex = 0; }
    if(rbo){ glDeleteRenderbuffers(1, &rbo); rbo = 0; }
    fbWidth = fbHeight = 0;
}

void GraphView::ensureFBOSize(int w, int h){
    if(w <= 0 || h <= 0) return;
    if(w == fbWidth && h == fbHeight) return;
    // free existing
    if(fbo){ glDeleteFramebuffers(1, &fbo); fbo = 0; }
    if(fboTex){ glDeleteTextures(1, &fboTex); fboTex = 0; }
    if(rbo){ glDeleteRenderbuffers(1, &rbo); rbo = 0; }

    fbWidth = w; fbHeight = h;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    glGenTextures(1, &fboTex);
    glBindTexture(GL_TEXTURE_2D, fboTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, fbWidth, fbHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex, 0);

    glGenRenderbuffers(1, &rbo);
    glBindRenderbuffer(GL_RENDERBUFFER, rbo);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, fbWidth, fbHeight);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);

    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){
        fprintf(stderr, "Failed to create FBO\n");
    }
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void GraphView::drawGraphGL(int canvasW, int canvasH){
    if(canvasW <= 0 || canvasH <= 0) return;
    // Bind FBO
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glViewport(0,0,canvasW,canvasH);
    glClearColor(0.08f,0.08f,0.08f,1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Draw edges (simple GL_LINES)
    if(showEdges && !edges.empty()){
        std::vector<float> lineVerts;
        lineVerts.reserve(edges.size()*4);
        for(auto &e : edges){
            auto aIt = nodeIndexById.find(e.a);
            auto bIt = nodeIndexById.find(e.b);
            if(aIt==nodeIndexById.end()||bIt==nodeIndexById.end()) continue;
            ImVec2 a = worldToScreen(nodes[aIt->second].pos, ImVec2(0,0), canvasW, canvasH);
            ImVec2 b = worldToScreen(nodes[bIt->second].pos, ImVec2(0,0), canvasW, canvasH);
            lineVerts.push_back(a.x); lineVerts.push_back(a.y);
            lineVerts.push_back(b.x); lineVerts.push_back(b.y);
        }
        if(!lineVerts.empty()){
            glUseProgram(lineProg);
            GLint screenLoc = glGetUniformLocation(lineProg, "uScreen");
            glUniform2f(screenLoc, (float)canvasW, (float)canvasH);
            GLint colorLoc = glGetUniformLocation(lineProg, "uColor");
            glUniform4f(colorLoc, 0.7f,0.7f,0.7f,0.9f);
            glBindVertexArray(lineVAO);
            glBindBuffer(GL_ARRAY_BUFFER, lineVBO);
            glBufferData(GL_ARRAY_BUFFER, lineVerts.size()*sizeof(float), lineVerts.data(), GL_STREAM_DRAW);
            glLineWidth(1.0f);
            glDrawArrays(GL_LINES, 0, (GLsizei)(lineVerts.size()/2));
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);
            glUseProgram(0);
        }
    }

    // Draw nodes as textured quads using circle texture
    if(!nodes.empty()){
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glUseProgram(quadProg);
        GLint screenLoc = glGetUniformLocation(quadProg, "uScreen");
        glUniform2f(screenLoc, (float)canvasW, (float)canvasH);
        GLint colorLoc = glGetUniformLocation(quadProg, "uColor");
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, circleTex);
        GLint texLoc = glGetUniformLocation(quadProg, "uTex"); glUniform1i(texLoc, 0);

        glBindVertexArray(quadVAO);
        for(size_t i=0;i<nodes.size();++i){
            Node &n = nodes[i];
            int selectedId = vault ? (int)vault->getSelectedItemID() : -1;
            float size = n.radius * scale;
            ImVec2 center = worldToScreen(n.pos, ImVec2(0,0), canvasW, canvasH);
            float x0 = center.x - size; float y0 = center.y - size;
            float x1 = center.x + size; float y1 = center.y + size;
            float quad[6*4] = {
                x0,y0, 0.0f,0.0f,
                x1,y0, 1.0f,0.0f,
                x1,y1, 1.0f,1.0f,
                x0,y0, 0.0f,0.0f,
                x1,y1, 1.0f,1.0f,
                x0,y1, 0.0f,1.0f
            };
            // determine node color based on tag filters and selection
            ImU32 col32;
            // base color
            float br=0.6f, bg=0.9f, bb=1.0f, ba=1.0f;
            if(selectedId == n.id){ br=1.0f; bg=0.86f; bb=0.47f; }

            // active tags
            std::vector<std::string> activeTags;
            for(auto &kv : tagToggles) if(kv.second) activeTags.push_back(kv.first);
            bool matches = true;
            if(!activeTags.empty()){
                if(tagModeAll){
                    for(auto &t : activeTags){ if(std::find(n.tags.begin(), n.tags.end(), t) == n.tags.end()){ matches = false; break; } }
                } else {
                    matches = false; for(auto &t : activeTags){ if(std::find(n.tags.begin(), n.tags.end(), t) != n.tags.end()){ matches = true; break; } }
                }
            }
            if(!activeTags.empty()){
                if(matches){
                    // blend colors of matching tags
                    float rr=0,gg=0,bbc=0; int cnt=0;
                    for(auto &t : activeTags){ if(std::find(n.tags.begin(), n.tags.end(), t) != n.tags.end()){ std::hash<std::string> hasher; uint64_t h = hasher(t); float hue = float(h%360); float s=0.65f, v=0.9f; float hh=hue/60.0f; int ii=(int)floor(hh); float f=hh-ii; float p=v*(1-s); float q=v*(1 - s*f); float tc=v*(1 - s*(1-f)); float r,gc,b2; switch(ii%6){ case 0: r=v;gc=tc; b2=p; break; case 1: r=q;gc=v;b2=p; break; case 2: r=p;gc=v;b2=tc; break; case 3: r=p;gc=q;b2=v; break; case 4: r=tc;gc=p;b2=v; break; default: r=v;gc=p;b2=q; break; } rr+=r; gg+=gc; bbc+=b2; cnt++; } }
                    if(cnt>0){ br = rr/cnt; bg = gg/cnt; bb = bbc/cnt; }
                } else {
                    // dim
                    br = 0.2f; bg = 0.2f; bb = 0.25f; ba = 0.6f;
                }
            }
            col32 = (0xFF<<24) | (((int)(br*255)&0xFF)<<16) | (((int)(bg*255)&0xFF)<<8) | ((int)(bb*255)&0xFF);

            glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
            glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STREAM_DRAW);
            glUniform4f(colorLoc, br,bg,bb,ba);
            glDrawArrays(GL_TRIANGLES, 0, 6);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }
        glBindVertexArray(0);
        glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
        glDisable(GL_BLEND);
    }

    // Draw labels into the FBO as quads sampling ImGui font atlas (avoids overlay duplicates/flicker)
    if(showLabels && !nodes.empty()){
        ImFont* font = ImGui::GetFont();
        if(font){
            GLuint fontTex = (GLuint)(intptr_t)ImGui::GetIO().Fonts->TexID;
            if(fontTex){
                std::vector<float> txtVerts;
                txtVerts.reserve(nodes.size()*64);

                for(size_t i=0;i<nodes.size();++i){
                    Node &n = nodes[i];
                    // baseline position: place labels above nodes using font ascent
                    ImVec2 center = worldToScreen(n.pos, ImVec2(0,0), canvasW, canvasH);
                    float x = center.x + n.radius*scale + 4.0f; // offset to the right
                    // use font ascent to align baseline above the node
                    float baseline = center.y - (n.radius*scale) - (font->Ascent * font->Scale);

                    const char* p = n.label.c_str();
                    const char* end = p + strlen(p);
                    while(p < end){
                        unsigned int codepoint = 0;
                        int nbytes = ImTextCharFromUtf8(&codepoint, p, end);
                        if(nbytes == 0) break;
                        p += nbytes;
                        ImWchar c = (ImWchar)codepoint;
                        const ImFontGlyph* g = font->FindGlyph(c);
                        if(!g) continue;
                        float gx0 = x + g->X0 * font->Scale;
                        float gy0 = baseline + g->Y0 * font->Scale;
                        float gx1 = x + g->X1 * font->Scale;
                        float gy1 = baseline + g->Y1 * font->Scale;
                        float u0 = g->U0, v0 = g->V0, u1 = g->U1, v1 = g->V1;
                        // Two triangles (6 verts) with attributes x,y,u,v (use font atlas UVs directly)
                        float quad[6*4] = {
                            gx0, gy0, u0, v0,
                            gx1, gy0, u1, v0,
                            gx1, gy1, u1, v1,
                            gx0, gy0, u0, v0,
                            gx1, gy1, u1, v1,
                            gx0, gy1, u0, v1
                        };
                        for(int k=0;k<6*4;++k) txtVerts.push_back(quad[k]);
                        x += g->AdvanceX * font->Scale;
                    }
                }

                if(!txtVerts.empty()){
                    // render using quadProg with font texture
                    glEnable(GL_BLEND);
                    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
                    glUseProgram(quadProg);
                    GLint screenLoc = glGetUniformLocation(quadProg, "uScreen");
                    glUniform2f(screenLoc, (float)canvasW, (float)canvasH);
                    GLint colorLoc = glGetUniformLocation(quadProg, "uColor");
                    glUniform4f(colorLoc, 0.95f,0.95f,0.95f,1.0f);
                    GLint texLoc = glGetUniformLocation(quadProg, "uTex"); glUniform1i(texLoc, 0);
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, fontTex);

                    glBindVertexArray(quadVAO);
                    glBindBuffer(GL_ARRAY_BUFFER, quadVBO);
                    glBufferData(GL_ARRAY_BUFFER, txtVerts.size()*sizeof(float), txtVerts.data(), GL_STREAM_DRAW);
                    glDrawArrays(GL_TRIANGLES, 0, (GLsizei)(txtVerts.size()/4));
                    glBindBuffer(GL_ARRAY_BUFFER, 0);
                    glBindVertexArray(0);

                    glBindTexture(GL_TEXTURE_2D, 0);
                    glUseProgram(0);
                    glDisable(GL_BLEND);
                }
            }
        }
    }

    // Unbind FBO and reset viewport (caller will render ImGui)
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}
