#pragma once
#include <imgui.h>
#include <WorldMaps/World/World.hpp>
#include <WorldMaps/World/Projections/MercatorProjection.hpp>
#include <WorldMaps/World/Projections/SphereProjection.hpp>

void worldMap(){
    //Worldmap window
        if(ImGui::Begin("World Map")){

            static World world;
            static MercatorProjection mercatorProj;
            static SphericalProjection sphereProj;
            static int worldMapLayer = 0;
            static std::string layerNames[] = { "elevation", "temperature", "humidity", "water", "biome", "river" };
            ImGui::Combo("layer", &worldMapLayer, "elevation\0temperature\0humidity\0water\0biome\0river\0");
            std::string selectedLayerName = layerNames[worldMapLayer];
            // Map control state
            static float mapCenterLon = 0.0f;
            static float mapCenterLat = 0.0f;
            static float mapZoom = 1.0f;
            static int lastLayer = -1;
            static GLuint worldMapTexture=0;
            static GLuint globeTexture=0;
            static float lastCenterLon = 0.0f, lastCenterLat = 0.0f, lastZoom = 1.0f;

            // Globe camera state (per-session)
            static float globeCenterLon = 0.0f; // degrees
            static float globeCenterLat = 0.0f; // degrees
            static float globeZoom = 3.0f;      // SphericalProjection::zoomLevel (smaller = closer)
            static float globeFovDeg = 45.0f;   // degrees
            static bool globeInvertY = false;   // invert vertical drag
            static float globeRotDegPerPixel = 0.25f; // degrees per pixel
            static float globeZoomFactor = 1.12f;     // per wheel tick
            static float globeMinZoom = 0.01f;
            static float globeMaxZoom = 64.0f;

            const int texWidth = 512, texHeight = 512;

            // Controls
            ImGui::Text("Controls: Drag to pan, mouse wheel to zoom");
            ImGui::PushItemWidth(100);
            ImGui::DragFloat("Lon", &mapCenterLon, 0.1f, 0.0f, 360.0f,"%.3f"); ImGui::SameLine();
            ImGui::DragFloat("Lat", &mapCenterLat, 0.1f, -90.0f, 90.0f,"%.3f");
            ImGui::SliderFloat("Zoom", &mapZoom, 1.0f, 32.0f, "x%.2f");
            if(ImGui::Button("Reset")) { mapCenterLon = 0.0f; mapCenterLat = 0.0f; mapZoom = 1.0f; }
            ImGui::PopItemWidth();

            // Image and interactions
            ImGui::Text(" "); // spacer
            ImVec2 imgSize((float)texWidth, (float)texHeight);
            ImGui::Text(" ");
            ImGui::BeginGroup();
            ImGui::BeginGroup();
            ImGui::Text("Map Preview:");

            //save current cursor position
            ImVec2 cursorPos = ImGui::GetCursorPos();

            // Update Mercator projection camera from UI state (deg -> rad)
            mercatorProj.setViewCenterRadians(mapCenterLon * static_cast<float>(M_PI) / 180.0f, mapCenterLat * static_cast<float>(M_PI) / 180.0f);
            mercatorProj.setZoomLevel(mapZoom);

            worldMapTexture = mercatorProj.project(world,texWidth, texHeight, selectedLayerName);

            if(worldMapTexture != 0){
                ImGui::Image((ImTextureID)(intptr_t)(worldMapTexture), imgSize, ImVec2(0,0), ImVec2(1,1));
            } else {
                // Reserve the image area so layout remains consistent
                ImGui::Dummy(imgSize);
            }

            //restore cursor position to overlay invisible button
            ImGui::SetCursorPos(cursorPos);

            // Use an invisible button over the image so we can reliably capture mouse interaction (hover, wheel, drag)
            ImGui::InvisibleButton("WorldMap_Invisible_Button", imgSize);
            ImGuiIO& io = ImGui::GetIO();

            // Mouse wheel zoom when hovered
            if (ImGui::IsItemHovered()){
                if(io.MouseWheel != 0.0f){
                    float factor = (io.MouseWheel > 0.0f) ? 1.1f : (1.0f/1.1f);
                    mapZoom = std::clamp(mapZoom * factor, 1.0f, 128.0f);
                }
            }

            // Panning with left mouse drag (operate in Mercator projected space)
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)){
                ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                float dx = drag.x;
                float dy = drag.y;
                // Convert current center lon/lat to projected u/v
                float u_center = (mapCenterLon + 180.0f) / 360.0f;
                float lat_rad = mapCenterLat * static_cast<float>(M_PI) / 180.0f;
                float mercN_center = std::log(std::tan(static_cast<float>(M_PI)/4.0f + lat_rad/2.0f));
                float v_center = 0.5f * (1.0f - mercN_center / static_cast<float>(M_PI));

                // Compute delta in projected normalized space
                float du = -dx / static_cast<float>(texWidth) / mapZoom; // negative so drag right moves map left
                float dv = dy / static_cast<float>(texHeight) / mapZoom;
                u_center += du;
                v_center += dv;

                // Wrap
                if (u_center < 0.0f) u_center = u_center - std::floor(u_center);
                if (u_center >= 1.0f) u_center = u_center - std::floor(u_center);
                if (v_center < 0.0f) v_center = v_center-std::floor(v_center);
                if (v_center > 1.0f) v_center = v_center - std::floor(v_center);

                // Convert back to lon/lat
                mapCenterLon = u_center * 360.0f - 180.0f;
                float mercN = static_cast<float>(M_PI) * (1.0f - 2.0f * v_center);
                mapCenterLat = 180.0f / static_cast<float>(M_PI) * std::atan(std::sinh(mercN));

                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            }
            ImGui::EndGroup();
            ImGui::SameLine();
            // --- Globe Preview (screen-space spherical sampling) ---
            ImGui::BeginGroup();
            ImGui::Separator();
            ImGui::Text("Globe Preview:");
            ImVec2 avail = ImGui::GetContentRegionAvail();
            // Make the globe viewport square: use the smaller of available width/height
            int maxDim = static_cast<int>(std::min(avail.x, avail.y));
            int glSide = std::max(128, std::min(2048, maxDim));
            int glW = glSide;
            int glH = glSide;
            ImVec2 globeSize((float)glSide, (float)glSide);

            // Save cursor for overlay
            ImVec2 globeCursor = ImGui::GetCursorPos();

            // Update spherical projection with current orbit camera state
            sphereProj.setViewCenterRadians(globeCenterLon * static_cast<float>(M_PI) / 180.0f, globeCenterLat * static_cast<float>(M_PI) / 180.0f);
            sphereProj.setZoomLevel(globeZoom);
            sphereProj.setFov(globeFovDeg * static_cast<float>(M_PI) / 180.0f);

            std::string selectedLayerNameGlobe = selectedLayerName; // reuse selection
            globeTexture = sphereProj.project(world, glW, glH, selectedLayerNameGlobe);

            if(globeTexture != 0){
                ImGui::Image((ImTextureID)(intptr_t)(globeTexture), globeSize, ImVec2(0,0), ImVec2(1,1));
            } else {
                ImGui::Dummy(globeSize);
            }

            // Overlay invisible button for interactions
            ImGui::SetCursorPos(globeCursor);
            ImGui::InvisibleButton("Globe_Invisible_Button", globeSize);

            // Mouse wheel zoom when hovered (wheel-up => zoom in)
            if(ImGui::IsItemHovered()){
                if(io.MouseWheel != 0.0f){
                    float factor = std::pow(globeZoomFactor, fabsf(io.MouseWheel));
                    if(io.MouseWheel > 0.0f){
                        // Wheel up -> zoom in (smaller zoom value => closer)
                        globeZoom = std::clamp(globeZoom / factor, globeMinZoom, globeMaxZoom);
                    } else {
                        // Wheel down -> zoom out
                        globeZoom = std::clamp(globeZoom * factor, globeMinZoom, globeMaxZoom);
                    }
                }
            }

            // Drag to rotate globe (left mouse drag)
            if(ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)){
                ImVec2 drag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
                float dx = drag.x; float dy = drag.y;
                globeCenterLon += dx * globeRotDegPerPixel;
                float sign = globeInvertY ? -1.0f : 1.0f;
                globeCenterLat += sign * dy * globeRotDegPerPixel;

                // Wrap longitude to [-180,180)
                while(globeCenterLon < -180.0f) globeCenterLon += 360.0f;
                while(globeCenterLon >= 180.0f) globeCenterLon -= 360.0f;

                // Clamp latitude to avoid gimbal lock
                globeCenterLat = std::clamp(globeCenterLat, -89.9f, 89.9f);

                ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
            }

            // Small overlay UI under the globe
            ImGui::BeginGroup();
            ImGui::SetCursorPos(ImVec2(globeCursor.x + 8, globeCursor.y + glSide - 60));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0,0,0,0.3f));
            ImGui::BeginChild("GlobeOverlay", ImVec2(glSide - 16, 56), false, ImGuiWindowFlags_NoDecoration);
            ImGui::Checkbox("Invert Y", &globeInvertY); ImGui::SameLine();
            ImGui::Text("Lon: %.2f  Lat: %.2f  Zoom: %.3f", globeCenterLon, globeCenterLat, globeZoom);
            ImGui::SameLine(); if(ImGui::Button("Reset Camera")) { globeCenterLon = 0.0f; globeCenterLat = 0.0f; globeZoom = 3.0f; globeFovDeg = 45.0f; globeInvertY = false; }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::EndGroup();

            ImGui::EndGroup();
            ImGui::EndGroup();
            
        }
        ImGui::End();
}