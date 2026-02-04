#include <CharacterEditor/CharacterEditorUI.hpp>
#include <GL/glew.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/quaternion.hpp>
#include <plog/Log.h>
#include <algorithm>
#include <cmath>
#include <filesystem>

namespace CharacterEditor {

// ============================================================
// Shader sources
// ============================================================

static const char* g_meshVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aTexCoord;
layout(location = 3) in vec4 aTangent;
layout(location = 4) in ivec4 aBoneIDs;
layout(location = 5) in vec4 aBoneWeights;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;

void main() {
    vec4 worldPos = uModel * vec4(aPosition, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = normalize(uNormalMatrix * aNormal);
    vTexCoord = aTexCoord;
    gl_Position = uProjection * uView * worldPos;
}
)";

static const char* g_meshFragmentShader = R"(
#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vTexCoord;

uniform vec3 uCameraPos;
uniform vec3 uLightDir;
uniform vec4 uBaseColor;
uniform int uShadingMode;  // 0=solid, 1=textured, 2=flat, 3=wireframe, 4=weights, 5=normals

out vec4 FragColor;

void main() {
    vec3 N = normalize(vNormal);
    vec3 L = normalize(-uLightDir);
    vec3 V = normalize(uCameraPos - vWorldPos);
    vec3 H = normalize(L + V);
    
    float diff = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 32.0);
    
    vec3 ambient = vec3(0.15);
    vec3 diffuse = diff * vec3(0.7);
    vec3 specular = spec * vec3(0.3);
    
    vec3 color = uBaseColor.rgb;
    
    // Shading modes
    if (uShadingMode == 5) {
        // Normal visualization
        color = N * 0.5 + 0.5;
    } else if (uShadingMode == 6) {
        // UV checker
        float checker = mod(floor(vTexCoord.x * 10.0) + floor(vTexCoord.y * 10.0), 2.0);
        color = mix(vec3(0.2), vec3(0.8), checker);
    }
    
    vec3 finalColor = color * (ambient + diffuse) + specular;
    FragColor = vec4(finalColor, uBaseColor.a);
}
)";

static const char* g_lineVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec4 aColor;

uniform mat4 uMVP;

out vec4 vColor;

void main() {
    vColor = aColor;
    gl_Position = uMVP * vec4(aPosition, 1.0);
}
)";

static const char* g_lineFragmentShader = R"(
#version 330 core
in vec4 vColor;
out vec4 FragColor;

void main() {
    FragColor = vColor;
}
)";

static const char* g_gridVertexShader = R"(
#version 330 core
layout(location = 0) in vec3 aPosition;

uniform mat4 uMVP;

out vec3 vWorldPos;

void main() {
    vWorldPos = aPosition;
    gl_Position = uMVP * vec4(aPosition, 1.0);
}
)";

static const char* g_gridFragmentShader = R"(
#version 330 core
in vec3 vWorldPos;

uniform vec4 uGridColor;
uniform float uGridSize;

out vec4 FragColor;

void main() {
    vec2 coord = vWorldPos.xz / uGridSize;
    vec2 grid = abs(fract(coord - 0.5) - 0.5) / fwidth(coord);
    float line = min(grid.x, grid.y);
    float alpha = 1.0 - min(line, 1.0);
    
    // Fade with distance
    float dist = length(vWorldPos.xz);
    alpha *= smoothstep(50.0, 10.0, dist);
    
    // Highlight axes
    float axisWidth = 0.05;
    if (abs(vWorldPos.x) < axisWidth) {
        FragColor = vec4(0.2, 0.2, 0.8, alpha * 2.0);  // Z axis (blue)
        return;
    }
    if (abs(vWorldPos.z) < axisWidth) {
        FragColor = vec4(0.8, 0.2, 0.2, alpha * 2.0);  // X axis (red)
        return;
    }
    
    FragColor = vec4(uGridColor.rgb, alpha * uGridColor.a);
}
)";

// ============================================================
// Helper functions
// ============================================================

static uint32_t compileShader(GLenum type, const char* source) {
    uint32_t shader = glCreateShader(type);
    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);
    
    int success;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetShaderInfoLog(shader, 512, nullptr, infoLog);
        PLOGE << "Shader compilation failed: " << infoLog;
        glDeleteShader(shader);
        return 0;
    }
    
    return shader;
}

static uint32_t createShaderProgram(const char* vertexSource, const char* fragmentSource) {
    uint32_t vertexShader = compileShader(GL_VERTEX_SHADER, vertexSource);
    if (!vertexShader) return 0;
    
    uint32_t fragmentShader = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
    if (!fragmentShader) {
        glDeleteShader(vertexShader);
        return 0;
    }
    
    uint32_t program = glCreateProgram();
    glAttachShader(program, vertexShader);
    glAttachShader(program, fragmentShader);
    glLinkProgram(program);
    
    int success;
    glGetProgramiv(program, GL_LINK_STATUS, &success);
    if (!success) {
        char infoLog[512];
        glGetProgramInfoLog(program, 512, nullptr, infoLog);
        PLOGE << "Shader program linking failed: " << infoLog;
        glDeleteProgram(program);
        program = 0;
    }
    
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    
    return program;
}

// ============================================================
// ViewportCamera implementation
// ============================================================

glm::vec3 ViewportCamera::getPosition() const {
    float yawRad = glm::radians(yaw);
    float pitchRad = glm::radians(pitch);
    
    glm::vec3 offset;
    offset.x = distance * cos(pitchRad) * sin(yawRad);
    offset.y = distance * sin(pitchRad);
    offset.z = distance * cos(pitchRad) * cos(yawRad);
    
    return target + offset;
}

glm::mat4 ViewportCamera::getViewMatrix() const {
    return glm::lookAt(getPosition(), target, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 ViewportCamera::getProjectionMatrix(float aspectRatio) const {
    return glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
}

void ViewportCamera::orbit(float deltaYaw, float deltaPitch) {
    yaw += deltaYaw;
    pitch = glm::clamp(pitch + deltaPitch, -89.0f, 89.0f);
}

void ViewportCamera::pan(float deltaX, float deltaY) {
    glm::vec3 pos = getPosition();
    glm::vec3 forward = glm::normalize(target - pos);
    glm::vec3 right = glm::normalize(glm::cross(forward, glm::vec3(0, 1, 0)));
    glm::vec3 up = glm::normalize(glm::cross(right, forward));
    
    float panSpeed = distance * 0.002f;
    target += right * (-deltaX * panSpeed) + up * (deltaY * panSpeed);
}

void ViewportCamera::zoom(float delta) {
    distance = glm::clamp(distance - delta * distance * 0.1f, 0.1f, 1000.0f);
}

void ViewportCamera::focusOn(const glm::vec3& center, float radius) {
    target = center;
    distance = radius * 2.5f;
}

// ============================================================
// MeshGPUData implementation
// ============================================================

void MeshGPUData::release() {
    if (vao) glDeleteVertexArrays(1, &vao);
    if (vbo) glDeleteBuffers(1, &vbo);
    if (ebo) glDeleteBuffers(1, &ebo);
    vao = vbo = ebo = 0;
    vertexCount = indexCount = 0;
    isValid = false;
}

// ============================================================
// CharacterEditorUI implementation
// ============================================================

CharacterEditorUI::CharacterEditorUI() = default;

CharacterEditorUI::~CharacterEditorUI() {
    shutdown();
}

bool CharacterEditorUI::initialize() {
    if (m_initialized) return true;
    
    PLOGI << "Initializing CharacterEditorUI...";
    
    if (!createShaders()) {
        PLOGE << "Failed to create shaders";
        return false;
    }
    
    if (!createFramebuffer(800, 600)) {
        PLOGE << "Failed to create framebuffer";
        destroyShaders();
        return false;
    }
    
    // Create line VAO/VBO for debug drawing
    glGenVertexArrays(1, &m_lineVAO);
    glGenBuffers(1, &m_lineVBO);
    
    glBindVertexArray(m_lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
    
    // Position (vec3) + Color (vec4)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
    
    glBindVertexArray(0);
    
    m_initialized = true;
    PLOGI << "CharacterEditorUI initialized successfully";
    return true;
}

void CharacterEditorUI::shutdown() {
    if (!m_initialized) return;
    
    PLOGI << "Shutting down CharacterEditorUI...";
    
    // Release all mesh GPU data
    for (auto& model : m_models) {
        for (auto& gpuData : model.meshGPUData) {
            gpuData.release();
        }
    }
    m_models.clear();
    
    // Release line VAO/VBO
    if (m_lineVAO) glDeleteVertexArrays(1, &m_lineVAO);
    if (m_lineVBO) glDeleteBuffers(1, &m_lineVBO);
    m_lineVAO = m_lineVBO = 0;
    
    destroyFramebuffer();
    destroyShaders();
    
    m_initialized = false;
}

bool CharacterEditorUI::createFramebuffer(int width, int height) {
    m_fbWidth = width;
    m_fbHeight = height;
    
    // Create framebuffer
    glGenFramebuffers(1, &m_fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    
    // Create color texture
    glGenTextures(1, &m_colorTexture);
    glBindTexture(GL_TEXTURE_2D, m_colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTexture, 0);
    
    // Create depth renderbuffer
    glGenRenderbuffers(1, &m_depthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_depthRBO);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_depthRBO);
    
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        PLOGE << "Framebuffer is not complete!";
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void CharacterEditorUI::resizeFramebuffer(int width, int height) {
    if (width == m_fbWidth && height == m_fbHeight) return;
    if (width <= 0 || height <= 0) return;
    
    destroyFramebuffer();
    createFramebuffer(width, height);
}

void CharacterEditorUI::destroyFramebuffer() {
    if (m_fbo) glDeleteFramebuffers(1, &m_fbo);
    if (m_colorTexture) glDeleteTextures(1, &m_colorTexture);
    if (m_depthRBO) glDeleteRenderbuffers(1, &m_depthRBO);
    m_fbo = m_colorTexture = m_depthRBO = 0;
}

bool CharacterEditorUI::createShaders() {
    m_meshShader = createShaderProgram(g_meshVertexShader, g_meshFragmentShader);
    if (!m_meshShader) return false;
    
    m_lineShader = createShaderProgram(g_lineVertexShader, g_lineFragmentShader);
    if (!m_lineShader) {
        glDeleteProgram(m_meshShader);
        m_meshShader = 0;
        return false;
    }
    
    m_gridShader = createShaderProgram(g_gridVertexShader, g_gridFragmentShader);
    if (!m_gridShader) {
        glDeleteProgram(m_meshShader);
        glDeleteProgram(m_lineShader);
        m_meshShader = m_lineShader = 0;
        return false;
    }
    
    return true;
}

void CharacterEditorUI::destroyShaders() {
    if (m_meshShader) glDeleteProgram(m_meshShader);
    if (m_lineShader) glDeleteProgram(m_lineShader);
    if (m_gridShader) glDeleteProgram(m_gridShader);
    m_meshShader = m_lineShader = m_gridShader = 0;
}

bool CharacterEditorUI::uploadMeshToGPU(const Mesh& mesh, MeshGPUData& gpuData) {
    gpuData.release();
    
    if (mesh.vertices.empty() || mesh.indices.empty()) {
        return false;
    }
    
    glGenVertexArrays(1, &gpuData.vao);
    glGenBuffers(1, &gpuData.vbo);
    glGenBuffers(1, &gpuData.ebo);
    
    glBindVertexArray(gpuData.vao);
    
    // Upload vertex data
    glBindBuffer(GL_ARRAY_BUFFER, gpuData.vbo);
    glBufferData(GL_ARRAY_BUFFER, mesh.vertices.size() * sizeof(Vertex), mesh.vertices.data(), GL_STATIC_DRAW);
    
    // Upload index data
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, gpuData.ebo);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, mesh.indices.size() * sizeof(uint32_t), mesh.indices.data(), GL_STATIC_DRAW);
    
    // Set up vertex attributes
    // Position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position));
    
    // Normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));
    
    // TexCoord
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv));
    
    // Tangent
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tangent));
    
    // Bone IDs (integer)
    glEnableVertexAttribArray(4);
    glVertexAttribIPointer(4, 4, GL_INT, sizeof(Vertex), (void*)offsetof(Vertex, boneIDs));
    
    // Bone Weights
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, boneWeights));
    
    glBindVertexArray(0);
    
    gpuData.vertexCount = static_cast<uint32_t>(mesh.vertices.size());
    gpuData.indexCount = static_cast<uint32_t>(mesh.indices.size());
    gpuData.isValid = true;
    
    return true;
}

bool CharacterEditorUI::loadModel(const std::string& filePath) {
    PLOGI << "Loading model: " << filePath;
    
    ImportConfig config;
    LoadResult result = ModelLoader::loadFromFile(filePath, config);
    
    if (!result.success) {
        for (const auto& error : result.errors) {
            PLOGE << "Load error: " << error;
        }
        return false;
    }
    
    LoadedModel model;
    model.filePath = filePath;
    model.loadResult = std::move(result);
    model.worldTransform = Transform();
    
    // Upload meshes to GPU
    for (const auto& mesh : model.loadResult.meshes) {
        MeshGPUData gpuData;
        if (uploadMeshToGPU(mesh, gpuData)) {
            model.meshGPUData.push_back(gpuData);
        } else {
            PLOGW << "Failed to upload mesh to GPU: " << mesh.name;
            model.meshGPUData.push_back(MeshGPUData{});  // Push empty placeholder
        }
    }
    
    // Focus camera on model
    if (!model.loadResult.meshes.empty()) {
        glm::vec3 boundsMin(FLT_MAX), boundsMax(-FLT_MAX);
        for (const auto& mesh : model.loadResult.meshes) {
            for (const auto& v : mesh.vertices) {
                boundsMin = glm::min(boundsMin, v.position);
                boundsMax = glm::max(boundsMax, v.position);
            }
        }
        glm::vec3 center = (boundsMin + boundsMax) * 0.5f;
        float radius = glm::length(boundsMax - boundsMin) * 0.5f;
        m_camera.focusOn(center, radius);
    }
    
    m_models.push_back(std::move(model));
    m_selectedModelIndex = static_cast<int>(m_models.size()) - 1;
    
    PLOGI << "Model loaded successfully with " << m_models.back().loadResult.meshes.size() << " meshes";
    return true;
}

void CharacterEditorUI::clearModels() {
    for (auto& model : m_models) {
        for (auto& gpuData : model.meshGPUData) {
            gpuData.release();
        }
    }
    m_models.clear();
    m_selectedModelIndex = -1;
    m_selectedBoneIndex = -1;
    m_selectedSocketIndex = -1;
}

void CharacterEditorUI::toggleDebugView(DebugView view) {
    if (hasFlag(m_debugViews, view)) {
        m_debugViews = static_cast<DebugView>(static_cast<uint32_t>(m_debugViews) & ~static_cast<uint32_t>(view));
    } else {
        m_debugViews |= view;
    }
}

void CharacterEditorUI::render() {
    if (!m_isOpen) return;
    
    if (!m_initialized) {
        if (!initialize()) {
            PLOGE << "Failed to initialize CharacterEditorUI";
            m_isOpen = false;
            return;
        }
    }
    
    ImGui::SetNextWindowSize(ImVec2(1200, 800), ImGuiCond_FirstUseEver);
    
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar;
    
    if (ImGui::Begin("Character Editor", &m_isOpen, windowFlags)) {
        renderMenuBar();
        
        // Main content area with panels
        float panelWidth = 250.0f;
        
        // Left panel - Hierarchy
        ImGui::BeginChild("LeftPanel", ImVec2(panelWidth, 0), true);
        renderHierarchyPanel();
        ImGui::EndChild();
        
        ImGui::SameLine();
        
        // Center - Viewport
        ImVec2 viewportSize = ImGui::GetContentRegionAvail();
        viewportSize.x -= panelWidth + 10;  // Right panel
        
        ImGui::BeginChild("ViewportArea", ImVec2(viewportSize.x, 0), false);
        renderToolbar();
        renderViewport();
        ImGui::EndChild();
        
        ImGui::SameLine();
        
        // Right panel - Properties
        ImGui::BeginChild("RightPanel", ImVec2(0, 0), true);
        renderPropertiesPanel();
        ImGui::Separator();
        renderShapeKeyPanel();
        ImGui::Separator();
        renderSocketPanel();
        ImGui::Separator();
        renderIKPanel();
        ImGui::Separator();
        renderDebugPanel();
        ImGui::EndChild();
    }
    ImGui::End();
}

void CharacterEditorUI::renderMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Load Model...", "Ctrl+O")) {
                // TODO: File dialog
            }
            if (ImGui::MenuItem("Clear All")) {
                clearModels();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Close")) {
                m_isOpen = false;
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("View")) {
            bool wireframe = hasFlag(m_debugViews, DebugView::Wireframe);
            if (ImGui::MenuItem("Wireframe", nullptr, &wireframe)) {
                toggleDebugView(DebugView::Wireframe);
            }
            
            bool bones = hasFlag(m_debugViews, DebugView::Bones);
            if (ImGui::MenuItem("Bones", nullptr, &bones)) {
                toggleDebugView(DebugView::Bones);
            }
            
            bool sockets = hasFlag(m_debugViews, DebugView::Sockets);
            if (ImGui::MenuItem("Sockets", nullptr, &sockets)) {
                toggleDebugView(DebugView::Sockets);
            }
            
            bool normals = hasFlag(m_debugViews, DebugView::Normals);
            if (ImGui::MenuItem("Normals", nullptr, &normals)) {
                toggleDebugView(DebugView::Normals);
            }
            
            bool bbox = hasFlag(m_debugViews, DebugView::BoundingBox);
            if (ImGui::MenuItem("Bounding Box", nullptr, &bbox)) {
                toggleDebugView(DebugView::BoundingBox);
            }
            
            ImGui::Separator();
            ImGui::MenuItem("Grid", nullptr, &m_showGrid);
            ImGui::MenuItem("Gizmo", nullptr, &m_showGizmo);
            
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Shading")) {
            if (ImGui::MenuItem("Solid", nullptr, m_shadingMode == ShadingMode::Solid)) {
                m_shadingMode = ShadingMode::Solid;
            }
            if (ImGui::MenuItem("Wireframe", nullptr, m_shadingMode == ShadingMode::Wireframe)) {
                m_shadingMode = ShadingMode::Wireframe;
            }
            if (ImGui::MenuItem("Normals", nullptr, m_shadingMode == ShadingMode::NormalMap)) {
                m_shadingMode = ShadingMode::NormalMap;
            }
            if (ImGui::MenuItem("UV Checker", nullptr, m_shadingMode == ShadingMode::UVChecker)) {
                m_shadingMode = ShadingMode::UVChecker;
            }
            ImGui::EndMenu();
        }
        
        ImGui::EndMenuBar();
    }
}

void CharacterEditorUI::renderToolbar() {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4, 4));
    
    if (ImGui::Button("Reset View")) {
        m_camera = ViewportCamera();
        if (m_selectedModelIndex >= 0 && m_selectedModelIndex < static_cast<int>(m_models.size())) {
            const auto& model = m_models[m_selectedModelIndex];
            if (!model.loadResult.meshes.empty()) {
                glm::vec3 boundsMin(FLT_MAX), boundsMax(-FLT_MAX);
                for (const auto& mesh : model.loadResult.meshes) {
                    for (const auto& v : mesh.vertices) {
                        boundsMin = glm::min(boundsMin, v.position);
                        boundsMax = glm::max(boundsMax, v.position);
                    }
                }
                m_camera.focusOn((boundsMin + boundsMax) * 0.5f, glm::length(boundsMax - boundsMin) * 0.5f);
            }
        }
    }
    
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    
    // Shading mode quick buttons
    if (ImGui::Button(m_shadingMode == ShadingMode::Solid ? "[Solid]" : "Solid")) {
        m_shadingMode = ShadingMode::Solid;
    }
    ImGui::SameLine();
    if (ImGui::Button(m_shadingMode == ShadingMode::Wireframe ? "[Wire]" : "Wire")) {
        m_shadingMode = ShadingMode::Wireframe;
    }
    
    ImGui::SameLine();
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::SameLine();
    
    // Debug toggles
    bool bones = hasFlag(m_debugViews, DebugView::Bones);
    if (ImGui::Checkbox("Bones", &bones)) {
        toggleDebugView(DebugView::Bones);
    }
    
    ImGui::SameLine();
    bool sockets = hasFlag(m_debugViews, DebugView::Sockets);
    if (ImGui::Checkbox("Sockets", &sockets)) {
        toggleDebugView(DebugView::Sockets);
    }
    
    ImGui::PopStyleVar();
}

void CharacterEditorUI::renderViewport() {
    ImVec2 regionAvail = ImGui::GetContentRegionAvail();
    if (regionAvail.x < 1 || regionAvail.y < 1) return;
    
    // Resize framebuffer if needed
    int newWidth = static_cast<int>(regionAvail.x);
    int newHeight = static_cast<int>(regionAvail.y);
    resizeFramebuffer(newWidth, newHeight);
    
    m_viewportPos = ImGui::GetCursorScreenPos();
    m_viewportSize = regionAvail;
    
    // Render to framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, m_fbo);
    glViewport(0, 0, m_fbWidth, m_fbHeight);
    
    glClearColor(m_clearColor.r, m_clearColor.g, m_clearColor.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);
    
    renderScene();
    renderDebugOverlays();
    
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    
    // Display framebuffer texture in ImGui
    ImGui::Image(
        (ImTextureID)(intptr_t)m_colorTexture,
        regionAvail,
        ImVec2(0, 1),  // Flip UV vertically
        ImVec2(1, 0)
    );
    
    // Handle input
    m_viewportHovered = ImGui::IsItemHovered();
    m_viewportFocused = ImGui::IsWindowFocused();
    
    if (m_viewportHovered) {
        handleViewportInput();
    }
}

void CharacterEditorUI::renderScene() {
    if (m_showGrid) {
        renderGrid();
    }
    
    float aspectRatio = m_viewportSize.x / m_viewportSize.y;
    glm::mat4 view = m_camera.getViewMatrix();
    glm::mat4 projection = m_camera.getProjectionMatrix(aspectRatio);
    
    // Render meshes
    glUseProgram(m_meshShader);
    
    glUniformMatrix4fv(glGetUniformLocation(m_meshShader, "uView"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(m_meshShader, "uProjection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(glGetUniformLocation(m_meshShader, "uCameraPos"), 1, glm::value_ptr(m_camera.getPosition()));
    glUniform3f(glGetUniformLocation(m_meshShader, "uLightDir"), 0.3f, -1.0f, 0.5f);
    glUniform1i(glGetUniformLocation(m_meshShader, "uShadingMode"), static_cast<int>(m_shadingMode));
    
    for (size_t mi = 0; mi < m_models.size(); ++mi) {
        const auto& model = m_models[mi];
        glm::mat4 modelMatrix = model.worldTransform.toMatrix();
        
        for (size_t i = 0; i < model.meshGPUData.size() && i < model.loadResult.meshes.size(); ++i) {
            const auto& gpuData = model.meshGPUData[i];
            const auto& mesh = model.loadResult.meshes[i];
            
            if (gpuData.isValid) {
                renderMesh(mesh, gpuData, modelMatrix);
            }
        }
    }
    
    // Render wireframe overlay if enabled
    if (m_shadingMode == ShadingMode::Wireframe || hasFlag(m_debugViews, DebugView::Wireframe)) {
        glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        glDisable(GL_CULL_FACE);
        
        glUniform4f(glGetUniformLocation(m_meshShader, "uBaseColor"), 0.0f, 0.0f, 0.0f, 1.0f);
        
        for (const auto& model : m_models) {
            glm::mat4 modelMatrix = model.worldTransform.toMatrix();
            for (size_t i = 0; i < model.meshGPUData.size(); ++i) {
                const auto& gpuData = model.meshGPUData[i];
                if (gpuData.isValid) {
                    glBindVertexArray(gpuData.vao);
                    glDrawElements(GL_TRIANGLES, gpuData.indexCount, GL_UNSIGNED_INT, nullptr);
                }
            }
        }
        
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        glEnable(GL_CULL_FACE);
    }
}

void CharacterEditorUI::renderMesh(const Mesh& mesh, const MeshGPUData& gpuData, const glm::mat4& modelMatrix) {
    glUniformMatrix4fv(glGetUniformLocation(m_meshShader, "uModel"), 1, GL_FALSE, glm::value_ptr(modelMatrix));
    
    glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
    glUniformMatrix3fv(glGetUniformLocation(m_meshShader, "uNormalMatrix"), 1, GL_FALSE, glm::value_ptr(normalMatrix));
    
    // Get material color
    glm::vec4 baseColor(0.7f, 0.7f, 0.7f, 1.0f);
    if (!mesh.materials.empty()) {
        baseColor = mesh.materials[0].baseColor;
    }
    glUniform4fv(glGetUniformLocation(m_meshShader, "uBaseColor"), 1, glm::value_ptr(baseColor));
    
    glBindVertexArray(gpuData.vao);
    glDrawElements(GL_TRIANGLES, gpuData.indexCount, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void CharacterEditorUI::renderDebugOverlays() {
    if (m_models.empty()) return;
    
    float aspectRatio = m_viewportSize.x / m_viewportSize.y;
    glm::mat4 view = m_camera.getViewMatrix();
    glm::mat4 projection = m_camera.getProjectionMatrix(aspectRatio);
    glm::mat4 vp = projection * view;
    
    glUseProgram(m_lineShader);
    
    glDisable(GL_DEPTH_TEST);
    glLineWidth(2.0f);
    
    for (const auto& model : m_models) {
        glm::mat4 modelMatrix = model.worldTransform.toMatrix();
        glm::mat4 mvp = vp * modelMatrix;
        
        glUniformMatrix4fv(glGetUniformLocation(m_lineShader, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
        
        if (hasFlag(m_debugViews, DebugView::Bones)) {
            renderBones(model.loadResult.skeleton, modelMatrix);
        }
        
        if (hasFlag(m_debugViews, DebugView::Sockets)) {
            renderSockets(model.loadResult.extractedSockets, modelMatrix);
        }
        
        if (hasFlag(m_debugViews, DebugView::BoundingBox)) {
            for (const auto& mesh : model.loadResult.meshes) {
                glm::vec3 boundsMin(FLT_MAX), boundsMax(-FLT_MAX);
                for (const auto& v : mesh.vertices) {
                    boundsMin = glm::min(boundsMin, v.position);
                    boundsMax = glm::max(boundsMax, v.position);
                }
                renderBoundingBox(boundsMin, boundsMax, modelMatrix);
            }
        }
    }
    
    glEnable(GL_DEPTH_TEST);
    glLineWidth(1.0f);
}

void CharacterEditorUI::renderBones(const Skeleton& skeleton, const glm::mat4& modelMatrix) {
    if (skeleton.empty()) return;
    
    std::vector<float> lineData;
    
    // Helper to add a line
    auto addLine = [&](const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& color) {
        lineData.push_back(p1.x); lineData.push_back(p1.y); lineData.push_back(p1.z);
        lineData.push_back(color.r); lineData.push_back(color.g); lineData.push_back(color.b); lineData.push_back(color.a);
        lineData.push_back(p2.x); lineData.push_back(p2.y); lineData.push_back(p2.z);
        lineData.push_back(color.r); lineData.push_back(color.g); lineData.push_back(color.b); lineData.push_back(color.a);
    };
    
    // Draw bone connections
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        const auto& bone = skeleton.bones[i];
        glm::vec3 bonePos = bone.localTransform.position;
        
        // Draw octahedron representation
        float boneSize = 0.05f;
        glm::vec4 color = (static_cast<int>(i) == m_selectedBoneIndex) ? m_selectedColor : m_boneColor;
        
        // Simple cross at bone position
        addLine(bonePos - glm::vec3(boneSize, 0, 0), bonePos + glm::vec3(boneSize, 0, 0), color);
        addLine(bonePos - glm::vec3(0, boneSize, 0), bonePos + glm::vec3(0, boneSize, 0), color);
        addLine(bonePos - glm::vec3(0, 0, boneSize), bonePos + glm::vec3(0, 0, boneSize), color);
        
        // Draw connection to parent
        if (bone.parentID != UINT32_MAX && bone.parentID < skeleton.bones.size()) {
            glm::vec3 parentPos = skeleton.bones[bone.parentID].localTransform.position;
            addLine(parentPos, bonePos, color * 0.7f);
        }
    }
    
    // Upload and draw
    if (!lineData.empty()) {
        glBindVertexArray(m_lineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
        glBufferData(GL_ARRAY_BUFFER, lineData.size() * sizeof(float), lineData.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineData.size() / 7));
        glBindVertexArray(0);
    }
}

void CharacterEditorUI::renderSockets(const std::vector<Socket>& sockets, const glm::mat4& modelMatrix) {
    std::vector<float> lineData;
    
    auto addLine = [&](const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& color) {
        lineData.push_back(p1.x); lineData.push_back(p1.y); lineData.push_back(p1.z);
        lineData.push_back(color.r); lineData.push_back(color.g); lineData.push_back(color.b); lineData.push_back(color.a);
        lineData.push_back(p2.x); lineData.push_back(p2.y); lineData.push_back(p2.z);
        lineData.push_back(color.r); lineData.push_back(color.g); lineData.push_back(color.b); lineData.push_back(color.a);
    };
    
    for (size_t i = 0; i < sockets.size(); ++i) {
        const auto& socket = sockets[i];
        glm::vec3 pos = socket.localOffset.position;
        glm::vec4 color = (static_cast<int>(i) == m_selectedSocketIndex) ? m_selectedColor : m_socketColor;
        
        float size = socket.influenceRadius > 0 ? socket.influenceRadius : 0.1f;
        
        // Draw socket axes
        glm::mat3 rot = glm::mat3_cast(socket.localOffset.rotation);
        glm::vec3 right = rot * glm::vec3(1, 0, 0) * size;
        glm::vec3 up = rot * glm::vec3(0, 1, 0) * size;
        glm::vec3 forward = rot * glm::vec3(0, 0, 1) * size;
        
        addLine(pos, pos + right, glm::vec4(1, 0, 0, 1));    // X - Red
        addLine(pos, pos + up, glm::vec4(0, 1, 0, 1));       // Y - Green
        addLine(pos, pos + forward, glm::vec4(0, 0, 1, 1));  // Z - Blue
        
        // Draw diamond shape
        float d = size * 0.5f;
        addLine(pos + glm::vec3(d, 0, 0), pos + glm::vec3(0, d, 0), color);
        addLine(pos + glm::vec3(0, d, 0), pos + glm::vec3(-d, 0, 0), color);
        addLine(pos + glm::vec3(-d, 0, 0), pos + glm::vec3(0, -d, 0), color);
        addLine(pos + glm::vec3(0, -d, 0), pos + glm::vec3(d, 0, 0), color);
        
        addLine(pos + glm::vec3(0, 0, d), pos + glm::vec3(0, d, 0), color);
        addLine(pos + glm::vec3(0, d, 0), pos + glm::vec3(0, 0, -d), color);
        addLine(pos + glm::vec3(0, 0, -d), pos + glm::vec3(0, -d, 0), color);
        addLine(pos + glm::vec3(0, -d, 0), pos + glm::vec3(0, 0, d), color);
    }
    
    if (!lineData.empty()) {
        glBindVertexArray(m_lineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
        glBufferData(GL_ARRAY_BUFFER, lineData.size() * sizeof(float), lineData.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineData.size() / 7));
        glBindVertexArray(0);
    }
}

void CharacterEditorUI::renderBoundingBox(const glm::vec3& min, const glm::vec3& max, const glm::mat4& modelMatrix) {
    std::vector<float> lineData;
    glm::vec4 color(0.5f, 0.5f, 0.0f, 1.0f);
    
    auto addLine = [&](const glm::vec3& p1, const glm::vec3& p2) {
        lineData.push_back(p1.x); lineData.push_back(p1.y); lineData.push_back(p1.z);
        lineData.push_back(color.r); lineData.push_back(color.g); lineData.push_back(color.b); lineData.push_back(color.a);
        lineData.push_back(p2.x); lineData.push_back(p2.y); lineData.push_back(p2.z);
        lineData.push_back(color.r); lineData.push_back(color.g); lineData.push_back(color.b); lineData.push_back(color.a);
    };
    
    // 12 edges of the box
    addLine(glm::vec3(min.x, min.y, min.z), glm::vec3(max.x, min.y, min.z));
    addLine(glm::vec3(max.x, min.y, min.z), glm::vec3(max.x, max.y, min.z));
    addLine(glm::vec3(max.x, max.y, min.z), glm::vec3(min.x, max.y, min.z));
    addLine(glm::vec3(min.x, max.y, min.z), glm::vec3(min.x, min.y, min.z));
    
    addLine(glm::vec3(min.x, min.y, max.z), glm::vec3(max.x, min.y, max.z));
    addLine(glm::vec3(max.x, min.y, max.z), glm::vec3(max.x, max.y, max.z));
    addLine(glm::vec3(max.x, max.y, max.z), glm::vec3(min.x, max.y, max.z));
    addLine(glm::vec3(min.x, max.y, max.z), glm::vec3(min.x, min.y, max.z));
    
    addLine(glm::vec3(min.x, min.y, min.z), glm::vec3(min.x, min.y, max.z));
    addLine(glm::vec3(max.x, min.y, min.z), glm::vec3(max.x, min.y, max.z));
    addLine(glm::vec3(max.x, max.y, min.z), glm::vec3(max.x, max.y, max.z));
    addLine(glm::vec3(min.x, max.y, min.z), glm::vec3(min.x, max.y, max.z));
    
    glBindVertexArray(m_lineVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
    glBufferData(GL_ARRAY_BUFFER, lineData.size() * sizeof(float), lineData.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineData.size() / 7));
    glBindVertexArray(0);
}

void CharacterEditorUI::renderGrid() {
    float aspectRatio = m_viewportSize.x / m_viewportSize.y;
    glm::mat4 view = m_camera.getViewMatrix();
    glm::mat4 projection = m_camera.getProjectionMatrix(aspectRatio);
    glm::mat4 mvp = projection * view;
    
    glUseProgram(m_gridShader);
    glUniformMatrix4fv(glGetUniformLocation(m_gridShader, "uMVP"), 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform4fv(glGetUniformLocation(m_gridShader, "uGridColor"), 1, glm::value_ptr(m_gridColor));
    glUniform1f(glGetUniformLocation(m_gridShader, "uGridSize"), 1.0f);
    
    // Draw a large quad for the grid
    static uint32_t gridVAO = 0, gridVBO = 0;
    if (gridVAO == 0) {
        float gridVertices[] = {
            -50.0f, 0.0f, -50.0f,
             50.0f, 0.0f, -50.0f,
             50.0f, 0.0f,  50.0f,
            -50.0f, 0.0f, -50.0f,
             50.0f, 0.0f,  50.0f,
            -50.0f, 0.0f,  50.0f,
        };
        
        glGenVertexArrays(1, &gridVAO);
        glGenBuffers(1, &gridVBO);
        
        glBindVertexArray(gridVAO);
        glBindBuffer(GL_ARRAY_BUFFER, gridVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(gridVertices), gridVertices, GL_STATIC_DRAW);
        
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), nullptr);
        
        glBindVertexArray(0);
    }
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glBindVertexArray(gridVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    
    glDisable(GL_BLEND);
}

void CharacterEditorUI::renderHierarchyPanel() {
    ImGui::Text("Hierarchy");
    ImGui::Separator();
    
    if (m_models.empty()) {
        ImGui::TextDisabled("No models loaded");
        return;
    }
    
    for (size_t mi = 0; mi < m_models.size(); ++mi) {
        const auto& model = m_models[mi];
        std::string label = std::filesystem::path(model.filePath).filename().string();
        
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
        if (static_cast<int>(mi) == m_selectedModelIndex) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        
        if (ImGui::TreeNodeEx(label.c_str(), flags)) {
            // Meshes
            if (ImGui::TreeNode("Meshes")) {
                for (size_t i = 0; i < model.loadResult.meshes.size(); ++i) {
                    const auto& mesh = model.loadResult.meshes[i];
                    ImGui::Text("%s (%zu verts, %zu tris)", 
                               mesh.name.empty() ? "Unnamed" : mesh.name.c_str(),
                               mesh.vertices.size(), mesh.indices.size() / 3);
                }
                ImGui::TreePop();
            }
            
            // Skeleton
            if (!model.loadResult.skeleton.empty()) {
                if (ImGui::TreeNode("Skeleton")) {
                    for (size_t i = 0; i < model.loadResult.skeleton.bones.size(); ++i) {
                        const auto& bone = model.loadResult.skeleton.bones[i];
                        ImGuiTreeNodeFlags boneFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                        if (static_cast<int>(i) == m_selectedBoneIndex) {
                            boneFlags |= ImGuiTreeNodeFlags_Selected;
                        }
                        
                        ImGui::TreeNodeEx(bone.name.c_str(), boneFlags);
                        if (ImGui::IsItemClicked()) {
                            m_selectedModelIndex = static_cast<int>(mi);
                            m_selectedBoneIndex = static_cast<int>(i);
                        }
                    }
                    ImGui::TreePop();
                }
            }
            
            // Sockets
            if (!model.loadResult.extractedSockets.empty()) {
                if (ImGui::TreeNode("Sockets")) {
                    for (size_t i = 0; i < model.loadResult.extractedSockets.size(); ++i) {
                        const auto& socket = model.loadResult.extractedSockets[i];
                        ImGuiTreeNodeFlags socketFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                        if (static_cast<int>(i) == m_selectedSocketIndex) {
                            socketFlags |= ImGuiTreeNodeFlags_Selected;
                        }
                        
                        ImGui::TreeNodeEx(socket.name.c_str(), socketFlags);
                        if (ImGui::IsItemClicked()) {
                            m_selectedModelIndex = static_cast<int>(mi);
                            m_selectedSocketIndex = static_cast<int>(i);
                        }
                    }
                    ImGui::TreePop();
                }
            }
            
            ImGui::TreePop();
        }
        
        if (ImGui::IsItemClicked()) {
            m_selectedModelIndex = static_cast<int>(mi);
        }
    }
}

void CharacterEditorUI::renderPropertiesPanel() {
    ImGui::Text("Properties");
    ImGui::Separator();
    
    if (m_selectedModelIndex < 0 || m_selectedModelIndex >= static_cast<int>(m_models.size())) {
        ImGui::TextDisabled("Select a model");
        return;
    }
    
    const auto& model = m_models[m_selectedModelIndex];
    
    ImGui::Text("File: %s", std::filesystem::path(model.filePath).filename().string().c_str());
    ImGui::Text("Meshes: %zu", model.loadResult.meshes.size());
    ImGui::Text("Bones: %zu", model.loadResult.skeleton.bones.size());
    ImGui::Text("Sockets: %zu", model.loadResult.extractedSockets.size());
    ImGui::Text("Materials: %zu", model.loadResult.materials.size());
    
    // Selected bone info
    if (m_selectedBoneIndex >= 0 && m_selectedBoneIndex < static_cast<int>(model.loadResult.skeleton.bones.size())) {
        ImGui::Separator();
        const auto& bone = model.loadResult.skeleton.bones[m_selectedBoneIndex];
        ImGui::Text("Bone: %s", bone.name.c_str());
        ImGui::Text("Position: %.2f, %.2f, %.2f", 
                   bone.localTransform.position.x,
                   bone.localTransform.position.y,
                   bone.localTransform.position.z);
        ImGui::Text("Parent ID: %u", bone.parentID);
        ImGui::Text("Children: %zu", bone.childIDs.size());
    }
    
    // Selected socket info
    if (m_selectedSocketIndex >= 0 && m_selectedSocketIndex < static_cast<int>(model.loadResult.extractedSockets.size())) {
        ImGui::Separator();
        const auto& socket = model.loadResult.extractedSockets[m_selectedSocketIndex];
        ImGui::Text("Socket: %s", socket.name.c_str());
        ImGui::Text("Bone: %s", socket.boneName.c_str());
        ImGui::Text("Category: %s", socket.profile.category.c_str());
        ImGui::Text("Profile: %s", socket.profile.profileID.c_str());
    }
}

void CharacterEditorUI::renderShapeKeyPanel() {
    ImGui::Text("Shape Keys");
    ImGui::Separator();
    
    if (m_selectedModelIndex < 0 || m_selectedModelIndex >= static_cast<int>(m_models.size())) {
        ImGui::TextDisabled("Select a model");
        return;
    }
    
    const auto& model = m_models[m_selectedModelIndex];
    
    bool hasShapeKeys = false;
    for (const auto& mesh : model.loadResult.meshes) {
        if (!mesh.shapeKeys.empty()) {
            hasShapeKeys = true;
            break;
        }
    }
    
    if (!hasShapeKeys) {
        ImGui::TextDisabled("No shape keys");
        return;
    }
    
    for (auto& mesh : m_models[m_selectedModelIndex].loadResult.meshes) {
        if (mesh.shapeKeys.empty()) continue;
        
        if (ImGui::TreeNode(mesh.name.empty() ? "Mesh" : mesh.name.c_str())) {
            for (auto& key : mesh.shapeKeys) {
                ImGui::PushID(key.name.c_str());
                ImGui::SliderFloat(key.name.c_str(), &key.weight, key.minWeight, key.maxWeight);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }
}

void CharacterEditorUI::renderSocketPanel() {
    ImGui::Text("Socket Testing");
    ImGui::Separator();
    
    if (m_selectedModelIndex < 0 || m_selectedModelIndex >= static_cast<int>(m_models.size())) {
        ImGui::TextDisabled("Select a model");
        return;
    }
    
    const auto& model = m_models[m_selectedModelIndex];
    
    if (model.loadResult.extractedSockets.empty()) {
        ImGui::TextDisabled("No sockets");
        return;
    }
    
    ImGui::Text("Available Sockets:");
    for (const auto& socket : model.loadResult.extractedSockets) {
        ImGui::BulletText("%s [%s/%s]", 
                         socket.name.c_str(),
                         socket.profile.category.c_str(),
                         socket.profile.profileID.c_str());
    }
    
    // TODO: Add socket attachment testing UI
}

void CharacterEditorUI::renderIKPanel() {
    ImGui::Text("IK Testing");
    ImGui::Separator();
    
    ImGui::Checkbox("Enable IK", &m_ikEnabled);
    
    if (!m_ikEnabled) {
        ImGui::TextDisabled("IK disabled");
        return;
    }
    
    // TODO: Implement IK testing UI
    ImGui::TextDisabled("IK testing not yet implemented");
    
    if (ImGui::Button("Add IK Target")) {
        IKTestTarget target;
        target.targetPosition = m_camera.target;
        m_ikTargets.push_back(target);
    }
    
    for (size_t i = 0; i < m_ikTargets.size(); ++i) {
        auto& target = m_ikTargets[i];
        ImGui::PushID(static_cast<int>(i));
        
        if (ImGui::TreeNode("Target")) {
            ImGui::DragFloat3("Position", glm::value_ptr(target.targetPosition), 0.01f);
            ImGui::Checkbox("Active", &target.isActive);
            
            if (ImGui::Button("Remove")) {
                m_ikTargets.erase(m_ikTargets.begin() + i);
                --i;
            }
            
            ImGui::TreePop();
        }
        
        ImGui::PopID();
    }
}

void CharacterEditorUI::renderDebugPanel() {
    ImGui::Text("Debug Options");
    ImGui::Separator();
    
    ImGui::ColorEdit3("Clear Color", glm::value_ptr(m_clearColor));
    ImGui::ColorEdit4("Bone Color", glm::value_ptr(m_boneColor));
    ImGui::ColorEdit4("Socket Color", glm::value_ptr(m_socketColor));
    
    ImGui::Separator();
    ImGui::Text("Camera");
    ImGui::DragFloat3("Target", glm::value_ptr(m_camera.target), 0.1f);
    ImGui::DragFloat("Distance", &m_camera.distance, 0.1f, 0.1f, 100.0f);
    ImGui::DragFloat("Yaw", &m_camera.yaw, 1.0f);
    ImGui::DragFloat("Pitch", &m_camera.pitch, 1.0f, -89.0f, 89.0f);
    ImGui::DragFloat("FOV", &m_camera.fov, 1.0f, 10.0f, 120.0f);
}

void CharacterEditorUI::handleViewportInput() {
    ImGuiIO& io = ImGui::GetIO();
    
    // Mouse position relative to viewport
    ImVec2 mousePos = io.MousePos;
    ImVec2 mouseDelta = io.MouseDelta;
    
    // Orbit with middle mouse button or Alt+Left
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
        (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Left))) {
        m_camera.orbit(-mouseDelta.x * 0.5f, -mouseDelta.y * 0.5f);
    }
    
    // Pan with Shift+Middle or Alt+Middle
    if ((io.KeyShift && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) ||
        (io.KeyAlt && ImGui::IsMouseDragging(ImGuiMouseButton_Middle))) {
        m_camera.pan(mouseDelta.x, mouseDelta.y);
    }
    
    // Zoom with scroll wheel
    if (io.MouseWheel != 0.0f) {
        m_camera.zoom(io.MouseWheel);
    }
    
    // Right-click panning
    if (ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        m_camera.pan(mouseDelta.x, mouseDelta.y);
    }
}

} // namespace CharacterEditor
