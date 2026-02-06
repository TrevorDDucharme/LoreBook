#pragma once
#include <imgui.h>
#include <string>
class Vault;

// Render the Resource Explorer dockable window. Pass a pointer to the open vault.
void RenderResourceExplorer(Vault *vault, bool *pOpen);

// Request the Resource Explorer to open and select a resource by external path (e.g., "vault://Scripts/foo.lua").
// The request is consumed by the main loop and will open the explorer on the next frame.
void RequestOpenResourceExplorer(const std::string &externalPath);

// Consume a pending open request (returns true if there was a pending request and clears it)
bool ConsumePendingOpenResourceExplorer(std::string *outExternalPath);

// Return whether there is a pending resource-explorer open request (does not consume it)
bool HasPendingOpenResourceExplorer();
