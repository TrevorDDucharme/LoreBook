#include <WorldMaps/WorldMap.hpp>

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
        ImGui::Image((ImTextureID)(intptr_t)(worldMapTexture), texSize, ImVec2(0, 0), ImVec2(1, 1));
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
            mapZoom = std::clamp(mapZoom * factor, 1.0f, 128.0f);
        }
    }

    // Panning with left mouse drag (operate in Mercator projected space)
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
        float dx = drag.x;
        float dy = drag.y;
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
    float globeMinZoom = 0.01f;
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
        globeMinZoomMap[id] = 0.01f;
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
    ImGui::Text("Lon: %.2f  Lat: %.2f  Zoom: %.3f", globeCenterLon, globeCenterLat, globeZoom);
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

void worldMap()
{
    // Worldmap window
    if (ImGui::Begin("World Map"))
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
            worldInitialized = true;
        }
        static ImVec2 texSize(512, 512);
        

        mercatorMap("Mercator World Map", texSize, world);
        ImGui::SameLine();
        globeMap("Globe World Map", texSize, world);
    }
    ImGui::End();
}