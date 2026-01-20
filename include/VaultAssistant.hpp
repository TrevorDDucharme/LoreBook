#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include <optional>

class Vault;
class OpenAIClient;

class VaultAssistant {
public:
    struct Node { int64_t id; std::string name; std::string content; std::string tags; double score = 0.0; };

    VaultAssistant(Vault* v, OpenAIClient* c);

    // Return a list of available model ids
    std::vector<std::string> listModels();

    // Retrieve up to k relevant nodes by simple substring-occurrence heuristic
    std::vector<Node> retrieveRelevantNodes(const std::string& query, int k = 5);

    // Ask a free-text question using RAG (returns assistant text or empty on error)
    std::string askTextWithRAG(const std::string& question, const std::string& model="gpt-4o-mini", int k=5);

    // Ask a question and request JSON output that should match a schema. Returns parsed JSON if success.
    std::optional<nlohmann::json> askJSONWithRAG(const std::string& question, const nlohmann::json& schema, const std::string& model="gpt-4o-mini", int k=5);

    // Create a node in the vault from JSON that contains at least {title, content}; returns created ID or -1
    int64_t createNodeFromJSON(const nlohmann::json& doc, int64_t parentIfAny = -1);

    // Build a RAG context text from nodes (public wrapper)
    std::string getRAGContext(const std::vector<Node>& nodes, int charLimitPerNode = 800);

private:
    Vault* vault_ = nullptr;
    OpenAIClient* client_ = nullptr;

    std::string buildRAGContext(const std::vector<Node>& nodes, int charLimitPerNode = 800);

    // Attempt to create and populate an FTS index for quick retrieval; returns true if FTS is usable
    bool ensureFTSIndex();

    // Validate a JSON document against a minimal schema (checks required properties and basic types)
    bool validateJsonAgainstSchema(const nlohmann::json& doc, const nlohmann::json& schema);
};
