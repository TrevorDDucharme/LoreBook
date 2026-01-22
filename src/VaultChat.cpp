#include "VaultChat.hpp"
#include "Vault.hpp"
#include "VaultAssistant.hpp"
#include "LLMClient.hpp"
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
    static LLMClient client;
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
    static std::string endpointUrl = "http://127.0.0.1:1234/v1";
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
        {"LMStudio (default)", "http://127.0.0.1:1234/v1"},
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
            std::thread([vaultPtr = vault, url = endpointUrl, key = apiKeyInput, headers = extraHeadersText, clientPtr = &client, pendingSystemMessagesPtr = &pendingSystemMessages, pendingSystemMutexPtr = &pendingSystemMutex](){
                LLMClient testClient = *clientPtr; // copy config
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
                std::lock_guard<std::mutex> lk2(*pendingSystemMutexPtr);
                if(!r.ok()){
                    (*pendingSystemMessagesPtr)[vaultPtr].push_back(std::string("Endpoint test failed: ") + r.curlError + " (" + std::to_string(r.httpCode) + ")");
                } else {
                    auto j = r.bodyJson();
                    if(j.contains("data") && j["data"].is_array()){
                        size_t n = j["data"].size();
                        (*pendingSystemMessagesPtr)[vaultPtr].push_back(std::string("Endpoint test success: found ") + std::to_string(n) + " models");
                    } else {
                        (*pendingSystemMessagesPtr)[vaultPtr].push_back(std::string("Endpoint test success (no models returned)"));
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
    // track whether a partial assistant message is present in the history (so we can update in-place)
    static std::map<Vault*, bool> hasPartialInHistory;

    if(models.empty() && ImGui::GetTime() - lastModelFetch > 0.0){
        models = assistant->listModels();
        lastModelFetch = ImGui::GetTime();
        if(models.empty()) models.push_back("wayfarer@q8_0");
    }
    if(models.empty() && ImGui::GetTime() - lastModelFetch > 0.0){
        models = assistant->listModels();
        lastModelFetch = ImGui::GetTime();
        if(models.empty()) models.push_back("wayfarer@q8_0");
    }

    auto &history = historyMap[vault];
    auto &input = inputMap[vault];
    auto &selectedModel = selectedModelMap[vault];
    if(selectedModel.empty()) selectedModel = models.empty() ? std::string("wayfarer@q8_0") : models[0];

    ImGui::TextDisabled("Model:"); ImGui::SameLine();
    if(ImGui::BeginCombo("##vault_model", selectedModel.c_str())){
        for(auto &m : models){
            bool sel = (m == selectedModel);
            if(ImGui::Selectable(m.c_str(), sel)) selectedModel = m;
        }
        ImGui::EndCombo();
    }

    // Show history (use wrapped text and color for assistant messages)
    ImGui::BeginChild("##vault_chat_history", ImVec2(0, -70), true);
    for(auto &m : history){
        if(m.first == "user"){
            ImGui::TextWrapped("%s", (std::string("You: ") + m.second).c_str());
        } else {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.2f,0.6f,0.9f,1.0f));
            ImGui::TextWrapped("%s", (std::string("Vault: ") + m.second).c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Separator();
    }
    if(ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();

    // Poll pending request completion and update partials
    if(pending && pending->type != PendingRequest::None){
        auto status = pending->fut.wait_for(std::chrono::milliseconds(0));
        // integrate partial streaming content into history (show where it will end up)
        if(streamingEnabled){
            std::lock_guard<std::mutex> lk(streamMutex);
            auto it = streamPartials.find(vault);
            if(it != streamPartials.end() && !it->second.empty()){
                // if no partial message in history yet, append one; otherwise update the last assistant message
                bool &hasPartial = hasPartialInHistory[vault];
                if(!hasPartial){
                    history.emplace_back("assistant", it->second);
                    hasPartial = true;
                } else {
                    if(!history.empty() && history.back().first == "assistant"){
                        history.back().second = it->second;
                    } else {
                        // fallback: append if last is not assistant for some reason
                        history.emplace_back("assistant", it->second);
                        hasPartial = true;
                    }
                }
            }
        }
        if(status == std::future_status::ready){
            std::string result = pending->fut.get();
            bool hadPartial = false;
            auto itHas = hasPartialInHistory.find(vault);
            if(itHas != hasPartialInHistory.end()) { hadPartial = itHas->second; }

            if(hadPartial){
                if(!history.empty() && history.back().first == "assistant"){
                    history.back().second = result.empty() ? std::string("(no response)") : result;
                } else {
                    history.emplace_back("assistant", result.empty() ? std::string("(no response)") : result);
                }
                // clear flag
                hasPartialInHistory[vault] = false;
            } else {
                history.emplace_back("assistant", result.empty() ? std::string("(no response)") : result);
            }

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
                pending->fut = std::async(std::launch::async, [assistantPtr = assistant, clientPtr = &client, inCopy, modelCopy, vault, streamPartialsPtr = &streamPartials, streamMutexPtr = &streamMutex](){
                    // build context and body similar to askTextWithRAG
                    auto nodes = assistant->retrieveRelevantNodes(inCopy, 5);
                    std::string context = assistant->getRAGContext(nodes);
                    nlohmann::json msg = nlohmann::json::array();
                    msg.push_back({{"role","system"},{"content","You are a helpful assistant that answers questions using the provided Vault context. Use only the context for facts and cite Node IDs when referencing specific notes."}});
                    msg.push_back({{"role","user"},{"content", std::string("Context:\n") + context + std::string("\nQuestion: ") + inCopy}});
                    nlohmann::json body = {{"model", modelCopy}, {"messages", msg}};

                    // Log RAG streaming request (truncate context for logs)
                    try{
                        std::ostringstream ns;
                        ns << "[";
                        for(size_t i=0;i<nodes.size() && i<10;i++){
                            ns << nodes[i].id << ":" << nodes[i].name;
                            if(i+1 < nodes.size() && i<9) ns << ", ";
                        }
                        ns << "]";
                        std::string ctxLog = context;
                        if(ctxLog.size() > 2000) ctxLog = ctxLog.substr(0,2000) + "...(truncated)";
                        PLOGI << "[RAG][stream] model=" << modelCopy << " question=" << inCopy << " nodes=" << nodes.size() << " nodeSummary=" << ns.str() << " context_len=" << context.size();
                        PLOGI << "[RAG][stream] context: \n" << ctxLog;
                        PLOGI << "[RAG][stream] body: " << body.dump();
                    } catch(...){}

                    std::string accumulated;
                    auto cb = [&accumulated, vault, streamPartialsPtr, streamMutexPtr](const std::string& chunk){
                        // log fragment (truncated) and append to accumulated and per-vault partial buffer
                        try{
                            std::string disp = chunk;
                            if(disp.size() > 200) disp = disp.substr(0,200) + "...";
                            PLOGI << "[RAG][stream][frag] len=" << chunk.size() << " data=\"" << disp << "\"";
                        } catch(...){}
                        accumulated += chunk;
                        std::lock_guard<std::mutex> lk(*streamMutexPtr);
                        (*streamPartialsPtr)[vault] += chunk;
                    };
                    auto r = clientPtr->streamChatCompletions(body, cb);
                    if(!r.ok()){
                        PLOGW << "[RAG][stream] request failed: " << r.curlError << " (" << r.httpCode << ")";
                        return std::string("[error] ") + r.curlError + " " + std::to_string(r.httpCode);
                    }
                    // log completion and accumulated content (truncated)
                    try{
                        PLOGI << "[RAG][stream] completed accumulated_len=" << accumulated.size();
                        std::string accLog = accumulated;
                        if(accLog.size() > 2000) accLog = accLog.substr(0,2000) + "...(truncated)";
                        PLOGI << "[RAG][stream] accumulated: \n" << accLog;
                    } catch(...){}
                    return accumulated;
                });
            } else {
                pending->fut = std::async(std::launch::async, [assistantPtr = assistant, inCopy, modelCopy](){
                    std::string r = assistantPtr->askTextWithRAG(inCopy, modelCopy, 5);
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
            pending->fut = std::async(std::launch::async, [assistantPtr = assistant, inCopy, modelCopy, schema, vaultPtr](){
                auto j = assistantPtr->askJSONWithRAG(inCopy, schema, modelCopy, 5);
                if(!j) return std::string("Failed to parse JSON from model response");
                int64_t parent = vaultPtr->getSelectedItemID() >= 0 ? vaultPtr->getSelectedItemID() : -1;
                int64_t id = assistantPtr->createNodeFromJSON(*j, parent);
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
