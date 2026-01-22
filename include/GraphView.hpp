#pragma once
#include <vector>
#include <unordered_map>
#include <string>
#include <imgui.h>
#include <cstdint>

struct Vault; // forward

class GraphView {
public:
    GraphView();
    ~GraphView();

    // Attach the current vault (can be nullptr). Does not take ownership.
    void setVault(class Vault* v);

    // Update internal graph from vault and simulate+render in one call
    void updateAndDraw(float dt);

    // Force rebuild of nodes/edges from vault
    void rebuildFromVault();

private:
    struct Node {
        int64_t id = -1;
        ImVec2 pos{0,0};
        ImVec2 vel{0,0};
        bool pinned = false;
        std::string label;
        float radius = 10.0f;
        std::vector<std::string> tags;
    };

    struct Edge {
        int64_t a=-1, b=-1; // a -> b (parent->child)
        float rest = 60.0f;
    };

    // Physics params
    float springK = 0.08f;
    float springRest = 60.0f;
    float repulsion = 9000.0f;
    float damping = 0.15f;
    int physicsSteps = 4;
    bool paused = false;

    // Rendering/interaction settings
    bool showLabels = true;
    bool showEdges = true;

    // Tag filters and mode
    std::vector<std::string> allTags;
    std::unordered_map<std::string,bool> tagToggles;
    bool tagModeAll = true; // true=AND, false=OR

    // Nodes/edges
    std::vector<Node> nodes;
    std::vector<Edge> edges;
    std::unordered_map<int64_t,int> nodeIndexById;

    // Camera (pan/zoom)
    ImVec2 pan{0,0};
    float scale = 1.0f;

    // Interaction state
    int draggingNodeIdx = -1;
    ImVec2 lastMousePos;

    // Attached Vault (not owned)
    Vault* vault = nullptr;
    bool needsRebuild = true;

    // OpenGL framebuffer & resources
    unsigned int fbo = 0;
    unsigned int fboTex = 0;
    unsigned int rbo = 0;
    int fbWidth = 0;
    int fbHeight = 0;

    // Simple GL programs and VAO/VBOs
    unsigned int lineProg = 0;
    unsigned int quadProg = 0;
    unsigned int quadVAO = 0;
    unsigned int quadVBO = 0;
    unsigned int lineVBO = 0;
    unsigned int lineVAO = 0;
    unsigned int circleTex = 0; // small alpha texture used to render circular nodes

private:
    void stepPhysics(float dt);
    void drawGraphGL(int canvasW, int canvasH);
    void ensureFBOSize(int w, int h);
    void initGLResources();
    void cleanupGLResources();

    ImVec2 worldToScreen(const ImVec2 &w, const ImVec2 &origin, int canvasW, int canvasH); // origin is top-left of canvas area
    ImVec2 screenToWorld(const ImVec2 &s, const ImVec2 &origin, int canvasW, int canvasH);
    int findNodeAt(const ImVec2 &worldPos);
};