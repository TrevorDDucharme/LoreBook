#include <WorldMaps/WorldMap.hpp>
#include <Vault.hpp>

// ── Shared editing state (accessible from mercatorMap and worldMap) ────
static int  g_editMode       = 0;     // 0=Browse, 1=Paint
static float g_brushRadius   = 3.0f;  // in texels at current zoom
static float g_brushStrength = 0.1f;
static int   g_brushDeltaMode = 0;    // 0=Add, 1=Set

void mercatorMap(const char *label, ImVec2 texSize, World &world)
{
    // get imgui id
    ImGuiID id = ImGui::GetID(label);

    ImGui::PushItemWidth(texSize.x);
    ImGui::PushID(id);
    
    static std::unordered_map<unsigned int, int> worldMapLayerMap;
    static std::unordered_map<unsigned int, float> mapCenterLonMap; // degrees
    static std::unordered_map<unsigned int, float> mapCenterLatMap; // degrees
    static std::unordered_map<unsigned int, float> mapZoomMap;      // zoom level
    static std::unordered_map<unsigned int, float> lastCenterLonMap; // degrees
    static std::unordered_map<unsigned int, float> lastCenterLatMap; // degrees
    static std::unordered_map<unsigned int, float> lastZoomMap;      // zoom
    static std::unordered_map<unsigned int, GLuint> worldMapTextureMap;


    // Map control state
    int worldMapLayer = 0;
    float mapCenterLon = 0.0f;
    float mapCenterLat = 0.0f;
    float mapZoom = 1.0f;
    GLuint worldMapTexture = 0;
    float lastCenterLon = 0.0f, lastCenterLat = 0.0f, lastZoom = 1.0f;
    
    if(worldMapLayerMap.find(id) == worldMapLayerMap.end())
    {
        worldMapLayerMap[id] = 0;
    }else{
        worldMapLayer = worldMapLayerMap[id];
    }
    if(mapCenterLonMap.find(id) == mapCenterLonMap.end())
    {
        mapCenterLonMap[id] = 0.0f;
    }else{
        mapCenterLon = mapCenterLonMap[id];
    }
    if(mapCenterLatMap.find(id) == mapCenterLatMap.end())
    {
        mapCenterLatMap[id] = 0.0f;
    }else{
        mapCenterLat = mapCenterLatMap[id];
    }
    if(mapZoomMap.find(id) == mapZoomMap.end())
    {
        mapZoomMap[id] = 1.0f;
    }else{
        mapZoom = mapZoomMap[id];
    }
    if(lastCenterLonMap.find(id) == lastCenterLonMap.end())
    {
        lastCenterLonMap[id] = 0.0f;
    }else{
        lastCenterLon = lastCenterLonMap[id];
    }
    if(lastCenterLatMap.find(id) == lastCenterLatMap.end())
    {
        lastCenterLatMap[id] = 0.0f;
    }else{
        lastCenterLat = lastCenterLatMap[id];
    }
    if(lastZoomMap.find(id) == lastZoomMap.end())
    {   
        lastZoomMap[id] = 1.0f;
    }else{
        lastZoom = lastZoomMap[id];
    }
    if(worldMapTextureMap.find(id) == worldMapTextureMap.end())
    {
        worldMapTextureMap[id] = 0;
    }else{
        worldMapTexture = worldMapTextureMap[id];
    }
    
    MercatorProjection mercatorProj;
    std::vector<std::string> layerNames = world.getLayerNames();
    std::string layerNamesNullSeparated;
    for (const auto &name : layerNames)
    {
        layerNamesNullSeparated += name + '\0';
    }
    layerNamesNullSeparated += '\0';

    ImGui::BeginGroup();
    ImGui::Text("Mercator Preview:");

    ImGui::Combo("layer", &worldMapLayer, layerNamesNullSeparated.c_str());
    std::string selectedLayerName = layerNames[worldMapLayer];
    // save current cursor position
    ImVec2 cursorPos = ImGui::GetCursorPos();

    // Update Mercator projection camera from UI state (deg -> rad)
    mercatorProj.setViewCenterRadians(mapCenterLon * static_cast<float>(M_PI) / 180.0f, mapCenterLat * static_cast<float>(M_PI) / 180.0f);
    mercatorProj.setZoomLevel(mapZoom);

    mercatorProj.project(world, texSize.x, texSize.y,worldMapTexture, selectedLayerName);

    if (worldMapTexture != 0)
    {
        ImGui::Image((ImTextureID)(intptr_t)(worldMapTexture), texSize, ImVec2(1, 0), ImVec2(0, 1));
    }
    else
    {
        // Reserve the image area so layout remains consistent
        ImGui::Dummy(texSize);
    }

    // restore cursor position to overlay invisible button
    ImGui::SetCursorPos(cursorPos);

    // Use an invisible button over the image so we can reliably capture mouse interaction (hover, wheel, drag)
    ImGui::InvisibleButton("WorldMap_Invisible_Button", texSize);
    ImGuiIO &io = ImGui::GetIO();

    // Mouse wheel zoom when hovered
    if (ImGui::IsItemHovered())
    {
        if (io.MouseWheel != 0.0f)
        {
            float factor = (io.MouseWheel > 0.0f) ? 1.1f : (1.0f / 1.1f);
            mapZoom = std::clamp(mapZoom * factor, 1.0f, 100000.0f);
        }
    }

    // Panning with left mouse drag (operate in Mercator projected space)
    if (g_editMode == 0 && ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        float dx = -drag.x;
        float dy = -drag.y;
        // Convert current center lon/lat to projected u/v
        float u_center = (mapCenterLon + 180.0f) / 360.0f;
        float lat_rad = mapCenterLat * static_cast<float>(M_PI) / 180.0f;
        float mercN_center = std::log(std::tan(static_cast<float>(M_PI) / 4.0f + lat_rad / 2.0f));
        float v_center = 0.5f * (1.0f - mercN_center / static_cast<float>(M_PI));

        // Compute delta in projected normalized space
        float du = -dx / static_cast<float>(texSize.x) / mapZoom; // negative so drag right moves map left
        float dv = dy / static_cast<float>(texSize.y) / mapZoom;
        u_center += du;
        v_center += dv;

        // Wrap
        if (u_center < 0.0f)
            u_center = u_center - std::floor(u_center);
        if (u_center >= 1.0f)
            u_center = u_center - std::floor(u_center);
        if (v_center < 0.0f)
            v_center = v_center - std::floor(v_center);
        if (v_center > 1.0f)
            v_center = v_center - std::floor(v_center);

        // Convert back to lon/lat
        mapCenterLon = u_center * 360.0f - 180.0f;
        float mercN = static_cast<float>(M_PI) * (1.0f - 2.0f * v_center);
        mapCenterLat = 180.0f / static_cast<float>(M_PI) * std::atan(std::sinh(mercN));

        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    }

    // ── Paint mode: apply brush to world deltas ──────────────────
    if (g_editMode == 1 && ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        // Compute mouse position relative to image top-left
        ImVec2 imagePos = ImGui::GetItemRectMin();
        ImVec2 mousePos = io.MousePos;
        float mx = mousePos.x - imagePos.x;
        float my = mousePos.y - imagePos.y;

        if (mx >= 0 && mx < texSize.x && my >= 0 && my < texSize.y)
        {
            // Convert screen pixel to world lon/lat (Mercator inverse)
            float nx = mx / texSize.x;
            float ny = my / texSize.y;
            float centerLonDeg = mapCenterLon;
            float lat_rad_center = mapCenterLat * static_cast<float>(M_PI) / 180.0f;
            float mercYCenter = std::log(std::tan(static_cast<float>(M_PI) / 4.0f + lat_rad_center / 2.0f));

            float lonDeg = centerLonDeg + (nx - 0.5f) * (360.0f / mapZoom);
            float mercY = mercYCenter + (0.5f - ny) * (2.0f * static_cast<float>(M_PI) / mapZoom);
            float latDeg = std::atan(std::sinh(mercY)) * 180.0f / static_cast<float>(M_PI);

            // Determine current depth from zoom
            int depth = QuadTree::computeDepthForZoom(mapZoom, std::max(static_cast<int>(texSize.x), static_cast<int>(texSize.y)));

            // Convert to radians for chunk lookup
            float lonRad = lonDeg * static_cast<float>(M_PI) / 180.0f;
            float latRad = latDeg * static_cast<float>(M_PI) / 180.0f;

            // Clamp to valid range
            lonRad = std::clamp(lonRad, -static_cast<float>(M_PI), static_cast<float>(M_PI));
            latRad = std::clamp(latRad, -static_cast<float>(M_PI) / 2.0f, static_cast<float>(M_PI) / 2.0f);

            // Find the chunk containing this point
            int cells = 1 << depth;
            float lonRange = static_cast<float>(2.0 * M_PI);
            float latRange = static_cast<float>(M_PI);
            float cellW = lonRange / cells;
            float cellH = latRange / cells;
            int cx = static_cast<int>(std::floor((lonRad + static_cast<float>(M_PI)) / cellW));
            int cy = static_cast<int>(std::floor((latRad + static_cast<float>(M_PI / 2.0)) / cellH));
            cx = std::clamp(cx, 0, cells - 1);
            cy = std::clamp(cy, 0, cells - 1);
            ChunkCoord coord{cx, cy, depth};

            // Find the sample position within the chunk
            float cLonMin, cLonMax, cLatMin, cLatMax;
            coord.getBoundsRadians(cLonMin, cLonMax, cLatMin, cLatMax);
            float localU = (lonRad - cLonMin) / (cLonMax - cLonMin);
            float localV = (latRad - cLatMin) / (cLatMax - cLatMin);

            // Apply brush (iterate over brush radius in sample space)
            int brushRadSamples = std::max(1, static_cast<int>(g_brushRadius));
            int centerSX = static_cast<int>(localU * CHUNK_BASE_RES);
            int centerSY = static_cast<int>(localV * CHUNK_BASE_RES);

            LayerDelta& delta = world.getOrCreateDelta(coord, selectedLayerName);
            delta.mode = static_cast<DeltaMode>(g_brushDeltaMode);

            for (int bdy = -brushRadSamples; bdy <= brushRadSamples; ++bdy) {
                for (int bdx = -brushRadSamples; bdx <= brushRadSamples; ++bdx) {
                    int sx = centerSX + bdx;
                    int sy = centerSY + bdy;
                    if (sx < 0 || sx >= CHUNK_BASE_RES || sy < 0 || sy >= CHUNK_BASE_RES)
                        continue;
                    float dist = std::sqrt(static_cast<float>(bdx * bdx + bdy * bdy));
                    if (dist > g_brushRadius) continue;
                    float falloff = 1.0f - (dist / g_brushRadius);
                    float strength = g_brushStrength * falloff;

                    if (g_brushDeltaMode == 0) { // Add
                        float old = delta.getDelta(sx, sy);
                        delta.setDelta(sx, sy, 0, old + strength);
                    } else { // Set
                        delta.setDelta(sx, sy, 0, strength);
                    }
                }
            }
            world.markChunkDirty(coord, selectedLayerName);
        }
    }

    // Overlay UI (translucent box at bottom-left of the Mercator preview)
    ImGui::BeginGroup();
    ImGui::SetCursorPos(ImVec2(cursorPos.x + 8, cursorPos.y + texSize.y - 56));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.3f));
    ImGui::BeginChild("MercatorOverlay", ImVec2(texSize.x - 16, 56), false, ImGuiWindowFlags_NoDecoration);
    ImGui::Text("Lon: %.2f  Lat: %.2f  Zoom: %.3f", mapCenterLon, mapCenterLat, mapZoom);
    ImGui::SameLine();
    ImGui::PushItemWidth(110);
    float mercDragSpeed = std::max(0.1f, mapZoom * 0.01f);
    ImGui::DragFloat("##MercatorZoom", &mapZoom, mercDragSpeed, 1.0f, 100000.0f, "Zoom: %.2f");
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Reset Camera"))
    {
        mapCenterLon = 0.0f;
        mapCenterLat = 0.0f;
        mapZoom = 1.0f;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::EndGroup();

    ImGui::EndGroup();

    // persist state back to maps
    worldMapLayerMap[id] = worldMapLayer;
    mapCenterLonMap[id] = mapCenterLon;
    mapCenterLatMap[id] = mapCenterLat;
    mapZoomMap[id] = mapZoom;
    lastCenterLonMap[id] = lastCenterLon;
    lastCenterLatMap[id] = lastCenterLat;
    lastZoomMap[id] = lastZoom;
    worldMapTextureMap[id] = worldMapTexture;

    ImGui::PopID();
    ImGui::PopItemWidth();
}

void globeMap(const char *label, ImVec2 texSize, World &world)
{
    // get imgui id
    ImGuiID id = ImGui::GetID(label);

    ImGui::PushItemWidth(texSize.x);
    ImGui::PushID(id);

    static std::unordered_map<unsigned int, int> worldMapLayerMap;
    // Globe camera state (per-session)
    static std::unordered_map<unsigned int, float> globeCenterLonMap;      // degrees
    static std::unordered_map<unsigned int, float> globeCenterLatMap;      // degrees
    static std::unordered_map<unsigned int, float> globeZoomMap;           // SphericalProjection::zoomLevel (smaller = closer)
    static std::unordered_map<unsigned int, float> globeFovDegMap;         // degrees
    static std::unordered_map<unsigned int, bool> globeInvertYMap;         // invert vertical drag
    static std::unordered_map<unsigned int, float> globeRotDegPerPixelMap; // degrees per pixel
    static std::unordered_map<unsigned int, float> globeZoomFactorMap;     // per wheel tick
    static std::unordered_map<unsigned int, float> globeMinZoomMap;
    static std::unordered_map<unsigned int, float> globeMaxZoomMap;

    static std::unordered_map<unsigned int, GLuint> globeTextureMap;

    // Globe state (per-session)
    int worldMapLayer = 0;
    float globeCenterLon = 0.0f;       // degrees
    float globeCenterLat = 0.0f;       // degrees
    float globeZoom = 3.0f;            // SphericalProjection::zoomLevel (smaller = closer)
    float globeFovDeg = 45.0f;         // degrees
    bool globeInvertY = false;         // invert vertical drag
    float globeRotDegPerPixel = 0.25f; // degrees per pixel
    float globeZoomFactor = 1.12f;     // per wheel tick
    // Allow camera to enter the sphere by permitting negative zoomLevel (distance from surface).
    // Keep a safe minimum to avoid the camera reaching the origin.
    float globeMinZoom = -0.99f;
    float globeMaxZoom = 64.0f;
    GLuint globeTexture = 0;

    // find or create values in maps
    if (worldMapLayerMap.find(id) == worldMapLayerMap.end())
    {
        worldMapLayerMap[id] = 0;
    }
    else
    {
        worldMapLayer = worldMapLayerMap[id];
    }
    if (globeCenterLonMap.find(id) == globeCenterLonMap.end())
    {
        globeCenterLonMap[id] = 0.0f;
    }
    else
    {
        globeCenterLon = globeCenterLonMap[id];
    }
    if (globeCenterLatMap.find(id) == globeCenterLatMap.end())
    {
        globeCenterLatMap[id] = 0.0f;
    }
    else
    {
        globeCenterLat = globeCenterLatMap[id];
    }
    if (globeZoomMap.find(id) == globeZoomMap.end())
    {
        globeZoomMap[id] = 3.0f;
    }
    else
    {
        globeZoom = globeZoomMap[id];
    }
    if (globeFovDegMap.find(id) == globeFovDegMap.end())
    {
        globeFovDegMap[id] = 45.0f;
    }
    else
    {
        globeFovDeg = globeFovDegMap[id];
    }
    if (globeInvertYMap.find(id) == globeInvertYMap.end())
    {
        globeInvertYMap[id] = false;
    }
    else
    {
        globeInvertY = globeInvertYMap[id];
    }
    if (globeRotDegPerPixelMap.find(id) == globeRotDegPerPixelMap.end())
    {
        globeRotDegPerPixelMap[id] = 0.25f;
    }
    else
    {
        globeRotDegPerPixel = globeRotDegPerPixelMap[id];
    }
    if (globeZoomFactorMap.find(id) == globeZoomFactorMap.end())
    {
        globeZoomFactorMap[id] = 1.12f;
    }
    else
    {
        globeZoomFactor = globeZoomFactorMap[id];
    }
    if (globeMinZoomMap.find(id) == globeMinZoomMap.end())
    {
        globeMinZoomMap[id] = -0.99f; // allow entering the sphere, but not reaching center
    }
    else
    {
        globeMinZoom = globeMinZoomMap[id];
    }
    if (globeMaxZoomMap.find(id) == globeMaxZoomMap.end())
    {
        globeMaxZoomMap[id] = 64.0f;
    }
    else
    {
        globeMaxZoom = globeMaxZoomMap[id];
    }
    if (globeTextureMap.find(id) == globeTextureMap.end())
    {
        globeTextureMap[id] = 0;
    }
    else
    {
        globeTexture = globeTextureMap[id];
    }

    ImGuiIO &io = ImGui::GetIO();

    SphericalProjection sphereProj;
    std::vector<std::string> layerNames = world.getLayerNames();
    std::string layerNamesNullSeparated;
    for (const auto &name : layerNames)
    {
        layerNamesNullSeparated += name + '\0';
    }
    layerNamesNullSeparated += '\0';

    ImGui::BeginGroup();
    ImGui::Text("Globe Preview:");
    ImGui::Combo("layer", &worldMapLayer, layerNamesNullSeparated.c_str());
    std::string selectedLayerName = layerNames[worldMapLayer];

    // Save cursor for overlay
    ImVec2 globeCursor = ImGui::GetCursorPos();

    // Update spherical projection with current orbit camera state
    sphereProj.setViewCenterRadians(globeCenterLon * static_cast<float>(M_PI) / 180.0f, globeCenterLat * static_cast<float>(M_PI) / 180.0f);
    sphereProj.setZoomLevel(globeZoom);
    sphereProj.setFov(globeFovDeg * static_cast<float>(M_PI) / 180.0f);

    std::string selectedLayerNameGlobe = selectedLayerName; // reuse selection
    sphereProj.project(world, texSize.x, texSize.y, globeTexture, selectedLayerNameGlobe);

    if (globeTexture != 0)
    {
        ImGui::Image((ImTextureID)(intptr_t)(globeTexture), texSize, ImVec2(0, 0), ImVec2(1, 1));
    }
    else
    {
        ImGui::Dummy(texSize);
    }

    // Overlay invisible button for interactions
    ImGui::SetCursorPos(globeCursor);
    ImGui::InvisibleButton("Globe_Invisible_Button", texSize);

    // Mouse wheel zoom when hovered (wheel-up => zoom in)
    if (ImGui::IsItemHovered())
    {
        if (io.MouseWheel != 0.0f)
        {
            float factor = std::pow(globeZoomFactor, fabsf(io.MouseWheel));
            if (io.MouseWheel > 0.0f)
            {
                // Wheel up -> zoom in (smaller zoom value => closer)
                globeZoom = std::clamp(globeZoom / factor, globeMinZoom, globeMaxZoom);
            }
            else
            {
                // Wheel down -> zoom out
                globeZoom = std::clamp(globeZoom * factor, globeMinZoom, globeMaxZoom);
            }
        }
    }

    // Drag to rotate globe (left mouse drag)
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        float dx = drag.x;
        float dy = drag.y;
        globeCenterLon += dx * globeRotDegPerPixel;
        float sign = globeInvertY ? -1.0f : 1.0f;
        globeCenterLat += sign * dy * globeRotDegPerPixel;

        // Wrap longitude to [-180,180)
        while (globeCenterLon < -180.0f)
            globeCenterLon += 360.0f;
        while (globeCenterLon >= 180.0f)
            globeCenterLon -= 360.0f;

        // Clamp latitude to avoid gimbal lock
        globeCenterLat = std::clamp(globeCenterLat, -89.9f, 89.9f);

        ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    }

    // Small overlay UI under the globe
    ImGui::BeginGroup();
    ImGui::SetCursorPos(ImVec2(globeCursor.x + 8, globeCursor.y + texSize.y - 60));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0.3f));
    ImGui::BeginChild("GlobeOverlay", ImVec2(texSize.x - 16, 56), false, ImGuiWindowFlags_NoDecoration);
    ImGui::Checkbox("Invert Y", &globeInvertY);
    ImGui::SameLine();
    {
        float fovDegOverlay = globeFovDeg;
        int depthOverlay = QuadTree::computeDepthForGlobeZoom(
            globeZoom, fovDegOverlay, static_cast<int>(std::max(texSize.x, texSize.y)));
        float extentDeg = QuadTree::computeVisibleExtent(globeZoom, fovDegOverlay)
                          * 180.0f / static_cast<float>(M_PI);
        ImGui::Text("Lon:%.1f Lat:%.1f Z:%.3f D:%d Ext:%.1f",
                    globeCenterLon, globeCenterLat, globeZoom, depthOverlay, extentDeg);
    }

    ImGui::SameLine();
    ImGui::PushItemWidth(110);
    ImGui::DragFloat("##GlobeFOV", &globeFovDeg, 1.0f, 10.0f, 120.0f, "FOV: %.1f");
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ImGui::Button("Reset Camera"))
    {
        globeCenterLon = 0.0f;
        globeCenterLat = 0.0f;
        globeZoom = 3.0f;
        globeFovDeg = 45.0f;
        globeInvertY = false;
    }
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::EndGroup();

    ImGui::EndGroup();

     // persist state back to maps
    worldMapLayerMap[id] = worldMapLayer;
    globeCenterLonMap[id] = globeCenterLon;
    globeCenterLatMap[id] = globeCenterLat;
    globeZoomMap[id] = globeZoom;
    globeFovDegMap[id] = globeFovDeg;
    globeInvertYMap[id] = globeInvertY;
    globeRotDegPerPixelMap[id] = globeRotDegPerPixel;
    globeZoomFactorMap[id] = globeZoomFactor;
    globeMinZoomMap[id] = globeMinZoom;
    globeMaxZoomMap[id] = globeMaxZoom;
    globeTextureMap[id] = globeTexture;

    ImGui::PopID();
    ImGui::PopItemWidth();
}

void worldMap(bool& m_isOpen, Vault* vault)
{
    // Worldmap window
    if (ImGui::Begin("World Map", &m_isOpen, ImGuiWindowFlags_MenuBar))
    {
        static World world;
        static bool worldInitialized = false;
        if (!worldInitialized)
        {
            world.addLayer("elevation", std::make_unique<ElevationLayer>());
            world.addLayer("humidity", std::make_unique<HumidityLayer>());
            world.addLayer("temperature", std::make_unique<TemperatureLayer>());
            world.addLayer("color", std::make_unique<ColorLayer>());
            world.addLayer("landtype", std::make_unique<LandTypeLayer>());
            world.addLayer("latitude", std::make_unique<LatitudeLayer>());
            world.addLayer("watertable", std::make_unique<WaterTableLayer>());
            world.addLayer("rivers", std::make_unique<RiverLayer>());
            world.addLayer("tectonics", std::make_unique<TectonicsLayer>());
            world.addLayer("buildings", std::make_unique<BuildingLayer>());
            worldInitialized = true;
        }
        // Keep vault pointer up to date (may change between frames)
        world.setVault(vault);

        // Load deltas from vault on first vault connection
        static Vault* lastVault = nullptr;
        if (vault && vault != lastVault) {
            world.loadDeltasFromVault();
            lastVault = vault;
        }

        static ImVec2 texSize(512, 512);

        // ── Menu Bar ─────────────────────────────────────────────
        if (ImGui::BeginMenuBar())
        {
            if (ImGui::BeginMenu("Mode"))
            {
                if (ImGui::MenuItem("Browse", nullptr, g_editMode == 0)) g_editMode = 0;
                if (ImGui::MenuItem("Paint",  nullptr, g_editMode == 1)) g_editMode = 1;
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View"))
            {
                ImGui::DragFloat2("Preview Size", &texSize.x, 8.0f, 128.0f, 2048.0f, "%.0f");
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("Deltas"))
            {
                if (ImGui::MenuItem("Save Edits to Vault", nullptr, false, vault != nullptr)) {
                    world.saveDeltasToVault();
                }
                if (ImGui::MenuItem("Reload Edits from Vault", nullptr, false, vault != nullptr)) {
                    world.loadDeltasFromVault();
                }
                ImGui::EndMenu();
            }

            ImGui::EndMenuBar();
        }

        // ── Paint Mode Controls ──────────────────────────────────
        if (g_editMode == 1)
        {
            ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.15f, 0.12f, 0.18f, 0.90f));
            ImGui::BeginChild("PaintControls", ImVec2(0, 60), true);
            ImGui::Columns(4, nullptr, false);

            ImGui::Text("Brush:");
            ImGui::NextColumn();
            ImGui::SliderFloat("Radius", &g_brushRadius, 0.5f, 16.0f, "%.1f");
            ImGui::NextColumn();
            ImGui::SliderFloat("Strength", &g_brushStrength, 0.01f, 1.0f, "%.2f");
            ImGui::NextColumn();
            ImGui::RadioButton("Add", &g_brushDeltaMode, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Set", &g_brushDeltaMode, 1);

            ImGui::Columns(1);
            ImGui::EndChild();
            ImGui::PopStyleColor();
        }

        mercatorMap("Mercator World Map", texSize, world);
        ImGui::SameLine();
        globeMap("Globe World Map", texSize, world);
    }
    ImGui::End();
}