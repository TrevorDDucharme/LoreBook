#pragma once
#include <WorldMaps/Orbital/OrbitalSystem.hpp>
#include <WorldMaps/Orbital/OrbitalProjection.hpp>
#include <imgui.h>
#include <GL/glew.h>
#include <cstdint>
#include <vector>
#include <string>

class Vault;

namespace Orbital {

class OrbitalEditor {
public:
    OrbitalEditor() = default;
    ~OrbitalEditor();

    bool isOpen() const { return m_isOpen; }
    void toggleOpen() { m_isOpen = !m_isOpen; }
    void setOpen(bool o) { m_isOpen = o; }
    void setVault(Vault* v) { m_vault = v; }

    void render();

private:
    bool m_isOpen = false;
    Vault* m_vault = nullptr;

    // Currently loaded system
    OrbitalSystem m_system;
    int64_t m_loadedSystemID = -1;

    // Projection / rendering
    OrbitalProjection m_projection;
    GLuint m_texture = 0;
    ImVec2 m_viewSize = {512, 512};

    // Time simulation
    double m_time = 0.0;
    float m_timeSpeed = 1.0f;  // simulation speed multiplier
    bool m_playing = false;

    // Camera drag
    bool m_dragging = false;
    ImVec2 m_lastMouse = {0, 0};

    // Selection
    int64_t m_selectedBodyID = -1;

    // System list cache
    std::vector<OrbitalSystemInfo> m_systemList;
    bool m_systemListDirty = true;

    // Last save status (displayed briefly after Save Changes)
    std::string m_lastSaveMsg;
    double m_lastSaveAt = 0.0;

    // Body creation state
    char m_newBodyName[128] = "";
    int m_newBodyType = 0;
    int64_t m_newBodyParentID = -1; // parent for newly created bodies
    float m_newBodyRadius = 0.5f;
    float m_newBodyOrbitRadius = 5.0f;
    float m_newBodyPeriod = 1.0f;

    // System creation state
    char m_newSystemName[128] = "";
    int m_newSystemType = 1; // Stellar by default

    void renderMenuBar();
    void renderSystemSelector();
    void renderBodyTree();
    void renderBodyTreeNode(CelestialBody* body);
    void renderPropertiesPanel();
    void renderViewport();
    void renderTimeControls();

    void refreshSystemList();
    void loadSystem(int64_t id);
    CelestialBody* selectedBody();
};

} // namespace Orbital
