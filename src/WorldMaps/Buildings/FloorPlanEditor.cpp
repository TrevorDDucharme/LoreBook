#include <WorldMaps/Buildings/FloorPlanEditor.hpp>
#include <WorldMaps/Buildings/FloorPlan.hpp>
#include <Vault.hpp>
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
                if (ImGui::MenuItem("Save Floor as Template...", nullptr, false, m_vault != nullptr)) {
                    if (Floor* f = bldg->getCurrentFloor()) {
                        m_showSaveTemplateModal = true;
                        // Pre-fill name from building name if available
                        std::string defaultName = bldg->getName();
                        if (defaultName.empty()) defaultName = "Untitled";
                        snprintf(m_templateName, sizeof(m_templateName), "%s", defaultName.c_str());
                        m_templateCategory[0] = '\0';
                        m_templateTags[0] = '\0';
                        m_templateStatusMsg.clear();
                    }
                }
                if (ImGui::MenuItem("Load Template...", nullptr, false, m_vault != nullptr)) {
                    m_showLoadTemplateModal = true;
                    m_templateStatusMsg.clear();
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
        
        // ------ Save Template Modal ------
        if (m_showSaveTemplateModal) {
            ImGui::OpenPopup("Save Floor as Template");
            m_showSaveTemplateModal = false;
        }
        if (ImGui::BeginPopupModal("Save Floor as Template", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::InputText("Name", m_templateName, sizeof(m_templateName));
            ImGui::InputText("Category", m_templateCategory, sizeof(m_templateCategory));
            ImGui::InputText("Tags", m_templateTags, sizeof(m_templateTags));
            ImGui::TextDisabled("(Tags: comma-separated, e.g. \"tavern, medieval, large\")");
            
            if (!m_templateStatusMsg.empty()) {
                bool isError = m_templateStatusMsg.find("Failed") != std::string::npos 
                            || m_templateStatusMsg.find("Error") != std::string::npos
                            || m_templateStatusMsg.find("No ") != std::string::npos;
                ImVec4 color = isError ? ImVec4(1.0f, 0.3f, 0.3f, 1.0f) : ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
                ImGui::TextColored(color, "%s", m_templateStatusMsg.c_str());
            }
            
            ImGui::Spacing();
            if (ImGui::Button("Save", ImVec2(120, 0))) {
                if (!m_vault) {
                    m_templateStatusMsg = "No vault connected.";
                } else if (strlen(m_templateName) == 0) {
                    m_templateStatusMsg = "No name specified.";
                } else {
                    Floor* f = bldg->getCurrentFloor();
                    if (!f) {
                        m_templateStatusMsg = "No floor selected.";
                    } else {
                        std::string json = FloorPlanJSON::serialize(f->plan);
                        int64_t id = m_vault->saveFloorPlanTemplate(
                            m_templateName, m_templateCategory, json, m_templateTags);
                        if (id > 0) {
                            m_templateStatusMsg = "Saved as template #" + std::to_string(id);
                        } else {
                            m_templateStatusMsg = "Failed to save (DB error, check log). JSON size=" + std::to_string(json.size());
                        }
                    }
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Close", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        
        // ------ Load Template Modal ------
        if (m_showLoadTemplateModal) {
            ImGui::OpenPopup("Load Template");
            m_showLoadTemplateModal = false;
        }
        if (ImGui::BeginPopupModal("Load Template", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            static std::vector<FloorPlanTemplateInfo> templates;
            static bool needsRefresh = true;
            static char filterCategory[64] = "";
            
            if (needsRefresh && m_vault) {
                templates = m_vault->listFloorPlanTemplates(filterCategory);
                needsRefresh = false;
            }
            
            ImGui::InputText("Filter Category", filterCategory, sizeof(filterCategory));
            ImGui::SameLine();
            if (ImGui::Button("Refresh")) {
                needsRefresh = true;
            }
            
            if (!m_templateStatusMsg.empty()) {
                ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.2f, 1.0f), "%s", m_templateStatusMsg.c_str());
            }
            
            ImGui::Separator();
            ImGui::BeginChild("TemplateList", ImVec2(400, 300), true);
            for (auto& t : templates) {
                ImGui::PushID(static_cast<int>(t.id));
                bool selected = false;
                std::string label = t.name;
                if (!t.category.empty()) label += "  [" + t.category + "]";
                if (!t.tags.empty()) label += "  (" + t.tags + ")";
                
                if (ImGui::Selectable(label.c_str(), &selected, ImGuiSelectableFlags_AllowDoubleClick)) {
                    if (ImGui::IsMouseDoubleClicked(0)) {
                        // Load on double-click
                        if (m_vault) {
                            std::string json = m_vault->loadFloorPlanTemplate(t.id);
                            if (!json.empty()) {
                                Floor* f = bldg->getCurrentFloor();
                                if (f) {
                                    f->plan = FloorPlanJSON::deserialize(json);
                                    clearSelection();
                                    m_templateStatusMsg = "Loaded: " + t.name;
                                }
                            } else {
                                m_templateStatusMsg = "Failed to load template.";
                            }
                        }
                    }
                }
                ImGui::SameLine(ImGui::GetContentRegionAvail().x - 50);
                if (ImGui::SmallButton("Load")) {
                    if (m_vault) {
                        std::string json = m_vault->loadFloorPlanTemplate(t.id);
                        if (!json.empty()) {
                            Floor* f = bldg->getCurrentFloor();
                            if (f) {
                                f->plan = FloorPlanJSON::deserialize(json);
                                clearSelection();
                                m_templateStatusMsg = "Loaded: " + t.name;
                            }
                        } else {
                            m_templateStatusMsg = "Failed to load template.";
                        }
                    }
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Del")) {
                    if (m_vault && m_vault->deleteFloorPlanTemplate(t.id)) {
                        needsRefresh = true;
                        m_templateStatusMsg = "Deleted: " + t.name;
                    }
                }
                ImGui::PopID();
            }
            if (templates.empty()) {
                ImGui::TextDisabled("No templates found.");
            }
            ImGui::EndChild();
            
            ImGui::Spacing();
            if (ImGui::Button("Close", ImVec2(120, 0))) {
                needsRefresh = true;  // refresh next time we open
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
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
    toolButton("L", FloorPlanTool::WallBSpline, "Draw B-Spline (L) - approximating curve, does NOT pass through points");
    toolButton("Z", FloorPlanTool::WallBezierSpline, "Draw Bezier Spline (Z) - interpolating curve, passes through points");
    ImGui::Separator();
    toolButton("R", FloorPlanTool::Room, "Draw Room (R)");
    toolButton("E", FloorPlanTool::RoomFromWalls, "Room from Walls (E) - click inside enclosed walls");
    toolButton("K", FloorPlanTool::RoomCurvedEdge, "Curve Room Edge (K) - select room, then click edge");
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
    if (ImGui::Button("B##AddBasement")) {
        bldg->addBasement();
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Add Basement Below");
    
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
        
        // Get boundary points - use wall geometry if available, otherwise room's own curves
        std::vector<ImVec2> worldBoundary;
        if (!room.wallIds.empty()) {
            worldBoundary = floor->plan.getRoomBoundaryWithWalls(room, m_curveSegments);
        } else if (room.hasCurvedEdges()) {
            worldBoundary = room.getSampledBoundary(m_curveSegments);
        }
        
        // Convert to screen coords
        std::vector<ImVec2> screenVerts;
        if (!worldBoundary.empty()) {
            for (const auto& v : worldBoundary) {
                screenVerts.push_back(worldToScreen(v));
            }
            // Remove duplicate closing point for polygon fill
            if (screenVerts.size() > 1) {
                screenVerts.pop_back();
            }
        } else {
            // Fallback to straight vertices
            for (const auto& v : room.vertices) {
                screenVerts.push_back(worldToScreen(v));
            }
        }
        
        if (screenVerts.size() < 3) continue;
        
        // Highlight if selected
        ImU32 fillColor = room.fillColor;
        ImU32 outlineColor = room.outlineColor;
        if (m_selectionType == SelectionType::Room && m_selectedId == room.id) {
            fillColor = IM_COL32(100, 150, 255, 80);
            outlineColor = IM_COL32(100, 150, 255, 255);
        }
        
        // Fill - use PathFillConvex for simple shapes, triangulation for complex
        // For rooms with curved edges, we need to use AddConcavePolyFilled or triangulate
        if (screenVerts.size() <= 64 && !room.hasCurvedEdges() && room.wallIds.empty()) {
            // Simple convex polygon
            drawList->AddConvexPolyFilled(screenVerts.data(), static_cast<int>(screenVerts.size()), fillColor);
        } else {
            // For complex/curved rooms, draw as a series of triangles from centroid
            ImVec2 center = worldToScreen(room.centroid());
            for (size_t i = 0; i < screenVerts.size(); i++) {
                size_t j = (i + 1) % screenVerts.size();
                drawList->AddTriangleFilled(center, screenVerts[i], screenVerts[j], fillColor);
            }
        }
        
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
        
        // Calculate door position on wall using parametric evaluation
        ImVec2 doorPos = wall->pointAt(door.positionOnWall);
        ImVec2 tangent = wall->tangentAt(door.positionOnWall);
        
        ImVec2 screenPos = worldToScreen(doorPos);
        float doorWidth = worldToScreenScale(door.width);
        
        // Get wall angle from tangent for proper orientation
        // Note: tangent.y is in world coords (Y up), but screen has Y down, so negate Y
        float wallAngle = atan2f(-tangent.y, tangent.x);
        
        ImU32 color = door.color;
        if (m_selectionType == SelectionType::Door && m_selectedId == door.id) {
            color = IM_COL32(100, 150, 255, 255);
        }
        
        // Draw door using tangent direction (in screen space)
        ImVec2 dir = ImVec2(cosf(wallAngle), sinf(wallAngle));
        ImVec2 p1 = ImVec2(screenPos.x - dir.x * doorWidth * 0.5f, screenPos.y - dir.y * doorWidth * 0.5f);
        ImVec2 p2 = ImVec2(screenPos.x + dir.x * doorWidth * 0.5f, screenPos.y + dir.y * doorWidth * 0.5f);
        
        // Door arc (swing indicator)
        drawList->AddCircle(screenPos, doorWidth * 0.5f, color, 16, 2.0f);
        drawList->AddLine(p1, p2, color, 4.0f);
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
        
        // Calculate window position using parametric evaluation
        ImVec2 winPos = wall->pointAt(window.positionOnWall);
        ImVec2 tangent = wall->tangentAt(window.positionOnWall);
        
        ImVec2 screenPos = worldToScreen(winPos);
        float winWidth = worldToScreenScale(window.width);
        // Note: tangent.y is in world coords (Y up), but screen has Y down, so negate Y
        float wallAngle = atan2f(-tangent.y, tangent.x);
        
        ImU32 color = window.color;
        if (m_selectionType == SelectionType::Window && m_selectedId == window.id) {
            color = IM_COL32(100, 150, 255, 200);
        }
        
        // Draw window as a line along the wall tangent (in screen space)
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
        ImU32 color = furn.color;
        if (m_selectionType == SelectionType::Furniture && m_selectedId == furn.id) {
            color = IM_COL32(100, 150, 255, 255);
        }
        
        // Get rotated corners in world space, then convert to screen
        ImVec2 worldCorners[4];
        furn.getCorners(worldCorners);
        ImVec2 screenCorners[4];
        for (int i = 0; i < 4; i++) {
            screenCorners[i] = worldToScreen(worldCorners[i]);
        }
        
        // Draw filled quad
        drawList->AddQuadFilled(
            screenCorners[0], screenCorners[1], screenCorners[2], screenCorners[3],
            color);
        // Draw outline
        drawList->AddQuad(
            screenCorners[0], screenCorners[1], screenCorners[2], screenCorners[3],
            IM_COL32(255, 255, 255, 100), 1.0f);
        
        // Draw rotation indicator (small line from center towards "front")
        ImVec2 screenPos = worldToScreen(furn.position);
        float indicatorLen = worldToScreenScale(furn.size.y * 0.4f);
        float c = cosf(furn.rotation);
        float s = sinf(furn.rotation);
        // Note: negative s for screen Y-flip
        ImVec2 front = ImVec2(screenPos.x + s * indicatorLen, screenPos.y - c * indicatorLen);
        drawList->AddLine(screenPos, front, IM_COL32(255, 255, 255, 200), 2.0f);
    }
}

void FloorPlanEditor::renderStaircases(ImDrawList* drawList)
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    for (const Staircase& stair : floor->plan.staircases) {
        ImU32 color = stair.color;
        if (m_selectionType == SelectionType::Staircase && m_selectedId == stair.id) {
            color = IM_COL32(100, 150, 255, 255);
        }
        
        // Sample points along the staircase centerline
        float width = stair.width;
        int numSegments = stair.numSteps;
        std::vector<ImVec2> centerPts;
        std::vector<ImVec2> leftPts;
        std::vector<ImVec2> rightPts;
        
        for (int i = 0; i <= numSegments; i++) {
            float t = (float)i / numSegments;
            ImVec2 pos = stair.pointAt(t);
            ImVec2 tangent = stair.tangentAt(t);
            ImVec2 normal(-tangent.y, tangent.x);
            
            centerPts.push_back(pos);
            leftPts.push_back(ImVec2(pos.x - normal.x * width * 0.5f, pos.y - normal.y * width * 0.5f));
            rightPts.push_back(ImVec2(pos.x + normal.x * width * 0.5f, pos.y + normal.y * width * 0.5f));
        }
        
        // Draw stair segments
        for (int i = 0; i < numSegments; i++) {
            ImVec2 tl = worldToScreen(leftPts[i]);
            ImVec2 tr = worldToScreen(rightPts[i]);
            ImVec2 br = worldToScreen(rightPts[i + 1]);
            ImVec2 bl = worldToScreen(leftPts[i + 1]);
            
            drawList->AddQuadFilled(tl, tr, br, bl, color);
            drawList->AddLine(tl, tr, IM_COL32(0, 0, 0, 100), 1.0f);
        }
        // Draw outer edges
        for (int i = 0; i < numSegments; i++) {
            ImVec2 l1 = worldToScreen(leftPts[i]);
            ImVec2 l2 = worldToScreen(leftPts[i + 1]);
            ImVec2 r1 = worldToScreen(rightPts[i]);
            ImVec2 r2 = worldToScreen(rightPts[i + 1]);
            drawList->AddLine(l1, l2, IM_COL32(0, 0, 0, 150), 1.0f);
            drawList->AddLine(r1, r2, IM_COL32(0, 0, 0, 150), 1.0f);
        }
        
        // Draw direction arrow at start
        if (!centerPts.empty()) {
            ImVec2 arrowPos = worldToScreen(centerPts[0]);
            ImVec2 tangent = stair.tangentAt(0);
            ImVec2 normal(-tangent.y, tangent.x);
            // Screen space arrow (Y flipped)
            float arrowSize = 8.0f;
            ImVec2 tip = ImVec2(arrowPos.x + tangent.x * arrowSize, arrowPos.y - tangent.y * arrowSize);
            ImVec2 left = ImVec2(arrowPos.x - normal.x * arrowSize * 0.5f - tangent.x * arrowSize * 0.3f,
                                 arrowPos.y + normal.y * arrowSize * 0.5f + tangent.y * arrowSize * 0.3f);
            ImVec2 right = ImVec2(arrowPos.x + normal.x * arrowSize * 0.5f - tangent.x * arrowSize * 0.3f,
                                  arrowPos.y - normal.y * arrowSize * 0.5f + tangent.y * arrowSize * 0.3f);
            drawList->AddTriangleFilled(tip, left, right, IM_COL32(255, 255, 255, 150));
        }
        
        // Draw control points if selected and curved
        if (m_selectionType == SelectionType::Staircase && m_selectedId == stair.id && 
            stair.isCurved() && m_showControlPoints) {
            // Start point
            ImVec2 startScreen = worldToScreen(stair.start);
            drawList->AddCircleFilled(startScreen, 6.0f, IM_COL32(0, 255, 0, 255));
            // End point
            ImVec2 endScreen = worldToScreen(stair.end);
            drawList->AddCircleFilled(endScreen, 6.0f, IM_COL32(255, 0, 0, 255));
            // Control points
            for (const auto& cp : stair.controlPoints) {
                ImVec2 cpScreen = worldToScreen(cp);
                drawList->AddCircleFilled(cpScreen, 5.0f, IM_COL32(255, 200, 100, 255));
                // Draw lines to adjacent points
                drawList->AddLine(startScreen, cpScreen, IM_COL32(255, 200, 100, 100), 1.0f);
            }
        }
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
    else if (m_currentTool == FloorPlanTool::WallBSpline ||
             m_currentTool == FloorPlanTool::WallBezierSpline) {
        // Draw spline preview
        float thickness = worldToScreenScale(m_wallThickness);
        ImU32 previewColor = IM_COL32(100, 200, 100, 200);
        ImU32 controlColor = IM_COL32(255, 200, 100, 255);
        ImU32 lineColor = IM_COL32(255, 200, 100, 100);
        ImU32 closeHintColor = IM_COL32(100, 255, 100, 150);
        
        // Collect all points: start + control points + current mouse position
        std::vector<ImVec2> pts;
        pts.push_back(m_drawStart);
        for (const auto& cp : m_curveControlPts) {
            pts.push_back(cp);
        }
        pts.push_back(m_drawCurrent);
        
        // Draw control polygon lines
        for (size_t i = 0; i < pts.size() - 1; i++) {
            ImVec2 sp1 = worldToScreen(pts[i]);
            ImVec2 sp2 = worldToScreen(pts[i + 1]);
            drawList->AddLine(sp1, sp2, lineColor, 1.0f);
        }
        
        // Draw potential close line (to start)
        if (pts.size() >= 3) {
            ImVec2 lastScreen = worldToScreen(pts.back());
            ImVec2 startScreen = worldToScreen(m_drawStart);
            drawList->AddLine(lastScreen, startScreen, closeHintColor, 1.0f);
        }
        
        // Draw curve preview
        std::vector<ImVec2> samples;
        if (pts.size() >= 2) {
            if (m_currentTool == FloorPlanTool::WallBSpline) {
                samples = BSplineUtil::sampleBSpline(pts, m_curveSegments / 2, false);
            } else {
                samples = BSplineUtil::sampleBezierSpline(pts, m_curveSegments, false);
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
        for (size_t i = 0; i < pts.size(); i++) {
            ImVec2 sp = worldToScreen(pts[i]);
            ImU32 ptColor = (i == 0) ? IM_COL32(100, 200, 255, 255) : controlColor;
            drawList->AddCircleFilled(sp, CONTROL_POINT_RADIUS, ptColor);
            drawList->AddCircle(sp, CONTROL_POINT_RADIUS, IM_COL32(255, 255, 255, 255), 12, 1.5f);
        }
        
        // Check if mouse is near start point (for close hint)
        float dx = m_drawCurrent.x - m_drawStart.x;
        float dy = m_drawCurrent.y - m_drawStart.y;
        float closeThreshold = m_gridSize * 0.75f;
        bool canClose = (m_curveControlPts.size() >= 2 && sqrtf(dx * dx + dy * dy) < closeThreshold);
        
        // Show hint text
        char hint[64];
        if (canClose) {
            snprintf(hint, sizeof(hint), "%zu pts - Click to CLOSE", pts.size());
        } else {
            snprintf(hint, sizeof(hint), "%zu pts - Right-click to finish", pts.size());
        }
        ImVec2 hintPos = ImVec2(m_canvasPos.x + 10, m_canvasPos.y + m_canvasSize.y - 25);
        drawList->AddText(hintPos, IM_COL32(255, 255, 255, 200), hint);
        
        // Highlight start point if can close
        if (canClose) {
            ImVec2 startScreen = worldToScreen(m_drawStart);
            drawList->AddCircle(startScreen, CONTROL_POINT_RADIUS + 4, closeHintColor, 16, 2.0f);
        }
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
    else if (m_currentTool == FloorPlanTool::RoomCurvedEdge && m_isDrawing && m_selectedRoomEdge >= 0) {
        // Preview curved edge on selected room
        Building* bldg = m_building ? m_building : &m_tempBuilding;
        Floor* floor = bldg->getCurrentFloor();
        if (floor) {
            Room* room = floor->plan.findRoom(m_selectedId);
            if (room && static_cast<size_t>(m_selectedRoomEdge) < room->vertices.size()) {
                // Get edge endpoints
                const ImVec2& v1 = room->vertices[m_selectedRoomEdge];
                const ImVec2& v2 = room->vertices[(m_selectedRoomEdge + 1) % room->vertices.size()];
                
                ImU32 edgeColor = IM_COL32(255, 150, 100, 255);
                ImU32 controlColor = IM_COL32(255, 200, 100, 255);
                ImU32 previewColor = IM_COL32(100, 255, 150, 200);
                
                // Highlight the selected edge
                ImVec2 sv1 = worldToScreen(v1);
                ImVec2 sv2 = worldToScreen(v2);
                drawList->AddLine(sv1, sv2, edgeColor, 3.0f);
                
                // Draw control points and current mouse position
                std::vector<ImVec2> pts;
                pts.push_back(v1);
                for (const auto& cp : m_curveControlPts) {
                    pts.push_back(cp);
                }
                pts.push_back(m_drawCurrent);
                pts.push_back(v2);
                
                // Draw control polygon
                for (size_t i = 0; i < pts.size() - 1; i++) {
                    ImVec2 sp1 = worldToScreen(pts[i]);
                    ImVec2 sp2 = worldToScreen(pts[i + 1]);
                    drawList->AddLine(sp1, sp2, IM_COL32(255, 200, 100, 100), 1.0f);
                }
                
                // Preview the curve
                std::vector<ImVec2> curvePts;
                if (m_curveControlPts.empty()) {
                    // Preview as arc through current mouse
                    ImVec2 center;
                    float radius, startAngle, endAngle;
                    if (ArcUtil::arcFrom3Points(v1, m_drawCurrent, v2, center, radius, startAngle, endAngle)) {
                        curvePts = ArcUtil::sampleArc(center, radius, startAngle, endAngle, m_curveSegments);
                    }
                } else if (m_curveControlPts.size() == 1) {
                    // Quadratic bezier
                    curvePts = BezierUtil::sampleQuadratic(v1, m_curveControlPts[0], v2, m_curveSegments);
                } else {
                    // Cubic bezier
                    curvePts = BezierUtil::sampleCubic(v1, m_curveControlPts[0], m_curveControlPts[1], v2, m_curveSegments);
                }
                
                // Draw curve preview
                for (size_t i = 1; i < curvePts.size(); i++) {
                    ImVec2 sp1 = worldToScreen(curvePts[i - 1]);
                    ImVec2 sp2 = worldToScreen(curvePts[i]);
                    drawList->AddLine(sp1, sp2, previewColor, 2.0f);
                }
                
                // Draw control points
                for (const auto& cp : m_curveControlPts) {
                    ImVec2 scp = worldToScreen(cp);
                    drawList->AddCircleFilled(scp, CONTROL_POINT_RADIUS, controlColor);
                    drawList->AddCircle(scp, CONTROL_POINT_RADIUS, IM_COL32(255, 255, 255, 255), 12, 1.5f);
                }
                
                // Draw current position
                ImVec2 scurrent = worldToScreen(m_drawCurrent);
                drawList->AddCircleFilled(scurrent, CONTROL_POINT_RADIUS, previewColor);
                drawList->AddCircle(scurrent, CONTROL_POINT_RADIUS, IM_COL32(255, 255, 255, 255), 12, 1.5f);
                
                // Hint text
                char hint[64];
                snprintf(hint, sizeof(hint), "Click to add control points, right-click to finish");
                ImVec2 hintPos = ImVec2(m_canvasPos.x + 10, m_canvasPos.y + m_canvasSize.y - 25);
                drawList->AddText(hintPos, IM_COL32(255, 255, 255, 200), hint);
            }
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
        if (ImGui::IsKeyPressed(ImGuiKey_L)) m_currentTool = FloorPlanTool::WallBSpline;
        if (ImGui::IsKeyPressed(ImGuiKey_Z)) m_currentTool = FloorPlanTool::WallBezierSpline;
        if (ImGui::IsKeyPressed(ImGuiKey_R)) m_currentTool = FloorPlanTool::Room;
        if (ImGui::IsKeyPressed(ImGuiKey_E)) m_currentTool = FloorPlanTool::RoomFromWalls;
        if (ImGui::IsKeyPressed(ImGuiKey_K)) m_currentTool = FloorPlanTool::RoomCurvedEdge;
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
    
    case FloorPlanTool::WallBSpline:
    case FloorPlanTool::WallBezierSpline:
        if (clicked) {
            if (!m_isDrawing) {
                startSplineDrawing(worldPos);
            } else {
                // Check if clicking near start point to close the spline
                float dx = worldPos.x - m_drawStart.x;
                float dy = worldPos.y - m_drawStart.y;
                float closeThreshold = m_gridSize * 0.75f;
                if (m_curveControlPts.size() >= 2 && sqrtf(dx * dx + dy * dy) < closeThreshold) {
                    // Close the spline
                    finishSplineDrawing(true);
                } else {
                    addSplineControlPoint(worldPos);
                }
            }
        }
        if (rightClicked && m_isDrawing) {
            // Right-click finishes the spline (open)
            if (m_curveControlPts.size() >= 1) {
                finishSplineDrawing(false);
            } else {
                cancelDrawing();
            }
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
    
    case FloorPlanTool::RoomFromWalls:
        if (clicked) {
            createRoomFromWallsAt(worldPos);
        }
        break;
    
    case FloorPlanTool::RoomCurvedEdge:
        if (clicked) {
            if (!m_isDrawing) {
                startRoomEdgeCurve(worldPos);
            } else {
                addRoomEdgeControlPoint(worldPos);
            }
        }
        if (rightClicked && m_isDrawing) {
            finishRoomEdgeCurve();
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

void FloorPlanEditor::startSplineDrawing(ImVec2 worldPos)
{
    m_isDrawing = true;
    m_drawStart = worldPos;
    m_drawCurrent = worldPos;
    m_curveControlPts.clear();
    m_curveClickCount = 1;
}

void FloorPlanEditor::addSplineControlPoint(ImVec2 worldPos)
{
    m_curveControlPts.push_back(worldPos);
    m_curveClickCount++;
}

void FloorPlanEditor::finishSplineDrawing(bool closed)
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    // Need at least start + 1 control point for a meaningful spline
    if (m_curveControlPts.empty()) {
        cancelDrawing();
        return;
    }
    
    // Create the wall
    Wall wall;
    wall.id = floor->plan.nextWallId++;
    wall.start = m_drawStart;
    wall.end = closed ? m_drawStart : m_drawCurrent;  // For closed splines, end == start
    wall.thickness = m_wallThickness;
    wall.curveSegments = m_curveSegments;
    wall.isClosed = closed;
    
    // Set spline type and control points
    if (m_currentTool == FloorPlanTool::WallBSpline) {
        wall.setBSpline(m_curveControlPts, closed);
    } else {
        wall.setBezierSpline(m_curveControlPts, closed);
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

void FloorPlanEditor::createRoomFromWallsAt(ImVec2 worldPos)
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    int roomId = floor->plan.createRoomFromWallsAt(worldPos, "Room");
    if (roomId > 0) {
        // Select the newly created room
        m_selectionType = SelectionType::Room;
        m_selectedId = roomId;
    }
}

void FloorPlanEditor::startRoomEdgeCurve(ImVec2 worldPos)
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    // Find the room containing or near this point
    Room* room = floor->plan.findRoomAt(worldPos);
    
    // If no room at point, find nearest room edge
    if (!room) {
        float minDist = 1e10f;
        int bestRoomId = -1;
        int bestEdge = -1;
        
        for (auto& r : floor->plan.rooms) {
            for (size_t i = 0; i < r.vertices.size(); i++) {
                const ImVec2& v1 = r.vertices[i];
                const ImVec2& v2 = r.vertices[(i + 1) % r.vertices.size()];
                
                // Distance to edge (simplified as distance to line segment)
                ImVec2 v = ImVec2(v2.x - v1.x, v2.y - v1.y);
                ImVec2 w = ImVec2(worldPos.x - v1.x, worldPos.y - v1.y);
                float c1 = v.x * w.x + v.y * w.y;
                float c2 = v.x * v.x + v.y * v.y;
                if (c2 > 0) {
                    float t = std::max(0.0f, std::min(1.0f, c1 / c2));
                    ImVec2 proj = ImVec2(v1.x + t * v.x, v1.y + t * v.y);
                    float dx = worldPos.x - proj.x;
                    float dy = worldPos.y - proj.y;
                    float dist = sqrtf(dx * dx + dy * dy);
                    if (dist < minDist && dist < m_gridSize * 2.0f) {
                        minDist = dist;
                        bestRoomId = r.id;
                        bestEdge = static_cast<int>(i);
                    }
                }
            }
        }
        
        if (bestRoomId > 0) {
            m_selectionType = SelectionType::Room;
            m_selectedId = bestRoomId;
            m_selectedRoomEdge = bestEdge;
            m_isDrawing = true;
            m_drawStart = worldPos;
            m_drawCurrent = worldPos;
            m_curveControlPts.clear();
            return;
        }
    } else {
        // Found room, find nearest edge
        float minDist = 1e10f;
        int bestEdge = -1;
        
        for (size_t i = 0; i < room->vertices.size(); i++) {
            const ImVec2& v1 = room->vertices[i];
            const ImVec2& v2 = room->vertices[(i + 1) % room->vertices.size()];
            
            ImVec2 v = ImVec2(v2.x - v1.x, v2.y - v1.y);
            ImVec2 w = ImVec2(worldPos.x - v1.x, worldPos.y - v1.y);
            float c1 = v.x * w.x + v.y * w.y;
            float c2 = v.x * v.x + v.y * v.y;
            if (c2 > 0) {
                float t = std::max(0.0f, std::min(1.0f, c1 / c2));
                ImVec2 proj = ImVec2(v1.x + t * v.x, v1.y + t * v.y);
                float dx = worldPos.x - proj.x;
                float dy = worldPos.y - proj.y;
                float dist = sqrtf(dx * dx + dy * dy);
                if (dist < minDist) {
                    minDist = dist;
                    bestEdge = static_cast<int>(i);
                }
            }
        }
        
        if (bestEdge >= 0) {
            m_selectionType = SelectionType::Room;
            m_selectedId = room->id;
            m_selectedRoomEdge = bestEdge;
            m_isDrawing = true;
            m_drawStart = worldPos;
            m_drawCurrent = worldPos;
            m_curveControlPts.clear();
        }
    }
}

void FloorPlanEditor::addRoomEdgeControlPoint(ImVec2 worldPos)
{
    m_curveControlPts.push_back(worldPos);
}

void FloorPlanEditor::finishRoomEdgeCurve()
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    Room* room = floor->plan.findRoom(m_selectedId);
    if (!room || m_selectedRoomEdge < 0) {
        cancelDrawing();
        return;
    }
    
    // Apply curve to the selected edge
    if (m_curveControlPts.size() == 1) {
        // Single control point -> quadratic bezier or arc
        room->setEdgeArc(static_cast<size_t>(m_selectedRoomEdge), m_curveControlPts[0]);
    } else if (m_curveControlPts.size() >= 2) {
        // Two or more control points -> cubic bezier
        room->setEdgeCubic(static_cast<size_t>(m_selectedRoomEdge), m_curveControlPts[0], m_curveControlPts[1]);
    }
    
    m_isDrawing = false;
    m_curveControlPts.clear();
    m_selectedRoomEdge = -1;
}

void FloorPlanEditor::placeDoor(ImVec2 worldPos)
{
    Building* bldg = m_building ? m_building : &m_tempBuilding;
    Floor* floor = bldg->getCurrentFloor();
    if (!floor) return;
    
    // Find wall at position
    Wall* wall = floor->plan.findWallAt(worldPos, 0.3f);
    if (!wall) return;
    
    // Calculate position on wall using closestPointTo (works for curves)
    float distance;
    float t = wall->closestPointTo(worldPos, &distance);
    
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
    
    // Calculate position on wall using closestPointTo (works for curves)
    float distance;
    float t = wall->closestPointTo(worldPos, &distance);
    
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
    // Default staircase: 1m wide, 3m long, vertical orientation
    stair.start = ImVec2(worldPos.x, worldPos.y - 1.5f);
    stair.end = ImVec2(worldPos.x, worldPos.y + 1.5f);
    stair.width = 1.0f;
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
        // Check distance from centerline of staircase
        // Sample a few points and check if click is within width/2 of any segment
        int numSamples = 10;
        float halfWidth = stair.width * 0.5f;
        for (int i = 0; i < numSamples; i++) {
            float t0 = (float)i / numSamples;
            float t1 = (float)(i + 1) / numSamples;
            ImVec2 p0 = stair.pointAt(t0);
            ImVec2 p1 = stair.pointAt(t1);
            
            // Point to segment distance
            float dx = p1.x - p0.x;
            float dy = p1.y - p0.y;
            float segLenSq = dx * dx + dy * dy;
            float t = 0.0f;
            if (segLenSq > 0.0001f) {
                t = ((worldPos.x - p0.x) * dx + (worldPos.y - p0.y) * dy) / segLenSq;
                t = std::max(0.0f, std::min(1.0f, t));
            }
            ImVec2 closest(p0.x + t * dx, p0.y + t * dy);
            float distSq = (worldPos.x - closest.x) * (worldPos.x - closest.x) + 
                          (worldPos.y - closest.y) * (worldPos.y - closest.y);
            if (distSq <= halfWidth * halfWidth) {
                m_selectionType = SelectionType::Staircase;
                m_selectedId = stair.id;
                return;
            }
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
    m_selectedRoomEdge = -1;
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
        
        // Editable floor level
        int level = floor->level;
        if (ImGui::DragInt("Level", &level, 0.1f, -10, 100)) {
            floor->level = level;
            floor->elevation = level * 3.0f;  // Update elevation to match
            // Re-sort floors in building
            auto& floors = bldg->getFloors();
            std::sort(floors.begin(), floors.end(), 
                [](const Floor& a, const Floor& b) { return a.level < b.level; });
        }
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("0=Ground, -1=Basement, 1+=Upper floors");
        
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
                
                // Curve type info (updated for splines)
                const char* typeNames[] = { "Straight", "Quadratic Bezier", "Cubic Bezier", "Arc", "B-Spline (approx)", "Bezier Spline (interp)" };
                ImGui::Text("Type: %s", typeNames[static_cast<int>(w->type)]);
                
                if (w->isCurved()) {
                    ImGui::Checkbox("Show Control Points", &m_showControlPoints);
                    ImGui::SliderInt("Curve Segments", &w->curveSegments, 5, 50);
                    
                    // Closed toggle for spline types
                    if (w->type == WallType::BSpline || w->type == WallType::BezierSpline) {
                        if (ImGui::Checkbox("Closed Loop", &w->isClosed)) {
                            // Update end point if closing/opening
                            if (w->isClosed) {
                                w->end = w->start;
                            }
                        }
                    }
                    
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
                        ImVec2 mid = w->midpoint();
                        w->setQuadraticBezier(mid);
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Cubic Bezier")) {
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
                        ImVec2 mid = w->midpoint();
                        ImVec2 normal = w->normal();
                        float offset = w->length() * 0.25f;
                        ImVec2 through = ImVec2(mid.x + normal.x * offset, mid.y + normal.y * offset);
                        w->setArc(through);
                    }
                    
                    // Spline conversions
                    ImGui::Separator();
                    ImGui::Text("Splines:");
                    if (ImGui::Button("B-Spline##approx")) {
                        // Create intermediate control points
                        std::vector<ImVec2> pts;
                        for (int i = 1; i < 4; i++) {
                            float t = (float)i / 4.0f;
                            pts.push_back(ImVec2(
                                w->start.x + (w->end.x - w->start.x) * t,
                                w->start.y + (w->end.y - w->start.y) * t
                            ));
                        }
                        w->setBSpline(pts, false);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Approximating spline - curve does NOT pass through control points");
                    ImGui::SameLine();
                    if (ImGui::Button("Bezier Spline##interp")) {
                        std::vector<ImVec2> pts;
                        pts.push_back(w->midpoint());
                        w->setBezierSpline(pts, false);
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Interpolating spline - curve passes through control points");
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
        case SelectionType::Furniture:
            for (auto& f : floor->plan.furniture) {
                if (f.id == m_selectedId) {
                    ImGui::Text("Furniture #%d", f.id);
                    
                    static char furnNameBuf[128];
                    strncpy(furnNameBuf, f.name.c_str(), sizeof(furnNameBuf) - 1);
                    if (ImGui::InputText("Name##Furn", furnNameBuf, sizeof(furnNameBuf))) {
                        f.name = furnNameBuf;
                    }
                    
                    static char furnTypeBuf[64];
                    strncpy(furnTypeBuf, f.type.c_str(), sizeof(furnTypeBuf) - 1);
                    if (ImGui::InputText("Type##Furn", furnTypeBuf, sizeof(furnTypeBuf))) {
                        f.type = furnTypeBuf;
                    }
                    
                    ImGui::DragFloat2("Position##Furn", &f.position.x, 0.1f);
                    ImGui::DragFloat2("Size (W x D)##Furn", &f.size.x, 0.1f, 0.1f, 10.0f, "%.2f m");
                    
                    // Rotation in degrees for easier editing
                    float rotDeg = f.rotation * 180.0f / 3.14159265f;
                    if (ImGui::DragFloat("Rotation##Furn", &rotDeg, 1.0f, -180.0f, 180.0f, "%.1f°")) {
                        f.rotation = rotDeg * 3.14159265f / 180.0f;
                    }
                    // Quick rotation buttons
                    if (ImGui::Button("0°##Furn")) f.rotation = 0.0f;
                    ImGui::SameLine();
                    if (ImGui::Button("90°##Furn")) f.rotation = 3.14159265f / 2.0f;
                    ImGui::SameLine();
                    if (ImGui::Button("180°##Furn")) f.rotation = 3.14159265f;
                    ImGui::SameLine();
                    if (ImGui::Button("-90°##Furn")) f.rotation = -3.14159265f / 2.0f;
                    
                    static char textureBuf[256];
                    strncpy(textureBuf, f.texture.c_str(), sizeof(textureBuf) - 1);
                    if (ImGui::InputText("Texture##Furn", textureBuf, sizeof(textureBuf))) {
                        f.texture = textureBuf;
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Texture/icon name (leave empty for solid color)");
                    
                    float col[4];
                    col[0] = ((f.color >> 0) & 0xFF) / 255.0f;
                    col[1] = ((f.color >> 8) & 0xFF) / 255.0f;
                    col[2] = ((f.color >> 16) & 0xFF) / 255.0f;
                    col[3] = ((f.color >> 24) & 0xFF) / 255.0f;
                    if (ImGui::ColorEdit4("Color##Furn", col)) {
                        f.color = IM_COL32(
                            (int)(col[0] * 255), (int)(col[1] * 255),
                            (int)(col[2] * 255), (int)(col[3] * 255));
                    }
                    break;
                }
            }
            break;
        case SelectionType::Staircase:
            for (auto& s : floor->plan.staircases) {
                if (s.id == m_selectedId) {
                    ImGui::Text("Staircase #%d", s.id);
                    ImGui::Text("Length: %.2f m", s.length());
                    
                    ImGui::DragFloat2("Start##Stair", &s.start.x, 0.1f);
                    ImGui::DragFloat2("End##Stair", &s.end.x, 0.1f);
                    ImGui::DragFloat("Width##Stair", &s.width, 0.1f, 0.3f, 5.0f, "%.2f m");
                    
                    ImGui::DragInt("Steps##Stair", &s.numSteps, 1.0f, 3, 50);
                    ImGui::DragInt("Connects to Floor##Stair", &s.connectsToFloor, 1.0f, -5, 20);
                    
                    // Curve type info
                    const char* typeNames[] = { "Straight", "Quadratic Bezier", "Cubic Bezier", "Arc", "B-Spline (approx)", "Bezier Spline (interp)" };
                    ImGui::Text("Type: %s", typeNames[static_cast<int>(s.curveType)]);
                    
                    if (s.isCurved()) {
                        ImGui::Checkbox("Show Control Points##Stair", &m_showControlPoints);
                        ImGui::SliderInt("Curve Segments##Stair", &s.curveSegments, 5, 50);
                        
                        // Closed toggle for spline types
                        if (s.curveType == StaircaseType::BSpline || s.curveType == StaircaseType::BezierSpline) {
                            if (ImGui::Checkbox("Closed Loop##Stair", &s.isClosed)) {
                                if (s.isClosed) s.end = s.start;
                            }
                        }
                        
                        ImGui::Text("Control Points: %zu", s.controlPoints.size());
                        
                        // Edit control points
                        for (size_t i = 0; i < s.controlPoints.size(); i++) {
                            char label[32];
                            snprintf(label, sizeof(label), "CP %zu##Stair", i + 1);
                            ImGui::DragFloat2(label, &s.controlPoints[i].x, 0.1f);
                        }
                        
                        // Add/remove control points for splines
                        if (s.curveType == StaircaseType::BSpline || s.curveType == StaircaseType::BezierSpline) {
                            if (ImGui::Button("Add CP##Stair")) {
                                // Add midpoint between last cp and end
                                ImVec2 newPt;
                                if (s.controlPoints.empty()) {
                                    newPt = ImVec2((s.start.x + s.end.x) * 0.5f, (s.start.y + s.end.y) * 0.5f);
                                } else {
                                    ImVec2& last = s.controlPoints.back();
                                    newPt = ImVec2((last.x + s.end.x) * 0.5f, (last.y + s.end.y) * 0.5f);
                                }
                                s.controlPoints.push_back(newPt);
                            }
                            ImGui::SameLine();
                            if (ImGui::Button("Remove CP##Stair") && !s.controlPoints.empty()) {
                                s.controlPoints.pop_back();
                            }
                        }
                        
                        // Convert to straight
                        if (ImGui::Button("Convert to Straight##Stair")) {
                            s.setStraight();
                        }
                    } else {
                        // Convert straight staircase to curve
                        ImGui::Separator();
                        ImGui::Text("Convert to:");
                        if (ImGui::Button("Quadratic Bezier##Stair")) {
                            ImVec2 mid((s.start.x + s.end.x) * 0.5f, (s.start.y + s.end.y) * 0.5f);
                            // Offset perpendicular to direction
                            float dx = s.end.x - s.start.x;
                            float dy = s.end.y - s.start.y;
                            float len = sqrtf(dx * dx + dy * dy);
                            if (len > 0) {
                                mid.x -= dy / len * len * 0.25f;
                                mid.y += dx / len * len * 0.25f;
                            }
                            s.setQuadraticBezier(mid);
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Cubic Bezier##Stair")) {
                            ImVec2 p1(s.start.x + (s.end.x - s.start.x) * 0.33f,
                                     s.start.y + (s.end.y - s.start.y) * 0.33f);
                            ImVec2 p2(s.start.x + (s.end.x - s.start.x) * 0.66f,
                                     s.start.y + (s.end.y - s.start.y) * 0.66f);
                            s.setCubicBezier(p1, p2);
                        }
                        if (ImGui::Button("Arc##Stair")) {
                            ImVec2 mid((s.start.x + s.end.x) * 0.5f, (s.start.y + s.end.y) * 0.5f);
                            float dx = s.end.x - s.start.x;
                            float dy = s.end.y - s.start.y;
                            float len = sqrtf(dx * dx + dy * dy);
                            if (len > 0) {
                                mid.x -= dy / len * len * 0.25f;
                                mid.y += dx / len * len * 0.25f;
                            }
                            s.setArc(mid);
                        }
                        
                        ImGui::Separator();
                        ImGui::Text("Splines:");
                        if (ImGui::Button("B-Spline##StairApprox")) {
                            std::vector<ImVec2> pts;
                            for (int i = 1; i < 4; i++) {
                                float t = (float)i / 4.0f;
                                pts.push_back(ImVec2(
                                    s.start.x + (s.end.x - s.start.x) * t,
                                    s.start.y + (s.end.y - s.start.y) * t
                                ));
                            }
                            s.setBSpline(pts, false);
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Approximating spline - curve does NOT pass through control points");
                        ImGui::SameLine();
                        if (ImGui::Button("Bezier Spline##StairInterp")) {
                            std::vector<ImVec2> pts;
                            pts.push_back(ImVec2((s.start.x + s.end.x) * 0.5f, (s.start.y + s.end.y) * 0.5f));
                            s.setBezierSpline(pts, false);
                        }
                        if (ImGui::IsItemHovered()) ImGui::SetTooltip("Interpolating spline - curve passes through control points");
                    }
                    
                    ImGui::Separator();
                    float col[4];
                    col[0] = ((s.color >> 0) & 0xFF) / 255.0f;
                    col[1] = ((s.color >> 8) & 0xFF) / 255.0f;
                    col[2] = ((s.color >> 16) & 0xFF) / 255.0f;
                    col[3] = ((s.color >> 24) & 0xFF) / 255.0f;
                    if (ImGui::ColorEdit4("Color##Stair", col)) {
                        s.color = IM_COL32(
                            (int)(col[0] * 255), (int)(col[1] * 255),
                            (int)(col[2] * 255), (int)(col[3] * 255));
                    }
                    break;
                }
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
