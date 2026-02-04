#pragma once
#include <WorldMaps/Buildings/Building.hpp>
#include <imgui.h>
#include <string>
#include <functional>

// ============================================================
// Tool types for floor plan editing
// ============================================================
enum class FloorPlanTool {
    Select,
    Wall,
    WallArc,        // Arc wall (3-point curve)
    WallBezier,     // Quadratic bezier wall
    WallCubic,      // Cubic bezier wall
    WallBSpline,    // B-spline wall (4+ control points)
    WallBezierSpline, // Connected Bezier spline (2+ control points)
    Room,
    RoomFromWalls,  // Click inside enclosed walls to create room
    RoomCurvedEdge, // Add curved edge to existing room
    Door,
    Window,
    Furniture,
    Staircase,
    Delete,
    Pan
};

// ============================================================
// FloorPlanEditor - ImGui-based floor plan editor
// ============================================================
class FloorPlanEditor
{
public:
    FloorPlanEditor();
    ~FloorPlanEditor() = default;
    
    // Main render function - call each frame
    void render();
    
    // Set the building to edit
    void setBuilding(Building* building);
    Building* getBuilding() { return m_building; }
    
    // Window visibility
    bool isOpen() const { return m_isOpen; }
    void setOpen(bool open) { m_isOpen = open; }
    void toggleOpen() { m_isOpen = !m_isOpen; }
    
    // Current tool
    FloorPlanTool getCurrentTool() const { return m_currentTool; }
    void setCurrentTool(FloorPlanTool tool) { m_currentTool = tool; }
    
    // View controls
    void resetView();
    void zoomToFit();
    
private:
    // Coordinate transforms
    ImVec2 worldToScreen(ImVec2 world) const;
    ImVec2 screenToWorld(ImVec2 screen) const;
    float worldToScreenScale(float worldSize) const;
    
    // Rendering
    void renderToolbar();
    void renderCanvas();
    void renderFloorSelector();
    void renderPropertiesPanel();
    void renderGrid(ImDrawList* drawList, ImVec2 canvasMin, ImVec2 canvasMax);
    void renderWalls(ImDrawList* drawList);
    void renderRooms(ImDrawList* drawList);
    void renderDoors(ImDrawList* drawList);
    void renderWindows(ImDrawList* drawList);
    void renderFurniture(ImDrawList* drawList);
    void renderStaircases(ImDrawList* drawList);
    void renderCurrentDrawing(ImDrawList* drawList);
    void renderSelection(ImDrawList* drawList);
    void renderControlPoints(ImDrawList* drawList);  // For bezier/arc curves
    
    // Input handling
    void handleCanvasInput();
    void handleToolInput();
    void handlePanZoom();
    
    // Tool actions - straight walls
    void startWallDrawing(ImVec2 worldPos);
    void updateWallDrawing(ImVec2 worldPos);
    void finishWallDrawing();
    void cancelDrawing();
    
    // Tool actions - curved walls
    void startCurvedWallDrawing(ImVec2 worldPos);
    void addCurveControlPoint(ImVec2 worldPos);
    void finishCurvedWallDrawing();
    
    // Tool actions - spline walls (multi-point)
    void startSplineDrawing(ImVec2 worldPos);
    void addSplineControlPoint(ImVec2 worldPos);
    void finishSplineDrawing(bool closed);
    
    void startRoomDrawing(ImVec2 worldPos);
    void addRoomVertex(ImVec2 worldPos);
    void finishRoomDrawing();
    
    // Tool actions - room from walls
    void createRoomFromWallsAt(ImVec2 worldPos);
    
    // Tool actions - room curved edges
    void startRoomEdgeCurve(ImVec2 worldPos);  // Click near room edge to start
    void addRoomEdgeControlPoint(ImVec2 worldPos);
    void finishRoomEdgeCurve();
    
    void placeDoor(ImVec2 worldPos);
    void placeWindow(ImVec2 worldPos);
    void placeFurniture(ImVec2 worldPos);
    void placeStaircase(ImVec2 worldPos);
    
    void selectAt(ImVec2 worldPos);
    void deleteAt(ImVec2 worldPos);
    void deleteSelection();
    
    // Control point dragging for selected curves
    void updateControlPointDrag(ImVec2 worldPos);
    int findControlPointAt(ImVec2 worldPos, float tolerance = 0.2f);
    
    // Selection
    enum class SelectionType { None, Wall, Room, Door, Window, Furniture, Staircase };
    SelectionType m_selectionType = SelectionType::None;
    int m_selectedId = -1;
    int m_draggedControlPoint = -1;  // -1=none, 0=start, 1+=control points, N=end
    bool m_isDraggingControlPoint = false;
    int m_selectedRoomEdge = -1;     // For room edge curve editing
    
    void clearSelection();
    
    // State
    Building* m_building = nullptr;
    bool m_isOpen = false;
    FloorPlanTool m_currentTool = FloorPlanTool::Select;
    
    // View state
    ImVec2 m_viewOffset{0, 0};    // Pan offset in screen pixels
    float m_viewZoom = 50.0f;     // Pixels per meter
    ImVec2 m_canvasPos{0, 0};     // Canvas position in screen space
    ImVec2 m_canvasSize{0, 0};    // Canvas size in pixels
    
    // Drawing state
    bool m_isDrawing = false;
    ImVec2 m_drawStart{0, 0};
    ImVec2 m_drawCurrent{0, 0};
    std::vector<ImVec2> m_roomVertices;     // For room polygon drawing
    std::vector<ImVec2> m_curveControlPts;  // For bezier/arc wall drawing
    int m_curveClickCount = 0;              // Number of clicks for curve definition
    
    // UI state
    bool m_showGrid = true;
    bool m_snapToGrid = true;
    bool m_showControlPoints = true;        // Show curve control points
    float m_gridSize = 0.5f;
    float m_wallThickness = 0.15f;
    int m_curveSegments = 20;               // Bezier sampling resolution
    std::string m_furnitureType = "chair";
    
    // Temp building for when none is set
    Building m_tempBuilding;
    
    // Constants
    static constexpr float MIN_ZOOM = 5.0f;
    static constexpr float MAX_ZOOM = 500.0f;
    static constexpr float ZOOM_SPEED = 0.1f;
    static constexpr float CONTROL_POINT_RADIUS = 6.0f;
};
