#include "VaultAssistant.hpp"
#include "LLMClient.hpp"
#include "Vault.hpp"
#include <plog/Log.h>
#include <sstream>
#include <algorithm>
#include <nlohmann/json.hpp>

VaultAssistant::VaultAssistant(Vault* v, LLMClient* c): vault_(v), client_(c){}

bool VaultAssistant::ensureFTSIndex(){
    if(!vault_) return false;
    // Prefer backend-managed FTS when a remote backend is in use
    auto b = vault_->getDBBackendPublic();
    if(b && b->isOpen()){
        if(!b->supportsFullText()) return false;
        std::string err;
        // create FTS entries/indices for Name, Content, Tags as appropriate
        if(!b->ensureFullTextIndex("VaultItems", "Name", &err)){
            PLOGW << "ensureFTSIndex: failed to create Name FTS index: " << err;
            return false;
        }
        if(!b->ensureFullTextIndex("VaultItems", "Content", &err)){
            PLOGW << "ensureFTSIndex: failed to create Content FTS index: " << err;
            return false;
        }
        if(!b->ensureFullTextIndex("VaultItems", "Tags", &err)){
            PLOGW << "ensureFTSIndex: failed to create Tags FTS index: " << err;
            return false;
        }
        return true;
    }

    // Fallback to SQLite local DB behavior
    sqlite3* db = vault_->getDBPublic();
    if(!db) return false;
    const char* createFTS = "CREATE VIRTUAL TABLE IF NOT EXISTS VaultItemsFTS USING fts5(Name, Content, Tags);";
    char* err = nullptr;
    if(sqlite3_exec(db, createFTS, nullptr, nullptr, &err) != SQLITE_OK){ if(err) sqlite3_free(err); return false; }
    // Rebuild content for the FTS table (simple approach: remove and reinsert)
    const char* clear = "DELETE FROM VaultItemsFTS;";
    if(sqlite3_exec(db, clear, nullptr, nullptr, &err) != SQLITE_OK){ if(err) sqlite3_free(err); return false; }
    const char* insertSQL = "INSERT INTO VaultItemsFTS(rowid, Name, Content, Tags) SELECT ID, Name, Content, Tags FROM VaultItems;";
    if(sqlite3_exec(db, insertSQL, nullptr, nullptr, &err) != SQLITE_OK){ if(err) sqlite3_free(err); return false; }
    return true;
}

static bool jsonTypeMatches(const nlohmann::json& v, const std::string& typ){
    if(typ == "string") return v.is_string();
    if(typ == "array") return v.is_array();
    if(typ == "object") return v.is_object();
    if(typ == "number") return v.is_number();
    if(typ == "integer") return v.is_number_integer();
    if(typ == "boolean") return v.is_boolean();
    return true; // unknown type - accept
}

// Simple Levenshtein distance (iterative DP) for fuzzy matching
static int levenshteinDistance(const std::string& a, const std::string& b){
    size_t na = a.size();
    size_t nb = b.size();
    if(na == 0) return (int)nb;
    if(nb == 0) return (int)na;
    std::vector<int> prev(nb+1), cur(nb+1);
    for(size_t j=0;j<=nb;++j) prev[j] = (int)j;
    for(size_t i=1;i<=na;++i){
        cur[0] = (int)i;
        for(size_t j=1;j<=nb;++j){
            int cost = (a[i-1] == b[j-1]) ? 0 : 1;
            cur[j] = std::min({ prev[j] + 1, cur[j-1] + 1, prev[j-1] + cost });
        }
        prev.swap(cur);
    }
    return prev[nb];
}

bool VaultAssistant::validateJsonAgainstSchema(const nlohmann::json& doc, const nlohmann::json& schema){
    if(!schema.is_object()) return true; // nothing to validate against
    if(schema.contains("required") && schema["required"].is_array()){
        for(auto &r : schema["required"]){ if(r.is_string()){
            if(!doc.contains(r.get<std::string>())) return false;
        } }
    }
    if(schema.contains("properties") && schema["properties"].is_object()){
        for(auto it = schema["properties"].begin(); it != schema["properties"].end(); ++it){
            const std::string key = it.key();
            const auto prop = it.value();
            if(!doc.contains(key)) continue;
            if(prop.contains("type") && prop["type"].is_string()){
                if(!jsonTypeMatches(doc[key], prop["type"].get<std::string>())) return false;
            }
        }
    }
    return true;
}

std::vector<std::string> VaultAssistant::listModels(){
    std::vector<std::string> out;
    if(!client_) return out;
    auto r = client_->listModels();
    if(!r.ok()) return out;
    auto j = r.bodyJson();
    if(!j.contains("data")) return out;
    for(auto &m : j["data"]){ if(m.contains("id")) out.push_back(m["id"].get<std::string>()); }
    return out;
}

std::vector<VaultAssistant::Node> VaultAssistant::retrieveRelevantNodes(const std::string& query, int k, const std::string& model, int maxTokens){
    // Per request: ask the LLM to produce a JSON array of keywords from the question, then use a SQL
    // query to find nodes where any keyword appears in Name, Content, or Tags. The caller must supply
    // the model to use (no fallback).
    std::vector<Node> out;
    if(!vault_ || query.empty() || !client_) return out;

    // Use structured output feature: request a JSON array of keywords via response_format/json_schema
    nlohmann::json msg = nlohmann::json::array();
    msg.push_back({{"role","system"},{"content","You are a keyword extractor. Given a user question, respond with a JSON array of keywords only. Do not include any other text."}});
    msg.push_back({{"role","user"},{"content", query}});

    nlohmann::json response_format = {
        {"type","json_schema"},
        {"json_schema", {
            {"name","keyword_list"},
            {"strict","true"},
            {"schema", {
                {"type","array"},
                {"items", { {"type","string"} }}
            }}
        }}
    };

    nlohmann::json body = {{"model", model}, {"messages", msg}, {"response_format", response_format}, {"temperature", 0.0}, {"max_tokens", maxTokens}};

    auto r = client_->chatCompletions(body);
    if(!r.ok()){
        PLOGE << "[RAG][keywords] LLM request failed: " << r.curlError << " (" << r.httpCode << ")";
        return out; // per instruction: no fallbacks
    }
    auto j = r.bodyJson();

    std::string content;
    try{
        if(j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()){
            auto &c = j["choices"][0];
            if(c.contains("message") && c["message"].contains("content")) content = c["message"]["content"].get<std::string>();
        }
    }catch(...){
        PLOGE << "[RAG][keywords] Exception while extracting content from LLM response";
        return out;
    }

    if(content.empty()){
        PLOGD << "[RAG][keywords] LLM returned empty content";
        return out;
    }

    // Parse content as JSON and expect an array of strings
    nlohmann::json parsed;
    try{
        parsed = nlohmann::json::parse(content);
    } catch(...){
        std::string ct = content.substr(0, std::min<size_t>(content.size(), 1000));
        PLOGE << "[RAG][keywords] Failed to parse JSON from LLM response: " << ct << (content.size() > 1000 ? "...(truncated)" : "");
        return out;
    }
    if(!parsed.is_array()){
        PLOGE << "[RAG][keywords] Parsed JSON is not an array";
        return out;
    }

    std::vector<std::string> keywords;
    for(auto &it : parsed){
        if(!it.is_string()) continue; // ignore non-strings
        std::string t = it.get<std::string>();
        // trim spaces
        size_t start = 0; while(start < t.size() && isspace((unsigned char)t[start])) ++start;
        size_t end = t.size(); while(end > start && isspace((unsigned char)t[end-1])) --end;
        if(end <= start) continue;
        std::string s = t.substr(start, end - start);
        std::transform(s.begin(), s.end(), s.begin(), ::tolower);
        if(std::find(keywords.begin(), keywords.end(), s) == keywords.end()) keywords.push_back(s);
    }

    if(keywords.empty()){
        PLOGD << "[RAG][keywords] no keywords extracted";
        return out;
    }
    PLOGI << "[RAG][keywords] extracted " << keywords.size() << " keywords";
    try{
        std::ostringstream ks;
        for(size_t i=0;i<keywords.size();++i){ if(i) ks << ", "; ks << keywords[i]; }
        PLOGD << "[RAG][keywords] keywords: " << ks.str();
    } catch(...){ }

    // Build SQL that prioritizes nodes with Name matches, then sorts by total keyword occurrence count.
    sqlite3* db = vault_->getDBPublic();
    if(!db) return out;

    // Build a score expression: for each keyword give a large boost if it appears in Name (priority),
    // also give a smaller boost if it appears when spaces are removed (handles keywords like "cinderhollow" vs "Cinder Hollow"),
    // plus add the occurrence counts across Name/Content/Tags.
    std::string scoreExpr;
    for(size_t i=0;i<keywords.size();++i){
        if(i) scoreExpr += " + ";
        // priority boost for exact substring in Name, secondary boost for whitespace-stripped match
        scoreExpr += "(CASE WHEN INSTR(lower(Name), ?) > 0 THEN 100000 ELSE 0 END) + (CASE WHEN INSTR(REPLACE(lower(Name), ' ', ''), ?) > 0 THEN 50000 ELSE 0 END) + ((LENGTH(lower(Name)) - LENGTH(REPLACE(lower(Name), ?, ''))) + (LENGTH(lower(Content)) - LENGTH(REPLACE(lower(Content), ?, ''))) + (LENGTH(lower(Tags)) - LENGTH(REPLACE(lower(Tags), ?, ''))))";
    }

    std::string whereExpr;
    for(size_t i=0;i<keywords.size();++i){
        if(i) whereExpr += " OR ";
        // match either raw or whitespace-stripped forms in Name/Content/Tags
        whereExpr += "(INSTR(lower(Name), ?) > 0 OR INSTR(lower(Content), ?) > 0 OR INSTR(lower(Tags), ?) > 0 OR INSTR(REPLACE(lower(Name),' ','') , ?) > 0 OR INSTR(REPLACE(lower(Content),' ','') , ?) > 0 OR INSTR(REPLACE(lower(Tags),' ','') , ?) > 0)";
    }

    std::string sql = "SELECT ID, Name, Content, Tags, (" + scoreExpr + ") AS score FROM VaultItems WHERE " + whereExpr + " ORDER BY score DESC LIMIT ?;";

    PLOGD << "[RAG][sql] " << (sql.size() > 2000 ? std::string(sql.c_str(), 2000) + "...(truncated)" : sql);

    sqlite3_stmt* stmt = nullptr;
    if(sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK){
        int bindIdx = 1;
        // Binding layout now (per keyword):
        // score expr: INSTR(lower(Name), ?) , INSTR(REPLACE(lower(Name),' ','') , ?), REPLACE(lower(Name), ?, ''), REPLACE(lower(Content), ?, ''), REPLACE(lower(Tags), ?, '')  => 5 binds
        // where expr: INSTR(lower(Name), ?), INSTR(lower(Content), ?), INSTR(lower(Tags), ?), INSTR(REPLACE(lower(Name),' ','') , ?), INSTR(REPLACE(lower(Content),' ','') , ?), INSTR(REPLACE(lower(Tags),' ','') , ?) => 6 binds
        for(const auto &kw : keywords){
            // expression binds (5 per keyword)
            sqlite3_bind_text(stmt, bindIdx++, kw.c_str(), -1, SQLITE_TRANSIENT); // INSTR(lower(Name), ?)
            sqlite3_bind_text(stmt, bindIdx++, kw.c_str(), -1, SQLITE_TRANSIENT); // INSTR(REPLACE(lower(Name),' ','') , ?)
            sqlite3_bind_text(stmt, bindIdx++, kw.c_str(), -1, SQLITE_TRANSIENT); // REPLACE(lower(Name), ?, '')
            sqlite3_bind_text(stmt, bindIdx++, kw.c_str(), -1, SQLITE_TRANSIENT); // REPLACE(lower(Content), ?, '')
            sqlite3_bind_text(stmt, bindIdx++, kw.c_str(), -1, SQLITE_TRANSIENT); // REPLACE(lower(Tags), ?, '')
        }
        for(const auto &kw : keywords){
            // where binds (6 per keyword)
            sqlite3_bind_text(stmt, bindIdx++, kw.c_str(), -1, SQLITE_TRANSIENT); // INSTR(lower(Name), ?)
            sqlite3_bind_text(stmt, bindIdx++, kw.c_str(), -1, SQLITE_TRANSIENT); // INSTR(lower(Content), ?)
            sqlite3_bind_text(stmt, bindIdx++, kw.c_str(), -1, SQLITE_TRANSIENT); // INSTR(lower(Tags), ?)
            sqlite3_bind_text(stmt, bindIdx++, kw.c_str(), -1, SQLITE_TRANSIENT); // INSTR(REPLACE(lower(Name),' ','') , ?)
            sqlite3_bind_text(stmt, bindIdx++, kw.c_str(), -1, SQLITE_TRANSIENT); // INSTR(REPLACE(lower(Content),' ','') , ?)
            sqlite3_bind_text(stmt, bindIdx++, kw.c_str(), -1, SQLITE_TRANSIENT); // INSTR(REPLACE(lower(Tags),' ','') , ?)
        }
        sqlite3_bind_int(stmt, bindIdx++, k);
        int rowCount = 0;
        while(sqlite3_step(stmt) == SQLITE_ROW){
            Node n;
            n.id = sqlite3_column_int64(stmt,0);
            const unsigned char* nm = sqlite3_column_text(stmt,1);
            const unsigned char* cont = sqlite3_column_text(stmt,2);
            const unsigned char* tags = sqlite3_column_text(stmt,3);
            n.name = nm ? reinterpret_cast<const char*>(nm) : std::string();
            n.content = cont ? reinterpret_cast<const char*>(cont) : std::string();
            n.tags = tags ? reinterpret_cast<const char*>(tags) : std::string();
            out.push_back(n);
            ++rowCount;
        }
        PLOGD << "[RAG][sql] rows_returned=" << rowCount;
    }
    if(stmt) sqlite3_finalize(stmt);

    // If no rows returned by the heuristic SQL, try an FTS5 MATCH fallback if available
    if(out.empty()){
        PLOGD << "[RAG][fts] no SQL hits, attempting FTS fallback";
        if(ensureFTSIndex()){
            sqlite3_stmt* fstmt = nullptr;
            // Build an FTS match string using keyword prefixes (more permissive)
            std::string matchQuery;
            for(size_t i=0;i<keywords.size();++i){ if(i) matchQuery += " OR "; matchQuery += keywords[i] + std::string("*"); }
            try{ PLOGD << "[RAG][fts] matchQuery='" << matchQuery << "'"; } catch(...){}
            std::string fsql = "SELECT v.ID, v.Name, v.Content, v.Tags FROM VaultItems v JOIN VaultItemsFTS f ON f.rowid = v.ID WHERE f MATCH ? LIMIT ?;";
            if(sqlite3_prepare_v2(db, fsql.c_str(), -1, &fstmt, nullptr) == SQLITE_OK){
                sqlite3_bind_text(fstmt, 1, matchQuery.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(fstmt, 2, k);
                int rowCount = 0;
                std::vector<Node> nameMatches, otherMatches;
                while(sqlite3_step(fstmt) == SQLITE_ROW){
                    Node n;
                    n.id = sqlite3_column_int64(fstmt,0);
                    const unsigned char* nm = sqlite3_column_text(fstmt,1);
                    const unsigned char* cont = sqlite3_column_text(fstmt,2);
                    const unsigned char* tags = sqlite3_column_text(fstmt,3);
                    n.name = nm ? reinterpret_cast<const char*>(nm) : std::string();
                    n.content = cont ? reinterpret_cast<const char*>(cont) : std::string();
                    n.tags = tags ? reinterpret_cast<const char*>(tags) : std::string();
                    // check if the Name field directly matches any keyword (or its whitespace-stripped form)
                    std::string nameLower = n.name; std::transform(nameLower.begin(), nameLower.end(), nameLower.begin(), ::tolower);
                    auto nameStripped = nameLower; nameStripped.erase(std::remove_if(nameStripped.begin(), nameStripped.end(), ::isspace), nameStripped.end());
                    bool isNameMatch = false;
                    for(const auto &kw : keywords){ if(nameLower.find(kw) != std::string::npos || nameStripped.find(kw) != std::string::npos){ isNameMatch = true; break; } }
                    if(isNameMatch) nameMatches.push_back(n); else otherMatches.push_back(n);
                    ++rowCount;
                }
                // append name matches first, then other matches, up to k
                for(auto &n : nameMatches){ if((int)out.size() >= k) break; out.push_back(n); }
                for(auto &n : otherMatches){ if((int)out.size() >= k) break; out.push_back(n); }
                PLOGD << "[RAG][fts] rows_returned(name/other)=" << nameMatches.size() << "/" << otherMatches.size();
            } else {
                PLOGW << "[RAG][fts] prepare failed for FTS fallback";
            }
            if(fstmt) sqlite3_finalize(fstmt);
        } else {
            PLOGD << "[RAG][fts] ensureFTSIndex() returned false, skipping FTS fallback";
        }
    }

    // If still no rows, run a lightweight fuzzy scan (Levenshtein) over Names / Content to catch spelling variations
    if(out.empty()){
        PLOGD << "[RAG][fuzzy] no FTS hits, attempting fuzzy scan";
        sqlite3_stmt* s = nullptr;
        const char* q = "SELECT ID, Name, Content, Tags FROM VaultItems;";
        if(sqlite3_prepare_v2(db, q, -1, &s, nullptr) == SQLITE_OK){
            int rowCount = 0;
            std::vector<Node> nameMatches, contentMatches;
            while(sqlite3_step(s) == SQLITE_ROW){
                Node n;
                n.id = sqlite3_column_int64(s,0);
                const unsigned char* nm = sqlite3_column_text(s,1);
                const unsigned char* cont = sqlite3_column_text(s,2);
                const unsigned char* tags = sqlite3_column_text(s,3);
                n.name = nm ? reinterpret_cast<const char*>(nm) : std::string();
                n.content = cont ? reinterpret_cast<const char*>(cont) : std::string();
                n.tags = tags ? reinterpret_cast<const char*>(tags) : std::string();

                auto normalize = [](const std::string &x){ std::string r; r.reserve(x.size()); for(char c : x){ if(std::isalnum((unsigned char)c)) r.push_back(std::tolower((unsigned char)c)); } return r; };
                std::string nameNorm = normalize(n.name);
                std::string contentNorm = normalize(n.content);

                int bestNameDist = INT_MAX;
                int bestContentDist = INT_MAX;
                for(const auto &kw : keywords){
                    std::string kwNorm = normalize(kw);
                    int dName = levenshteinDistance(kwNorm, nameNorm);
                    if(dName < bestNameDist) bestNameDist = dName;
                    std::string csub = contentNorm.substr(0, std::min<size_t>(contentNorm.size(), kwNorm.size() + 10));
                    int dContent = levenshteinDistance(kwNorm, csub);
                    if(dContent < bestContentDist) bestContentDist = dContent;
                    if(bestNameDist <= 1 || bestContentDist <= 1) break; // early accept
                }
                // thresholds based on keyword length
                bool isNameAccept = false;
                bool isContentAccept = false;
                if(!keywords.empty()){
                    size_t kwlen = keywords[0].size();
                    int nameThresh = (kwlen <= 6) ? 1 : 2;
                    int contentThresh = (kwlen <= 6) ? 1 : 2;
                    if(bestNameDist <= nameThresh) isNameAccept = true;
                    if(bestContentDist <= contentThresh) isContentAccept = true;
                }
                if(isNameAccept) { nameMatches.push_back(n); }
                else if(isContentAccept) { contentMatches.push_back(n); }
                if((int)nameMatches.size() + (int)contentMatches.size() >= k) break;
            }
            // prefer name matches
            for(auto &n : nameMatches){ if((int)out.size() >= k) break; out.push_back(n); }
            for(auto &n : contentMatches){ if((int)out.size() >= k) break; out.push_back(n); }
            PLOGD << "[RAG][fuzzy] rows_returned(name/content)=" << nameMatches.size() << "/" << contentMatches.size();
        } else {
            PLOGW << "[RAG][fuzzy] prepare failed for fuzzy scan";
        }
        if(s) sqlite3_finalize(s);
    }

    return out;
}

std::string VaultAssistant::buildRAGContext(const std::vector<Node>& nodes, int charLimitPerNode, int topNodeCharLimit, bool topNodeFullContent){
    std::ostringstream ss;
    for(size_t idx=0; idx<nodes.size(); ++idx){
        const auto &n = nodes[idx];
        ss << "[Node " << n.id << "] " << n.name << "\n";
        std::string content = n.content;
        if(idx == 0){
            // top-scored node
            if(topNodeFullContent){
                // keep full content
            } else {
                int useLimit = (topNodeCharLimit >= 0) ? topNodeCharLimit : charLimitPerNode;
                if(useLimit >= 0 && (int)content.size() > useLimit) content = content.substr(0, useLimit) + "...";
            }
        } else {
            if((int)content.size() > charLimitPerNode) content = content.substr(0, charLimitPerNode) + "...";
        }
        ss << content << "\n";
        if(!n.tags.empty()) ss << "Tags: " << n.tags << "\n";
        ss << "---\n";
    }
    return ss.str();
}

std::string VaultAssistant::askTextWithRAG(const std::string& question, const std::string& model, int k, int charLimitPerNode, int topNodeCharLimit, bool topNodeFullContent, int maxTokens){
    if(!client_) return std::string();
    auto nodes = retrieveRelevantNodes(question, k, model, maxTokens);
    std::string context = buildRAGContext(nodes, charLimitPerNode, topNodeCharLimit, topNodeFullContent);
    nlohmann::json msg = nlohmann::json::array();
    msg.push_back({{"role","system"},{"content","You are a helpful assistant that answers questions using the provided Vault context. Use only the context for facts and cite Node IDs when referencing specific notes."}});
    msg.push_back({{"role","user"},{"content", std::string("Context:\n") + context + std::string("\nQuestion: ") + question}});

    nlohmann::json body = {
        {"model", model},
        {"messages", msg},
        {"max_tokens", maxTokens}
    };

    // Log RAG inputs for debugging (truncate context to avoid huge logs)
    try{
        std::string ctxLog = context;
        if(ctxLog.size() > 2000) ctxLog = ctxLog.substr(0,2000) + "...(truncated)";
        PLOGI << "[RAG] askTextWithRAG model=" << model << " question=" << question << " context_len=" << context.size();
        PLOGI << "[RAG] context: \n" << ctxLog;
        PLOGI << "[RAG] messages: " << body.dump();
    } catch(...){}

    auto r = client_->chatCompletions(body);
    if(!r.ok()) return std::string("[error]") + r.curlError + " " + std::to_string(r.httpCode);
    auto j = r.bodyJson();
    try{
        if(j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()){
            auto &c = j["choices"][0];
            if(c.contains("message") && c["message"].contains("content")) return c["message"]["content"].get<std::string>();
        }
    }catch(...){}
    return std::string();
}

std::optional<nlohmann::json> VaultAssistant::askJSONWithRAG(const std::string& question, const nlohmann::json& schema, const std::string& model, int k, int charLimitPerNode, int topNodeCharLimit, bool topNodeFullContent, int maxTokens){
    if(!client_) return std::nullopt;
    auto nodes = retrieveRelevantNodes(question, k, model, maxTokens);
    std::string context = buildRAGContext(nodes, charLimitPerNode, topNodeCharLimit, topNodeFullContent);
    std::string sys = "You are an assistant that must respond only with a JSON document that conforms to the following JSON schema. Do not include any other text. Only output valid JSON.\nSchema: ";
    sys += schema.dump();
    nlohmann::json msg = nlohmann::json::array();
    msg.push_back({{"role","system"},{"content",sys}});
    msg.push_back({{"role","user"},{"content", std::string("Context:\n") + context + std::string("\nQuestion: ") + question}});

    nlohmann::json body = {
        {"model", model},
        {"messages", msg},
        {"temperature", 0.2},
        {"max_tokens", maxTokens}
    };

    // Log RAG JSON request (include schema and truncated context for debugging)
    try{
        std::string ctxLog = context;
        if(ctxLog.size() > 2000) ctxLog = ctxLog.substr(0,2000) + "...(truncated)";
        PLOGI << "[RAG] askJSONWithRAG model=" << model << " question=" << question << " context_len=" << context.size();
        PLOGI << "[RAG] schema: " << schema.dump();
        PLOGI << "[RAG] context: \n" << ctxLog;
        PLOGI << "[RAG] messages: " << body.dump();
    } catch(...){}

    auto r = client_->chatCompletions(body);
    if(!r.ok()) return std::nullopt;
    auto j = r.bodyJson();
    try{
        if(j.contains("choices") && j["choices"].is_array() && !j["choices"].empty()){
            auto &c = j["choices"][0];
            if(c.contains("message") && c["message"].contains("content")){
                std::string content = c["message"]["content"].get<std::string>();
                // content should be pure JSON; try to parse
                try{
                    auto parsed = nlohmann::json::parse(content);
                    if(validateJsonAgainstSchema(parsed, schema)) return parsed;
                    return std::nullopt;
                } catch(...){
                    // try to strip surrounding markdown code fences if present
                    size_t pos = 0;
                    if((pos = content.find("```")) != std::string::npos){
                        // remove leading/trailing fences
                        size_t start = content.find('\n', pos);
                        if(start != std::string::npos){
                            size_t last = content.rfind("```");
                            if(last != std::string::npos && last > start){
                                std::string inner = content.substr(start+1, last - start - 1);
                                try{ auto p2 = nlohmann::json::parse(inner); if(validateJsonAgainstSchema(p2, schema)) return p2; } catch(...){}
                            }
                        }
                    }
                }
            }
        }
    }catch(...){}
    return std::nullopt;
}

int64_t VaultAssistant::createNodeFromJSON(const nlohmann::json& doc, int64_t parentIfAny){
    if(!vault_) return -1;
    if(!doc.is_object()) return -1;
    std::string title;
    std::string content;
    std::vector<std::string> tags;
    if(doc.contains("title") && doc["title"].is_string()) title = doc["title"].get<std::string>();
    if(doc.contains("content") && doc["content"].is_string()) content = doc["content"].get<std::string>();
    if(doc.contains("tags") && doc["tags"].is_array()){
        for(auto &t : doc["tags"]) if(t.is_string()) tags.push_back(t.get<std::string>());
    }
    if(title.empty() || content.empty()) return -1;
    int64_t id = vault_->createItemWithContent(title, content, tags, parentIfAny);
    return id;
}

std::string VaultAssistant::getRAGContext(const std::vector<Node>& nodes, int charLimitPerNode, int topNodeCharLimit, bool topNodeFullContent){
    return buildRAGContext(nodes, charLimitPerNode, topNodeCharLimit, topNodeFullContent);
}
