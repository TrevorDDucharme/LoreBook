#include "OpenAIClient.hpp"
#include <curl/curl.h>
#include <sstream>

bool OpenAIClient::curlInitialized_ = false;

static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp){
    size_t realsize = size * nmemb;
    std::string* mem = reinterpret_cast<std::string*>(userp);
    if(mem) mem->append(reinterpret_cast<char*>(contents), realsize);
    return realsize;
}

// Streaming write callback: userp is a pointer to a std::function<void(const std::string&)> inside a struct
static size_t streamWriteCallback(void* contents, size_t size, size_t nmemb, void* userp){
    size_t realsize = size * nmemb;
    if(userp){
        auto cb = reinterpret_cast<std::function<void(const std::string&)>*>(userp);
        try{ (*cb)(std::string(reinterpret_cast<char*>(contents), realsize)); } catch(...){}
    }
    return realsize;
}

OpenAIClient::OpenAIClient(){
    if(!curlInitialized_){
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curlInitialized_ = true;
    }
}

OpenAIClient::OpenAIClient(const std::string& apiKey): OpenAIClient(){
    apiKey_ = apiKey;
}

OpenAIClient::~OpenAIClient(){
    // We don't call curl_global_cleanup here to avoid interfering if other parts of the program
    // are also using libcurl and may expect it to remain initialized until process exit.
}

std::string OpenAIClient::buildUrl(const std::string& path) const {
    if(path.empty()) return baseUrl_;
    if(path.front() == '/') return baseUrl_ + path;
    return baseUrl_ + "/" + path;
}

OpenAIClient::Response OpenAIClient::get(const std::string& path, const std::vector<std::string>& extraHeaders){
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

OpenAIClient::Response OpenAIClient::postJson(const std::string& path, const std::string& jsonBody, const std::vector<std::string>& extraHeaders){
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

OpenAIClient::Response OpenAIClient::streamChatCompletions(const nlohmann::json& body, StreamCallback cb){
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
    // pass callback object by pointer
    auto cbHolder = new std::function<void(const std::string&)>(cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, cbHolder);
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
    delete cbHolder;
    if(headers) curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return resp;
}

// Higher-level helpers
OpenAIClient::Response OpenAIClient::listModels(){
    return get("/models");
}

OpenAIClient::Response OpenAIClient::chatCompletions(const nlohmann::json& body){
    return postJson("/chat/completions", body);
}
