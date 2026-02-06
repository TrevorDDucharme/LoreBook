#include "ResourceExplorer.hpp"
#include "Vault.hpp"
#include "ModelViewer.hpp"
#include <plog/Log.h>
#include "LuaCanvasBindings.hpp"
#include "Icons.hpp"
#include "ScriptEditor.hpp"
#include <WorldMaps/WorldMap.hpp>
#include <filesystem>

enum class ResourceType { None, Script, Asset, FloorPlan };

static bool isModelFileName(const std::string &n)
{
    try {
        std::string ext = std::filesystem::path(n).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        static const std::vector<std::string> models = {".obj", ".fbx", ".gltf", ".glb", ".ply", ".dae", ".stl"};
        for (auto &m : models)
            if (ext == m)
                return true;
    } catch (...) {}
    return false;
}

static std::string s_pendingOpenPath;

void RequestOpenResourceExplorer(const std::string &externalPath)
{
    s_pendingOpenPath = externalPath;
}

bool ConsumePendingOpenResourceExplorer(std::string *outExternalPath)
{
    if (s_pendingOpenPath.empty()) return false;
    if (outExternalPath) *outExternalPath = s_pendingOpenPath;
    s_pendingOpenPath.clear();
    return true;
}

bool HasPendingOpenResourceExplorer()
{
    return !s_pendingOpenPath.empty();
}

void RenderResourceExplorer(Vault *vault, bool *pOpen)
{
    // If a pending request exists, ensure the window is opened
    if (!s_pendingOpenPath.empty() && pOpen)
        *pOpen = true;

    if (!ImGui::Begin("Resource Explorer", pOpen)) { ImGui::End(); return; }
    if (!vault || !vault->isOpen())
    {
        ImGui::TextDisabled("Open a vault to browse resources.");
        ImGui::End();
        return;
    }

    static char filterBuf[128] = "";
    ImGui::InputTextWithHint("##resfilter", "Filter (substring)", filterBuf, sizeof(filterBuf));
    ImGui::Separator();

    ImGui::BeginChild("res_list", ImVec2(300, 0), true);
    static int selectedIdx = -1;
    static ResourceType selectedType = ResourceType::None;
    static std::string selectedPath;

    if (ImGui::CollapsingHeader("Scripts", ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto scripts = vault->listScripts();
        for (size_t i = 0; i < scripts.size(); ++i)
        {
            std::string full = scripts[i];
            std::string name = full;
            const std::string prefix = "vault://Scripts/";
            if (full.rfind(prefix, 0) == 0) name = full.substr(prefix.size());
            if (filterBuf[0] && name.find(filterBuf) == std::string::npos) continue;
            if (ImGui::Selectable(("script: " + name).c_str(), selectedType==ResourceType::Script && selectedIdx == (int)i))
            {
                selectedIdx = (int)i;
                selectedType = ResourceType::Script;
                selectedPath = full;
                // double-click to open in Script Editor
                if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                {
                    RequestOpenScriptEditor(full);
                }
            }
        }
    }

    if (ImGui::CollapsingHeader("Assets", ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto assets = vault->listAttachmentsByPrefix("vault://Assets/");
        for (size_t i = 0; i < assets.size(); ++i)
        {
            auto &a = assets[i];
            std::string display = a.name.empty() ? a.externalPath : a.name;
            if (filterBuf[0] && display.find(filterBuf) == std::string::npos) continue;
            if (ImGui::Selectable(("asset: " + display).c_str(), selectedType==ResourceType::Asset && selectedIdx == (int)i))
            {
                selectedIdx = (int)i;
                selectedType = ResourceType::Asset;
                selectedPath = a.externalPath;
            }
        }
    }

    if (ImGui::CollapsingHeader("Floor Plan Templates", ImGuiTreeNodeFlags_DefaultOpen))
    {
        auto fps = vault->listFloorPlanTemplates();
        for (size_t i = 0; i < fps.size(); ++i)
        {
            auto &f = fps[i];
            std::string display = f.name.empty() ? f.category : f.name;
            if (filterBuf[0] && display.find(filterBuf) == std::string::npos) continue;
            if (ImGui::Selectable(("floorplan: " + display).c_str(), selectedType==ResourceType::FloorPlan && selectedIdx == (int)i))
            {
                selectedIdx = (int)i;
                selectedType = ResourceType::FloorPlan;
                selectedPath = std::to_string(f.id);
            }
        }
    }

    // Handle pending external open requests
    if (!s_pendingOpenPath.empty())
    {
        const std::string req = s_pendingOpenPath;
        s_pendingOpenPath.clear();
        const std::string scriptsPrefix = "vault://Scripts/";
        const std::string assetsPrefix = "vault://Assets/";
        if (req.rfind(scriptsPrefix, 0) == 0)
        {
            auto scripts = vault->listScripts();
            for (size_t i = 0; i < scripts.size(); ++i)
            {
                if (scripts[i] == req)
                {
                    selectedIdx = (int)i;
                    selectedType = ResourceType::Script;
                    selectedPath = req;
                    break;
                }
            }
        }
        else if (req.rfind(assetsPrefix, 0) == 0)
        {
            auto assets = vault->listAttachmentsByPrefix(assetsPrefix);
            for (size_t i = 0; i < assets.size(); ++i)
            {
                if (assets[i].externalPath == req)
                {
                    selectedIdx = (int)i;
                    selectedType = ResourceType::Asset;
                    selectedPath = req;
                    break;
                }
            }
        }
    }

    ImGui::EndChild();

    ImGui::SameLine();
    ImGui::BeginChild("res_preview", ImVec2(0, 0), true);

    if (selectedType == ResourceType::Script)
    {
        ImGui::Text("Script: %s", selectedPath.c_str());
        std::string name = selectedPath;
        const std::string prefix = "vault://Scripts/";
        if (name.rfind(prefix, 0) == 0) name = name.substr(prefix.size());
        std::string src = vault->getScript(name);
        if (src.empty())
        {
            ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Script missing or failed to load: %s", name.c_str());
        }
        else
        {
            if (ImGui::SmallButton("Edit")) RequestOpenScriptEditor(selectedPath);
            ImGui::SameLine();
            ImGui::InputTextMultiline("##script_preview", (char*)src.c_str(), (int)src.size()+1, ImVec2(-FLT_MIN, ImGui::GetTextLineHeight() * 20), ImGuiInputTextFlags_ReadOnly);
        }
    }
    else if (selectedType == ResourceType::Asset)
    {
        ImGui::Text("Asset: %s", selectedPath.c_str());
        int64_t aid = vault->findAttachmentByExternalPath(selectedPath);
        if (aid == -1)
        {
            ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Asset not found: %s", selectedPath.c_str());
        }
        else
        {
            auto meta = vault->getAttachmentMeta(aid);
            if (meta.size > 0)
            {
                std::string key = std::string("vault:assets:") + std::to_string(aid);
                IconTexture cached = GetDynamicTexture(key);
                if (cached.loaded)
                {
                    float availW = ImGui::GetContentRegionAvail().x;
                    float width = std::min(availW, static_cast<float>(cached.width));
                    float scale = width / static_cast<float>(cached.width);
                    float height = static_cast<float>(cached.height) * scale;
                    ImGui::Image((ImTextureID)(intptr_t)cached.textureID, ImVec2(width, height));
                }
                else if (isModelFileName(meta.name) || isModelFileName(meta.externalPath))
                {
                    ModelViewer *mv = vault->getOrCreateModelViewerForSrc(selectedPath);
                    if (mv && mv->isLoaded())
                    {
                        ImVec2 avail = ImVec2(std::min(480.0f, ImGui::GetContentRegionAvail().x), std::min(320.0f, ImGui::GetTextLineHeight() * 12.0f));
                        mv->renderToRegion(avail);
                    }
                    else if (mv && mv->loadFailed())
                    {
                        ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Failed to load model: %s", meta.name.c_str());
                    }
                    else
                    {
                        ImGui::Text("Model: %s (loading or placeholder)", meta.name.c_str());
                    }
                }
                else
                {
                    ImGui::Text("Image: %s (%lld bytes)", meta.name.c_str(), (long long)meta.size);
                    ImGui::Text("(Texture not yet loaded; it will appear when loaded in background)");
                    // kick off a background load if data is present but texture not created
                    std::thread([vaultPtr = vault, aid, key]() {
                        auto data = vaultPtr->getAttachmentData(aid);
                        if (!data.empty()) {
                            auto dataPtr = std::make_shared<std::vector<uint8_t>>(std::move(data));
                            vaultPtr->enqueueMainThreadTask([key, dataPtr, aid]() {
                                LoadTextureFromMemory(key, *dataPtr);
                                PLOGV << "resource-explorer: loaded image aid=" << aid;
                            });
                        }
                    }).detach();
                }
            }
            else
            {
                ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Asset has no data: %s", meta.name.c_str());
            }
        }
    }
    else if (selectedType == ResourceType::FloorPlan)
    {
        ImGui::Text("Floor Plan: %s", selectedPath.c_str());
        ImGui::Text("Preview not implemented yet.");
    }
    else
    {
        ImGui::TextDisabled("Select a resource on the left to preview it.");
    }

    ImGui::EndChild();
    ImGui::End();
}
