#include "VaultAssistant.hpp"
#include "LLMClient.hpp"
#include "Vault.hpp"
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

std::vector<VaultAssistant::Node> VaultAssistant::retrieveRelevantNodes(const std::string& query, int k){
    std::vector<Node> out;
    if(!vault_ || query.empty()) return out;
    std::string lowerq = query;
    std::transform(lowerq.begin(), lowerq.end(), lowerq.begin(), ::tolower);

    // Use a simple SQL-based occurrence heuristic
    const char* sql = "SELECT ID, Name, Content, Tags, "
                      "( (LENGTH(lower(Name)) - LENGTH(REPLACE(lower(Name), ?, ''))) + "
                      "  (LENGTH(lower(Content)) - LENGTH(REPLACE(lower(Content), ?, ''))) + "
                      "  (LENGTH(lower(Tags)) - LENGTH(REPLACE(lower(Tags), ?, '')))) AS score "
                      "FROM VaultItems "
                      "WHERE lower(Name) LIKE '%' || ? || '%' OR lower(Content) LIKE '%' || ? || '%' OR lower(Tags) LIKE '%' || ? || '%' "
                      "ORDER BY score DESC LIMIT ?;";
    sqlite3_stmt* stmt = nullptr;
    sqlite3* db = vault_->getDBPublic();
    if(!db) return out;
    if(sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK){
        // bind 1..3 for REPLACE count, 4..6 for LIKE, 7 for limit
        for(int i=1;i<=3;++i) sqlite3_bind_text(stmt, i, lowerq.c_str(), -1, SQLITE_TRANSIENT);
        for(int i=4;i<=6;++i) sqlite3_bind_text(stmt, i, lowerq.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt,7,k);
        while(sqlite3_step(stmt) == SQLITE_ROW){
            Node n;
            n.id = sqlite3_column_int64(stmt,0);
            const unsigned char* nm = sqlite3_column_text(stmt,1);
            const unsigned char* cont = sqlite3_column_text(stmt,2);
            const unsigned char* tags = sqlite3_column_text(stmt,3);
            n.score = sqlite3_column_double(stmt,4);
            n.name = nm ? reinterpret_cast<const char*>(nm) : std::string();
            n.content = cont ? reinterpret_cast<const char*>(cont) : std::string();
            n.tags = tags ? reinterpret_cast<const char*>(tags) : std::string();
            out.push_back(n);
        }
    }
    if(stmt) sqlite3_finalize(stmt);
    return out;
}

std::string VaultAssistant::buildRAGContext(const std::vector<Node>& nodes, int charLimitPerNode){
    std::ostringstream ss;
    for(const auto &n : nodes){
        ss << "[Node " << n.id << "] " << n.name << "\n";
        std::string content = n.content;
        if((int)content.size() > charLimitPerNode) content = content.substr(0, charLimitPerNode) + "...";
        ss << content << "\n";
        if(!n.tags.empty()) ss << "Tags: " << n.tags << "\n";
        ss << "---\n";
    }
    return ss.str();
}

std::string VaultAssistant::askTextWithRAG(const std::string& question, const std::string& model, int k){
    if(!client_) return std::string();
    auto nodes = retrieveRelevantNodes(question, k);
    std::string context = buildRAGContext(nodes);
    nlohmann::json msg = nlohmann::json::array();
    msg.push_back({{"role","system"},{"content","You are a helpful assistant that answers questions using the provided Vault context. Use only the context for facts and cite Node IDs when referencing specific notes."}});
    msg.push_back({{"role","user"},{"content", std::string("Context:\n") + context + std::string("\nQuestion: ") + question}});

    nlohmann::json body = {
        {"model", model},
        {"messages", msg}
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

std::optional<nlohmann::json> VaultAssistant::askJSONWithRAG(const std::string& question, const nlohmann::json& schema, const std::string& model, int k){
    if(!client_) return std::nullopt;
    auto nodes = retrieveRelevantNodes(question, k);
    std::string context = buildRAGContext(nodes);
    std::string sys = "You are an assistant that must respond only with a JSON document that conforms to the following JSON schema. Do not include any other text. Only output valid JSON.\nSchema: ";
    sys += schema.dump();
    nlohmann::json msg = nlohmann::json::array();
    msg.push_back({{"role","system"},{"content",sys}});
    msg.push_back({{"role","user"},{"content", std::string("Context:\n") + context + std::string("\nQuestion: ") + question}});

    nlohmann::json body = {
        {"model", model},
        {"messages", msg},
        {"temperature", 0.2}
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

std::string VaultAssistant::getRAGContext(const std::vector<Node>& nodes, int charLimitPerNode){
    return buildRAGContext(nodes, charLimitPerNode);
}
