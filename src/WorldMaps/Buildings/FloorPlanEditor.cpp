#include <WorldMaps/Buildings/FloorPlanEditor.hpp>
#include <imgui_internal.h>
#include <algorithm>
#include <cmath>

// ============================================================
// Constructor
// ============================================================

FloorPlanEditor::FloorPlanEditor()
{
    m_tempBuilding.initialize();
}

// ============================================================
// Building Management
// ============================================================

void FloorPlanEditor::setBuilding(Building* building)
{
    m_building = building;
    clearSelection();
    if (m_building) {
        m_building->initialize();
    }
}

// ============================================================
// Coordinate Transforms
// ============================================================

ImVec2 FloorPlanEditor::worldToScreen(ImVec2 world) const
{
    return ImVec2(
        m_canvasPos.x + m_canvasSize.x * 0.5f + (world.x * m_viewZoom) + m_viewOffset.x,
        m_canvasPos.y + m_canvasSize.y * 0.5f - (world.y * m_viewZoom) + m_viewOffset.y  // Y flipped
    );
}

ImVec2 FloorPlanEditor::screenToWorld(ImVec2 screen) const
{
    return ImVec2(
        (screen.x - m_canvasPos.x - m_canvasSize.x * 0.5f - m_viewOffset.x) / m_viewZoom,
        -(screen.y - m_canvasPos.y - m_canvasSize.y * 0.5f - m_viewOffset.y) / m_viewZoom  // Y flipped
    );
}

float FloorPlanEditor::worldToScreenScale(float worldSize) const
{
    return worldSize * m_viewZoom;
}

// ============================================================
// View Controls
// ============================================================

void FloorPlanEditor::resetView()
{
    m_viewOffset = ImVec2(0, 0);
    m_viewZoom = 50.0f;
}

void FloorPlanEditor::zoomToFit()
{
    // TODO: Calculate bounding box and fit
    resetView();
}

// ============================================================
// Main Render
// ============================================================

void FloorPlanEditor::render()
{
    if (!m_isOpen) return;
    
    ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_FirstUseEver);
    
    if (ImGui::Begin("Floor Plan Editor", &m_isOpen, ImGuiWindowFlags_MenuBar))
    {
        // Use temp building if none set
        Building* bldg = m_building ? m_building : &m_tempBuilding;
        
        // Menu bar
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("File"))
            {
                if (ImGui::MenuItem("New Building")) {
                    bldg->clear();
                    clearSelection();
                }
                if (ImGui::MenuItem("Clear Floor")) {
                    if (Floor* f = bldg->getCurrentFloor()) {
                        f->plan.clear();
                        clearSelection();
                    }
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Reset View")) {
                    resetView();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View"))
            {
                ImGui::Checkbox("Show Grid", &m_showGrid);
                ImGui::Checkbox("Snap to Grid", &m_snapToGrid);
                ImGui::SliderFloat("Grid Size", &m_gridSize, 0.1f, 2.0f, "%.1f m");
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }
        
        // Layout: Toolbar | Canvas | Properties
        float toolbarWidth = 50.0f;
        float propertiesWidth = 200.0f;
        
        // Toolbar (left)
        ImGui::BeginChild("Toolbar", ImVec2(toolbarWidth, 0), true);
        renderToolbar();
        ImGui::EndChild();
        
        ImGui::SameLine();
        
        // Canvas (center)
        float canvasWidth = ImGui::GetContentRegionAvail().x - propertiesWidth - ImGui::GetStyle().ItemSpacing.x;
        ImGui::BeginChild("Canvas", ImVec2(canvasWidth, 0), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        renderFloorSelector();
        renderCanvas();
        ImGui::EndChild();
        
        ImGui::SameLine();
        
        // Properties panel (right)
        ImGui::BeginChild("Properties", ImVec2(propertiesWidth, 0), true);
        renderPropertiesPanel();
        ImGui::EndChild();
    }
    ImGui::End();
}

// ============================================================
// Toolbar
// ============================================================

void FloorPlanEditor::renderToolbar()
{
    auto toolButton = [this](const char* label, FloorPlanTool tool, const char* tooltip) {
        bool selected = (m_currentTool == tool);
        if (selected) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button(label, ImVec2(32, 32))) {
            m_currentTool = tool;
            cancelDrawing();
        }
        if (selected) {
            ImGui::PopStyleColor();
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%s", tooltip);
        }
    };
    
    toolButton("S", FloorPlanTool::Select, "Select (S)");
    toolButton("W", FloorPlanTool::Wall, "Draw Straight Wall (W)");
    toolButton("A", FloorPlanTool::WallArc, "Draw Arc Wall (A) - 3 clicks");
    toolButton("B", FloorPlanTool::WallBezier, "Draw Quadratic Bezier (B) - 3 clicks");
    toolButton("C", FloorPlanTool::WallCubic, "Draw Cubic Bezier (C) - 4 clicks");
    ImGui::Separator();
    toolButton("R", FloorPlanTool::Room, "Draw Room (R)");
    toolButton("D", FloorPlanTool::Door, "Place Door (D)");
    toolButton("N", FloorPlanTool::Window, "Place Window (N)");
    toolButton("F", FloorPlanTool::Furniture, "Place Furniture (F)");
    toolButton("T", FloorPlanTool::Staircase, "Place Staircase (T)");
    ImGui::Separator();
    toolButton("X", FloorPlanTool::Delete, "Delete (Del)");
    toolButton("P", FloorPlanTool::Pan, "Pan View (Space)");
}

// ============================================================
// Floor Selector
// ============================================================

void FloorPlanEditor::renderFloorSelector()
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    
    ImGui::Text("Floor:");
    ImGui::SameLine();
    
    // Floor dropdown
    Floor* current = bldg->getCurrentFloor();
    std::string preview = current ? current->displayName() : "No Floor";
    
    ImGui::SetNextItemWidth(150);
    if (ImGui::BeginCombo("##FloorSelect", preview.c_str()))
    {
        for (size_t i = 0; i < bldg->getFloorCount(); i++)
        {
            Floor& f = bldg->getFloor(i);
            bool isSelected = (bldg->getCurrentFloorIndex() == (int)i);
            if (ImGui::Selectable(f.displayName().c_str(), isSelected)) {
                bldg->setCurrentFloor(static_cast<int>(i));
                clearSelection();
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("+##AddFloor")) {
        bldg->addFloor();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add Floor Above");
    
    ImGui::SameLine();
    if (ImGui::Button("-##RemoveFloor")) {
        if (bldg->getFloorCount() > 1) {
            bldg->removeFloor(bldg->getCurrentFloorIndex());
        }
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Remove Current Floor");
    
    ImGui::Separator();
}

// ============================================================
// Canvas
// ============================================================

void FloorPlanEditor::renderCanvas()
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    // Update floor plan settings
    floor->plan.snapToGrid = m_snapToGrid;
    floor->plan.gridSize = m_gridSize;
    
    // Get canvas region
    m_canvasSize = ImGui::GetContentRegionAvail();
    m_canvasPos = ImGui::GetCursorScreenPos();
    
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    
    // Background
    drawList->AddRectFilled(m_canvasPos, 
        ImVec2(m_canvasPos.x + m_canvasSize.x, m_canvasPos.y + m_canvasSize.y),
        IM_COL32(30, 30, 35, 255));
    
    // Clip to canvas
    drawList->PushClipRect(m_canvasPos, 
        ImVec2(m_canvasPos.x + m_canvasSize.x, m_canvasPos.y + m_canvasSize.y), true);
    
    // Grid
    if (m_showGrid) {
        renderGrid(drawList, m_canvasPos, 
            ImVec2(m_canvasPos.x + m_canvasSize.x, m_canvasPos.y + m_canvasSize.y));
    }
    
    // Origin marker
    ImVec2 origin = worldToScreen(ImVec2(0, 0));
    drawList->AddCircleFilled(origin, 5.0f, IM_COL32(255, 100, 100, 200));
    
    // Render floor plan elements
    renderRooms(drawList);
    renderWalls(drawList);
    renderDoors(drawList);
    renderWindows(drawList);
    renderFurniture(drawList);
    renderStaircases(drawList);
    
    // Current drawing preview
    renderCurrentDrawing(drawList);
    
    // Selection highlight
    renderSelection(drawList);
    
    drawList->PopClipRect();
    
    // Invisible button for input
    ImGui::SetCursorScreenPos(m_canvasPos);
    ImGui::InvisibleButton("FloorPlanCanvas", m_canvasSize, 
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
    
    // Handle input
    handleCanvasInput();
}

// ============================================================
// Grid Rendering
// ============================================================

void FloorPlanEditor::renderGrid(ImDrawList* drawList, ImVec2 canvasMin, ImVec2 canvasMax)
{
    float gridSizeScreen = m_gridSize * m_viewZoom;
    if (gridSizeScreen < 5.0f) return; // Don't draw if too small
    
    ImU32 gridColor = IM_COL32(50, 50, 60, 255);
    ImU32 gridColorMajor = IM_COL32(70, 70, 80, 255);
    
    // Calculate world bounds visible in canvas
    ImVec2 worldMin = screenToWorld(canvasMin);
    ImVec2 worldMax = screenToWorld(canvasMax);
    
    // Swap if needed (Y is flipped)
    if (worldMin.y > worldMax.y) std::swap(worldMin.y, worldMax.y);
    
    // Snap to grid
    float startX = floorf(worldMin.x / m_gridSize) * m_gridSize;
    float startY = floorf(worldMin.y / m_gridSize) * m_gridSize;
    
    // Vertical lines
    for (float x = startX; x <= worldMax.x; x += m_gridSize) {
        ImVec2 p1 = worldToScreen(ImVec2(x, worldMin.y));
        ImVec2 p2 = worldToScreen(ImVec2(x, worldMax.y));
        bool isMajor = (fmodf(fabsf(x), m_gridSize * 5.0f) < 0.001f);
        drawList->AddLine(p1, p2, isMajor ? gridColorMajor : gridColor, isMajor ? 1.5f : 1.0f);
    }
    
    // Horizontal lines
    for (float y = startY; y <= worldMax.y; y += m_gridSize) {
        ImVec2 p1 = worldToScreen(ImVec2(worldMin.x, y));
        ImVec2 p2 = worldToScreen(ImVec2(worldMax.x, y));
        bool isMajor = (fmodf(fabsf(y), m_gridSize * 5.0f) < 0.001f);
        drawList->AddLine(p1, p2, isMajor ? gridColorMajor : gridColor, isMajor ? 1.5f : 1.0f);
    }
}

// ============================================================
// Element Rendering
// ============================================================

void FloorPlanEditor::renderWalls(ImDrawList* drawList)
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    for (const Wall& wall : floor->plan.walls) {
        float thickness = worldToScreenScale(wall.thickness);
        
        // Highlight if selected
        ImU32 color = wall.color;
        if (m_selectionType == SelectionType::Wall && m_selectedId == wall.id) {
            color = IM_COL32(100, 150, 255, 255);
        }
        
        if (wall.isCurved()) {
            // Render curved wall using sampled points
            auto samples = wall.getSampledPoints();
            if (samples.size() >= 2) {
                for (size_t i = 1; i < samples.size(); i++) {
                    ImVec2 p1 = worldToScreen(samples[i - 1]);
                    ImVec2 p2 = worldToScreen(samples[i]);
                    drawList->AddLine(p1, p2, color, thickness);
                }
            }
        } else {
            // Render straight wall
            ImVec2 p1 = worldToScreen(wall.start);
            ImVec2 p2 = worldToScreen(wall.end);
            drawList->AddLine(p1, p2, color, thickness);
        }
    }
}

void FloorPlanEditor::renderRooms(ImDrawList* drawList)
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    for (const Room& room : floor->plan.rooms) {
        if (room.vertices.size() < 3) continue;
        
        // Convert to screen coords
        std::vector<ImVec2> screenVerts;
        for (const auto& v : room.vertices) {
            screenVerts.push_back(worldToScreen(v));
        }
        
        // Highlight if selected
        ImU32 fillColor = room.fillColor;
        ImU32 outlineColor = room.outlineColor;
        if (m_selectionType == SelectionType::Room && m_selectedId == room.id) {
            fillColor = IM_COL32(100, 150, 255, 80);
            outlineColor = IM_COL32(100, 150, 255, 255);
        }
        
        // Fill
        drawList->AddConvexPolyFilled(screenVerts.data(), static_cast<int>(screenVerts.size()), fillColor);
        
        // Outline
        for (size_t i = 0; i < screenVerts.size(); i++) {
            size_t j = (i + 1) % screenVerts.size();
            drawList->AddLine(screenVerts[i], screenVerts[j], outlineColor, 2.0f);
        }
        
        // Room name
        ImVec2 center = worldToScreen(room.centroid());
        ImVec2 textSize = ImGui::CalcTextSize(room.name.c_str());
        drawList->AddText(ImVec2(center.x - textSize.x * 0.5f, center.y - textSize.y * 0.5f),
            IM_COL32(255, 255, 255, 200), room.name.c_str());
    }
}

void FloorPlanEditor::renderDoors(ImDrawList* drawList)
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    for (const Door& door : floor->plan.doors) {
        Wall* wall = floor->plan.findWall(door.wallId);
        if (!wall) continue;
        
        // Calculate door position on wall
        ImVec2 wallDir = ImVec2(wall->end.x - wall->start.x, wall->end.y - wall->start.y);
        ImVec2 doorPos = ImVec2(
            wall->start.x + wallDir.x * door.positionOnWall,
            wall->start.y + wallDir.y * door.positionOnWall
        );
        
        ImVec2 screenPos = worldToScreen(doorPos);
        float doorWidth = worldToScreenScale(door.width);
        
        // Draw door opening (gap in wall)
        float angle = wall->angle();
        ImVec2 perpOffset = ImVec2(-sinf(angle) * doorWidth * 0.5f, cosf(angle) * doorWidth * 0.5f);
        
        ImU32 color = door.color;
        if (m_selectionType == SelectionType::Door && m_selectedId == door.id) {
            color = IM_COL32(100, 150, 255, 255);
        }
        
        // Door arc (swing indicator)
        drawList->AddCircle(screenPos, doorWidth * 0.5f, color, 16, 2.0f);
        drawList->AddRectFilled(
            ImVec2(screenPos.x - doorWidth * 0.5f, screenPos.y - 3),
            ImVec2(screenPos.x + doorWidth * 0.5f, screenPos.y + 3),
            color);
    }
}

void FloorPlanEditor::renderWindows(ImDrawList* drawList)
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    for (const Window& window : floor->plan.windows) {
        Wall* wall = floor->plan.findWall(window.wallId);
        if (!wall) continue;
        
        ImVec2 wallDir = ImVec2(wall->end.x - wall->start.x, wall->end.y - wall->start.y);
        ImVec2 winPos = ImVec2(
            wall->start.x + wallDir.x * window.positionOnWall,
            wall->start.y + wallDir.y * window.positionOnWall
        );
        
        ImVec2 screenPos = worldToScreen(winPos);
        float winWidth = worldToScreenScale(window.width);
        float wallAngle = wall->angle();
        
        ImU32 color = window.color;
        if (m_selectionType == SelectionType::Window && m_selectedId == window.id) {
            color = IM_COL32(100, 150, 255, 200);
        }
        
        // Draw window as a line with breaks
        ImVec2 dir = ImVec2(cosf(wallAngle), sinf(wallAngle));
        ImVec2 p1 = ImVec2(screenPos.x - dir.x * winWidth * 0.5f, screenPos.y - dir.y * winWidth * 0.5f);
        ImVec2 p2 = ImVec2(screenPos.x + dir.x * winWidth * 0.5f, screenPos.y + dir.y * winWidth * 0.5f);
        
        drawList->AddLine(p1, p2, color, 4.0f);
    }
}

void FloorPlanEditor::renderFurniture(ImDrawList* drawList)
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    for (const Furniture& furn : floor->plan.furniture) {
        ImVec2 screenPos = worldToScreen(furn.position);
        ImVec2 screenSize = ImVec2(worldToScreenScale(furn.size.x), worldToScreenScale(furn.size.y));
        
        ImU32 color = furn.color;
        if (m_selectionType == SelectionType::Furniture && m_selectedId == furn.id) {
            color = IM_COL32(100, 150, 255, 255);
        }
        
        // Simple rectangle for now
        ImVec2 halfSize = ImVec2(screenSize.x * 0.5f, screenSize.y * 0.5f);
        drawList->AddRectFilled(
            ImVec2(screenPos.x - halfSize.x, screenPos.y - halfSize.y),
            ImVec2(screenPos.x + halfSize.x, screenPos.y + halfSize.y),
            color);
        drawList->AddRect(
            ImVec2(screenPos.x - halfSize.x, screenPos.y - halfSize.y),
            ImVec2(screenPos.x + halfSize.x, screenPos.y + halfSize.y),
            IM_COL32(255, 255, 255, 100), 0, 0, 1.0f);
    }
}

void FloorPlanEditor::renderStaircases(ImDrawList* drawList)
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    for (const Staircase& stair : floor->plan.staircases) {
        ImVec2 screenPos = worldToScreen(stair.position);
        ImVec2 screenSize = ImVec2(worldToScreenScale(stair.size.x), worldToScreenScale(stair.size.y));
        
        ImU32 color = stair.color;
        if (m_selectionType == SelectionType::Staircase && m_selectedId == stair.id) {
            color = IM_COL32(100, 150, 255, 255);
        }
        
        ImVec2 halfSize = ImVec2(screenSize.x * 0.5f, screenSize.y * 0.5f);
        
        // Staircase outline
        drawList->AddRectFilled(
            ImVec2(screenPos.x - halfSize.x, screenPos.y - halfSize.y),
            ImVec2(screenPos.x + halfSize.x, screenPos.y + halfSize.y),
            color);
        
        // Draw steps
        float stepHeight = screenSize.y / stair.numSteps;
        for (int i = 0; i < stair.numSteps; i++) {
            float y = screenPos.y - halfSize.y + i * stepHeight;
            drawList->AddLine(
                ImVec2(screenPos.x - halfSize.x, y),
                ImVec2(screenPos.x + halfSize.x, y),
                IM_COL32(0, 0, 0, 100), 1.0f);
        }
        
        // Arrow indicating direction
        drawList->AddTriangleFilled(
            ImVec2(screenPos.x, screenPos.y - halfSize.y + 10),
            ImVec2(screenPos.x - 8, screenPos.y - halfSize.y + 25),
            ImVec2(screenPos.x + 8, screenPos.y - halfSize.y + 25),
            IM_COL32(255, 255, 255, 150));
    }
}

void FloorPlanEditor::renderCurrentDrawing(ImDrawList* drawList)
{
    if (!m_isDrawing) return;
    
    if (m_currentTool == FloorPlanTool::Wall) {
        ImVec2 p1 = worldToScreen(m_drawStart);
        ImVec2 p2 = worldToScreen(m_drawCurrent);
        float thickness = worldToScreenScale(m_wallThickness);
        drawList->AddLine(p1, p2, IM_COL32(100, 200, 100, 200), thickness);
    }
    else if (m_currentTool == FloorPlanTool::WallArc || 
             m_currentTool == FloorPlanTool::WallBezier ||
             m_currentTool == FloorPlanTool::WallCubic) {
        // Draw curve preview
        float thickness = worldToScreenScale(m_wallThickness);
        ImU32 previewColor = IM_COL32(100, 200, 100, 200);
        ImU32 controlColor = IM_COL32(255, 200, 100, 255);
        ImU32 lineColor = IM_COL32(255, 200, 100, 100);
        
        // Collect all points: start + control points + current mouse position
        std::vector<ImVec2> pts;
        pts.push_back(m_drawStart);
        for (const auto& cp : m_curveControlPts) {
            pts.push_back(cp);
        }
        pts.push_back(m_drawCurrent);
        
        // Draw control lines (tangent handles)
        for (size_t i = 0; i < pts.size() - 1; i++) {
            ImVec2 sp1 = worldToScreen(pts[i]);
            ImVec2 sp2 = worldToScreen(pts[i + 1]);
            drawList->AddLine(sp1, sp2, lineColor, 1.0f);
        }
        
        // Draw curve preview based on current tool and number of points
        std::vector<ImVec2> samples;
        
        if (m_currentTool == FloorPlanTool::WallArc) {
            if (pts.size() >= 3) {
                // Have enough points for arc preview
                ImVec2 center;
                float radius, startAngle, endAngle;
                if (ArcUtil::arcFrom3Points(pts[0], pts[1], pts[2], center, radius, startAngle, endAngle)) {
                    samples = ArcUtil::sampleArc(center, radius, startAngle, endAngle, m_curveSegments);
                }
            }
        }
        else if (m_currentTool == FloorPlanTool::WallBezier) {
            if (pts.size() >= 3) {
                samples = BezierUtil::sampleQuadratic(pts[0], pts[1], pts[2], m_curveSegments);
            } else if (pts.size() == 2) {
                // Not enough control points yet, show line
                samples = pts;
            }
        }
        else if (m_currentTool == FloorPlanTool::WallCubic) {
            if (pts.size() >= 4) {
                samples = BezierUtil::sampleCubic(pts[0], pts[1], pts[2], pts[3], m_curveSegments);
            } else if (pts.size() == 3) {
                // Show quadratic preview
                samples = BezierUtil::sampleQuadratic(pts[0], pts[1], pts[2], m_curveSegments);
            } else if (pts.size() == 2) {
                samples = pts;
            }
        }
        
        // Draw the sampled curve
        if (samples.size() >= 2) {
            for (size_t i = 1; i < samples.size(); i++) {
                ImVec2 sp1 = worldToScreen(samples[i - 1]);
                ImVec2 sp2 = worldToScreen(samples[i]);
                drawList->AddLine(sp1, sp2, previewColor, thickness);
            }
        }
        
        // Draw control points
        for (const auto& pt : pts) {
            ImVec2 sp = worldToScreen(pt);
            drawList->AddCircleFilled(sp, CONTROL_POINT_RADIUS, controlColor);
            drawList->AddCircle(sp, CONTROL_POINT_RADIUS, IM_COL32(255, 255, 255, 255), 12, 1.5f);
        }
        
        // Show click count hint
        int neededClicks = (m_currentTool == FloorPlanTool::WallCubic) ? 4 : 3;
        int currentClicks = static_cast<int>(pts.size());
        char hint[32];
        snprintf(hint, sizeof(hint), "%d/%d", currentClicks, neededClicks);
        ImVec2 hintPos = ImVec2(m_canvasPos.x + 10, m_canvasPos.y + m_canvasSize.y - 25);
        drawList->AddText(hintPos, IM_COL32(255, 255, 255, 200), hint);
    }
    else if (m_currentTool == FloorPlanTool::Room) {
        // Draw existing vertices
        std::vector<ImVec2> screenVerts;
        for (const auto& v : m_roomVertices) {
            screenVerts.push_back(worldToScreen(v));
        }
        screenVerts.push_back(worldToScreen(m_drawCurrent));
        
        // Draw polygon preview
        if (screenVerts.size() >= 2) {
            for (size_t i = 0; i < screenVerts.size() - 1; i++) {
                drawList->AddLine(screenVerts[i], screenVerts[i + 1], IM_COL32(100, 200, 100, 200), 2.0f);
            }
        }
        
        // Draw closing line to first vertex
        if (screenVerts.size() >= 3) {
            drawList->AddLine(screenVerts.back(), screenVerts[0], IM_COL32(100, 200, 100, 100), 1.0f);
        }
        
        // Draw vertices
        for (const auto& sv : screenVerts) {
            drawList->AddCircleFilled(sv, 5.0f, IM_COL32(100, 200, 100, 255));
        }
    }
}

void FloorPlanEditor::renderSelection(ImDrawList* drawList)
{
    // Selection is already highlighted in individual render functions
    // Additionally render control points for selected curved walls
    if (!m_showControlPoints) return;
    if (m_selectionType != SelectionType::Wall) return;
    
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    Wall* wall = floor->plan.findWall(m_selectedId);
    if (!wall || !wall->isCurved()) return;
    
    // Render control points for selected curved wall
    ImU32 controlColor = IM_COL32(255, 200, 100, 255);
    ImU32 endpointColor = IM_COL32(100, 200, 255, 255);
    
    // Start point
    ImVec2 startScreen = worldToScreen(wall->start);
    drawList->AddCircleFilled(startScreen, CONTROL_POINT_RADIUS, endpointColor);
    drawList->AddCircle(startScreen, CONTROL_POINT_RADIUS, IM_COL32(255, 255, 255, 255), 12, 2.0f);
    
    // Control points
    for (size_t i = 0; i < wall->controlPoints.size(); i++) {
        ImVec2 cpScreen = worldToScreen(wall->controlPoints[i]);
        drawList->AddCircleFilled(cpScreen, CONTROL_POINT_RADIUS, controlColor);
        drawList->AddCircle(cpScreen, CONTROL_POINT_RADIUS, IM_COL32(255, 255, 255, 255), 12, 2.0f);
        
        // Draw tangent lines from endpoints to control points
        if (i == 0) {
            drawList->AddLine(startScreen, cpScreen, IM_COL32(255, 200, 100, 100), 1.0f);
        }
        if (i == wall->controlPoints.size() - 1) {
            ImVec2 endScreen = worldToScreen(wall->end);
            drawList->AddLine(cpScreen, endScreen, IM_COL32(255, 200, 100, 100), 1.0f);
        } else if (i < wall->controlPoints.size() - 1) {
            ImVec2 nextCp = worldToScreen(wall->controlPoints[i + 1]);
            drawList->AddLine(cpScreen, nextCp, IM_COL32(255, 200, 100, 100), 1.0f);
        }
    }
    
    // End point
    ImVec2 endScreen = worldToScreen(wall->end);
    drawList->AddCircleFilled(endScreen, CONTROL_POINT_RADIUS, endpointColor);
    drawList->AddCircle(endScreen, CONTROL_POINT_RADIUS, IM_COL32(255, 255, 255, 255), 12, 2.0f);
}

void FloorPlanEditor::renderControlPoints(ImDrawList* drawList)
{
    // This is now handled within renderSelection for the selected wall
}

void FloorPlanEditor::updateControlPointDrag(ImVec2 worldPos)
{
    if (!m_isDraggingControlPoint || m_draggedControlPoint < 0) return;
    if (m_selectionType != SelectionType::Wall) return;
    
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    Wall* wall = floor->plan.findWall(m_selectedId);
    if (!wall) return;
    
    if (m_snapToGrid) {
        worldPos.x = roundf(worldPos.x / m_gridSize) * m_gridSize;
        worldPos.y = roundf(worldPos.y / m_gridSize) * m_gridSize;
    }
    
    if (m_draggedControlPoint == 0) {
        wall->start = worldPos;
    } else if (m_draggedControlPoint == static_cast<int>(wall->controlPoints.size()) + 1) {
        wall->end = worldPos;
    } else {
        int cpIndex = m_draggedControlPoint - 1;
        if (cpIndex >= 0 && cpIndex < static_cast<int>(wall->controlPoints.size())) {
            wall->controlPoints[cpIndex] = worldPos;
        }
    }
}

int FloorPlanEditor::findControlPointAt(ImVec2 worldPos, float tolerance)
{
    if (m_selectionType != SelectionType::Wall) return -1;
    
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return -1;
    
    Wall* wall = floor->plan.findWall(m_selectedId);
    if (!wall) return -1;
    
    // Check start point
    float dx = worldPos.x - wall->start.x;
    float dy = worldPos.y - wall->start.y;
    if (sqrtf(dx * dx + dy * dy) < tolerance) return 0;
    
    // Check control points
    for (size_t i = 0; i < wall->controlPoints.size(); i++) {
        dx = worldPos.x - wall->controlPoints[i].x;
        dy = worldPos.y - wall->controlPoints[i].y;
        if (sqrtf(dx * dx + dy * dy) < tolerance) return static_cast<int>(i) + 1;
    }
    
    // Check end point  
    dx = worldPos.x - wall->end.x;
    dy = worldPos.y - wall->end.y;
    if (sqrtf(dx * dx + dy * dy) < tolerance) return static_cast<int>(wall->controlPoints.size()) + 1;
    
    return -1;
}

// ============================================================
// Input Handling
// ============================================================

void FloorPlanEditor::handleCanvasInput()
{
    bool isHovered = ImGui::IsItemHovered();
    bool isActive = ImGui::IsItemActive();
    
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mousePos = io.MousePos;
    ImVec2 worldPos = screenToWorld(mousePos);
    
    // Snap to grid if enabled
    if (m_snapToGrid) {
        worldPos.x = roundf(worldPos.x / m_gridSize) * m_gridSize;
        worldPos.y = roundf(worldPos.y / m_gridSize) * m_gridSize;
    }
    
    // Pan with middle mouse
    if (isHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        m_viewOffset.x += io.MouseDelta.x;
        m_viewOffset.y += io.MouseDelta.y;
    }
    
    // Pan with right mouse in Pan tool
    if (isHovered && m_currentTool == FloorPlanTool::Pan && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        m_viewOffset.x += io.MouseDelta.x;
        m_viewOffset.y += io.MouseDelta.y;
    }
    
    // Zoom with scroll
    if (isHovered && io.MouseWheel != 0) {
        float zoomDelta = io.MouseWheel * m_viewZoom * ZOOM_SPEED;
        float newZoom = std::clamp(m_viewZoom + zoomDelta, MIN_ZOOM, MAX_ZOOM);
        
        // Zoom toward mouse position
        ImVec2 mouseWorldBefore = screenToWorld(mousePos);
        m_viewZoom = newZoom;
        ImVec2 mouseWorldAfter = screenToWorld(mousePos);
        
        m_viewOffset.x += (mouseWorldAfter.x - mouseWorldBefore.x) * m_viewZoom;
        m_viewOffset.y -= (mouseWorldAfter.y - mouseWorldBefore.y) * m_viewZoom;
    }
    
    // Keyboard shortcuts
    if (isHovered) {
        if (ImGui::IsKeyPressed(ImGuiKey_S)) m_currentTool = FloorPlanTool::Select;
        if (ImGui::IsKeyPressed(ImGuiKey_W)) m_currentTool = FloorPlanTool::Wall;
        if (ImGui::IsKeyPressed(ImGuiKey_A)) m_currentTool = FloorPlanTool::WallArc;
        if (ImGui::IsKeyPressed(ImGuiKey_B)) m_currentTool = FloorPlanTool::WallBezier;
        if (ImGui::IsKeyPressed(ImGuiKey_C)) m_currentTool = FloorPlanTool::WallCubic;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) m_currentTool = FloorPlanTool::Room;
        if (ImGui::IsKeyPressed(ImGuiKey_D)) m_currentTool = FloorPlanTool::Door;
        if (ImGui::IsKeyPressed(ImGuiKey_N)) m_currentTool = FloorPlanTool::Window;
        if (ImGui::IsKeyPressed(ImGuiKey_F)) m_currentTool = FloorPlanTool::Furniture;
        if (ImGui::IsKeyPressed(ImGuiKey_T)) m_currentTool = FloorPlanTool::Staircase;
        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) deleteSelection();
        if (ImGui::IsKeyPressed(ImGuiKey_Escape)) cancelDrawing();
    }
    
    // Tool input
    handleToolInput();
}

void FloorPlanEditor::handleToolInput()
{
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mousePos = io.MousePos;
    ImVec2 worldPos = screenToWorld(mousePos);
    
    if (m_snapToGrid) {
        worldPos.x = roundf(worldPos.x / m_gridSize) * m_gridSize;
        worldPos.y = roundf(worldPos.y / m_gridSize) * m_gridSize;
    }
    
    bool clicked = ImGui::IsMouseClicked(ImGuiMouseButton_Left) && ImGui::IsItemHovered();
    bool released = ImGui::IsMouseReleased(ImGuiMouseButton_Left);
    bool rightClicked = ImGui::IsMouseClicked(ImGuiMouseButton_Right) && ImGui::IsItemHovered();
    
    switch (m_currentTool)
    {
    case FloorPlanTool::Select:
        if (clicked) {
            selectAt(worldPos);
        }
        break;
        
    case FloorPlanTool::Wall:
        if (clicked && !m_isDrawing) {
            startWallDrawing(worldPos);
        } else if (m_isDrawing) {
            updateWallDrawing(worldPos);
            if (released) {
                finishWallDrawing();
            }
        }
        break;
    
    case FloorPlanTool::WallArc:
    case FloorPlanTool::WallBezier:
    case FloorPlanTool::WallCubic:
        if (clicked) {
            if (!m_isDrawing) {
                startCurvedWallDrawing(worldPos);
            } else {
                addCurveControlPoint(worldPos);
            }
        }
        if (rightClicked && m_isDrawing) {
            cancelDrawing();
        }
        if (m_isDrawing) {
            m_drawCurrent = worldPos;
        }
        break;
        
    case FloorPlanTool::Room:
        if (clicked) {
            if (!m_isDrawing) {
                startRoomDrawing(worldPos);
            } else {
                addRoomVertex(worldPos);
            }
        }
        if (rightClicked && m_isDrawing) {
            finishRoomDrawing();
        }
        if (m_isDrawing) {
            m_drawCurrent = worldPos;
        }
        break;
        
    case FloorPlanTool::Door:
        if (clicked) {
            placeDoor(worldPos);
        }
        break;
        
    case FloorPlanTool::Window:
        if (clicked) {
            placeWindow(worldPos);
        }
        break;
        
    case FloorPlanTool::Furniture:
        if (clicked) {
            placeFurniture(worldPos);
        }
        break;
        
    case FloorPlanTool::Staircase:
        if (clicked) {
            placeStaircase(worldPos);
        }
        break;
        
    case FloorPlanTool::Delete:
        if (clicked) {
            deleteAt(worldPos);
        }
        break;
        
    case FloorPlanTool::Pan:
        // Handled in handleCanvasInput
        break;
    }
}

// ============================================================
// Tool Actions
// ============================================================

void FloorPlanEditor::startWallDrawing(ImVec2 worldPos)
{
    m_isDrawing = true;
    m_drawStart = worldPos;
    m_drawCurrent = worldPos;
}

void FloorPlanEditor::updateWallDrawing(ImVec2 worldPos)
{
    m_drawCurrent = worldPos;
}

void FloorPlanEditor::finishWallDrawing()
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    // Only add wall if length > 0
    float dx = m_drawCurrent.x - m_drawStart.x;
    float dy = m_drawCurrent.y - m_drawStart.y;
    if (sqrtf(dx * dx + dy * dy) > 0.01f) {
        floor->plan.addWall(m_drawStart, m_drawCurrent, m_wallThickness);
    }
    
    m_isDrawing = false;
}

void FloorPlanEditor::cancelDrawing()
{
    m_isDrawing = false;
    m_roomVertices.clear();
    m_curveControlPts.clear();
    m_curveClickCount = 0;
}

void FloorPlanEditor::startCurvedWallDrawing(ImVec2 worldPos)
{
    m_isDrawing = true;
    m_drawStart = worldPos;
    m_drawCurrent = worldPos;
    m_curveControlPts.clear();
    m_curveClickCount = 1;
}

void FloorPlanEditor::addCurveControlPoint(ImVec2 worldPos)
{
    int neededClicks = (m_currentTool == FloorPlanTool::WallCubic) ? 4 : 3;
    m_curveClickCount++;
    
    if (m_curveClickCount < neededClicks) {
        // Add as control point
        m_curveControlPts.push_back(worldPos);
    } else {
        // Final click - finish the wall with this as end point
        finishCurvedWallDrawing();
    }
}

void FloorPlanEditor::finishCurvedWallDrawing()
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    // Create the wall
    Wall wall;
    wall.id = floor->plan.nextWallId++;
    wall.start = m_drawStart;
    wall.end = m_drawCurrent;
    wall.thickness = m_wallThickness;
    wall.curveSegments = m_curveSegments;
    
    // Set curve type and control points
    switch (m_currentTool) {
        case FloorPlanTool::WallArc:
            if (m_curveControlPts.size() >= 1) {
                wall.setArc(m_curveControlPts[0]);
            }
            break;
        case FloorPlanTool::WallBezier:
            if (m_curveControlPts.size() >= 1) {
                wall.setQuadraticBezier(m_curveControlPts[0]);
            }
            break;
        case FloorPlanTool::WallCubic:
            if (m_curveControlPts.size() >= 2) {
                wall.setCubicBezier(m_curveControlPts[0], m_curveControlPts[1]);
            } else if (m_curveControlPts.size() == 1) {
                wall.setQuadraticBezier(m_curveControlPts[0]);
            }
            break;
        default:
            break;
    }
    
    floor->plan.walls.push_back(wall);
    
    // Reset drawing state
    m_isDrawing = false;
    m_curveControlPts.clear();
    m_curveClickCount = 0;
}

void FloorPlanEditor::startRoomDrawing(ImVec2 worldPos)
{
    m_isDrawing = true;
    m_roomVertices.clear();
    m_roomVertices.push_back(worldPos);
    m_drawCurrent = worldPos;
}

void FloorPlanEditor::addRoomVertex(ImVec2 worldPos)
{
    // Check if clicking near first vertex to close
    if (m_roomVertices.size() >= 3) {
        float dx = worldPos.x - m_roomVertices[0].x;
        float dy = worldPos.y - m_roomVertices[0].y;
        float closeThreshold = m_gridSize * 0.5f;
        if (sqrtf(dx * dx + dy * dy) < closeThreshold) {
            finishRoomDrawing();
            return;
        }
    }
    
    m_roomVertices.push_back(worldPos);
}

void FloorPlanEditor::finishRoomDrawing()
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    if (m_roomVertices.size() >= 3) {
        floor->plan.addRoom(m_roomVertices, "Room");
    }
    
    m_isDrawing = false;
    m_roomVertices.clear();
}

void FloorPlanEditor::placeDoor(ImVec2 worldPos)
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    // Find wall at position
    Wall* wall = floor->plan.findWallAt(worldPos, 0.3f);
    if (!wall) return;
    
    // Calculate position on wall (0-1)
    float wallLen = wall->length();
    if (wallLen < 0.01f) return;
    
    float dx = worldPos.x - wall->start.x;
    float dy = worldPos.y - wall->start.y;
    float wallDx = wall->end.x - wall->start.x;
    float wallDy = wall->end.y - wall->start.y;
    float t = (dx * wallDx + dy * wallDy) / (wallLen * wallLen);
    t = std::clamp(t, 0.0f, 1.0f);
    
    Door door;
    door.id = floor->plan.nextDoorId++;
    door.wallId = wall->id;
    door.positionOnWall = t;
    door.width = 0.9f;
    floor->plan.doors.push_back(door);
}

void FloorPlanEditor::placeWindow(ImVec2 worldPos)
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    Wall* wall = floor->plan.findWallAt(worldPos, 0.3f);
    if (!wall) return;
    
    float wallLen = wall->length();
    if (wallLen < 0.01f) return;
    
    float dx = worldPos.x - wall->start.x;
    float dy = worldPos.y - wall->start.y;
    float wallDx = wall->end.x - wall->start.x;
    float wallDy = wall->end.y - wall->start.y;
    float t = (dx * wallDx + dy * wallDy) / (wallLen * wallLen);
    t = std::clamp(t, 0.0f, 1.0f);
    
    Window window;
    window.id = floor->plan.nextWindowId++;
    window.wallId = wall->id;
    window.positionOnWall = t;
    window.width = 1.2f;
    floor->plan.windows.push_back(window);
}

void FloorPlanEditor::placeFurniture(ImVec2 worldPos)
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    Furniture furn;
    furn.id = floor->plan.nextFurnitureId++;
    furn.position = worldPos;
    furn.size = ImVec2(0.5f, 0.5f);
    furn.type = m_furnitureType;
    furn.name = m_furnitureType;
    floor->plan.furniture.push_back(furn);
}

void FloorPlanEditor::placeStaircase(ImVec2 worldPos)
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    Staircase stair;
    stair.id = floor->plan.nextStaircaseId++;
    stair.position = worldPos;
    stair.size = ImVec2(1.0f, 3.0f);
    floor->plan.staircases.push_back(stair);
}

void FloorPlanEditor::selectAt(ImVec2 worldPos)
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    clearSelection();
    
    // Check walls
    if (Wall* wall = floor->plan.findWallAt(worldPos, 0.2f)) {
        m_selectionType = SelectionType::Wall;
        m_selectedId = wall->id;
        return;
    }
    
    // Check rooms
    if (Room* room = floor->plan.findRoomAt(worldPos)) {
        m_selectionType = SelectionType::Room;
        m_selectedId = room->id;
        return;
    }
    
    // Check furniture
    for (auto& furn : floor->plan.furniture) {
        ImVec2 half = ImVec2(furn.size.x * 0.5f, furn.size.y * 0.5f);
        if (worldPos.x >= furn.position.x - half.x && worldPos.x <= furn.position.x + half.x &&
            worldPos.y >= furn.position.y - half.y && worldPos.y <= furn.position.y + half.y) {
            m_selectionType = SelectionType::Furniture;
            m_selectedId = furn.id;
            return;
        }
    }
    
    // Check staircases
    for (auto& stair : floor->plan.staircases) {
        ImVec2 half = ImVec2(stair.size.x * 0.5f, stair.size.y * 0.5f);
        if (worldPos.x >= stair.position.x - half.x && worldPos.x <= stair.position.x + half.x &&
            worldPos.y >= stair.position.y - half.y && worldPos.y <= stair.position.y + half.y) {
            m_selectionType = SelectionType::Staircase;
            m_selectedId = stair.id;
            return;
        }
    }
}

void FloorPlanEditor::deleteAt(ImVec2 worldPos)
{
    selectAt(worldPos);
    deleteSelection();
}

void FloorPlanEditor::deleteSelection()
{
    if (m_selectionType == SelectionType::None) return;
    
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    switch (m_selectionType) {
    case SelectionType::Wall:
        floor->plan.removeWall(m_selectedId);
        break;
    case SelectionType::Room:
        floor->plan.removeRoom(m_selectedId);
        break;
    case SelectionType::Door:
        floor->plan.doors.erase(
            std::remove_if(floor->plan.doors.begin(), floor->plan.doors.end(),
                [this](const Door& d) { return d.id == m_selectedId; }),
            floor->plan.doors.end());
        break;
    case SelectionType::Window:
        floor->plan.windows.erase(
            std::remove_if(floor->plan.windows.begin(), floor->plan.windows.end(),
                [this](const Window& w) { return w.id == m_selectedId; }),
            floor->plan.windows.end());
        break;
    case SelectionType::Furniture:
        floor->plan.furniture.erase(
            std::remove_if(floor->plan.furniture.begin(), floor->plan.furniture.end(),
                [this](const Furniture& f) { return f.id == m_selectedId; }),
            floor->plan.furniture.end());
        break;
    case SelectionType::Staircase:
        floor->plan.staircases.erase(
            std::remove_if(floor->plan.staircases.begin(), floor->plan.staircases.end(),
                [this](const Staircase& s) { return s.id == m_selectedId; }),
            floor->plan.staircases.end());
        break;
    default:
        break;
    }
    
    clearSelection();
}

void FloorPlanEditor::clearSelection()
{
    m_selectionType = SelectionType::None;
    m_selectedId = -1;
}

// ============================================================
// Properties Panel
// ============================================================

void FloorPlanEditor::renderPropertiesPanel()
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    
    // Building info
    ImGui::Text("Building");
    ImGui::Separator();
    
    static char nameBuf[128];
    strncpy(nameBuf, bldg->getName().c_str(), sizeof(nameBuf) - 1);
    if (ImGui::InputText("Name##Building", nameBuf, sizeof(nameBuf))) {
        bldg->setName(nameBuf);
    }
    
    ImGui::Text("Floors: %zu", bldg->getFloorCount());
    ImGui::Spacing();
    
    // Floor info
    if (floor) {
        ImGui::Text("Current Floor");
        ImGui::Separator();
        
        static char floorNameBuf[128];
        strncpy(floorNameBuf, floor->name.c_str(), sizeof(floorNameBuf) - 1);
        if (ImGui::InputText("Name##Floor", floorNameBuf, sizeof(floorNameBuf))) {
            floor->name = floorNameBuf;
        }
        
        ImGui::DragFloat("Height", &floor->height, 0.1f, 2.0f, 10.0f, "%.1f m");
        ImGui::Text("Level: %d", floor->level);
        ImGui::Spacing();
        
        // Stats
        ImGui::Text("Elements:");
        ImGui::BulletText("Walls: %zu", floor->plan.walls.size());
        ImGui::BulletText("Rooms: %zu", floor->plan.rooms.size());
        ImGui::BulletText("Doors: %zu", floor->plan.doors.size());
        ImGui::BulletText("Windows: %zu", floor->plan.windows.size());
        ImGui::BulletText("Furniture: %zu", floor->plan.furniture.size());
        ImGui::BulletText("Stairs: %zu", floor->plan.staircases.size());
    }
    
    ImGui::Spacing();
    ImGui::Separator();
    
    // Tool settings
    ImGui::Text("Tool Settings");
    ImGui::Separator();
    
    if (m_currentTool == FloorPlanTool::Wall) {
        ImGui::DragFloat("Wall Thickness", &m_wallThickness, 0.01f, 0.05f, 0.5f, "%.2f m");
    }
    
    // Selection properties
    if (m_selectionType != SelectionType::None && floor) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Text("Selection");
        ImGui::Separator();
        
        switch (m_selectionType) {
        case SelectionType::Wall:
            if (Wall* w = floor->plan.findWall(m_selectedId)) {
                ImGui::Text("Wall #%d", w->id);
                ImGui::Text("Length: %.2f m", w->length());
                ImGui::DragFloat("Thickness##Wall", &w->thickness, 0.01f, 0.05f, 0.5f, "%.2f m");
                
                // Curve type info
                const char* typeNames[] = { "Straight", "Quadratic Bezier", "Cubic Bezier", "Arc" };
                ImGui::Text("Type: %s", typeNames[static_cast<int>(w->type)]);
                
                if (w->isCurved()) {
                    ImGui::Checkbox("Show Control Points", &m_showControlPoints);
                    ImGui::SliderInt("Curve Segments", &w->curveSegments, 5, 50);
                    
                    ImGui::Text("Control Points: %zu", w->controlPoints.size());
                    
                    // Edit control points
                    for (size_t i = 0; i < w->controlPoints.size(); i++) {
                        char label[32];
                        snprintf(label, sizeof(label), "CP %zu", i + 1);
                        ImGui::DragFloat2(label, &w->controlPoints[i].x, 0.1f);
                    }
                    
                    // Convert to straight
                    if (ImGui::Button("Convert to Straight")) {
                        w->setStraight();
                    }
                } else {
                    // Convert straight wall to curve
                    ImGui::Separator();
                    ImGui::Text("Convert to:");
                    if (ImGui::Button("Quadratic Bezier")) {
                        // Add control point at midpoint
                        ImVec2 mid = w->midpoint();
                        w->setQuadraticBezier(mid);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cubic Bezier")) {
                        // Add two control points at 1/3 and 2/3
                        ImVec2 p1 = ImVec2(
                            w->start.x + (w->end.x - w->start.x) * 0.33f,
                            w->start.y + (w->end.y - w->start.y) * 0.33f
                        );
                        ImVec2 p2 = ImVec2(
                            w->start.x + (w->end.x - w->start.x) * 0.66f,
                            w->start.y + (w->end.y - w->start.y) * 0.66f
                        );
                        w->setCubicBezier(p1, p2);
                    }
                    if (ImGui::Button("Arc")) {
                        // Add control point perpendicular to midpoint
                        ImVec2 mid = w->midpoint();
                        ImVec2 normal = w->normal();
                        float offset = w->length() * 0.25f;
                        ImVec2 through = ImVec2(mid.x + normal.x * offset, mid.y + normal.y * offset);
                        w->setArc(through);
                    }
                }
            }
            break;
        case SelectionType::Room:
            if (Room* r = floor->plan.findRoom(m_selectedId)) {
                static char roomNameBuf[128];
                strncpy(roomNameBuf, r->name.c_str(), sizeof(roomNameBuf) - 1);
                if (ImGui::InputText("Name##Room", roomNameBuf, sizeof(roomNameBuf))) {
                    r->name = roomNameBuf;
                }
                ImGui::Text("Area: %.2f m²", r->area());
            }
            break;
        default:
            ImGui::Text("ID: %d", m_selectedId);
            break;
        }
        
        if (ImGui::Button("Delete Selection")) {
            deleteSelection();
        }
    }
    
    // Zoom info
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Text("View: %.0f%%", m_viewZoom * 2.0f);
    if (ImGui::Button("Reset View")) {
        resetView();
    }
}
