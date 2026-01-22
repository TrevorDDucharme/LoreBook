#include "MergeConflictUI.hpp"
#include "VaultHistory.hpp"
#include <plog/Log.h>
#include <imgui.h>

namespace LoreBook {

void RenderMergeConflictModal(Vault* vault, bool* pOpen){
    if(!pOpen || !*pOpen) return;
    if(!vault) return;
    int64_t curUser = vault->getCurrentUserID();
    if(curUser <= 0 || !vault->isUserAdmin(curUser)){
        ImGui::OpenPopup("Not authorized");
        if(ImGui::BeginPopupModal("Not authorized", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::Text("Only admins can resolve merge conflicts.");
            if(ImGui::Button("OK")) { ImGui::CloseCurrentPopup(); *pOpen = false; }
            ImGui::EndPopup();
        }
        return;
    }

    static std::string selectedConflict;
    static std::vector<ConflictRecord> conflicts;
    static std::map<std::string,std::string> mergedEdits;

    ImGui::SetNextWindowSize(ImVec2(900,600), ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("Merge Conflicts", pOpen, ImGuiWindowFlags_NoSavedSettings)){
        ImGui::End();
        return;
    }

    // Refresh list
    conflicts.clear();
    LoreBook::VaultHistory* hist = vault->getHistoryPublic();
    if(hist){
        conflicts = hist->listOpenConflicts(-1);
    }

    ImGui::Columns(2, "mc_cols", true);
    ImGui::BeginChild("left_conflicts");
    ImGui::Text("Open Conflicts (%zu)", conflicts.size());
    ImGui::Separator();
    for(auto &c : conflicts){
        std::string label = c.ConflictID + " - item:" + std::to_string(c.ItemID) + " field:" + c.FieldName;
        if(ImGui::Selectable(label.c_str(), selectedConflict == c.ConflictID)){
            selectedConflict = c.ConflictID;
            mergedEdits.clear();
        }
    }
    ImGui::EndChild();
    ImGui::NextColumn();

    ImGui::BeginChild("right_detail");
    if(selectedConflict.empty()){
        ImGui::Text("Select a conflict to view details");
    } else {
        ConflictRecord detail = hist->getConflictDetail(selectedConflict);
        ImGui::Text("Conflict: %s", detail.ConflictID.c_str());
        ImGui::Text("Item: %lld Field: %s", (long long)detail.ItemID, detail.FieldName.c_str());
        ImGui::Text("Originator: %lld Created: %lld", (long long)detail.OriginatorUserID, (long long)detail.CreatedAt);
        ImGui::Separator();
        // Show Base, Local, Remote and editable Merged
        std::string baseVal = hist ? hist->getFieldValue(detail.BaseRevisionID, detail.ItemID, detail.FieldName) : std::string();
        std::string localVal = hist ? hist->getFieldValue(detail.LocalRevisionID, detail.ItemID, detail.FieldName) : std::string();
        std::string remoteVal = hist ? hist->getFieldValue(detail.RemoteRevisionID, detail.ItemID, detail.FieldName) : std::string();
        ImGui::Text("Base:"); ImGui::Separator(); ImGui::TextWrapped("%s", baseVal.c_str());
        ImGui::Text("Local:"); ImGui::Separator(); ImGui::TextWrapped("%s", localVal.c_str());
        ImGui::Text("Remote:"); ImGui::Separator(); ImGui::TextWrapped("%s", remoteVal.c_str());
        ImGui::Separator();
        // Merged editable
        if(mergedEdits.find(detail.FieldName) == mergedEdits.end()) mergedEdits[detail.FieldName] = localVal;
        ImGui::Text("Merged (editable):");
        static ImGuiInputTextFlags flags = ImGuiInputTextFlags_AllowTabInput | ImGuiInputTextFlags_CallbackResize;
        std::string &mergedRef = mergedEdits[detail.FieldName];
        ImGui::InputTextMultiline(("merged_" + detail.ConflictID).c_str(), &mergedRef);
        ImGui::NewLine();
        if(ImGui::Button("Accept Merged")){
            std::map<std::string,std::string> mvals; mvals[detail.FieldName] = mergedRef;
            bool ok = hist->adminResolveConflict(selectedConflict, curUser, mvals, "Admin resolved", true);
            if(ok){
                ImGui::OpenPopup("Resolved");
                ImGui::CloseCurrentPopup();
                selectedConflict.clear();
            } else {
                ImGui::OpenPopup("ResolveFailed");
            }
        }
        ImGui::SameLine();
        if(ImGui::Button("Reject Local (keep remote)")){
            std::map<std::string,std::string> mvals; mvals[detail.FieldName] = remoteVal;
            hist->adminResolveConflict(selectedConflict, curUser, mvals, "Admin rejected local - kept remote", true);
            selectedConflict.clear();
        }
    }
    ImGui::EndChild();

    ImGui::Columns(1);
    ImGui::End();
}

} // namespace LoreBook