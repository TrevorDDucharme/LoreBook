#pragma once

#include <string>

class Vault;

// Render the Script Editor window (dockable)
void RenderScriptEditor(Vault *vault, bool *pOpen);

// Request the editor to open a script by external path (e.g., "vault://Scripts/foo.lua")
void RequestOpenScriptEditor(const std::string &externalPath);
bool ConsumePendingOpenScriptEditor(std::string *outExternalPath);
bool HasPendingOpenScriptEditor();
