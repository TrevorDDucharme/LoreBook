#pragma once
#include <CharacterEditor/ModelLoader.hpp>
#include <CharacterEditor/Part.hpp>
#include <CharacterEditor/PartLibrary.hpp>
#include <CharacterEditor/CharacterManager.hpp>
#include <CharacterEditor/IKSolver.hpp>
#include <imgui.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <filesystem>

namespace CharacterEditor {

/**
 * @brief Debug visualization flags
 */
enum class DebugView : uint32_t {
    None            = 0,
    Wireframe       = 1 << 0,
    Normals         = 1 << 1,
    Bones           = 1 << 2,
    Sockets         = 1 << 3,
    BoneWeights     = 1 << 4,
    BoundingBox     = 1 << 5,
    SocketInfluence = 1 << 6,
    IKChains        = 1 << 7,
    All             = 0xFFFFFFFF
};

inline DebugView operator|(DebugView a, DebugView b) {
    return static_cast<DebugView>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

inline DebugView operator&(DebugView a, DebugView b) {
    return static_cast<DebugView>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

inline DebugView& operator|=(DebugView& a, DebugView b) {
    return a = a | b;
}

inline DebugView& operator&=(DebugView& a, DebugView b) {
    return a = a & b;
}

inline bool hasFlag(DebugView flags, DebugView flag) {
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

/**
 * @brief Shading mode for mesh rendering
 */
enum class ShadingMode : uint32_t {
    Solid,
    Textured,
    Flat,
    Wireframe,
    BoneWeightHeatmap,
    NormalMap,
    UVChecker
};

/**
 * @brief Camera for 3D viewport
 */
struct ViewportCamera {
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    float distance = 5.0f;
    float yaw = 45.0f;      // degrees
    float pitch = 30.0f;    // degrees
    float fov = 45.0f;      // degrees
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    
    glm::vec3 getPosition() const;
    glm::mat4 getViewMatrix() const;
    glm::mat4 getProjectionMatrix(float aspectRatio) const;
    
    void orbit(float deltaYaw, float deltaPitch);
    void pan(float deltaX, float deltaY);
    void zoom(float delta);
    void focusOn(const glm::vec3& center, float radius);
};

/**
 * @brief GPU resources for a loaded mesh
 */
struct MeshGPUData {
    uint32_t vao = 0;
    uint32_t vbo = 0;
    uint32_t ebo = 0;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    bool isValid = false;
    
    void release();
};

/**
 * @brief Loaded model with GPU resources
 */
struct LoadedModel {
    std::string filePath;
    LoadResult loadResult;
    std::vector<MeshGPUData> meshGPUData;
    Transform worldTransform;
    
    bool isLoaded() const { return loadResult.success && !meshGPUData.empty(); }
};

/**
 * @brief IK test target for debugging
 */
struct IKTestTarget {
    std::string name;
    IKChain chain;
    IKTarget target;
    bool isActive = false;
    int startBoneIndex = -1;
    int endBoneIndex = -1;
    float blendWeight = 1.0f;
};

/**
 * @brief CharacterEditorUI - ImGui-based 3D character editor
 * 
 * Provides a framebuffer-based 3D viewport for viewing and testing
 * character parts, sockets, shape keys, and IK.
 */
class CharacterEditorUI {
public:
    CharacterEditorUI();
    ~CharacterEditorUI();
    
    // Non-copyable
    CharacterEditorUI(const CharacterEditorUI&) = delete;
    CharacterEditorUI& operator=(const CharacterEditorUI&) = delete;
    
    /**
     * @brief Initialize OpenGL resources (shaders, framebuffer)
     * @return true if initialization succeeded
     */
    bool initialize();
    
    /**
     * @brief Cleanup OpenGL resources
     */
    void shutdown();
    
    /**
     * @brief Main render function - call each frame
     */
    void render();
    
    /**
     * @brief Load a model file
     * @param filePath Path to the model file (FBX, GLTF, etc.)
     * @return true if loading succeeded
     */
    bool loadModel(const std::string& filePath);
    
    /**
     * @brief Clear all loaded models
     */
    void clearModels();
    
    // Window visibility
    bool isOpen() const { return m_isOpen; }
    void setOpen(bool open) { m_isOpen = open; }
    void toggleOpen() { m_isOpen = !m_isOpen; }
    
    // Debug view control
    void setDebugViews(DebugView views) { m_debugViews = views; }
    DebugView getDebugViews() const { return m_debugViews; }
    void toggleDebugView(DebugView view);
    
    // Shading mode
    void setShadingMode(ShadingMode mode) { m_shadingMode = mode; }
    ShadingMode getShadingMode() const { return m_shadingMode; }
    
    // Camera access
    ViewportCamera& getCamera() { return m_camera; }
    const ViewportCamera& getCamera() const { return m_camera; }
    
    // Parts Library
    void setPartLibrary(PartLibrary* library);
    void refreshPartsList();
    
    // Character Manager (prefabs/characters)
    void setCharacterManager(CharacterManager* manager);
    void refreshPrefabsList();
    void refreshCharactersList();
    
    // Drag-drop state for parts
    bool isDraggingPart() const { return m_isDraggingPart; }
    void startPartDrag(int64_t partDbID);
    void cancelPartDrag();
    
private:
    // Framebuffer management
    bool createFramebuffer(int width, int height);
    void resizeFramebuffer(int width, int height);
    void destroyFramebuffer();
    
    // Shader management
    bool createShaders();
    void destroyShaders();
    
    // Mesh GPU upload
    bool uploadMeshToGPU(const Mesh& mesh, MeshGPUData& gpuData);
    void updateMeshVertices(const Mesh& mesh, MeshGPUData& gpuData);
    
    // Rendering
    void renderViewport();
    void renderScene();
    void renderMesh(const Mesh& mesh, const MeshGPUData& gpuData, const glm::mat4& modelMatrix);
    void renderDebugOverlays();
    void renderBones(const Skeleton& skeleton, const glm::mat4& modelMatrix);
    void renderSockets(const std::vector<Socket>& sockets, const Skeleton& skeleton, const glm::mat4& modelMatrix);
    void renderBoundingBox(const glm::vec3& min, const glm::vec3& max, const glm::mat4& modelMatrix);
    void renderIKTargets(const glm::mat4& modelMatrix);
    void renderGrid();
    void renderGizmo(const glm::vec3& position, float size);
    
    // UI panels
    void renderMenuBar();
    void renderToolbar();
    void renderHierarchyPanel();
    void renderPropertiesPanel();
    void renderShapeKeyPanel();
    void renderSocketPanel();
    void renderIKPanel();
    void renderDebugPanel();
    void renderPartsLibraryPanel();
    void renderPrefabsPanel();
    void renderCharactersPanel();
    
    // Drag-drop handling
    void handlePartDragDrop();
    Socket* findNearestCompatibleSocket(const Part& part, const glm::vec2& screenPos, float& outDist);
    
    // Gizmo enums (must be before function declarations that use them)
    enum class GizmoAxis { None, X, Y, Z, XY, XZ, YZ, All };
    enum class GizmoMode { Translate, Rotate, Scale };
    
    // Input handling
    void handleViewportInput();
    void handleMouseOrbit();
    void handleMousePan();
    void handleMouseZoom();
    void handleSelection();
    void handleGizmo();
    void handleBoneGizmo();
    bool pickGizmoAxis(const glm::vec3& gizmoPos, const glm::vec2& mousePos, GizmoAxis& outAxis);
    bool pickRotationGizmoAxis(const glm::vec3& gizmoPos, const glm::vec2& mousePos, float radius, GizmoAxis& outAxis);
    int pickBone(const glm::vec2& mousePos);
    int pickMesh(const glm::vec2& mousePos);  // Returns mesh index or -1
    glm::vec3 projectMouseOntoPlane(const glm::vec2& mousePos, const glm::vec3& planeOrigin, const glm::vec3& planeNormal);
    void renderTranslationGizmo(const glm::vec3& position, float size);
    void renderRotationGizmo(const glm::vec3& position, const glm::quat& rotation, float radius);
    
    // State
    bool m_isOpen = false;
    bool m_initialized = false;
    
    // Framebuffer
    uint32_t m_fbo = 0;
    uint32_t m_colorTexture = 0;
    uint32_t m_depthRBO = 0;
    int m_fbWidth = 0;
    int m_fbHeight = 0;
    
    // Shaders
    uint32_t m_meshShader = 0;
    uint32_t m_lineShader = 0;
    uint32_t m_gridShader = 0;
    
    // Line rendering VAO/VBO for debug drawing
    uint32_t m_lineVAO = 0;
    uint32_t m_lineVBO = 0;
    
    // Camera
    ViewportCamera m_camera;
    
    // Viewport state
    ImVec2 m_viewportPos{0, 0};
    ImVec2 m_viewportSize{800, 600};
    bool m_viewportHovered = false;
    bool m_viewportFocused = false;
    
    // Input state
    bool m_isOrbiting = false;
    bool m_isPanning = false;
    ImVec2 m_lastMousePos{0, 0};
    
    // Gizmo state
    GizmoMode m_gizmoMode = GizmoMode::Rotate;  // Default to rotation for bone posing
    GizmoAxis m_gizmoDragAxis = GizmoAxis::None;
    bool m_isDraggingGizmo = false;
    glm::vec3 m_gizmoDragStart{0.0f};
    glm::vec3 m_gizmoTargetStart{0.0f};
    glm::quat m_gizmoRotationStart{1.0f, 0.0f, 0.0f, 0.0f};
    float m_gizmoRotationAngleStart = 0.0f;
    
    // Bone pose editing
    std::unordered_map<uint32_t, glm::quat> m_bonePoseOverrides;  // Bone index -> rotation override
    Skeleton m_posedSkeleton;  // Skeleton with pose overrides applied
    
    // Loaded models
    std::vector<LoadedModel> m_models;
    int m_selectedModelIndex = -1;
    int m_selectedMeshIndex = -1;
    int m_selectedBoneIndex = -1;
    int m_selectedSocketIndex = -1;
    
    // Display options
    DebugView m_debugViews = DebugView::Sockets | DebugView::Bones;
    ShadingMode m_shadingMode = ShadingMode::Solid;
    bool m_showGrid = true;
    bool m_showGizmo = true;
    glm::vec4 m_clearColor{0.15f, 0.15f, 0.18f, 0.0f};
    
    // Shape key editing
    std::string m_selectedShapeKey;
    
    // IK testing
    std::vector<IKTestTarget> m_ikTargets;
    int m_selectedIKTarget = -1;
    bool m_ikEnabled = false;
    FABRIKSolver m_ikSolver;
    Skeleton m_solvedSkeleton;  // Copy for IK modifications
    
    // Colors for debug visualization
    glm::vec4 m_boneColor{0.2f, 0.8f, 0.3f, 1.0f};
    glm::vec4 m_socketColor{1.0f, 0.5f, 0.0f, 1.0f};
    glm::vec4 m_selectedColor{1.0f, 1.0f, 0.0f, 1.0f};
    glm::vec4 m_ikTargetColor{1.0f, 0.0f, 0.5f, 1.0f};
    glm::vec4 m_gridColor{0.3f, 0.3f, 0.3f, 1.0f};
    
    // Parts Library
    PartLibrary* m_partLibrary = nullptr;     // Non-owning pointer (owned by Vault)
    std::vector<PartSummary> m_partSummaries;
    std::vector<std::string> m_partCategories;
    std::string m_partsSearchQuery;
    std::string m_partsSelectedCategory;      // Empty = all categories
    int m_selectedPartSummaryIndex = -1;
    std::unique_ptr<Part> m_previewPart;      // Part being previewed
    std::string m_partImportError;
    
    // Part import file browser state
    bool m_showImportPartModal = false;
    std::filesystem::path m_importBrowserPath;
    std::string m_importBrowserSelectedFile;
    
    // Character Manager (prefabs/characters)
    CharacterManager* m_characterManager = nullptr;
    std::vector<PrefabSummary> m_prefabSummaries;
    std::vector<CharacterSummary> m_characterSummaries;
    std::string m_prefabSearchQuery;
    std::string m_characterSearchQuery;
    int m_selectedPrefabIndex = -1;
    int m_selectedCharacterIndex = -1;
    std::unique_ptr<CharacterPrefab> m_loadedPrefab;   // Currently editing prefab
    std::unique_ptr<Character> m_loadedCharacter;      // Currently editing character
    
    // Active character assembly (parts attached in viewport)
    std::vector<ActiveAttachment> m_activeAttachments;
    int m_selectedAttachmentIndex = -1;
    
    // Drag-drop state for parts
    bool m_isDraggingPart = false;
    int64_t m_draggingPartDbID = -1;
    std::unique_ptr<Part> m_draggingPart;
    glm::vec2 m_dragStartPos{0.0f};
    Socket* m_highlightedSocket = nullptr;
};

} // namespace CharacterEditor
