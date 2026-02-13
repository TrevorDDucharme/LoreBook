#include <WorldMaps/Orbital/OrbitalEditor.hpp>
#include <WorldMaps/Orbital/OrbitalMechanics.hpp>
#include <Vault.hpp>
#include <imgui.h>
#include <plog/Log.h>
#include <cstring>
#include <algorithm>

namespace Orbital {

OrbitalEditor::~OrbitalEditor() {
    if (m_texture) { glDeleteTextures(1, &m_texture); m_texture = 0; }
}

void OrbitalEditor::render() {
    if (!m_isOpen) return;

    ImGui::SetNextWindowSize(ImVec2(900, 700), ImGuiCond_FirstUseEver);

    if (ImGui::Begin("Orbital System Editor", &m_isOpen, ImGuiWindowFlags_MenuBar)) {
        if (!m_vault) {
            ImGui::TextDisabled("Open a vault to edit orbital systems.");
            ImGui::End();
            return;
        }

        renderMenuBar();

        // Time controls across the top
        renderTimeControls();
        ImGui::Separator();

        // Left panel: system selector + body tree
        float panelWidth = 240.0f;
        ImGui::BeginChild("OrbLeftPanel", ImVec2(panelWidth, 0), true);
        renderSystemSelector();
        ImGui::Separator();
        renderBodyTree();
        ImGui::EndChild();

        ImGui::SameLine();

        // Center: viewport
        ImVec2 avail = ImGui::GetContentRegionAvail();
        float rightPanelWidth = 260.0f;
        float viewW = avail.x - rightPanelWidth - 10.0f;
        if (viewW < 200) viewW = 200;

        ImGui::BeginChild("OrbViewport", ImVec2(viewW, 0), false);
        renderViewport();
        ImGui::EndChild();

        ImGui::SameLine();

        // Right panel: properties
        ImGui::BeginChild("OrbRightPanel", ImVec2(0, 0), true);
        renderPropertiesPanel();
        ImGui::EndChild();
    }
    ImGui::End();
}

void OrbitalEditor::renderMenuBar() {
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("System")) {
            if (ImGui::BeginMenu("New System")) {
                ImGui::InputText("Name", m_newSystemName, sizeof(m_newSystemName));
                ImGui::Combo("Type", &m_newSystemType, "Galaxy\0Stellar\0");
                if (ImGui::Button("Create") && std::strlen(m_newSystemName) > 0) {
                    OrbitalSystemInfo info;
                    info.name = m_newSystemName;
                    info.type = (m_newSystemType == 0) ? SystemType::Galaxy : SystemType::Stellar;
                    int64_t id = m_vault->createOrbitalSystem(info.name, systemTypeName(info.type));
                    if (id > 0) {
                        m_systemListDirty = true;
                        loadSystem(id);
                        m_newSystemName[0] = '\0';
                    }
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Delete System", nullptr, false, m_loadedSystemID > 0)) {
                m_vault->deleteOrbitalSystem(m_loadedSystemID);
                m_loadedSystemID = -1;
                m_system = OrbitalSystem();
                m_selectedBodyID = -1;
                m_systemListDirty = true;
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Body", m_loadedSystemID > 0)) {
            if (ImGui::BeginMenu("Add Body")) {
                ImGui::InputText("Name##body", m_newBodyName, sizeof(m_newBodyName));
                ImGui::Combo("Type##body", &m_newBodyType, "Star\0Planet\0Moon\0AsteroidBelt\0Comet\0Station\0");

                // Parent selector for the new body
                {
                    std::string parentLabel = (m_newBodyParentID < 0) ? "None (root)" : "";
                    if (m_newBodyParentID >= 0) {
                        CelestialBody* par = m_system.findBody(m_newBodyParentID);
                        parentLabel = par ? par->name : "Unknown";
                    }
                    if (ImGui::BeginCombo("Parent##newbody", parentLabel.c_str())) {
                        if (ImGui::Selectable("None (root)##nb", m_newBodyParentID < 0)) {
                            m_newBodyParentID = -1;
                        }
                        for (auto& b : m_system.bodies()) {
                            bool sel = (b->id == m_newBodyParentID);
                            std::string lbl = b->name + " (" + bodyTypeName(b->bodyType) + ")##nb";
                            if (ImGui::Selectable(lbl.c_str(), sel)) {
                                m_newBodyParentID = b->id;
                            }
                        }
                        ImGui::EndCombo();
                    }
                }

                ImGui::DragFloat("Radius", &m_newBodyRadius, 0.01f, 0.01f, 100.0f);
                ImGui::DragFloat("Orbit Radius (AU)", &m_newBodyOrbitRadius, 0.1f, 0.0f, 1000.0f);
                ImGui::DragFloat("Period (years)", &m_newBodyPeriod, 0.1f, 0.01f, 10000.0f);
                if (ImGui::Button("Create##body") && std::strlen(m_newBodyName) > 0) {
                    CelestialBody body;
                    body.name = m_newBodyName;
                    body.systemID = m_loadedSystemID;
                    body.parentBodyID = m_newBodyParentID;
                    body.bodyType = static_cast<BodyType>(m_newBodyType);
                    body.radius = m_newBodyRadius;
                    body.orbit.semiMajorAxis = m_newBodyOrbitRadius;
                    body.orbit.period = m_newBodyPeriod;

                    // Default colors by type
                    switch (body.bodyType) {
                        case BodyType::Star: body.colorTint[0]=1.0f; body.colorTint[1]=0.9f; body.colorTint[2]=0.5f; body.luminosity=1.0f; break;
                        case BodyType::Planet: body.colorTint[0]=0.3f; body.colorTint[1]=0.5f; body.colorTint[2]=0.8f; break;
                        case BodyType::Moon: body.colorTint[0]=0.7f; body.colorTint[1]=0.7f; body.colorTint[2]=0.7f; break;
                        case BodyType::Comet: body.colorTint[0]=0.6f; body.colorTint[1]=0.8f; body.colorTint[2]=1.0f; break;
                        default: body.colorTint[0]=0.5f; body.colorTint[1]=0.5f; body.colorTint[2]=0.5f; break;
                    }

                    m_vault->createCelestialBody(body);
                    loadSystem(m_loadedSystemID);
                    m_newBodyName[0] = '\0';
                    ImGui::CloseCurrentPopup();
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Delete Body", nullptr, false, m_selectedBodyID > 0)) {
                m_vault->deleteCelestialBody(m_selectedBodyID);
                m_selectedBodyID = -1;
                loadSystem(m_loadedSystemID);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("View")) {
            bool drawOrbits = m_projection.drawOrbits();
            if (ImGui::MenuItem("Show Orbits", nullptr, &drawOrbits)) {
                m_projection.setDrawOrbits(drawOrbits);
            }
            ImGui::DragFloat2("Preview Size", &m_viewSize.x, 8.0f, 128.0f, 2048.0f, "%.0f");
            ImGui::EndMenu();
        }

        ImGui::EndMenuBar();
    }
}

void OrbitalEditor::renderSystemSelector() {
    if (m_systemListDirty) {
        refreshSystemList();
        m_systemListDirty = false;
    }

    ImGui::TextUnformatted("Systems");
    for (auto& sys : m_systemList) {
        bool selected = (sys.id == m_loadedSystemID);
        std::string label = sys.name + " (" + systemTypeName(sys.type) + ")";
        if (ImGui::Selectable(label.c_str(), selected)) {
            loadSystem(sys.id);
        }
    }
    if (m_systemList.empty()) {
        ImGui::TextDisabled("No systems. Create one from System menu.");
    }
}

void OrbitalEditor::renderBodyTree() {
    if (!m_system.isLoaded()) return;

    ImGui::TextUnformatted("Bodies");
    auto roots = m_system.rootBodies();
    for (auto* body : roots) {
        renderBodyTreeNode(body);
    }
}

void OrbitalEditor::renderBodyTreeNode(CelestialBody* body) {
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (body->children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
    if (body->id == m_selectedBodyID) flags |= ImGuiTreeNodeFlags_Selected;

    std::string label = body->name + " (" + bodyTypeName(body->bodyType) + ")";
    bool open = ImGui::TreeNodeEx((void*)(intptr_t)body->id, flags, "%s", label.c_str());

    if (ImGui::IsItemClicked()) {
        m_selectedBodyID = body->id;
    }

    if (open) {
        for (auto* child : body->children) {
            renderBodyTreeNode(child);
        }
        ImGui::TreePop();
    }
}

void OrbitalEditor::renderPropertiesPanel() {
    CelestialBody* body = selectedBody();
    if (!body) {
        ImGui::TextDisabled("Select a body to edit.");
        return;
    }

    ImGui::Text("Body: %s", body->name.c_str());
    ImGui::Separator();

    // Basic properties
    char nameBuf[128];
    std::strncpy(nameBuf, body->name.c_str(), sizeof(nameBuf) - 1);
    nameBuf[sizeof(nameBuf) - 1] = '\0';
    if (ImGui::InputText("Name##prop", nameBuf, sizeof(nameBuf))) {
        body->name = nameBuf;
    }

    int typeIdx = static_cast<int>(body->bodyType);
    if (ImGui::Combo("Type##prop", &typeIdx, "Star\0Planet\0Moon\0AsteroidBelt\0Comet\0Station\0")) {
        body->bodyType = static_cast<BodyType>(typeIdx);
    }

    // Parent body selector
    {
        std::string currentParentLabel = "None (root)";
        if (body->parentBodyID >= 0) {
            CelestialBody* par = m_system.findBody(body->parentBodyID);
            if (par) currentParentLabel = par->name;
        }
        if (ImGui::BeginCombo("Parent##prop", currentParentLabel.c_str())) {
            // "None" option to make root
            if (ImGui::Selectable("None (root)", body->parentBodyID < 0)) {
                body->parentBodyID = -1;
            }
            for (auto& b : m_system.bodies()) {
                if (b->id == body->id) continue;
                // Prevent cycles: skip descendants of this body
                bool isDesc = false;
                CelestialBody* walk = b.get();
                while (walk) {
                    if (walk->id == body->id) { isDesc = true; break; }
                    walk = walk->parent;
                }
                if (isDesc) continue;
                bool selected = (b->id == body->parentBodyID);
                std::string lbl = b->name + " (" + bodyTypeName(b->bodyType) + ")";
                if (ImGui::Selectable(lbl.c_str(), selected)) {
                    body->parentBodyID = b->id;
                }
            }
            ImGui::EndCombo();
        }
    }

    float radiusF = (float)body->radius;
    if (ImGui::DragFloat("Radius", &radiusF, 0.01f, 0.01f, 1000.0f)) body->radius = radiusF;
    float massF = (float)body->mass;
    if (ImGui::DragFloat("Mass", &massF, 0.01f, 0.0f, 1e12f)) body->mass = massF;
    ImGui::DragFloat("Luminosity", &body->luminosity, 0.01f, 0.0f, 100.0f);
    ImGui::ColorEdit3("Color Tint", body->colorTint);

    ImGui::Separator();
    ImGui::TextUnformatted("Orbital Elements");
    float sma = (float)body->orbit.semiMajorAxis;
    if (ImGui::DragFloat("Semi-Major Axis (AU)", &sma, 0.1f, 0.0f, 10000.0f)) body->orbit.semiMajorAxis = sma;
    float ecc = (float)body->orbit.eccentricity;
    if (ImGui::DragFloat("Eccentricity", &ecc, 0.001f, 0.0f, 0.999f)) body->orbit.eccentricity = ecc;
    float inc = (float)body->orbit.inclination;
    if (ImGui::DragFloat("Inclination (deg)", &inc, 0.1f, -180.0f, 180.0f)) body->orbit.inclination = inc;
    float lan = (float)body->orbit.longAscNode;
    if (ImGui::DragFloat("Long. Asc. Node (deg)", &lan, 0.1f, 0.0f, 360.0f)) body->orbit.longAscNode = lan;
    float ap = (float)body->orbit.argPeriapsis;
    if (ImGui::DragFloat("Arg. Periapsis (deg)", &ap, 0.1f, 0.0f, 360.0f)) body->orbit.argPeriapsis = ap;
    float mae = (float)body->orbit.meanAnomalyEpoch;
    if (ImGui::DragFloat("Mean Anomaly Epoch (deg)", &mae, 0.1f, 0.0f, 360.0f)) body->orbit.meanAnomalyEpoch = mae;
    float per = (float)body->orbit.period;
    if (ImGui::DragFloat("Period (years)", &per, 0.1f, 0.001f, 100000.0f)) body->orbit.period = per;

    ImGui::Separator();
    ImGui::TextUnformatted("Rotation");
    float at = (float)body->axialTilt;
    if (ImGui::DragFloat("Axial Tilt (deg)", &at, 0.1f, -180.0f, 180.0f)) body->axialTilt = at;
    float rp = (float)body->rotationPeriod;
    if (ImGui::DragFloat("Rotation Period", &rp, 0.01f, 0.001f, 10000.0f)) body->rotationPeriod = rp;

    ImGui::Separator();
    // World surface config (for drill-down to world view)
    char wcBuf[256];
    std::strncpy(wcBuf, body->worldConfig.c_str(), sizeof(wcBuf) - 1);
    wcBuf[sizeof(wcBuf) - 1] = '\0';
    if (ImGui::InputText("World Config", wcBuf, sizeof(wcBuf))) {
        body->worldConfig = wcBuf;
    }
    if (body->hasSurface()) {
        ImGui::TextColored(ImVec4(0.4f, 0.8f, 0.4f, 1.0f), "Has surface (config: %s)", body->worldConfig.c_str());
    }

    ImGui::Separator();
    if (ImGui::Button("Save Changes")) {
        m_vault->updateCelestialBody(*body);
        loadSystem(m_loadedSystemID); // rebuild hierarchy after potential parent change
    }
}

void OrbitalEditor::renderViewport() {
    if (!m_system.isLoaded()) {
        ImGui::TextDisabled("Load a system to view.");
        return;
    }

    ImVec2 avail = ImGui::GetContentRegionAvail();
    int w = std::max((int)avail.x, 64);
    int h = std::max((int)(avail.y - 4), 64);

    // Update time
    if (m_playing) {
        m_time += (double)ImGui::GetIO().DeltaTime * m_timeSpeed;
        m_projection.setTime(m_time);
    }

    // Project
    m_projection.project(m_system, w, h, m_texture);

    if (m_texture) {
        ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::Image((ImTextureID)(intptr_t)m_texture, ImVec2((float)w, (float)h));

        // Camera drag interaction
        if (ImGui::IsItemHovered()) {
            // Scroll to zoom
            float scroll = ImGui::GetIO().MouseWheel;
            if (scroll != 0.0f) {
                float z = m_projection.zoom();
                z *= (1.0f - scroll * 0.1f);
                z = std::clamp(z, 0.5f, 1000.0f);
                m_projection.setZoom(z);
            }

            // Drag to rotate
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ImVec2 delta = ImGui::GetIO().MouseDelta;
                float lon = m_projection.centerLon() - delta.x * 0.005f;
                float lat = m_projection.centerLat() + delta.y * 0.005f;
                lat = std::clamp(lat, -1.5f, 1.5f);
                m_projection.setViewCenter(lon, lat);
            }
        }

        // Body label overlay
        auto positions = m_system.bodyPositionsAt(m_time);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        for (auto& bp : positions) {
            // Simple 3D→2D projection for label placement
            float cx = m_projection.zoom() * cosf(m_projection.centerLat()) * cosf(m_projection.centerLon());
            float cy = m_projection.zoom() * sinf(m_projection.centerLat());
            float cz = m_projection.zoom() * cosf(m_projection.centerLat()) * sinf(m_projection.centerLon());

            float fx = -cx, fy = -cy, fz = -cz;
            float fl = sqrtf(fx*fx+fy*fy+fz*fz);
            if (fl < 1e-6f) continue;
            fx/=fl; fy/=fl; fz/=fl;

            float rx = fy*0 - fz*1, ry = fz*0 - fx*0, rz = fx*1 - fy*0;
            // Use proper world-up cross product
            float wuX = 0, wuY = 1, wuZ = 0;
            rx = fy*wuZ - fz*wuY; ry = fz*wuX - fx*wuZ; rz = fx*wuY - fy*wuX;
            float rl = sqrtf(rx*rx+ry*ry+rz*rz);
            if (rl < 1e-6f) continue;
            rx/=rl; ry/=rl; rz/=rl;

            float ux = ry*fz-rz*fy, uy = rz*fx-rx*fz, uz = rx*fy-ry*fx;

            float bx = (float)bp.position.x - cx;
            float by = (float)bp.position.y - cy;
            float bz = (float)bp.position.z - cz;
            float depth = bx*fx + by*fy + bz*fz;
            if (depth < 0.1f) continue;

            float fov = m_projection.fovY();
            float aspect = (float)w / (float)h;
            float sx = (bx*rx + by*ry + bz*rz) / (depth * tanf(fov*0.5f) * aspect);
            float sy = (bx*ux + by*uy + bz*uz) / (depth * tanf(fov*0.5f));

            float pixX = pos.x + (sx + 1.0f) * 0.5f * (float)w;
            float pixY = pos.y + (1.0f - sy) * 0.5f * (float)h;

            if (pixX >= pos.x && pixX < pos.x + w && pixY >= pos.y && pixY < pos.y + h) {
                dl->AddText(ImVec2(pixX + 6, pixY - 6), IM_COL32(255,255,255,200), bp.body->name.c_str());
            }
        }
    }
}

void OrbitalEditor::renderTimeControls() {
    if (ImGui::Button(m_playing ? "Pause" : "Play")) {
        m_playing = !m_playing;
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset")) {
        m_time = 0.0;
        m_projection.setTime(0.0);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(120);
    ImGui::DragFloat("Speed", &m_timeSpeed, 0.1f, 0.01f, 100.0f, "%.2fx");
    ImGui::SameLine();
    ImGui::Text("T = %.3f yr", m_time);
}

void OrbitalEditor::refreshSystemList() {
    if (!m_vault) return;
    m_systemList = m_vault->listOrbitalSystems();
}

void OrbitalEditor::loadSystem(int64_t id) {
    if (!m_vault) return;
    m_system = OrbitalSystem();
    m_system.loadFromVault(m_vault, id);
    m_loadedSystemID = id;
    m_selectedBodyID = -1;
}

CelestialBody* OrbitalEditor::selectedBody() {
    if (m_selectedBodyID < 0 || !m_system.isLoaded()) return nullptr;
    return m_system.findBody(m_selectedBodyID);
}

} // namespace Orbital
