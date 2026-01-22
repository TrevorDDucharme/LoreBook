#include "LLMClient.hpp"
#include <curl/curl.h>
#include <sstream>
#include <cctype>

bool LLMClient::curlInitialized_ = false;

static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp){
    size_t realsize = size * nmemb;
    std::string* mem = reinterpret_cast<std::string*>(userp);
    if(mem) mem->append(reinterpret_cast<char*>(contents), realsize);
    return realsize;
}

// Streaming write callback that parses SSE "data:" lines and extracts content fragments
// It expects userp to be a pointer to a small StreamState struct allocated by the caller.
// StreamState used by the SSE parser
struct StreamState {
    std::function<void(const std::string&)> cb;
    std::string buf;
};

static size_t streamWriteCallback(void* contents, size_t size, size_t nmemb, void* userp){
    size_t realsize = size * nmemb;
    if(!userp) return realsize;

    auto state = reinterpret_cast<StreamState*>(userp);
    // append incoming bytes
    state->buf.append(reinterpret_cast<char*>(contents), realsize);

    // process complete lines (SSE events are line-delimited)
    size_t pos = 0;
    while((pos = state->buf.find('\n')) != std::string::npos){
        std::string line = state->buf.substr(0, pos);
        // drop CR if present
        if(!line.empty() && line.back() == '\r') line.pop_back();
        state->buf.erase(0, pos + 1);

        // trim leading spaces
        size_t start = 0;
        while(start < line.size() && isspace((unsigned char)line[start])) ++start;
        if(start) line = line.substr(start);

        // only process data: lines
        if(line.rfind("data:", 0) == 0){
            std::string payload = line.substr(5);
            // trim leading spaces
            size_t pstart = 0;
            while(pstart < payload.size() && isspace((unsigned char)payload[pstart])) ++pstart;
            if(pstart) payload = payload.substr(pstart);
            if(payload == "[DONE]"){
                // stream finished
                continue;
            }
            try{
                auto j = nlohmann::json::parse(payload);
                if(j.is_object() && j.contains("choices")){
                    for(auto &c : j["choices"]){
                        // delta.content (streaming)
                        if(c.contains("delta") && c["delta"].is_object() && c["delta"].contains("content") && c["delta"]["content"].is_string()){
                            std::string content = c["delta"]["content"].get<std::string>();
                            if(!content.empty()){
                                try{ state->cb(content); } catch(...){}
                            }
                        }
                        // final message content (non-streamed or final chunk)
                        else if(c.contains("message") && c["message"].is_object() && c["message"].contains("content") && c["message"]["content"].is_string()){
                            std::string content = c["message"]["content"].get<std::string>();
                            if(!content.empty()){
                                try{ state->cb(content); } catch(...){}
                            }
                        }
                        // legacy/text field
                        else if(c.contains("text") && c["text"].is_string()){
                            std::string content = c["text"].get<std::string>();
                            if(!content.empty()){
                                try{ state->cb(content); } catch(...){}
                            }
                        }
                    }
                } else {
                    // not a choices object — as a fallback, emit the payload (trimmed)
                    if(!payload.empty()){
                        try{ state->cb(payload); } catch(...){}
                    }
                }
            } catch(...){
                // ignore parse errors — partial JSON may arrive later, or it's non-JSON SSE
            }
        }
    }

    return realsize;
}

LLMClient::LLMClient(){
    if(!curlInitialized_){
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curlInitialized_ = true;
    }
}

LLMClient::LLMClient(const std::string& apiKey): LLMClient(){
    apiKey_ = apiKey;
}

LLMClient::~LLMClient(){
    // We don't call curl_global_cleanup here to avoid interfering if other parts of the program
    // are also using libcurl and may expect it to remain initialized until process exit.
}

std::string LLMClient::buildUrl(const std::string& path) const {
    if(path.empty()) return baseUrl_;
    if(path.front() == '/') return baseUrl_ + path;
    return baseUrl_ + "/" + path;
}

LLMClient::Response LLMClient::get(const std::string& path, const std::vector<std::string>& extraHeaders){
    Response resp;
    CURL* curl = curl_easy_init();
    if(!curl){ resp.curlError = "curl_easy_init failed"; return resp; }

    std::string url = buildUrl(path);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds_);

    // Build headers (include default headers first)
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Accept: application/json");
    for(const auto &dh : defaultHeaders_) headers = curl_slist_append(headers, dh.c_str());
    if(!apiKey_.empty()){
        std::string auth = std::string("Authorization: Bearer ") + apiKey_;
        headers = curl_slist_append(headers, auth.c_str());
    }
    for(const auto& h : extraHeaders){ headers = curl_slist_append(headers, h.c_str()); }
    if(headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    if(res != CURLE_OK){ resp.curlError = curl_easy_strerror(res); }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.httpCode);

    if(headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}

LLMClient::Response LLMClient::postJson(const std::string& path, const std::string& jsonBody, const std::vector<std::string>& extraHeaders){
    Response resp;
    CURL* curl = curl_easy_init();
    if(!curl){ resp.curlError = "curl_easy_init failed"; return resp; }

    std::string url = buildUrl(path);
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)jsonBody.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &resp.body);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds_);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: application/json");
    for(const auto &dh : defaultHeaders_) headers = curl_slist_append(headers, dh.c_str());
    if(!apiKey_.empty()){
        std::string auth = std::string("Authorization: Bearer ") + apiKey_;
        headers = curl_slist_append(headers, auth.c_str());
    }
    for(const auto& h : extraHeaders){ headers = curl_slist_append(headers, h.c_str()); }
    if(headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    CURLcode res = curl_easy_perform(curl);
    if(res != CURLE_OK){ resp.curlError = curl_easy_strerror(res); }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.httpCode);

    if(headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}

LLMClient::Response LLMClient::streamChatCompletions(const nlohmann::json& body, StreamCallback cb){
    Response resp;
    CURL* curl = curl_easy_init();
    if(!curl){ resp.curlError = "curl_easy_init failed"; return resp; }

    // Ensure streaming flag in payload
    nlohmann::json bodyCopy = body;
    bodyCopy["stream"] = true;
    std::string payload = bodyCopy.dump();

    std::string url = buildUrl("/chat/completions");
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, (long)payload.size());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, streamWriteCallback);
    // pass callback object by pointer wrapped in a small StreamState so the write callback can accumulate and parse SSE
    auto state = new StreamState{cb, std::string()};
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, state);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeoutSeconds_);

    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    headers = curl_slist_append(headers, "Accept: text/event-stream, application/json");
    for(const auto &dh : defaultHeaders_) headers = curl_slist_append(headers, dh.c_str());
    if(!apiKey_.empty()){
        std::string auth = std::string("Authorization: Bearer ") + apiKey_;
        headers = curl_slist_append(headers, auth.c_str());
    }
    if(headers) curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

    // Perform (blocking) — caller should run this on worker thread
    CURLcode res = curl_easy_perform(curl);
    if(res != CURLE_OK){ resp.curlError = curl_easy_strerror(res); }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &resp.httpCode);

    // cleanup
    delete state;
    if(headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}

// Higher-level helpers
LLMClient::Response LLMClient::listModels(){
    return get("/models");
}

LLMClient::Response LLMClient::chatCompletions(const nlohmann::json& body){
    return postJson("/chat/completions", body);
}
