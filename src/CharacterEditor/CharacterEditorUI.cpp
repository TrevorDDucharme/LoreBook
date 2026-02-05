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

// Skeletal animation
uniform bool uEnableSkinning;
uniform mat4 uBoneMatrices[128];  // Max 128 bones

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vTexCoord;

void main() {
    vec3 skinnedPos = aPosition;
    vec3 skinnedNormal = aNormal;
    
    if (uEnableSkinning) {
        // Apply skeletal animation
        mat4 boneTransform = mat4(0.0);
        float totalWeight = 0.0;
        
        for (int i = 0; i < 4; ++i) {
            if (aBoneIDs[i] >= 0 && aBoneWeights[i] > 0.0) {
                boneTransform += uBoneMatrices[aBoneIDs[i]] * aBoneWeights[i];
                totalWeight += aBoneWeights[i];
            }
        }
        
        if (totalWeight > 0.0) {
            boneTransform /= totalWeight;
            skinnedPos = (boneTransform * vec4(aPosition, 1.0)).xyz;
            skinnedNormal = mat3(boneTransform) * aNormal;
        }
    }
    
    vec4 worldPos = uModel * vec4(skinnedPos, 1.0);
    vWorldPos = worldPos.xyz;
    vNormal = normalize(uNormalMatrix * skinnedNormal);
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
    pitch = glm::clamp(pitch - deltaPitch, -89.0f, 89.0f);
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
    
    // Release attached parts GPU data
    for (auto& model : m_attachedPartModels) {
        for (auto& gpuData : model.meshGPUData) {
            gpuData.release();
        }
    }
    m_attachedPartModels.clear();
    
    // Release preview part GPU data
    m_previewPartGPU.release();
    
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
    
    // Get vertices (with shape keys applied if any)
    std::vector<Vertex> uploadVertices = mesh.getDeformedVertices();
    
    // Upload vertex data - use DYNAMIC_DRAW for shape key updates
    glBindBuffer(GL_ARRAY_BUFFER, gpuData.vbo);
    glBufferData(GL_ARRAY_BUFFER, uploadVertices.size() * sizeof(Vertex), uploadVertices.data(), GL_DYNAMIC_DRAW);
    
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

void CharacterEditorUI::updateMeshVertices(const Mesh& mesh, MeshGPUData& gpuData) {
    if (!gpuData.isValid || gpuData.vbo == 0) return;
    
    // Get deformed vertices with shape keys applied
    std::vector<Vertex> deformedVertices = mesh.getDeformedVertices();
    
    // Update the VBO with new vertex data
    glBindBuffer(GL_ARRAY_BUFFER, gpuData.vbo);
    glBufferSubData(GL_ARRAY_BUFFER, 0, deformedVertices.size() * sizeof(Vertex), deformedVertices.data());
    glBindBuffer(GL_ARRAY_BUFFER, 0);
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
    
    // Initialize combined skeleton in part library with the base model's skeleton
    if (m_partLibrary && !m_models.back().loadResult.skeleton.empty()) {
        m_partLibrary->rebuildCombinedSkeleton(m_models.back().loadResult.skeleton);
        PLOGI << "Initialized combined skeleton with " << m_models.back().loadResult.skeleton.bones.size() << " bones";
    }
    
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
    
    for (auto& model : m_attachedPartModels) {
        for (auto& gpuData : model.meshGPUData) {
            gpuData.release();
        }
    }
    m_attachedPartModels.clear();
    
    // Clear preview part GPU data
    for (auto& gpuData : m_previewPartGPUs) {
        gpuData.release();
    }
    m_previewPartGPUs.clear();
    m_previewPart.reset();
    
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
        
        // Handle drag-drop (needs to be called early for proper cursor handling)
        handlePartDragDrop();
        
        // Main content area with panels
        float panelWidth = 250.0f;
        
        // Left panel - Hierarchy + Parts Library
        ImGui::BeginChild("LeftPanel", ImVec2(panelWidth, 0), true);
        
        if (ImGui::BeginTabBar("LeftTabs")) {
            if (ImGui::BeginTabItem("Hierarchy")) {
                renderHierarchyPanel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Parts")) {
                renderPartsLibraryPanel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Prefabs")) {
                renderPrefabsPanel();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Characters")) {
                renderCharactersPanel();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        
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
    
    glClearColor(m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a);
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
    float aspectRatio = m_viewportSize.x / m_viewportSize.y;
    glm::mat4 view = m_camera.getViewMatrix();
    glm::mat4 projection = m_camera.getProjectionMatrix(aspectRatio);
    
    // Render meshes first
    glUseProgram(m_meshShader);
    
    glUniformMatrix4fv(glGetUniformLocation(m_meshShader, "uView"), 1, GL_FALSE, glm::value_ptr(view));
    glUniformMatrix4fv(glGetUniformLocation(m_meshShader, "uProjection"), 1, GL_FALSE, glm::value_ptr(projection));
    glUniform3fv(glGetUniformLocation(m_meshShader, "uCameraPos"), 1, glm::value_ptr(m_camera.getPosition()));
    glUniform3f(glGetUniformLocation(m_meshShader, "uLightDir"), 0.3f, -1.0f, 0.5f);
    glUniform1i(glGetUniformLocation(m_meshShader, "uShadingMode"), static_cast<int>(m_shadingMode));
    
    // Render all models (base + attached parts)
    auto renderModelList = [&](const std::vector<LoadedModel>& models, bool useBaseModel = true) {
        for (size_t mi = 0; mi < models.size(); ++mi) {
            const auto& model = models[mi];
            glm::mat4 modelMatrix = model.worldTransform.toMatrix();
        
        // Determine which skeleton to use for skinning
        // For base model: use posed/solved skeleton if available, otherwise model skeleton
        // For attached parts: ALWAYS use combined skeleton from part library if available
        const Skeleton* skeletonForSkinning = nullptr;
        
        if (useBaseModel) {
            // Base model - use posed or solved skeleton
            if (m_ikEnabled && !m_solvedSkeleton.empty()) {
                skeletonForSkinning = &m_solvedSkeleton;
            } else if (!m_posedSkeleton.empty()) {
                skeletonForSkinning = &m_posedSkeleton;
            } else {
                skeletonForSkinning = &model.loadResult.skeleton;
            }
        } else {
            // Attached parts - use combined skeleton from part library
            if (m_partLibrary && !m_partLibrary->getCombinedSkeleton().skeleton.empty()) {
                skeletonForSkinning = &m_partLibrary->getCombinedSkeleton().skeleton;
            } else {
                skeletonForSkinning = &model.loadResult.skeleton;
            }
        }
        
        // Compute and upload bone matrices for GPU skinning
        bool enableSkinning = skeletonForSkinning && !skeletonForSkinning->empty();
        glUniform1i(glGetUniformLocation(m_meshShader, "uEnableSkinning"), enableSkinning ? 1 : 0);
        
        if (enableSkinning) {
            std::vector<glm::mat4> boneMatrices(std::min(static_cast<size_t>(128), skeletonForSkinning->bones.size()));
            
            for (size_t bi = 0; bi < boneMatrices.size(); ++bi) {
                const Bone& bone = skeletonForSkinning->bones[bi];
                // Skinning matrix = worldTransform * inverseBindMatrix
                glm::mat4 worldMat = skeletonForSkinning->getWorldTransform(static_cast<uint32_t>(bi)).toMatrix();
                glm::mat4 invBindMat = bone.inverseBindMatrix.toMatrix();
                boneMatrices[bi] = worldMat * invBindMat;
            }
            
            GLint loc = glGetUniformLocation(m_meshShader, "uBoneMatrices");
            glUniformMatrix4fv(loc, static_cast<GLsizei>(boneMatrices.size()), GL_FALSE, 
                               glm::value_ptr(boneMatrices[0]));
        }
        
        for (size_t i = 0; i < model.meshGPUData.size() && i < model.loadResult.meshes.size(); ++i) {
            const auto& gpuData = model.meshGPUData[i];
            const auto& mesh = model.loadResult.meshes[i];
            
            if (gpuData.isValid) {
                renderMesh(mesh, gpuData, modelMatrix);
            }
        }
    }
    };
    
    // Render base models
    renderModelList(m_models, true);
    
    // Render attached parts (using combined skeleton)
    renderModelList(m_attachedPartModels, false);
    
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
    
    // Highlight selected mesh with wireframe outline
    if (m_selectedMeshIndex >= 0 && m_selectedModelIndex >= 0 && 
        m_selectedModelIndex < static_cast<int>(m_models.size())) {
        const auto& model = m_models[m_selectedModelIndex];
        if (m_selectedMeshIndex < static_cast<int>(model.meshGPUData.size())) {
            const auto& gpuData = model.meshGPUData[m_selectedMeshIndex];
            if (gpuData.isValid) {
                glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
                glDisable(GL_DEPTH_TEST);  // Draw on top
                glLineWidth(2.0f);
                
                // Highlight color (yellow)
                glUniform4f(glGetUniformLocation(m_meshShader, "uBaseColor"), 1.0f, 1.0f, 0.0f, 1.0f);
                
                glm::mat4 modelMatrix = model.worldTransform.toMatrix();
                glUniformMatrix4fv(glGetUniformLocation(m_meshShader, "uModel"), 1, GL_FALSE, glm::value_ptr(modelMatrix));
                
                glBindVertexArray(gpuData.vao);
                glDrawElements(GL_TRIANGLES, gpuData.indexCount, GL_UNSIGNED_INT, nullptr);
                glBindVertexArray(0);
                
                glEnable(GL_DEPTH_TEST);
                glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
                glLineWidth(1.0f);
            }
        }
    }
    
    // Render preview part (offset to the side for visibility)
    if (m_previewPart && !m_previewPartGPUs.empty()) {
        glm::mat4 previewModel = glm::translate(glm::mat4(1.0f), glm::vec3(3.0f, 0.0f, 0.0f));
        glUniformMatrix4fv(glGetUniformLocation(m_meshShader, "uModel"), 1, GL_FALSE, glm::value_ptr(previewModel));
        glm::mat3 normalMatrix = glm::transpose(glm::inverse(glm::mat3(previewModel)));
        glUniformMatrix3fv(glGetUniformLocation(m_meshShader, "uNormalMatrix"), 1, GL_FALSE, glm::value_ptr(normalMatrix));
        glUniform4f(glGetUniformLocation(m_meshShader, "uBaseColor"), 0.3f, 0.7f, 1.0f, 1.0f);
        glUniform1i(glGetUniformLocation(m_meshShader, "uEnableSkinning"), 0);
        
        // Render all meshes in the preview part
        for (const auto& gpuData : m_previewPartGPUs) {
            if (gpuData.isValid) {
                glBindVertexArray(gpuData.vao);
                glDrawElements(GL_TRIANGLES, gpuData.indexCount, GL_UNSIGNED_INT, nullptr);
                glBindVertexArray(0);
            }
        }
    }
    
    // Render grid AFTER meshes, with depth write disabled so it doesn't occlude
    if (m_showGrid) {
        renderGrid();
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
        
        // Determine which skeleton to use for rendering
        const Skeleton& skeletonToRender = 
            (m_ikEnabled && !m_solvedSkeleton.empty()) ? m_solvedSkeleton :
            (!m_posedSkeleton.empty()) ? m_posedSkeleton :
            model.loadResult.skeleton;
        
        if (hasFlag(m_debugViews, DebugView::Bones)) {
            renderBones(skeletonToRender, modelMatrix);
        }
        
        if (hasFlag(m_debugViews, DebugView::Sockets)) {
            renderSockets(model.loadResult.extractedSockets, skeletonToRender, modelMatrix);
        }
        
        // Render IK targets
        if (m_ikEnabled && hasFlag(m_debugViews, DebugView::IKChains)) {
            renderIKTargets(modelMatrix);
        }
        
        // Render rotation gizmo for selected bone (when not in IK mode)
        if (!m_ikEnabled && m_selectedBoneIndex >= 0 && 
            m_selectedBoneIndex < static_cast<int>(skeletonToRender.bones.size())) {
            Transform boneWorld = skeletonToRender.getWorldTransform(static_cast<uint32_t>(m_selectedBoneIndex));
            renderRotationGizmo(boneWorld.position, boneWorld.rotation, 0.3f);
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
    
    // Pre-compute world transforms for all bones
    std::vector<glm::vec3> worldPositions(skeleton.bones.size());
    
    // Log first few bones for debugging
    static bool loggedOnce = false;
    bool shouldLog = !loggedOnce;
    if (shouldLog) {
        PLOGI << "=== Bone World Transform Debug (first 10 bones) ===";
    }
    
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        Transform worldTransform = skeleton.getWorldTransform(static_cast<uint32_t>(i));
        worldPositions[i] = worldTransform.position;
        
        if (shouldLog && i < 10) {
            const auto& bone = skeleton.bones[i];
            PLOGI << "Bone[" << i << "] '" << bone.name << "': "
                  << "parent=" << (bone.parentID == UINT32_MAX ? -1 : (int)bone.parentID)
                  << ", local=(" << bone.localTransform.position.x 
                  << ", " << bone.localTransform.position.y 
                  << ", " << bone.localTransform.position.z << ")"
                  << ", world=(" << worldPositions[i].x 
                  << ", " << worldPositions[i].y 
                  << ", " << worldPositions[i].z << ")";
        }
    }
    
    if (shouldLog) {
        loggedOnce = true;
    }
    
    // Draw bone connections
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        const auto& bone = skeleton.bones[i];
        glm::vec3 bonePos = worldPositions[i];
        
        // Draw octahedron representation
        float boneSize = 0.05f;
        glm::vec4 color = (static_cast<int>(i) == m_selectedBoneIndex) ? m_selectedColor : m_boneColor;
        
        // Simple cross at bone position
        addLine(bonePos - glm::vec3(boneSize, 0, 0), bonePos + glm::vec3(boneSize, 0, 0), color);
        addLine(bonePos - glm::vec3(0, boneSize, 0), bonePos + glm::vec3(0, boneSize, 0), color);
        addLine(bonePos - glm::vec3(0, 0, boneSize), bonePos + glm::vec3(0, 0, boneSize), color);
        
        // Draw connection to parent
        if (bone.parentID != UINT32_MAX && bone.parentID < skeleton.bones.size()) {
            glm::vec3 parentPos = worldPositions[bone.parentID];
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

void CharacterEditorUI::renderSockets(const std::vector<Socket>& sockets, const Skeleton& skeleton, const glm::mat4& modelMatrix) {
    std::vector<float> lineData;
    
    auto addLine = [&](const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& color) {
        lineData.push_back(p1.x); lineData.push_back(p1.y); lineData.push_back(p1.z);
        lineData.push_back(color.r); lineData.push_back(color.g); lineData.push_back(color.b); lineData.push_back(color.a);
        lineData.push_back(p2.x); lineData.push_back(p2.y); lineData.push_back(p2.z);
        lineData.push_back(color.r); lineData.push_back(color.g); lineData.push_back(color.b); lineData.push_back(color.a);
    };
    
    for (size_t i = 0; i < sockets.size(); ++i) {
        const auto& socket = sockets[i];
        
        // Find the bone this socket is attached to and compute world position
        glm::vec3 pos = socket.localOffset.position;
        auto it = skeleton.boneNameToIndex.find(socket.boneName);
        if (it != skeleton.boneNameToIndex.end()) {
            Transform worldTransform = skeleton.getWorldTransform(static_cast<uint32_t>(it->second));
            pos = worldTransform.position;
        }
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

void CharacterEditorUI::renderIKTargets(const glm::mat4& modelMatrix) {
    if (m_ikTargets.empty()) return;
    
    std::vector<float> lineData;
    
    auto addLine = [&](const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& color) {
        lineData.push_back(p1.x); lineData.push_back(p1.y); lineData.push_back(p1.z);
        lineData.push_back(color.r); lineData.push_back(color.g); lineData.push_back(color.b); lineData.push_back(color.a);
        lineData.push_back(p2.x); lineData.push_back(p2.y); lineData.push_back(p2.z);
        lineData.push_back(color.r); lineData.push_back(color.g); lineData.push_back(color.b); lineData.push_back(color.a);
    };
    
    for (size_t i = 0; i < m_ikTargets.size(); ++i) {
        const auto& ikTarget = m_ikTargets[i];
        glm::vec3 pos = ikTarget.target.position;
        
        // Color: selected = yellow, active = magenta, inactive = gray
        glm::vec4 color;
        if (static_cast<int>(i) == m_selectedIKTarget) {
            color = m_selectedColor;
        } else if (ikTarget.isActive) {
            color = m_ikTargetColor;
        } else {
            color = glm::vec4(0.5f, 0.5f, 0.5f, 1.0f);
        }
        
        float size = 0.15f;
        
        // Draw crosshair at target position
        addLine(pos - glm::vec3(size, 0, 0), pos + glm::vec3(size, 0, 0), color);
        addLine(pos - glm::vec3(0, size, 0), pos + glm::vec3(0, size, 0), color);
        addLine(pos - glm::vec3(0, 0, size), pos + glm::vec3(0, 0, size), color);
        
        // Draw sphere-like wireframe
        int segments = 8;
        float radius = size * 0.5f;
        for (int j = 0; j < segments; ++j) {
            float a1 = (float)j / segments * 2.0f * 3.14159f;
            float a2 = (float)(j + 1) / segments * 2.0f * 3.14159f;
            
            // XY circle
            addLine(pos + glm::vec3(std::cos(a1) * radius, std::sin(a1) * radius, 0),
                    pos + glm::vec3(std::cos(a2) * radius, std::sin(a2) * radius, 0), color);
            // XZ circle
            addLine(pos + glm::vec3(std::cos(a1) * radius, 0, std::sin(a1) * radius),
                    pos + glm::vec3(std::cos(a2) * radius, 0, std::sin(a2) * radius), color);
            // YZ circle
            addLine(pos + glm::vec3(0, std::cos(a1) * radius, std::sin(a1) * radius),
                    pos + glm::vec3(0, std::cos(a2) * radius, std::sin(a2) * radius), color);
        }
        
        // Draw chain visualization if valid
        if (ikTarget.chain.isValid() && !m_solvedSkeleton.empty()) {
            glm::vec4 chainColor = ikTarget.isActive ? glm::vec4(1.0f, 0.8f, 0.0f, 1.0f) : glm::vec4(0.5f, 0.4f, 0.0f, 1.0f);
            
            for (size_t ci = 0; ci < ikTarget.chain.boneIndices.size() - 1; ++ci) {
                uint32_t boneIdx = ikTarget.chain.boneIndices[ci];
                uint32_t nextIdx = ikTarget.chain.boneIndices[ci + 1];
                
                glm::vec3 p1 = m_solvedSkeleton.getWorldTransform(boneIdx).position;
                glm::vec3 p2 = m_solvedSkeleton.getWorldTransform(nextIdx).position;
                
                // Thicker line effect
                addLine(p1, p2, chainColor);
            }
            
            // Draw line from end effector to target
            if (ikTarget.isActive) {
                uint32_t tipIdx = ikTarget.chain.tipBoneIndex;
                glm::vec3 effectorPos = m_solvedSkeleton.getWorldTransform(tipIdx).position;
                addLine(effectorPos, pos, glm::vec4(1.0f, 0.0f, 0.5f, 0.5f));
            }
        }
    }
    
    if (!lineData.empty()) {
        glBindVertexArray(m_lineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
        glBufferData(GL_ARRAY_BUFFER, lineData.size() * sizeof(float), lineData.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineData.size() / 7));
        glBindVertexArray(0);
    }
    
    // Render translation gizmo for selected target
    if (m_selectedIKTarget >= 0 && m_selectedIKTarget < static_cast<int>(m_ikTargets.size())) {
        renderTranslationGizmo(m_ikTargets[m_selectedIKTarget].target.position, 0.5f);
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
    
    // Disable face culling so grid is visible from both sides
    glDisable(GL_CULL_FACE);
    
    // Disable depth write so grid doesn't occlude meshes
    glDepthMask(GL_FALSE);
    
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    glBindVertexArray(gridVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
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
                    
                    ImGuiTreeNodeFlags meshFlags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                    if (static_cast<int>(mi) == m_selectedModelIndex && static_cast<int>(i) == m_selectedMeshIndex) {
                        meshFlags |= ImGuiTreeNodeFlags_Selected;
                    }
                    
                    std::string meshLabel = mesh.name.empty() ? "Unnamed" : mesh.name;
                    meshLabel += " (" + std::to_string(mesh.vertices.size()) + " verts";
                    if (!mesh.shapeKeys.empty()) {
                        meshLabel += ", " + std::to_string(mesh.shapeKeys.size()) + " keys";
                    }
                    meshLabel += ")";
                    
                    ImGui::TreeNodeEx(meshLabel.c_str(), meshFlags);
                    if (ImGui::IsItemClicked()) {
                        m_selectedModelIndex = static_cast<int>(mi);
                        m_selectedMeshIndex = static_cast<int>(i);
                    }
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
                            
                            // Initialize posed skeleton when selecting a bone
                            if (m_posedSkeleton.empty()) {
                                m_posedSkeleton = model.loadResult.skeleton;
                            }
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
        
        // Pose editing controls
        ImGui::Separator();
        ImGui::Text("Pose Editing");
        
        // Initialize posed skeleton if needed
        if (m_posedSkeleton.empty()) {
            m_posedSkeleton = model.loadResult.skeleton;
        }
        
        if (m_selectedBoneIndex < static_cast<int>(m_posedSkeleton.bones.size())) {
            auto& posedBone = m_posedSkeleton.bones[m_selectedBoneIndex];
            
            // Rotation as Euler angles for easier editing
            glm::vec3 euler = glm::degrees(glm::eulerAngles(posedBone.localTransform.rotation));
            if (ImGui::DragFloat3("Rotation", glm::value_ptr(euler), 1.0f, -180.0f, 180.0f)) {
                posedBone.localTransform.rotation = glm::quat(glm::radians(euler));
                m_bonePoseOverrides[m_selectedBoneIndex] = posedBone.localTransform.rotation;
            }
            
            // Reset this bone button
            if (ImGui::Button("Reset Bone")) {
                posedBone.localTransform.rotation = bone.localTransform.rotation;
                m_bonePoseOverrides.erase(m_selectedBoneIndex);
            }
        }
        
        ImGui::Separator();
        
        // Reset all pose button
        if (!m_bonePoseOverrides.empty()) {
            if (ImGui::Button("Reset All Pose")) {
                m_posedSkeleton = model.loadResult.skeleton;
                m_bonePoseOverrides.clear();
            }
            ImGui::SameLine();
            ImGui::Text("(%zu bones modified)", m_bonePoseOverrides.size());
        }
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
    
    auto& model = m_models[m_selectedModelIndex];
    
    // Count meshes with shape keys
    int meshesWithKeys = 0;
    for (const auto& mesh : model.loadResult.meshes) {
        if (!mesh.shapeKeys.empty()) meshesWithKeys++;
    }
    
    if (meshesWithKeys == 0) {
        ImGui::TextDisabled("No shape keys in model");
        return;
    }
    
    // If a mesh is selected, show its shape keys prominently
    if (m_selectedMeshIndex >= 0 && m_selectedMeshIndex < static_cast<int>(model.loadResult.meshes.size())) {
        auto& mesh = model.loadResult.meshes[m_selectedMeshIndex];
        
        if (!mesh.shapeKeys.empty()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Selected: %s", 
                mesh.name.empty() ? "Unnamed Mesh" : mesh.name.c_str());
            ImGui::Spacing();
            
            // Reset all button
            if (ImGui::Button("Reset All##selected")) {
                for (auto& key : mesh.shapeKeys) {
                    key.weight = 0.0f;
                }
                if (m_selectedMeshIndex < static_cast<int>(model.meshGPUData.size())) {
                    updateMeshVertices(mesh, model.meshGPUData[m_selectedMeshIndex]);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Max All##selected")) {
                for (auto& key : mesh.shapeKeys) {
                    key.weight = key.maxWeight;
                }
                if (m_selectedMeshIndex < static_cast<int>(model.meshGPUData.size())) {
                    updateMeshVertices(mesh, model.meshGPUData[m_selectedMeshIndex]);
                }
            }
            
            ImGui::Separator();
            
            bool anyChanged = false;
            for (auto& key : mesh.shapeKeys) {
                ImGui::PushID(key.name.c_str());
                
                // Slider with input
                ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - 60);
                if (ImGui::SliderFloat("##slider", &key.weight, key.minWeight, key.maxWeight, "%.3f")) {
                    anyChanged = true;
                }
                ImGui::SameLine();
                ImGui::Text("%s", key.name.c_str());
                
                ImGui::PopID();
            }
            
            if (anyChanged && m_selectedMeshIndex < static_cast<int>(model.meshGPUData.size())) {
                updateMeshVertices(mesh, model.meshGPUData[m_selectedMeshIndex]);
            }
        } else {
            ImGui::TextDisabled("Selected mesh has no shape keys");
        }
        
        ImGui::Spacing();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Other Meshes:");
    }
    
    // Show other meshes with shape keys in collapsible sections
    for (size_t mi = 0; mi < model.loadResult.meshes.size(); ++mi) {
        // Skip the selected mesh (already shown above)
        if (static_cast<int>(mi) == m_selectedMeshIndex) continue;
        
        auto& mesh = model.loadResult.meshes[mi];
        if (mesh.shapeKeys.empty()) continue;
        
        std::string header = mesh.name.empty() ? "Mesh " + std::to_string(mi) : mesh.name;
        header += " (" + std::to_string(mesh.shapeKeys.size()) + " keys)";
        
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_None;
        if (ImGui::TreeNodeEx(header.c_str(), flags)) {
            bool anyChanged = false;
            for (auto& key : mesh.shapeKeys) {
                ImGui::PushID((std::to_string(mi) + key.name).c_str());
                if (ImGui::SliderFloat(key.name.c_str(), &key.weight, key.minWeight, key.maxWeight)) {
                    anyChanged = true;
                }
                ImGui::PopID();
            }
            
            if (anyChanged && mi < model.meshGPUData.size()) {
                updateMeshVertices(mesh, model.meshGPUData[mi]);
            }
            
            ImGui::TreePop();
        }
        
        // Click header to select mesh
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right)) {
            m_selectedMeshIndex = static_cast<int>(mi);
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
    ImGui::Text("Live IK Testing");
    ImGui::Separator();
    
    ImGui::Checkbox("Enable IK", &m_ikEnabled);
    
    if (!m_ikEnabled) {
        ImGui::TextDisabled("IK disabled");
        return;
    }
    
    // Get current model's skeleton
    if (m_models.empty() || m_selectedModelIndex < 0 || 
        m_selectedModelIndex >= static_cast<int>(m_models.size())) {
        ImGui::TextDisabled("No model loaded");
        return;
    }
    
    auto& model = m_models[m_selectedModelIndex];
    const Skeleton& skeleton = model.loadResult.skeleton;
    
    if (skeleton.empty()) {
        ImGui::TextDisabled("Model has no skeleton");
        return;
    }
    
    // Copy skeleton for IK modifications if needed
    if (m_solvedSkeleton.bones.size() != skeleton.bones.size()) {
        m_solvedSkeleton = skeleton;
    }
    
    // IK Solver settings
    if (ImGui::CollapsingHeader("Solver Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        static float tolerance = 0.001f;
        static int maxIterations = 10;
        
        if (ImGui::DragFloat("Tolerance", &tolerance, 0.0001f, 0.0001f, 0.1f, "%.4f")) {
            m_ikSolver.setTolerance(tolerance);
        }
        if (ImGui::DragInt("Max Iterations", &maxIterations, 1, 1, 50)) {
            m_ikSolver.setMaxIterations(maxIterations);
        }
    }
    
    ImGui::Separator();
    
    // Add new IK chain
    if (ImGui::Button("Add IK Chain")) {
        IKTestTarget target;
        target.name = "Chain " + std::to_string(m_ikTargets.size() + 1);
        target.target.position = m_camera.target;
        m_ikTargets.push_back(target);
        m_selectedIKTarget = static_cast<int>(m_ikTargets.size()) - 1;
    }
    
    ImGui::Separator();
    
    // IK chains list
    for (size_t i = 0; i < m_ikTargets.size(); ++i) {
        auto& ikTarget = m_ikTargets[i];
        ImGui::PushID(static_cast<int>(i));
        
        bool isSelected = (m_selectedIKTarget == static_cast<int>(i));
        std::string label = ikTarget.name + (ikTarget.chain.isValid() ? " [OK]" : " [Setup]");
        
        if (ImGui::Selectable(label.c_str(), isSelected)) {
            m_selectedIKTarget = static_cast<int>(i);
        }
        
        if (isSelected && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            // Double-click to toggle active
            ikTarget.isActive = !ikTarget.isActive;
        }
        
        ImGui::PopID();
    }
    
    ImGui::Separator();
    
    // Selected chain details
    if (m_selectedIKTarget >= 0 && m_selectedIKTarget < static_cast<int>(m_ikTargets.size())) {
        auto& ikTarget = m_ikTargets[m_selectedIKTarget];
        
        ImGui::Text("Chain Setup");
        
        // Chain name
        char nameBuf[128];
        strncpy(nameBuf, ikTarget.name.c_str(), sizeof(nameBuf) - 1);
        nameBuf[sizeof(nameBuf) - 1] = '\0';
        if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf))) {
            ikTarget.name = nameBuf;
        }
        
        // Start bone selection (combo box)
        if (ImGui::BeginCombo("Start Bone", 
            ikTarget.startBoneIndex >= 0 ? skeleton.bones[ikTarget.startBoneIndex].name.c_str() : "None")) {
            for (size_t bi = 0; bi < skeleton.bones.size(); ++bi) {
                bool selected = (ikTarget.startBoneIndex == static_cast<int>(bi));
                if (ImGui::Selectable(skeleton.bones[bi].name.c_str(), selected)) {
                    ikTarget.startBoneIndex = static_cast<int>(bi);
                    // Rebuild chain if both bones are set
                    if (ikTarget.endBoneIndex >= 0) {
                        ikTarget.chain = FABRIKSolver::buildChain(
                            skeleton, 
                            static_cast<uint32_t>(ikTarget.startBoneIndex),
                            static_cast<uint32_t>(ikTarget.endBoneIndex));
                    }
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        
        // End bone selection (combo box)
        if (ImGui::BeginCombo("End Bone", 
            ikTarget.endBoneIndex >= 0 ? skeleton.bones[ikTarget.endBoneIndex].name.c_str() : "None")) {
            for (size_t bi = 0; bi < skeleton.bones.size(); ++bi) {
                bool selected = (ikTarget.endBoneIndex == static_cast<int>(bi));
                if (ImGui::Selectable(skeleton.bones[bi].name.c_str(), selected)) {
                    ikTarget.endBoneIndex = static_cast<int>(bi);
                    // Rebuild chain if both bones are set
                    if (ikTarget.startBoneIndex >= 0) {
                        ikTarget.chain = FABRIKSolver::buildChain(
                            skeleton, 
                            static_cast<uint32_t>(ikTarget.startBoneIndex),
                            static_cast<uint32_t>(ikTarget.endBoneIndex));
                    }
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        
        // Chain status
        if (ikTarget.chain.isValid()) {
            ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), 
                "Chain valid: %zu bones", ikTarget.chain.length());
        } else if (ikTarget.startBoneIndex >= 0 && ikTarget.endBoneIndex >= 0) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), 
                "Invalid chain (bones not connected)");
        } else {
            ImGui::TextDisabled("Select start and end bones");
        }
        
        ImGui::Separator();
        ImGui::Text("Target");
        
        // Target position
        ImGui::DragFloat3("Position", glm::value_ptr(ikTarget.target.position), 0.01f);
        
        // Quick set target to current end effector position
        if (ikTarget.chain.isValid() && ImGui::Button("Set to End Effector")) {
            Transform endWorld = skeleton.getWorldTransform(ikTarget.chain.tipBoneIndex);
            ikTarget.target.position = endWorld.position;
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Set to Camera Target")) {
            ikTarget.target.position = m_camera.target;
        }
        
        // Blend weight
        ImGui::SliderFloat("Blend Weight", &ikTarget.blendWeight, 0.0f, 1.0f);
        
        // Active toggle
        ImGui::Checkbox("Active", &ikTarget.isActive);
        
        ImGui::Separator();
        
        // Remove button
        if (ImGui::Button("Remove Chain")) {
            m_ikTargets.erase(m_ikTargets.begin() + m_selectedIKTarget);
            m_selectedIKTarget = -1;
        }
    }
    
    // Run IK solver for all active chains
    if (m_ikEnabled) {
        // Reset to original skeleton
        m_solvedSkeleton = skeleton;
        
        for (auto& ikTarget : m_ikTargets) {
            if (ikTarget.isActive && ikTarget.chain.isValid()) {
                IKSolveResult result = m_ikSolver.solve(m_solvedSkeleton, ikTarget.chain, ikTarget.target);
                if (!result.solvedTransforms.empty()) {
                    FABRIKSolver::applyResult(m_solvedSkeleton, ikTarget.chain, result, ikTarget.blendWeight);
                }
            }
        }
    }
}

void CharacterEditorUI::renderDebugPanel() {
    ImGui::Text("Debug Options");
    ImGui::Separator();
    
    ImGui::ColorEdit4("Clear Color", glm::value_ptr(m_clearColor));
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
    glm::vec2 viewportMouse(io.MousePos.x - m_viewportPos.x, io.MousePos.y - m_viewportPos.y);
    
    // Handle gizmo first (if we have a selected IK target)
    if (m_ikEnabled && m_selectedIKTarget >= 0 && m_selectedIKTarget < static_cast<int>(m_ikTargets.size())) {
        handleGizmo();
        if (m_isDraggingGizmo) {
            return;  // Don't process other input while dragging gizmo
        }
    }
    
    // Handle bone gizmo (if we have a selected bone and not in IK mode)
    if (!m_ikEnabled && m_selectedBoneIndex >= 0) {
        handleBoneGizmo();
        if (m_isDraggingGizmo) {
            return;  // Don't process other input while dragging gizmo
        }
    }
    
    // Click to select bone or mesh (when not Alt-clicking for orbit)
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt && !m_isDraggingGizmo) {
        // First try to pick a bone
        int pickedBone = pickBone(viewportMouse);
        if (pickedBone >= 0) {
            m_selectedBoneIndex = pickedBone;
            m_selectedMeshIndex = -1;  // Deselect mesh when selecting bone
            
            // Initialize posed skeleton when first selecting a bone
            if (m_posedSkeleton.empty() && !m_models.empty()) {
                m_posedSkeleton = m_models[0].loadResult.skeleton;
            }
        } else {
            // Try to pick a mesh if no bone was hit
            int pickedMesh = pickMesh(viewportMouse);
            if (pickedMesh >= 0) {
                m_selectedMeshIndex = pickedMesh;
                m_selectedBoneIndex = -1;  // Deselect bone when selecting mesh
            }
        }
    }
    
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
    
    // Press Escape to deselect bone and mesh
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        m_selectedBoneIndex = -1;
        m_selectedMeshIndex = -1;
    }
}

void CharacterEditorUI::handleGizmo() {
    ImGuiIO& io = ImGui::GetIO();
    
    if (m_selectedIKTarget < 0 || m_selectedIKTarget >= static_cast<int>(m_ikTargets.size())) {
        return;
    }
    
    auto& ikTarget = m_ikTargets[m_selectedIKTarget];
    glm::vec3& targetPos = ikTarget.target.position;
    
    // Convert mouse position to viewport-relative coordinates
    glm::vec2 mousePos(io.MousePos.x - m_viewportPos.x, io.MousePos.y - m_viewportPos.y);
    
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt) {
        // Try to pick a gizmo axis
        GizmoAxis axis;
        if (pickGizmoAxis(targetPos, mousePos, axis)) {
            m_isDraggingGizmo = true;
            m_gizmoDragAxis = axis;
            m_gizmoTargetStart = targetPos;
            m_gizmoDragStart = projectMouseOntoPlane(mousePos, targetPos, 
                m_camera.getPosition() - targetPos);
        }
    }
    
    if (m_isDraggingGizmo && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        // Project current mouse position and calculate delta
        glm::vec3 currentDrag = projectMouseOntoPlane(mousePos, m_gizmoTargetStart,
            m_camera.getPosition() - m_gizmoTargetStart);
        glm::vec3 delta = currentDrag - m_gizmoDragStart;
        
        // Constrain to axis
        switch (m_gizmoDragAxis) {
            case GizmoAxis::X:
                delta.y = 0.0f; delta.z = 0.0f;
                break;
            case GizmoAxis::Y:
                delta.x = 0.0f; delta.z = 0.0f;
                break;
            case GizmoAxis::Z:
                delta.x = 0.0f; delta.y = 0.0f;
                break;
            default:
                break;
        }
        
        targetPos = m_gizmoTargetStart + delta;
    }
    
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_isDraggingGizmo = false;
        m_gizmoDragAxis = GizmoAxis::None;
    }
}

bool CharacterEditorUI::pickGizmoAxis(const glm::vec3& gizmoPos, const glm::vec2& mousePos, GizmoAxis& outAxis) {
    float aspectRatio = m_viewportSize.x / m_viewportSize.y;
    glm::mat4 view = m_camera.getViewMatrix();
    glm::mat4 projection = m_camera.getProjectionMatrix(aspectRatio);
    glm::mat4 vp = projection * view;
    
    float gizmoSize = 0.5f;  // World space size
    
    // Project gizmo origin and axis endpoints to screen space
    auto project = [&](const glm::vec3& worldPos) -> glm::vec2 {
        glm::vec4 clip = vp * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0) return glm::vec2(-10000);
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        return glm::vec2(
            (ndc.x * 0.5f + 0.5f) * m_viewportSize.x,
            (1.0f - (ndc.y * 0.5f + 0.5f)) * m_viewportSize.y
        );
    };
    
    glm::vec2 origin = project(gizmoPos);
    glm::vec2 xEnd = project(gizmoPos + glm::vec3(gizmoSize, 0, 0));
    glm::vec2 yEnd = project(gizmoPos + glm::vec3(0, gizmoSize, 0));
    glm::vec2 zEnd = project(gizmoPos + glm::vec3(0, 0, gizmoSize));
    
    // Distance from mouse to each axis line
    auto distToLine = [](const glm::vec2& p, const glm::vec2& a, const glm::vec2& b) -> float {
        glm::vec2 ab = b - a;
        float len = glm::length(ab);
        if (len < 0.001f) return glm::length(p - a);
        glm::vec2 n = ab / len;
        float t = glm::clamp(glm::dot(p - a, n), 0.0f, len);
        return glm::length(p - (a + n * t));
    };
    
    float pickThreshold = 15.0f;  // Pixels
    
    float distX = distToLine(mousePos, origin, xEnd);
    float distY = distToLine(mousePos, origin, yEnd);
    float distZ = distToLine(mousePos, origin, zEnd);
    
    float minDist = std::min({distX, distY, distZ});
    
    if (minDist > pickThreshold) {
        outAxis = GizmoAxis::None;
        return false;
    }
    
    if (distX == minDist) {
        outAxis = GizmoAxis::X;
    } else if (distY == minDist) {
        outAxis = GizmoAxis::Y;
    } else {
        outAxis = GizmoAxis::Z;
    }
    
    return true;
}

glm::vec3 CharacterEditorUI::projectMouseOntoPlane(const glm::vec2& mousePos, 
                                                    const glm::vec3& planeOrigin, 
                                                    const glm::vec3& planeNormal) {
    float aspectRatio = m_viewportSize.x / m_viewportSize.y;
    glm::mat4 view = m_camera.getViewMatrix();
    glm::mat4 projection = m_camera.getProjectionMatrix(aspectRatio);
    glm::mat4 invVP = glm::inverse(projection * view);
    
    // Convert mouse position to NDC
    float ndcX = (mousePos.x / m_viewportSize.x) * 2.0f - 1.0f;
    float ndcY = 1.0f - (mousePos.y / m_viewportSize.y) * 2.0f;
    
    // Create ray from camera through mouse position
    glm::vec4 nearPoint = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farPoint = invVP * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    
    nearPoint /= nearPoint.w;
    farPoint /= farPoint.w;
    
    glm::vec3 rayOrigin = glm::vec3(nearPoint);
    glm::vec3 rayDir = glm::normalize(glm::vec3(farPoint) - rayOrigin);
    
    // Intersect ray with plane
    glm::vec3 n = glm::normalize(planeNormal);
    float denom = glm::dot(n, rayDir);
    
    if (std::abs(denom) < 0.0001f) {
        // Ray parallel to plane - return origin
        return planeOrigin;
    }
    
    float t = glm::dot(planeOrigin - rayOrigin, n) / denom;
    return rayOrigin + rayDir * t;
}

void CharacterEditorUI::renderTranslationGizmo(const glm::vec3& position, float size) {
    std::vector<float> lineData;
    
    auto addLine = [&](const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& color) {
        lineData.push_back(p1.x); lineData.push_back(p1.y); lineData.push_back(p1.z);
        lineData.push_back(color.r); lineData.push_back(color.g); lineData.push_back(color.b); lineData.push_back(color.a);
        lineData.push_back(p2.x); lineData.push_back(p2.y); lineData.push_back(p2.z);
        lineData.push_back(color.r); lineData.push_back(color.g); lineData.push_back(color.b); lineData.push_back(color.a);
    };
    
    // Colors - highlight if being dragged
    glm::vec4 xColor = (m_gizmoDragAxis == GizmoAxis::X) ? glm::vec4(1.0f, 1.0f, 0.0f, 1.0f) : glm::vec4(1.0f, 0.3f, 0.3f, 1.0f);
    glm::vec4 yColor = (m_gizmoDragAxis == GizmoAxis::Y) ? glm::vec4(1.0f, 1.0f, 0.0f, 1.0f) : glm::vec4(0.3f, 1.0f, 0.3f, 1.0f);
    glm::vec4 zColor = (m_gizmoDragAxis == GizmoAxis::Z) ? glm::vec4(1.0f, 1.0f, 0.0f, 1.0f) : glm::vec4(0.3f, 0.3f, 1.0f, 1.0f);
    
    // X axis
    addLine(position, position + glm::vec3(size, 0, 0), xColor);
    // Arrow head
    addLine(position + glm::vec3(size, 0, 0), position + glm::vec3(size * 0.85f, size * 0.1f, 0), xColor);
    addLine(position + glm::vec3(size, 0, 0), position + glm::vec3(size * 0.85f, -size * 0.1f, 0), xColor);
    addLine(position + glm::vec3(size, 0, 0), position + glm::vec3(size * 0.85f, 0, size * 0.1f), xColor);
    addLine(position + glm::vec3(size, 0, 0), position + glm::vec3(size * 0.85f, 0, -size * 0.1f), xColor);
    
    // Y axis
    addLine(position, position + glm::vec3(0, size, 0), yColor);
    // Arrow head
    addLine(position + glm::vec3(0, size, 0), position + glm::vec3(size * 0.1f, size * 0.85f, 0), yColor);
    addLine(position + glm::vec3(0, size, 0), position + glm::vec3(-size * 0.1f, size * 0.85f, 0), yColor);
    addLine(position + glm::vec3(0, size, 0), position + glm::vec3(0, size * 0.85f, size * 0.1f), yColor);
    addLine(position + glm::vec3(0, size, 0), position + glm::vec3(0, size * 0.85f, -size * 0.1f), yColor);
    
    // Z axis
    addLine(position, position + glm::vec3(0, 0, size), zColor);
    // Arrow head
    addLine(position + glm::vec3(0, 0, size), position + glm::vec3(size * 0.1f, 0, size * 0.85f), zColor);
    addLine(position + glm::vec3(0, 0, size), position + glm::vec3(-size * 0.1f, 0, size * 0.85f), zColor);
    addLine(position + glm::vec3(0, 0, size), position + glm::vec3(0, size * 0.1f, size * 0.85f), zColor);
    addLine(position + glm::vec3(0, 0, size), position + glm::vec3(0, -size * 0.1f, size * 0.85f), zColor);
    
    if (!lineData.empty()) {
        glLineWidth(3.0f);
        glBindVertexArray(m_lineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
        glBufferData(GL_ARRAY_BUFFER, lineData.size() * sizeof(float), lineData.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineData.size() / 7));
        glBindVertexArray(0);
        glLineWidth(1.0f);
    }
}

int CharacterEditorUI::pickBone(const glm::vec2& mousePos) {
    if (m_models.empty()) return -1;
    
    const auto& model = m_models[0];
    const Skeleton& skeleton = m_posedSkeleton.empty() ? model.loadResult.skeleton : m_posedSkeleton;
    if (skeleton.empty()) return -1;
    
    float aspectRatio = m_viewportSize.x / m_viewportSize.y;
    glm::mat4 view = m_camera.getViewMatrix();
    glm::mat4 projection = m_camera.getProjectionMatrix(aspectRatio);
    glm::mat4 vp = projection * view;
    
    // Project function
    auto project = [&](const glm::vec3& worldPos) -> glm::vec2 {
        glm::vec4 clip = vp * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0) return glm::vec2(-10000);
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        return glm::vec2(
            (ndc.x * 0.5f + 0.5f) * m_viewportSize.x,
            (1.0f - (ndc.y * 0.5f + 0.5f)) * m_viewportSize.y
        );
    };
    
    float pickRadius = 20.0f;  // Pixels
    float closestDist = pickRadius;
    int closestBone = -1;
    
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        Transform worldTransform = skeleton.getWorldTransform(static_cast<uint32_t>(i));
        glm::vec2 screenPos = project(worldTransform.position);
        
        float dist = glm::length(mousePos - screenPos);
        if (dist < closestDist) {
            closestDist = dist;
            closestBone = static_cast<int>(i);
        }
    }
    
    return closestBone;
}

int CharacterEditorUI::pickMesh(const glm::vec2& mousePos) {
    if (m_models.empty() || m_selectedModelIndex < 0 || 
        m_selectedModelIndex >= static_cast<int>(m_models.size())) {
        return -1;
    }
    
    const auto& model = m_models[m_selectedModelIndex];
    if (model.loadResult.meshes.empty()) return -1;
    
    float aspectRatio = m_viewportSize.x / m_viewportSize.y;
    glm::mat4 view = m_camera.getViewMatrix();
    glm::mat4 projection = m_camera.getProjectionMatrix(aspectRatio);
    glm::mat4 modelMatrix = model.worldTransform.toMatrix();
    
    // Convert mouse position to normalized device coordinates
    float ndcX = (mousePos.x / m_viewportSize.x) * 2.0f - 1.0f;
    float ndcY = 1.0f - (mousePos.y / m_viewportSize.y) * 2.0f;
    
    // Create ray from camera through mouse position
    glm::mat4 invProj = glm::inverse(projection);
    glm::mat4 invView = glm::inverse(view);
    
    glm::vec4 rayClip(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 rayEye = invProj * rayClip;
    rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
    glm::vec3 rayDir = glm::normalize(glm::vec3(invView * rayEye));
    glm::vec3 rayOrigin = m_camera.getPosition();
    
    // Test against each mesh's bounding box
    int closestMesh = -1;
    float closestT = FLT_MAX;
    
    for (size_t i = 0; i < model.loadResult.meshes.size(); ++i) {
        const auto& mesh = model.loadResult.meshes[i];
        
        // Transform bounds to world space
        glm::vec3 minWorld = glm::vec3(modelMatrix * glm::vec4(mesh.boundsMin, 1.0f));
        glm::vec3 maxWorld = glm::vec3(modelMatrix * glm::vec4(mesh.boundsMax, 1.0f));
        
        // Ensure min < max after transform
        glm::vec3 boundsMin = glm::min(minWorld, maxWorld);
        glm::vec3 boundsMax = glm::max(minWorld, maxWorld);
        
        // Ray-AABB intersection (slab method)
        glm::vec3 invDir = 1.0f / rayDir;
        glm::vec3 t1 = (boundsMin - rayOrigin) * invDir;
        glm::vec3 t2 = (boundsMax - rayOrigin) * invDir;
        
        glm::vec3 tMin = glm::min(t1, t2);
        glm::vec3 tMax = glm::max(t1, t2);
        
        float tEnter = glm::max(tMin.x, glm::max(tMin.y, tMin.z));
        float tExit = glm::min(tMax.x, glm::min(tMax.y, tMax.z));
        
        if (tEnter <= tExit && tExit > 0.0f) {
            float t = (tEnter > 0.0f) ? tEnter : tExit;
            if (t < closestT) {
                closestT = t;
                closestMesh = static_cast<int>(i);
            }
        }
    }
    
    return closestMesh;
}

bool CharacterEditorUI::pickRotationGizmoAxis(const glm::vec3& gizmoPos, const glm::vec2& mousePos,
                                                float radius, GizmoAxis& outAxis) {
    float aspectRatio = m_viewportSize.x / m_viewportSize.y;
    glm::mat4 view = m_camera.getViewMatrix();
    glm::mat4 projection = m_camera.getProjectionMatrix(aspectRatio);
    glm::mat4 vp = projection * view;
    
    auto project = [&](const glm::vec3& worldPos) -> glm::vec2 {
        glm::vec4 clip = vp * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0) return glm::vec2(-10000);
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        return glm::vec2(
            (ndc.x * 0.5f + 0.5f) * m_viewportSize.x,
            (1.0f - (ndc.y * 0.5f + 0.5f)) * m_viewportSize.y
        );
    };
    
    // Distance from mouse to each rotation ring
    auto distToRing = [&](const glm::vec3& axis) -> float {
        float minDist = FLT_MAX;
        int segments = 32;
        
        // Get perpendicular vectors
        glm::vec3 up = (std::abs(axis.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        glm::vec3 perp1 = glm::normalize(glm::cross(axis, up));
        glm::vec3 perp2 = glm::cross(axis, perp1);
        
        for (int i = 0; i < segments; ++i) {
            float a1 = (float)i / segments * 2.0f * 3.14159f;
            float a2 = (float)(i + 1) / segments * 2.0f * 3.14159f;
            
            glm::vec3 p1 = gizmoPos + (perp1 * std::cos(a1) + perp2 * std::sin(a1)) * radius;
            glm::vec3 p2 = gizmoPos + (perp1 * std::cos(a2) + perp2 * std::sin(a2)) * radius;
            
            glm::vec2 sp1 = project(p1);
            glm::vec2 sp2 = project(p2);
            
            // Distance from point to line segment
            glm::vec2 ab = sp2 - sp1;
            float len = glm::length(ab);
            if (len < 0.001f) continue;
            glm::vec2 n = ab / len;
            float t = glm::clamp(glm::dot(mousePos - sp1, n), 0.0f, len);
            float d = glm::length(mousePos - (sp1 + n * t));
            minDist = std::min(minDist, d);
        }
        
        return minDist;
    };
    
    float pickThreshold = 12.0f;  // Pixels
    
    float distX = distToRing(glm::vec3(1, 0, 0));
    float distY = distToRing(glm::vec3(0, 1, 0));
    float distZ = distToRing(glm::vec3(0, 0, 1));
    
    float minDist = std::min({distX, distY, distZ});
    
    if (minDist > pickThreshold) {
        outAxis = GizmoAxis::None;
        return false;
    }
    
    if (distX == minDist) {
        outAxis = GizmoAxis::X;
    } else if (distY == minDist) {
        outAxis = GizmoAxis::Y;
    } else {
        outAxis = GizmoAxis::Z;
    }
    
    return true;
}

void CharacterEditorUI::handleBoneGizmo() {
    ImGuiIO& io = ImGui::GetIO();
    
    if (m_selectedBoneIndex < 0 || m_models.empty()) return;
    
    const auto& model = m_models[0];
    Skeleton& skeleton = m_posedSkeleton.empty() ? m_posedSkeleton : m_posedSkeleton;
    
    // Initialize posed skeleton if needed
    if (m_posedSkeleton.empty() && !model.loadResult.skeleton.empty()) {
        m_posedSkeleton = model.loadResult.skeleton;
    }
    
    if (m_selectedBoneIndex >= static_cast<int>(m_posedSkeleton.bones.size())) return;
    
    glm::vec2 mousePos(io.MousePos.x - m_viewportPos.x, io.MousePos.y - m_viewportPos.y);
    Transform boneWorld = m_posedSkeleton.getWorldTransform(static_cast<uint32_t>(m_selectedBoneIndex));
    
    float gizmoRadius = 0.3f;
    
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !io.KeyAlt) {
        // Try to pick a rotation axis
        GizmoAxis axis;
        if (pickRotationGizmoAxis(boneWorld.position, mousePos, gizmoRadius, axis)) {
            m_isDraggingGizmo = true;
            m_gizmoDragAxis = axis;
            m_gizmoRotationStart = m_posedSkeleton.bones[m_selectedBoneIndex].localTransform.rotation;
            m_gizmoDragStart = projectMouseOntoPlane(mousePos, boneWorld.position,
                m_camera.getPosition() - boneWorld.position);
        }
    }
    
    if (m_isDraggingGizmo && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        glm::vec3 currentDrag = projectMouseOntoPlane(mousePos, boneWorld.position,
            m_camera.getPosition() - boneWorld.position);
        
        // Calculate rotation based on movement around the gizmo center
        glm::vec3 startDir = glm::normalize(m_gizmoDragStart - boneWorld.position);
        glm::vec3 currentDir = glm::normalize(currentDrag - boneWorld.position);
        
        // Determine rotation axis in world space
        glm::vec3 rotAxis;
        switch (m_gizmoDragAxis) {
            case GizmoAxis::X: rotAxis = glm::vec3(1, 0, 0); break;
            case GizmoAxis::Y: rotAxis = glm::vec3(0, 1, 0); break;
            case GizmoAxis::Z: rotAxis = glm::vec3(0, 0, 1); break;
            default: return;
        }
        
        // Project directions onto rotation plane
        glm::vec3 startProj = startDir - rotAxis * glm::dot(startDir, rotAxis);
        glm::vec3 currentProj = currentDir - rotAxis * glm::dot(currentDir, rotAxis);
        
        if (glm::length(startProj) > 0.001f && glm::length(currentProj) > 0.001f) {
            startProj = glm::normalize(startProj);
            currentProj = glm::normalize(currentProj);
            
            float angle = std::acos(glm::clamp(glm::dot(startProj, currentProj), -1.0f, 1.0f));
            float sign = glm::dot(rotAxis, glm::cross(startProj, currentProj)) > 0 ? 1.0f : -1.0f;
            angle *= sign;
            
            // Convert to local space rotation
            // Get parent's world rotation to transform the axis
            glm::quat parentWorldRot(1, 0, 0, 0);
            uint32_t parentID = m_posedSkeleton.bones[m_selectedBoneIndex].parentID;
            if (parentID != UINT32_MAX && parentID < m_posedSkeleton.bones.size()) {
                parentWorldRot = m_posedSkeleton.getWorldTransform(parentID).rotation;
            }
            
            // Transform rotation axis to local space
            glm::vec3 localAxis = glm::inverse(parentWorldRot) * rotAxis;
            
            // Create delta rotation and apply
            glm::quat deltaRot = glm::angleAxis(angle, localAxis);
            m_posedSkeleton.bones[m_selectedBoneIndex].localTransform.rotation = 
                glm::normalize(deltaRot * m_gizmoRotationStart);
            
            // Store the pose override
            m_bonePoseOverrides[m_selectedBoneIndex] = 
                m_posedSkeleton.bones[m_selectedBoneIndex].localTransform.rotation;
        }
    }
    
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        m_isDraggingGizmo = false;
        m_gizmoDragAxis = GizmoAxis::None;
    }
}

void CharacterEditorUI::renderRotationGizmo(const glm::vec3& position, 
                                             const glm::quat& rotation, float radius) {
    std::vector<float> lineData;
    
    auto addLine = [&](const glm::vec3& p1, const glm::vec3& p2, const glm::vec4& color) {
        lineData.push_back(p1.x); lineData.push_back(p1.y); lineData.push_back(p1.z);
        lineData.push_back(color.r); lineData.push_back(color.g); lineData.push_back(color.b); lineData.push_back(color.a);
        lineData.push_back(p2.x); lineData.push_back(p2.y); lineData.push_back(p2.z);
        lineData.push_back(color.r); lineData.push_back(color.g); lineData.push_back(color.b); lineData.push_back(color.a);
    };
    
    // Draw a ring for each axis
    auto drawRing = [&](const glm::vec3& axis, const glm::vec4& color) {
        int segments = 32;
        glm::vec3 up = (std::abs(axis.y) < 0.99f) ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        glm::vec3 perp1 = glm::normalize(glm::cross(axis, up));
        glm::vec3 perp2 = glm::cross(axis, perp1);
        
        for (int i = 0; i < segments; ++i) {
            float a1 = (float)i / segments * 2.0f * 3.14159f;
            float a2 = (float)(i + 1) / segments * 2.0f * 3.14159f;
            
            glm::vec3 p1 = position + (perp1 * std::cos(a1) + perp2 * std::sin(a1)) * radius;
            glm::vec3 p2 = position + (perp1 * std::cos(a2) + perp2 * std::sin(a2)) * radius;
            
            addLine(p1, p2, color);
        }
    };
    
    // Colors - highlight if being dragged
    glm::vec4 xColor = (m_gizmoDragAxis == GizmoAxis::X) ? glm::vec4(1.0f, 1.0f, 0.0f, 1.0f) : glm::vec4(1.0f, 0.3f, 0.3f, 1.0f);
    glm::vec4 yColor = (m_gizmoDragAxis == GizmoAxis::Y) ? glm::vec4(1.0f, 1.0f, 0.0f, 1.0f) : glm::vec4(0.3f, 1.0f, 0.3f, 1.0f);
    glm::vec4 zColor = (m_gizmoDragAxis == GizmoAxis::Z) ? glm::vec4(1.0f, 1.0f, 0.0f, 1.0f) : glm::vec4(0.3f, 0.3f, 1.0f, 1.0f);
    
    drawRing(glm::vec3(1, 0, 0), xColor);  // X - rotate around X axis (YZ plane)
    drawRing(glm::vec3(0, 1, 0), yColor);  // Y - rotate around Y axis (XZ plane)
    drawRing(glm::vec3(0, 0, 1), zColor);  // Z - rotate around Z axis (XY plane)
    
    if (!lineData.empty()) {
        glLineWidth(2.0f);
        glBindVertexArray(m_lineVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
        glBufferData(GL_ARRAY_BUFFER, lineData.size() * sizeof(float), lineData.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineData.size() / 7));
        glBindVertexArray(0);
        glLineWidth(1.0f);
    }
}

// ============================================================
// Parts Library Methods
// ============================================================

void CharacterEditorUI::setPartLibrary(PartLibrary* library) {
    m_partLibrary = library;
    if (m_partLibrary) {
        refreshPartsList();
    }
}

void CharacterEditorUI::refreshPartsList() {
    m_partSummaries.clear();
    m_partCategories.clear();
    m_selectedPartSummaryIndex = -1;
    
    if (!m_partLibrary) return;
    
    // Get all part summaries
    m_partSummaries = m_partLibrary->getAllPartSummaries();
    m_partCategories = m_partLibrary->getAllCategories();
}

// ============================================================
// Character Manager Methods
// ============================================================

void CharacterEditorUI::setCharacterManager(CharacterManager* manager) {
    m_characterManager = manager;
    if (m_characterManager) {
        refreshPrefabsList();
        refreshCharactersList();
    }
}

void CharacterEditorUI::refreshPrefabsList() {
    m_prefabSummaries.clear();
    m_selectedPrefabIndex = -1;
    
    if (!m_characterManager) return;
    m_prefabSummaries = m_characterManager->getAllPrefabSummaries();
}

void CharacterEditorUI::refreshCharactersList() {
    m_characterSummaries.clear();
    m_selectedCharacterIndex = -1;
    
    if (!m_characterManager) return;
    m_characterSummaries = m_characterManager->getAllCharacterSummaries();
}

void CharacterEditorUI::startPartDrag(int64_t partDbID) {
    if (!m_partLibrary) return;
    
    m_isDraggingPart = true;
    m_draggingPartDbID = partDbID;
    m_draggingPart = m_partLibrary->loadPart(partDbID);
    m_dragStartPos = glm::vec2(ImGui::GetIO().MousePos.x, ImGui::GetIO().MousePos.y);
    m_highlightedSocket = nullptr;
    
    if (m_draggingPart) {
        PLOGI << "Started dragging part: " << m_draggingPart->name;
    }
}

void CharacterEditorUI::cancelPartDrag() {
    m_isDraggingPart = false;
    m_draggingPartDbID = -1;
    m_draggingPart.reset();
    m_highlightedSocket = nullptr;
}

void CharacterEditorUI::renderPartsLibraryPanel() {
    ImGui::Text("Parts Library");
    ImGui::Separator();
    
    if (!m_partLibrary) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "No vault connected");
        ImGui::TextWrapped("Connect to a vault to access the parts library.");
        return;
    }
    
    // Search bar
    static char searchBuf[256] = "";
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##PartsSearch", "Search parts...", searchBuf, sizeof(searchBuf))) {
        m_partsSearchQuery = searchBuf;
    }
    
    // Category filter
    if (!m_partCategories.empty()) {
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##Category", m_partsSelectedCategory.empty() ? "All Categories" : m_partsSelectedCategory.c_str())) {
            if (ImGui::Selectable("All Categories", m_partsSelectedCategory.empty())) {
                m_partsSelectedCategory.clear();
            }
            for (const auto& cat : m_partCategories) {
                if (ImGui::Selectable(cat.c_str(), m_partsSelectedCategory == cat)) {
                    m_partsSelectedCategory = cat;
                }
            }
            ImGui::EndCombo();
        }
    }
    
    // Import button
    if (ImGui::Button("Import Part...", ImVec2(-1, 0))) {
        m_showImportPartModal = true;
        m_partImportError.clear();
        // Initialize browser path to current directory if not set
        if (m_importBrowserPath.empty()) {
            m_importBrowserPath = std::filesystem::current_path();
        }
        ImGui::OpenPopup("Import Part File");
    }
    
    // Import Part File browser modal
    if (ImGui::BeginPopupModal("Import Part File", &m_showImportPartModal, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Select a model file to import:");
        ImGui::Separator();
        
        // Current path display
        ImGui::TextWrapped("%s", m_importBrowserPath.string().c_str());
        ImGui::SameLine();
        if (ImGui::Button("Up##import")) {
            if (m_importBrowserPath.has_parent_path()) {
                m_importBrowserPath = m_importBrowserPath.parent_path();
            }
        }
        ImGui::Separator();
        
        // File listing
        ImGui::BeginChild("ImportFileList", ImVec2(400, 300), true);
        try {
            std::vector<std::filesystem::directory_entry> dirs, files;
            for (auto& e : std::filesystem::directory_iterator(m_importBrowserPath)) {
                if (e.is_directory()) {
                    dirs.push_back(e);
                } else if (e.is_regular_file()) {
                    // Filter by extension
                    auto ext = e.path().extension().string();
                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                    if (ext == ".fbx" || ext == ".glb" || ext == ".gltf" || ext == ".obj") {
                        files.push_back(e);
                    }
                }
            }
            
            // Sort alphabetically
            std::sort(dirs.begin(), dirs.end(), [](const auto& a, const auto& b) {
                return a.path().filename().string() < b.path().filename().string();
            });
            std::sort(files.begin(), files.end(), [](const auto& a, const auto& b) {
                return a.path().filename().string() < b.path().filename().string();
            });
            
            // Display directories
            for (auto& d : dirs) {
                std::string label = "[DIR] " + d.path().filename().string();
                if (ImGui::Selectable(label.c_str())) {
                    m_importBrowserPath = d.path();
                    m_importBrowserSelectedFile.clear();
                }
            }
            
            // Display files
            for (auto& f : files) {
                std::string fname = f.path().filename().string();
                bool selected = (m_importBrowserSelectedFile == fname);
                if (ImGui::Selectable(fname.c_str(), selected)) {
                    m_importBrowserSelectedFile = fname;
                }
                
                // Double-click to import
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
                    m_importBrowserSelectedFile = fname;
                    // Perform import
                    std::filesystem::path fullPath = m_importBrowserPath / fname;
                    std::string error;
                    auto part = m_partLibrary->importFromFile(fullPath.string(), &error);
                    if (part) {
                        int64_t dbID = m_partLibrary->savePart(*part);
                        if (dbID > 0) {
                            PLOGI << "Imported and saved part: " << part->name << " (ID: " << dbID << ")";
                            refreshPartsList();
                            m_partImportError.clear();
                        } else {
                            m_partImportError = "Failed to save part to database";
                        }
                    } else {
                        m_partImportError = error.empty() ? "Failed to import part" : error;
                    }
                    ImGui::CloseCurrentPopup();
                }
            }
        } catch (const std::exception& ex) {
            ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s", ex.what());
        }
        ImGui::EndChild();
        
        // Selected file display
        if (!m_importBrowserSelectedFile.empty()) {
            ImGui::Text("Selected: %s", m_importBrowserSelectedFile.c_str());
        }
        
        // Action buttons
        ImGui::Separator();
        if (ImGui::Button("Import", ImVec2(100, 0))) {
            if (!m_importBrowserSelectedFile.empty()) {
                std::filesystem::path fullPath = m_importBrowserPath / m_importBrowserSelectedFile;
                std::string error;
                auto part = m_partLibrary->importFromFile(fullPath.string(), &error);
                if (part) {
                    int64_t dbID = m_partLibrary->savePart(*part);
                    if (dbID > 0) {
                        PLOGI << "Imported and saved part: " << part->name << " (ID: " << dbID << ")";
                        refreshPartsList();
                        m_partImportError.clear();
                    } else {
                        m_partImportError = "Failed to save part to database";
                    }
                } else {
                    m_partImportError = error.empty() ? "Failed to import part" : error;
                }
                ImGui::CloseCurrentPopup();
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
    
    if (!m_partImportError.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Error: %s", m_partImportError.c_str());
    }
    
    ImGui::Separator();
    
    // Parts list
    ImGui::BeginChild("PartsList", ImVec2(0, 200), true);
    
    // Filter and display parts
    std::vector<PartSummary> filteredParts;
    for (const auto& summary : m_partSummaries) {
        // Category filter
        if (!m_partsSelectedCategory.empty() && summary.category != m_partsSelectedCategory) {
            continue;
        }
        
        // Search filter
        if (!m_partsSearchQuery.empty()) {
            std::string searchLower = m_partsSearchQuery;
            std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);
            
            std::string nameLower = summary.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            
            std::string tagsLower = summary.tags;
            std::transform(tagsLower.begin(), tagsLower.end(), tagsLower.begin(), ::tolower);
            
            if (nameLower.find(searchLower) == std::string::npos &&
                tagsLower.find(searchLower) == std::string::npos) {
                continue;
            }
        }
        
        filteredParts.push_back(summary);
    }
    
    for (size_t i = 0; i < filteredParts.size(); ++i) {
        const auto& summary = filteredParts[i];
        
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        
        // Find original index
        int originalIndex = -1;
        for (size_t j = 0; j < m_partSummaries.size(); ++j) {
            if (m_partSummaries[j].dbID == summary.dbID) {
                originalIndex = static_cast<int>(j);
                break;
            }
        }
        
        if (originalIndex == m_selectedPartSummaryIndex) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        
        // Check compatibility with selected socket
        bool isCompatible = false;
        if (m_selectedModelIndex >= 0 && m_selectedModelIndex < static_cast<int>(m_models.size())) {
            auto& model = m_models[m_selectedModelIndex];
            if (m_selectedSocketIndex >= 0 && 
                m_selectedSocketIndex < static_cast<int>(model.loadResult.extractedSockets.size())) {
                auto& socket = model.loadResult.extractedSockets[m_selectedSocketIndex];
                if (!summary.rootSocket.empty() && summary.rootSocket == socket.profile.profileID) {
                    isCompatible = true;
                }
            }
        }
        
        // Display with category prefix if showing all
        std::string displayName = summary.name;
        if (m_partsSelectedCategory.empty() && !summary.category.empty()) {
            displayName = "[" + summary.category + "] " + summary.name;
        }
        
        // Add compatibility indicator
        if (isCompatible) {
            displayName = "✓ " + displayName;
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.3f, 1.0f, 0.3f, 1.0f));
        }
        
        ImGui::TreeNodeEx((void*)(intptr_t)summary.dbID, flags, "%s", displayName.c_str());
        
        if (isCompatible) {
            ImGui::PopStyleColor();
        }
        
        if (ImGui::IsItemClicked()) {
            m_selectedPartSummaryIndex = originalIndex;
        }
        
        // Start drag on mouse drag
        if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 5.0f)) {
            if (!m_isDraggingPart) {
                startPartDrag(summary.dbID);
            }
        }
        
        // Tooltip with part details
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Name: %s", summary.name.c_str());
            if (!summary.category.empty()) {
                ImGui::Text("Category: %s", summary.category.c_str());
            }
            ImGui::Text("Vertices: %d", summary.vertexCount);
            ImGui::Text("Bones: %d", summary.boneCount);
            ImGui::Text("Sockets: %d", summary.socketCount);
            if (!summary.rootSocket.empty()) {
                ImGui::Text("Attaches via: %s", summary.rootSocket.c_str());
            }
            if (!summary.tags.empty()) {
                ImGui::Text("Tags: %s", summary.tags.c_str());
            }
            
            // Show compatibility with selected socket
            if (m_selectedModelIndex >= 0 && m_selectedModelIndex < static_cast<int>(m_models.size())) {
                auto& model = m_models[m_selectedModelIndex];
                if (m_selectedSocketIndex >= 0 && 
                    m_selectedSocketIndex < static_cast<int>(model.loadResult.extractedSockets.size())) {
                    auto& socket = model.loadResult.extractedSockets[m_selectedSocketIndex];
                    ImGui::Separator();
                    if (isCompatible) {
                        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), 
                                         "✓ Compatible with selected socket '%s'", socket.name.c_str());
                    } else if (!summary.rootSocket.empty()) {
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), 
                                         "✗ Incompatible: needs '%s', socket is '%s'", 
                                         summary.rootSocket.c_str(), socket.profile.profileID.c_str());
                    } else {
                        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), 
                                         "✗ Part has no root socket defined");
                    }
                }
            }
            
            ImGui::EndTooltip();
        }
        
        // Double-click to load/preview
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            m_previewPart = m_partLibrary->loadPart(summary.dbID);
            if (m_previewPart) {
                // Upload all meshes to GPU
                m_previewPartGPUs.clear();
                bool allUploaded = true;
                for (const auto& mesh : m_previewPart->meshes) {
                    MeshGPUData gpuData;
                    if (uploadMeshToGPU(mesh, gpuData)) {
                        m_previewPartGPUs.push_back(gpuData);
                    } else {
                        PLOGE << "Failed to upload preview part mesh to GPU: " << mesh.name;
                        allUploaded = false;
                        break;
                    }
                }
                
                if (allUploaded) {
                    PLOGI << "Loaded part for preview: " << m_previewPart->name 
                          << " (" << m_previewPartGPUs.size() << " meshes)";
                } else {
                    m_previewPart.reset();
                    m_previewPartGPUs.clear();
                }
            }
        }
    }
    
    ImGui::EndChild();
    
    // Part details / actions
    if (m_selectedPartSummaryIndex >= 0 && m_selectedPartSummaryIndex < static_cast<int>(m_partSummaries.size())) {
        const auto& selected = m_partSummaries[m_selectedPartSummaryIndex];
        
        ImGui::Separator();
        ImGui::Text("Selected: %s", selected.name.c_str());
        
        // Check compatibility with selected socket
        bool canAttach = false;
        std::string compatibilityInfo;
        
        if (m_selectedModelIndex >= 0 && m_selectedModelIndex < static_cast<int>(m_models.size())) {
            auto& model = m_models[m_selectedModelIndex];
            if (m_selectedSocketIndex >= 0 && 
                m_selectedSocketIndex < static_cast<int>(model.loadResult.extractedSockets.size())) {
                
                auto& socket = model.loadResult.extractedSockets[m_selectedSocketIndex];
                
                // Check if the part's root socket matches the selected socket's profile
                if (!selected.rootSocket.empty() && selected.rootSocket == socket.profile.profileID) {
                    canAttach = true;
                    compatibilityInfo = "Compatible with socket: " + socket.name;
                } else if (selected.rootSocket.empty()) {
                    compatibilityInfo = "Part has no root socket defined";
                } else {
                    compatibilityInfo = "Incompatible: needs '" + selected.rootSocket + 
                                      "', socket is '" + socket.profile.profileID + "'";
                }
            }
        }
        
        // Show compatibility status
        if (!compatibilityInfo.empty()) {
            if (canAttach) {
                ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "%s", compatibilityInfo.c_str());
            } else {
                ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "%s", compatibilityInfo.c_str());
            }
        }
        
        // Action buttons
        if (ImGui::Button("Load Preview", ImVec2(-1, 0))) {
            m_previewPart = m_partLibrary->loadPart(selected.dbID);
            if (m_previewPart) {
                // Upload all meshes to GPU
                m_previewPartGPUs.clear();
                bool allUploaded = true;
                for (const auto& mesh : m_previewPart->meshes) {
                    MeshGPUData gpuData;
                    if (uploadMeshToGPU(mesh, gpuData)) {
                        m_previewPartGPUs.push_back(gpuData);
                    } else {
                        PLOGE << "Failed to upload preview part mesh to GPU: " << mesh.name;
                        m_partImportError = "Failed to upload preview part mesh: " + mesh.name;
                        allUploaded = false;
                        break;
                    }
                }
                
                if (allUploaded) {
                    PLOGI << "Loaded preview part: " << m_previewPart->name 
                          << " (" << m_previewPartGPUs.size() << " meshes)";
                    m_partImportError.clear();
                } else {
                    m_previewPart.reset();
                    m_previewPartGPUs.clear();
                }
            } else {
                m_partImportError = "Failed to load part from database";
            }
        }
        
        // Attach to socket button
        if (m_selectedModelIndex >= 0 && m_selectedModelIndex < static_cast<int>(m_models.size())) {
            // Disable button if not compatible
            if (!canAttach) {
                ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
                ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            }
            
            if (ImGui::Button("Attach to Selected Socket", ImVec2(-1, 0))) {
                if (m_selectedSocketIndex >= 0 && canAttach) {
                    auto& model = m_models[m_selectedModelIndex];
                    auto& socket = model.loadResult.extractedSockets[m_selectedSocketIndex];
                    
                    // Load the part from database
                    auto partToAttach = m_partLibrary->loadPart(selected.dbID);
                    
                    if (partToAttach) {
                        auto result = m_partLibrary->attachPart(*partToAttach, socket, model.loadResult.skeleton);
                        
                        if (result.success) {
                            PLOGI << "Attached part to socket: " << socket.name;
                            
                            // Create a loaded model for the attached part
                            // Use IDENTITY transform - skeleton handles positioning
                            LoadedModel attachedModel;
                            attachedModel.filePath = "Attached: " + partToAttach->name;
                            attachedModel.loadResult.success = true;
                            
                            // Add ALL meshes from the part
                            attachedModel.loadResult.meshes = partToAttach->meshes;
                            
                            // Get skeleton from part (first mesh that has one)
                            const Skeleton* partSkel = partToAttach->getSkeleton();
                            if (partSkel) {
                                attachedModel.loadResult.skeleton = *partSkel;
                            }
                            
                            attachedModel.worldTransform = Transform();  // Identity - skeleton positions it
                            
                            // Upload all meshes to GPU
                            bool allUploaded = true;
                            for (const auto& mesh : attachedModel.loadResult.meshes) {
                                MeshGPUData gpuData;
                                if (uploadMeshToGPU(mesh, gpuData)) {
                                    attachedModel.meshGPUData.push_back(gpuData);
                                } else {
                                    PLOGE << "Failed to upload attached part mesh: " << mesh.name;
                                    allUploaded = false;
                                    break;
                                }
                            }
                            
                            if (allUploaded) {
                                m_attachedPartModels.push_back(attachedModel);
                                PLOGI << "Added attached part to render list (" 
                                      << attachedModel.meshGPUData.size() << " meshes)";
                                
                                // Update posed skeleton to use combined skeleton
                                if (m_partLibrary && !m_partLibrary->getCombinedSkeleton().skeleton.empty()) {
                                    m_posedSkeleton = m_partLibrary->getCombinedSkeleton().skeleton;
                                    PLOGI << "Updated posed skeleton with combined skeleton (" 
                                          << m_posedSkeleton.bones.size() << " bones)";
                                }
                            } else {
                                m_partImportError = "Failed to upload all meshes to GPU";
                            }
                            
                            // Clear any existing preview
                            m_previewPart.reset();
                            for (auto& gpu : m_previewPartGPUs) {
                                gpu.release();
                            }
                            m_previewPartGPUs.clear();
                            m_partImportError.clear();
                        } else {
                            PLOGE << "Failed to attach part: " << result.error;
                            m_partImportError = result.error;
                        }
                    } else {
                        m_partImportError = "Failed to load part from database";
                    }
                } else {
                    m_partImportError = "Select a compatible socket first";
                }
            }
            
            if (!canAttach) {
                ImGui::PopItemFlag();
                ImGui::PopStyleVar();
            }
        }
        
        ImGui::Separator();
        
        if (ImGui::Button("Delete Part", ImVec2(-1, 0))) {
            // TODO: Confirm dialog
            if (m_partLibrary->deletePart(selected.dbID)) {
                refreshPartsList();
            }
        }
    }
}

// ============================================================
// Prefabs Panel
// ============================================================

void CharacterEditorUI::renderPrefabsPanel() {
    ImGui::Text("Prefabs");
    ImGui::Separator();
    
    if (!m_characterManager) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "No vault connected");
        return;
    }
    
    // Save current assembly as prefab
    if (!m_activeAttachments.empty()) {
        if (ImGui::Button("Save as Prefab...", ImVec2(-1, 0))) {
            ImGui::OpenPopup("SavePrefabPopup");
        }
    }
    
    // Save prefab popup
    if (ImGui::BeginPopup("SavePrefabPopup")) {
        static char prefabNameBuf[128] = "";
        static char prefabCategoryBuf[64] = "";
        
        ImGui::Text("Save Current Assembly as Prefab");
        ImGui::Separator();
        
        ImGui::InputText("Name", prefabNameBuf, sizeof(prefabNameBuf));
        ImGui::InputText("Category", prefabCategoryBuf, sizeof(prefabCategoryBuf));
        
        if (ImGui::Button("Save", ImVec2(100, 0))) {
            if (strlen(prefabNameBuf) > 0) {
                CharacterPrefab prefab = m_characterManager->createPrefabFromParts(
                    prefabNameBuf, m_activeAttachments);
                prefab.category = prefabCategoryBuf;
                
                int64_t id = m_characterManager->savePrefab(prefab);
                if (id > 0) {
                    PLOGI << "Saved prefab: " << prefabNameBuf;
                    refreshPrefabsList();
                    prefabNameBuf[0] = '\0';
                    prefabCategoryBuf[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
            }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
    
    // Search
    static char searchBuf[256] = "";
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##PrefabSearch", "Search prefabs...", searchBuf, sizeof(searchBuf))) {
        m_prefabSearchQuery = searchBuf;
    }
    
    ImGui::Separator();
    
    // Prefabs list
    ImGui::BeginChild("PrefabsList", ImVec2(0, 150), true);
    
    for (size_t i = 0; i < m_prefabSummaries.size(); ++i) {
        const auto& summary = m_prefabSummaries[i];
        
        // Search filter
        if (!m_prefabSearchQuery.empty()) {
            std::string searchLower = m_prefabSearchQuery;
            std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);
            
            std::string nameLower = summary.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            
            if (nameLower.find(searchLower) == std::string::npos) {
                continue;
            }
        }
        
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (static_cast<int>(i) == m_selectedPrefabIndex) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        
        std::string displayName = summary.name;
        if (!summary.category.empty()) {
            displayName = "[" + summary.category + "] " + summary.name;
        }
        displayName += " (" + std::to_string(summary.partCount) + " parts)";
        
        ImGui::TreeNodeEx((void*)(intptr_t)summary.dbID, flags, "%s", displayName.c_str());
        
        if (ImGui::IsItemClicked()) {
            m_selectedPrefabIndex = static_cast<int>(i);
        }
        
        // Double-click to load
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            m_loadedPrefab = m_characterManager->loadPrefab(summary.dbID);
            if (m_loadedPrefab) {
                // Convert prefab parts to active attachments
                m_activeAttachments.clear();
                for (const auto& inst : m_loadedPrefab->parts) {
                    ActiveAttachment att;
                    att.partID = inst.partID;
                    att.attachmentID = inst.attachmentID;
                    att.socketID = inst.parentSocketID;
                    att.transform = inst.localTransform;
                    m_activeAttachments.push_back(att);
                }
                PLOGI << "Loaded prefab: " << m_loadedPrefab->name << " with " << m_activeAttachments.size() << " parts";
            }
        }
        
        // Tooltip
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Name: %s", summary.name.c_str());
            if (!summary.category.empty()) {
                ImGui::Text("Category: %s", summary.category.c_str());
            }
            ImGui::Text("Parts: %d", summary.partCount);
            if (!summary.description.empty()) {
                ImGui::TextWrapped("Description: %s", summary.description.c_str());
            }
            ImGui::EndTooltip();
        }
    }
    
    ImGui::EndChild();
    
    // Actions for selected prefab
    if (m_selectedPrefabIndex >= 0 && m_selectedPrefabIndex < static_cast<int>(m_prefabSummaries.size())) {
        const auto& selected = m_prefabSummaries[m_selectedPrefabIndex];
        
        ImGui::Separator();
        ImGui::Text("Selected: %s", selected.name.c_str());
        
        if (ImGui::Button("Load to Editor", ImVec2(-1, 0))) {
            m_loadedPrefab = m_characterManager->loadPrefab(selected.dbID);
            if (m_loadedPrefab) {
                m_activeAttachments.clear();
                for (const auto& inst : m_loadedPrefab->parts) {
                    ActiveAttachment att;
                    att.partID = inst.partID;
                    att.attachmentID = inst.attachmentID;
                    att.socketID = inst.parentSocketID;
                    att.transform = inst.localTransform;
                    m_activeAttachments.push_back(att);
                }
            }
        }
        
        if (ImGui::Button("Create Character from Prefab", ImVec2(-1, 0))) {
            if (m_loadedPrefab || (m_loadedPrefab = m_characterManager->loadPrefab(selected.dbID))) {
                m_loadedCharacter = std::make_unique<Character>(
                    m_characterManager->createCharacterFromPrefab("New Character", *m_loadedPrefab));
                m_loadedCharacter->basePrefabID = selected.dbID;
                PLOGI << "Created character from prefab";
            }
        }
        
        if (ImGui::Button("Delete Prefab", ImVec2(-1, 0))) {
            if (m_characterManager->deletePrefab(selected.dbID)) {
                refreshPrefabsList();
            }
        }
    }
}

// ============================================================
// Characters Panel
// ============================================================

void CharacterEditorUI::renderCharactersPanel() {
    ImGui::Text("Characters");
    ImGui::Separator();
    
    if (!m_characterManager) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "No vault connected");
        return;
    }
    
    // Create new character button
    if (ImGui::Button("New Character", ImVec2(-1, 0))) {
        ImGui::OpenPopup("NewCharacterPopup");
    }
    
    // New character popup
    if (ImGui::BeginPopup("NewCharacterPopup")) {
        static char charNameBuf[128] = "";
        
        ImGui::Text("Create New Character");
        ImGui::Separator();
        
        ImGui::InputText("Name", charNameBuf, sizeof(charNameBuf));
        
        if (ImGui::Button("Create Empty", ImVec2(120, 0))) {
            if (strlen(charNameBuf) > 0) {
                m_loadedCharacter = std::make_unique<Character>(
                    m_characterManager->createEmptyCharacter(charNameBuf));
                m_activeAttachments.clear();
                charNameBuf[0] = '\0';
                ImGui::CloseCurrentPopup();
            }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(80, 0))) {
            ImGui::CloseCurrentPopup();
        }
        
        ImGui::EndPopup();
    }
    
    // Save current character
    if (m_loadedCharacter) {
        if (ImGui::Button("Save Character", ImVec2(-1, 0))) {
            // Update character parts from active attachments
            m_loadedCharacter->parts.clear();
            for (const auto& att : m_activeAttachments) {
                AttachedPartInstance inst;
                inst.partID = att.partID;
                inst.attachmentID = att.attachmentID;
                inst.parentSocketID = att.socketID;
                inst.localTransform = att.transform;
                m_loadedCharacter->parts.push_back(inst);
            }
            
            int64_t id = m_characterManager->saveCharacter(*m_loadedCharacter);
            if (id > 0) {
                PLOGI << "Saved character: " << m_loadedCharacter->name;
                refreshCharactersList();
            }
        }
    }
    
    // Search
    static char searchBuf[256] = "";
    ImGui::SetNextItemWidth(-1);
    if (ImGui::InputTextWithHint("##CharSearch", "Search characters...", searchBuf, sizeof(searchBuf))) {
        m_characterSearchQuery = searchBuf;
    }
    
    ImGui::Separator();
    
    // Characters list
    ImGui::BeginChild("CharactersList", ImVec2(0, 150), true);
    
    for (size_t i = 0; i < m_characterSummaries.size(); ++i) {
        const auto& summary = m_characterSummaries[i];
        
        // Search filter
        if (!m_characterSearchQuery.empty()) {
            std::string searchLower = m_characterSearchQuery;
            std::transform(searchLower.begin(), searchLower.end(), searchLower.begin(), ::tolower);
            
            std::string nameLower = summary.name;
            std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
            
            if (nameLower.find(searchLower) == std::string::npos) {
                continue;
            }
        }
        
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
        if (static_cast<int>(i) == m_selectedCharacterIndex) {
            flags |= ImGuiTreeNodeFlags_Selected;
        }
        
        ImGui::TreeNodeEx((void*)(intptr_t)summary.dbID, flags, "%s", summary.name.c_str());
        
        if (ImGui::IsItemClicked()) {
            m_selectedCharacterIndex = static_cast<int>(i);
        }
        
        // Double-click to load
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) {
            m_loadedCharacter = m_characterManager->loadCharacter(summary.dbID);
            if (m_loadedCharacter) {
                // Convert character parts to active attachments
                m_activeAttachments.clear();
                for (const auto& inst : m_loadedCharacter->parts) {
                    ActiveAttachment att;
                    att.partID = inst.partID;
                    att.attachmentID = inst.attachmentID;
                    att.socketID = inst.parentSocketID;
                    att.transform = inst.localTransform;
                    m_activeAttachments.push_back(att);
                }
                PLOGI << "Loaded character: " << m_loadedCharacter->name;
            }
        }
        
        // Tooltip
        if (ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            ImGui::Text("Name: %s", summary.name.c_str());
            if (!summary.description.empty()) {
                ImGui::TextWrapped("Description: %s", summary.description.c_str());
            }
            if (summary.basePrefabID > 0) {
                ImGui::Text("Based on prefab ID: %ld", summary.basePrefabID);
            }
            ImGui::EndTooltip();
        }
    }
    
    ImGui::EndChild();
    
    // Actions for selected character
    if (m_selectedCharacterIndex >= 0 && m_selectedCharacterIndex < static_cast<int>(m_characterSummaries.size())) {
        const auto& selected = m_characterSummaries[m_selectedCharacterIndex];
        
        ImGui::Separator();
        ImGui::Text("Selected: %s", selected.name.c_str());
        
        if (ImGui::Button("Load to Editor", ImVec2(-1, 0))) {
            m_loadedCharacter = m_characterManager->loadCharacter(selected.dbID);
            if (m_loadedCharacter) {
                m_activeAttachments.clear();
                for (const auto& inst : m_loadedCharacter->parts) {
                    ActiveAttachment att;
                    att.partID = inst.partID;
                    att.attachmentID = inst.attachmentID;
                    att.socketID = inst.parentSocketID;
                    att.transform = inst.localTransform;
                    m_activeAttachments.push_back(att);
                }
            }
        }
        
        if (ImGui::Button("Delete Character", ImVec2(-1, 0))) {
            if (m_characterManager->deleteCharacter(selected.dbID)) {
                if (m_loadedCharacter && m_loadedCharacter->characterID == selected.characterID) {
                    m_loadedCharacter.reset();
                }
                refreshCharactersList();
            }
        }
    }
    
    // Show currently loaded character info
    if (m_loadedCharacter) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.3f, 0.8f, 0.3f, 1.0f), "Editing: %s", m_loadedCharacter->name.c_str());
        ImGui::Text("Parts: %zu", m_activeAttachments.size());
    }
}

// ============================================================
// Drag-Drop Handling
// ============================================================

void CharacterEditorUI::handlePartDragDrop() {
    ImGuiIO& io = ImGui::GetIO();
    
    if (!m_isDraggingPart || !m_draggingPart) return;
    
    // Check for mouse release (drop)
    if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        if (m_viewportHovered && m_highlightedSocket) {
            // Drop on highlighted socket
            ActiveAttachment att;
            att.partID = m_draggingPart->id;  // Part uses 'id' field
            att.attachmentID = m_characterManager ? m_characterManager->generateUUID() : std::to_string(m_activeAttachments.size());
            att.socketID = m_highlightedSocket->name;
            att.transform = m_highlightedSocket->localOffset;
            
            m_activeAttachments.push_back(att);
            PLOGI << "Attached part '" << m_draggingPart->name << "' to socket '" << m_highlightedSocket->name << "'";
        }
        
        cancelPartDrag();
        return;
    }
    
    // Check for Escape to cancel
    if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
        cancelPartDrag();
        return;
    }
    
    // If hovering over viewport, find nearest compatible socket
    if (m_viewportHovered) {
        glm::vec2 mousePos(io.MousePos.x - m_viewportPos.x, io.MousePos.y - m_viewportPos.y);
        float dist;
        m_highlightedSocket = findNearestCompatibleSocket(*m_draggingPart, mousePos, dist);
    } else {
        m_highlightedSocket = nullptr;
    }
    
    // Draw drag preview cursor
    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    
    // Draw floating tooltip showing what's being dragged
    ImGui::BeginTooltip();
    ImGui::Text("Dragging: %s", m_draggingPart->name.c_str());
    if (m_highlightedSocket) {
        ImGui::TextColored(ImVec4(0.3f, 1.0f, 0.3f, 1.0f), "Drop on: %s", m_highlightedSocket->name.c_str());
    } else if (m_viewportHovered) {
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.2f, 1.0f), "No compatible socket nearby");
    }
    ImGui::EndTooltip();
}

Socket* CharacterEditorUI::findNearestCompatibleSocket(const Part& part, const glm::vec2& screenPos, float& outDist) {
    outDist = FLT_MAX;
    Socket* nearest = nullptr;
    
    if (m_models.empty()) return nullptr;
    
    const auto& model = m_models[0];
    if (model.loadResult.extractedSockets.empty()) return nullptr;
    
    float aspectRatio = m_viewportSize.x / m_viewportSize.y;
    glm::mat4 view = m_camera.getViewMatrix();
    glm::mat4 projection = m_camera.getProjectionMatrix(aspectRatio);
    glm::mat4 vp = projection * view;
    
    // Project function
    auto project = [&](const glm::vec3& worldPos) -> glm::vec2 {
        glm::vec4 clip = vp * glm::vec4(worldPos, 1.0f);
        if (clip.w <= 0) return glm::vec2(-10000);
        glm::vec3 ndc = glm::vec3(clip) / clip.w;
        return glm::vec2(
            (ndc.x * 0.5f + 0.5f) * m_viewportSize.x,
            (1.0f - (ndc.y * 0.5f + 0.5f)) * m_viewportSize.y
        );
    };
    
    // Get part's attachment specs category (what sockets it can connect to)
    std::string partCategory;
    if (!part.attachmentSpecs.empty()) {
        // Use the first attachment spec's required profile category
        partCategory = part.attachmentSpecs[0].requiredProfile.category;
    }
    
    const Skeleton& skeleton = m_posedSkeleton.empty() ? model.loadResult.skeleton : m_posedSkeleton;
    
    float snapThreshold = 50.0f;  // Pixels
    
    // Note: extractedSockets is const, so we need to cast to non-const to return mutable pointer
    // This is safe since we're returning pointer to member data that we know exists
    auto& mutableSockets = const_cast<std::vector<Socket>&>(model.loadResult.extractedSockets);
    
    for (size_t i = 0; i < mutableSockets.size(); ++i) {
        auto& socket = mutableSockets[i];
        
        // Check socket category compatibility using Part::canAttachTo
        if (!part.canAttachTo(socket)) {
            continue;
        }
        
        // Get socket world position
        glm::vec3 socketWorldPos = socket.localOffset.position;
        auto it = skeleton.boneNameToIndex.find(socket.boneName);
        if (it != skeleton.boneNameToIndex.end()) {
            Transform boneWorld = skeleton.getWorldTransform(static_cast<uint32_t>(it->second));
            socketWorldPos = boneWorld.position + boneWorld.rotation * socket.localOffset.position;
        }
        
        glm::vec2 socketScreen = project(socketWorldPos);
        float dist = glm::length(screenPos - socketScreen);
        
        if (dist < snapThreshold && dist < outDist) {
            outDist = dist;
            nearest = &socket;
        }
    }
    
    return nearest;
}

} // namespace CharacterEditor
