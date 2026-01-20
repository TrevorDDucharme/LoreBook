#pragma once

#include <string>
#include <vector>
#include <functional>
#include <nlohmann/json.hpp>

// Simple OpenAI-compatible HTTP client using libcurl
// - Supports GET and JSON POST with Authorization header
// - Returns Response containing status, body, and any libcurl error
// - Supports streaming completions via a callback

class OpenAIClient {
public:
    struct Response {
        long httpCode = 0;
        std::string body;
        std::string curlError; // empty on success
        bool ok() const { return curlError.empty() && (httpCode >= 200 && httpCode < 300); }
        nlohmann::json bodyJson() const { try { return nlohmann::json::parse(body); } catch(...) { return nlohmann::json(); } }
    };

    OpenAIClient();
    explicit OpenAIClient(const std::string& apiKey);
    ~OpenAIClient();

    void setApiKey(const std::string& key) { apiKey_ = key; }
    void setBaseUrl(const std::string& url) { baseUrl_ = url; }
    void setTimeoutSeconds(long seconds) { timeoutSeconds_ = seconds; }

    // Default headers applied to every request (e.g., for Azure's "api-key: ...")
    void setDefaultHeaders(const std::vector<std::string>& headers) { defaultHeaders_ = headers; }
    void addDefaultHeader(const std::string& h){ defaultHeaders_.push_back(h); }
    void clearDefaultHeaders(){ defaultHeaders_.clear(); }
    // Perform a GET request to baseUrl + path (path may include leading "/")
    Response get(const std::string& path, const std::vector<std::string>& extraHeaders = {});

    // Perform a POST with JSON body. Automatically sets
    // Content-Type: application/json and Authorization: Bearer <apiKey>
    Response postJson(const std::string& path, const std::string& jsonBody, const std::vector<std::string>& extraHeaders = {});
    Response postJson(const std::string& path, const nlohmann::json& jsonBody, const std::vector<std::string>& extraHeaders = {}){ return postJson(path, jsonBody.dump(), extraHeaders); }

    // Higher-level helpers
    Response listModels();
    Response chatCompletions(const nlohmann::json& body);

    // Streamed chat completions: body will have streaming options; cb is invoked for each received chunk
    using StreamCallback = std::function<void(const std::string& chunk)>;
    Response streamChatCompletions(const nlohmann::json& body, StreamCallback cb);

private:
    std::string buildUrl(const std::string& path) const;
    std::string apiKey_;
    std::string baseUrl_ = "https://api.openai.com/v1";
    long timeoutSeconds_ = 30;

    // Default headers applied to every request
    std::vector<std::string> defaultHeaders_;

    // libcurl global init flag
    static bool curlInitialized_;
};
