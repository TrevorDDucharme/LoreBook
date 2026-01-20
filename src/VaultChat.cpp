#include "VaultChat.hpp"
#include "Vault.hpp"
#include "VaultAssistant.hpp"
#include "OpenAIClient.hpp"
#include <imgui.h>
#include <cstdlib>
#include <vector>
#include <string>
#include <map>
#include <nlohmann/json.hpp>
#include <future>
#include <mutex>
#include <chrono>
#include <sstream>
#include <thread>

struct PendingRequest {
    enum Type { None=0, Text=1, Create=2 };
    Type type = None;
    std::future<std::string> fut;
};

void RenderVaultChat(Vault* vault){
    if(!vault) return;
    static std::map<Vault*, std::unique_ptr<PendingRequest>> pendingMap;
    static std::map<Vault*, int> spinnerPhase;

    // per-vault UI maps
    static std::map<Vault*, std::vector<std::pair<std::string,std::string>>> historyMap;
    static std::map<Vault*, std::string> inputMap;
    static std::map<Vault*, std::string> selectedModelMap;

    if(!vault) return;
    static OpenAIClient client;
    static bool clientInitialized = false;
    if(!clientInitialized){
        const char* env = std::getenv("OPENAI_API_KEY");
        if(env) client.setApiKey(env);
        clientInitialized = true;
    }

    static VaultAssistant* assistant = nullptr;
    if(!assistant) assistant = new VaultAssistant(vault, &client);

    // UI/endpoint state
    static std::string apiKeyInput;
    static std::string endpointUrl = "https://api.openai.com/v1";
    static std::string extraHeadersText;
    static std::map<Vault*, std::vector<std::string>> pendingSystemMessages;
    static std::mutex pendingSystemMutex;
    // timeout & streaming default controls (declare early so UI can use them)
    static int timeoutSeconds = 30;
    static bool streamingEnabled = false;

    if(!clientInitialized){
        const char* env = std::getenv("OPENAI_API_KEY");
        if(env) { client.setApiKey(env); apiKeyInput = env; }
        client.setBaseUrl(endpointUrl);
        client.setTimeoutSeconds(timeoutSeconds);
        clientInitialized = true;
    }

    // endpoint presets
    static std::vector<std::pair<std::string,std::string>> presets = {
        {"OpenAI (default)", "https://api.openai.com/v1"},
        {"Azure OpenAI (example)", "https://<your-resource>.openai.azure.com/openai"},
        {"Hugging Face (example)", "https://api-inference.huggingface.co/models"},
        {"Custom", ""}
    };

    // make models available for endpoint Apply
    static std::vector<std::string> models;
    static double lastModelFetch = 0.0;

    // Endpoint editor
    if(ImGui::CollapsingHeader("Endpoint & API", ImGuiTreeNodeFlags_DefaultOpen)){
        static int presetIdx = 0;
        if(ImGui::BeginCombo("Preset", presets[presetIdx].first.c_str())){
            for(int i=0;i<(int)presets.size();++i){
                bool sel = (i==presetIdx);
                if(ImGui::Selectable(presets[i].first.c_str(), sel)){
                    presetIdx = i;
                    if(!presets[i].second.empty()) endpointUrl = presets[i].second;
                    if(presets[i].first.find("Azure") != std::string::npos) extraHeadersText = "api-key: ";
                    else if(presets[i].first.find("Hugging Face") != std::string::npos) extraHeadersText = "Authorization: Bearer ";
                    else extraHeadersText.clear();
                }
            }
            ImGui::EndCombo();
        }
        ImGui::InputText("Endpoint URL", &endpointUrl);
        ImGui::InputText("API Key", &apiKeyInput);
        ImGui::InputTextMultiline("Extra Headers (one per line)", &extraHeadersText, ImVec2(0,80));
        ImGui::InputInt("Timeout (s)", &timeoutSeconds);
        ImGui::Checkbox("Enable Streaming", &streamingEnabled);
        if(ImGui::Button("Apply")){
            client.setBaseUrl(endpointUrl);
            client.setApiKey(apiKeyInput);
            client.setTimeoutSeconds(timeoutSeconds);
            std::vector<std::string> headers;
            std::istringstream hss(extraHeadersText);
            std::string line;
            while(std::getline(hss, line)){
                while(!line.empty() && isspace((unsigned char)line.back())) line.pop_back();
                while(!line.empty() && isspace((unsigned char)line.front())) line.erase(line.begin());
                if(!line.empty()) headers.push_back(line);
            }
            client.setDefaultHeaders(headers);
            // refresh model list synchronously (quick)
            models = assistant->listModels();
        }
        ImGui::SameLine();
        if(ImGui::Button("Test Endpoint")){
            // run async test and queue result for main thread to display
            std::lock_guard<std::mutex> lk(pendingSystemMutex);
            pendingSystemMessages[vault].push_back("Testing endpoint...");
            std::thread([vaultPtr = vault, url = endpointUrl, key = apiKeyInput, headers = extraHeadersText, &client, &pendingSystemMessages, &pendingSystemMutex](){
                OpenAIClient testClient = client; // copy config
                testClient.setBaseUrl(url);
                testClient.setApiKey(key);
                // apply headers
                std::vector<std::string> hvec;
                std::istringstream hss(headers);
                std::string l;
                while(std::getline(hss, l)){
                    while(!l.empty() && isspace((unsigned char)l.back())) l.pop_back();
                    while(!l.empty() && isspace((unsigned char)l.front())) l.erase(l.begin());
                    if(!l.empty()) hvec.push_back(l);
                }
                testClient.setDefaultHeaders(hvec);
                auto r = testClient.listModels();
                std::lock_guard<std::mutex> lk2(pendingSystemMutex);
                if(!r.ok()){
                    pendingSystemMessages[vaultPtr].push_back(std::string("Endpoint test failed: ") + r.curlError + " (" + std::to_string(r.httpCode) + ")");
                } else {
                    auto j = r.bodyJson();
                    if(j.contains("data") && j["data"].is_array()){
                        size_t n = j["data"].size();
                        pendingSystemMessages[vaultPtr].push_back(std::string("Endpoint test success: found ") + std::to_string(n) + " models");
                    } else {
                        pendingSystemMessages[vaultPtr].push_back(std::string("Endpoint test success (no models returned)"));
                    }
                }
            }).detach();
        }
    }

    // drain any pending system messages into history (thread-safe)
    {
        std::lock_guard<std::mutex> lk(pendingSystemMutex);
        auto it = pendingSystemMessages.find(vault);
        if(it != pendingSystemMessages.end()){
            for(auto &m : it->second) historyMap[vault].emplace_back("system", m);
            it->second.clear();
        }
    }

    // ensure per-vault pending request exists in map
    if(pendingMap.find(vault) == pendingMap.end()) pendingMap[vault] = std::make_unique<PendingRequest>();
    auto &pending = pendingMap[vault];

    // streaming UI state
    static std::map<Vault*, std::string> streamPartials;
    static std::mutex streamMutex;

    if(models.empty() && ImGui::GetTime() - lastModelFetch > 0.0){
        models = assistant->listModels();
        lastModelFetch = ImGui::GetTime();
        if(models.empty()) models.push_back("gpt-4o-mini");
    }
    if(models.empty() && ImGui::GetTime() - lastModelFetch > 0.0){
        models = assistant->listModels();
        lastModelFetch = ImGui::GetTime();
        if(models.empty()) models.push_back("gpt-4o-mini");
    }

    auto &history = historyMap[vault];
    auto &input = inputMap[vault];
    auto &selectedModel = selectedModelMap[vault];
    if(selectedModel.empty()) selectedModel = models.empty() ? std::string("gpt-4o-mini") : models[0];

    ImGui::TextDisabled("Model:"); ImGui::SameLine();
    if(ImGui::BeginCombo("##vault_model", selectedModel.c_str())){
        for(auto &m : models){
            bool sel = (m == selectedModel);
            if(ImGui::Selectable(m.c_str(), sel)) selectedModel = m;
        }
        ImGui::EndCombo();
    }

    // Show history
    ImGui::BeginChild("##vault_chat_history", ImVec2(0, -70), true);
    for(auto &m : history){
        if(m.first == "user"){
            ImGui::TextWrapped("%s", (std::string("You: ") + m.second).c_str());
        } else {
            ImGui::TextColored(ImVec4(0.2f,0.6f,0.9f,1.0f), "%s", (std::string("Vault: ") + m.second).c_str());
        }
        ImGui::Separator();
    }
    if(ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    // Poll pending request completion and update partials
    if(pending && pending->type != PendingRequest::None){
        auto status = pending->fut.wait_for(std::chrono::milliseconds(0));
        // show partial streaming content live
        if(streamingEnabled){
            std::lock_guard<std::mutex> lk(streamMutex);
            auto it = streamPartials.find(vault);
            if(it != streamPartials.end() && !it->second.empty()){
                // render partial directly under history (do not consume)
                ImGui::TextColored(ImVec4(0.5f,0.5f,0.6f,1.0f), "%s", (std::string("Vault (partial): ") + it->second).c_str());
            }
        }
        if(status == std::future_status::ready){
            std::string result = pending->fut.get();
            history.emplace_back("assistant", result.empty() ? std::string("(no response)") : result);
            // clear any partials
            {
                std::lock_guard<std::mutex> lk(streamMutex);
                streamPartials[vault].clear();
            }
            pendingMap[vault].reset(new PendingRequest());
        }
    }

    ImGui::InputTextMultiline("##vault_chat_input", &input, ImVec2(0,80));
    ImGui::SameLine();
    bool busy = (pending && pending->type != PendingRequest::None);
    if(busy) ImGui::BeginDisabled();
    if(ImGui::Button("Ask")){
        if(!input.empty() && !busy){
            history.emplace_back("user", input);
            // launch async request
            auto inCopy = input;
            auto modelCopy = selectedModel;
            pending->type = PendingRequest::Text;
            if(streamingEnabled){
                // streaming path: collect partials via callback
                {
                    std::lock_guard<std::mutex> lk(streamMutex);
                    streamPartials[vault].clear();
                }
                pending->fut = std::async(std::launch::async, [assistant, &client, inCopy, modelCopy, vault, &streamPartials, &streamMutex](){
                    // build context and body similar to askTextWithRAG
                    auto nodes = assistant->retrieveRelevantNodes(inCopy, 5);
                    std::string context = assistant->getRAGContext(nodes);
                    nlohmann::json msg = nlohmann::json::array();
                    msg.push_back({{"role","system"},{"content","You are a helpful assistant that answers questions using the provided Vault context. Use only the context for facts and cite Node IDs when referencing specific notes."}});
                    msg.push_back({{"role","user"},{"content", std::string("Context:\n") + context + std::string("\nQuestion: ") + inCopy}});
                    nlohmann::json body = {{"model", modelCopy}, {"messages", msg}};

                    std::string accumulated;
                    auto cb = [&accumulated, vault, &streamPartials, &streamMutex](const std::string& chunk){
                        // append to accumulated and also to per-vault partial buffer
                        accumulated += chunk;
                        std::lock_guard<std::mutex> lk(streamMutex);
                        streamPartials[vault] += chunk;
                    };
                    auto r = client.streamChatCompletions(body, cb);
                    if(!r.ok()) return std::string("[error] ") + r.curlError + " " + std::to_string(r.httpCode);
                    // optionally parse accumulated to extract final assistant content from SSE lines
                    return accumulated;
                });
            } else {
                pending->fut = std::async(std::launch::async, [assistant, inCopy, modelCopy](){
                    std::string r = assistant->askTextWithRAG(inCopy, modelCopy, 5);
                    if(r.empty()) r = "(no response)";
                    return r;
                });
            }
            input.clear();
        }
    }
    ImGui::SameLine();
    if(ImGui::Button("Ask & Create Node")){
        if(!input.empty() && !busy){
            history.emplace_back("user", input);
            // prepare schema
            nlohmann::json schema = {
                {"title","Node"},
                {"type","object"},
                {"properties", {
                    {"title", { {"type","string"} }},
                    {"content", { {"type","string"} }},
                    {"tags", { {"type","array"}, {"items", { {"type","string"} }}} }
                }},
                {"required", {"title","content"}}
            };
            auto inCopy = input;
            auto modelCopy = selectedModel;
            auto vaultPtr = vault;
            pending->type = PendingRequest::Create;
            pending->fut = std::async(std::launch::async, [assistant, inCopy, modelCopy, schema, vaultPtr](){
                auto j = assistant->askJSONWithRAG(inCopy, schema, modelCopy, 5);
                if(!j) return std::string("Failed to parse JSON from model response");
                int64_t parent = vaultPtr->getSelectedItemID() >= 0 ? vaultPtr->getSelectedItemID() : -1;
                int64_t id = assistant->createNodeFromJSON(*j, parent);
                if(id > 0) return std::string("Created node ID: ") + std::to_string(id);
                return std::string("Failed to create node from response");
            });
            input.clear();
        }
    }
    if(busy) ImGui::EndDisabled();

    // show small spinner/indicator when pending
    if(pending && pending->type != PendingRequest::None){
        int &phase = spinnerPhase[vault]; phase = (phase+1)%4;
        std::string dots(phase, '.');
        ImGui::SameLine(); ImGui::TextDisabled("Requesting%s", dots.c_str());
    }

    // Small help UI
    ImGui::SameLine();
    if(ImGui::Button("Clear")) history.clear();
    // Small help UI
    ImGui::SameLine();
    if(ImGui::Button("Clear")) history.clear();
}
