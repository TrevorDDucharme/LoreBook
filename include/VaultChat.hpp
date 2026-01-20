#pragma once

class Vault;

// Render the Vault chat UI (declarative): a model selector, history, input box, and actions to ask and create nodes.
// This uses OpenAI via VaultAssistant internally.
void RenderVaultChat(Vault* vault);
