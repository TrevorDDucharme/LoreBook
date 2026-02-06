#include "ScriptEditor.hpp"
#include "Vault.hpp"
#include <plog/Log.h>
#include <imgui.h>
#include <vector>
#include <string>
#include <algorithm>
#include <memory>

#include <Editors/Lua/LuaEditor.hpp>
#include <FileBackends/VaultFileBackend.hpp>

static std::string s_pendingOpenScriptPath;

void RequestOpenScriptEditor(const std::string &externalPath)
{
    s_pendingOpenScriptPath = externalPath;
}

bool ConsumePendingOpenScriptEditor(std::string *outExternalPath)
{
    if (s_pendingOpenScriptPath.empty()) return false;
    if (outExternalPath) *outExternalPath = s_pendingOpenScriptPath;
    s_pendingOpenScriptPath.clear();
    return true;
}

bool HasPendingOpenScriptEditor()
{
    return !s_pendingOpenScriptPath.empty();
}

void RenderScriptEditor(Vault *vault, bool *pOpen)
{
    // If there's a pending request, ensure the window is opened
    if (!s_pendingOpenScriptPath.empty() && pOpen) *pOpen = true;

    // Ensure Script Editor opens on the main viewport and docks into the main dockspace
    if (ImGui::GetMainViewport())
        ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGui::SetNextWindowDockID(ImGui::GetID("MyDockSpace"), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Script Editor", pOpen)) { ImGui::End(); return; }

    if (!vault || !vault->isOpen()) {
        ImGui::TextDisabled("Open a vault to edit scripts."); ImGui::End(); return;
    }

    // Ensure LuaEditor uses a VaultFileBackend so it can read/write vault:// URIs
    try {
        auto vb = std::make_shared<VaultFileBackend>(vault);
        Lua::LuaEditor::get().setFileBackend(vb);
    } catch (...) {
        PLOGW << "ScriptEditor: failed to attach VaultFileBackend to LuaEditor";
    }

    // Handle pending open requests (support vault://Scripts/<name>)
    if (!s_pendingOpenScriptPath.empty())
    {
        std::string req = s_pendingOpenScriptPath;
        s_pendingOpenScriptPath.clear();
        const std::string prefix = "vault://Scripts/";
        if (req.rfind(prefix, 0) == 0)
        {
            // Pass the vault URI directly to LuaEditor; FileUri parsing will handle it
            Lua::LuaEditor::get().openFile(std::filesystem::path(req));
        }
    }

    // Draw the Lua editor UI (it will manage its own tabs/state)
    Lua::LuaEditor::get().draw();

    ImGui::End();
}
