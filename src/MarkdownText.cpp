#include <MarkdownText.hpp>
#include <md4c.h>
#include <md4c-html.h>
#include <string>
#include <vector>
#include <stack>
#include <Fonts.hpp>
#include "Vault.hpp"
#include "Icons.hpp"

// Minimal md4c->ImGui renderer using callbacks
namespace ImGui {

struct MD4CRenderer {
    // State
    std::stack<MD_BLOCKTYPE> blocks;
    std::stack<MD_SPANTYPE> spans;
    std::string linkUrl;
    void* ctx = nullptr;
    bool in_code_block = false;
    std::string code_lang;

    // Helpers
    void pushHeader(int level) {
        ImFont* headerFont = GetFont(FontStyle::Bold);
        PushFont(headerFont);
        float scale = 1.4f - (level - 1) * 0.1f; if(scale < 0.9f) scale = 0.9f;
        SetWindowFontScale(scale);
    }
    void popHeader(){ SetWindowFontScale(1.0f); PopFont(); }

    void beginParagraph(){ PushTextWrapPos(GetContentRegionAvail().x); }
    void endParagraph(){ PopTextWrapPos(); NewLine(); }

    void beginCodeBlock(const MD_ATTRIBUTE* langAttr){
        in_code_block = true; code_lang.clear();
        if(langAttr && langAttr->text) code_lang.assign(langAttr->text, langAttr->size);
        Spacing(); PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f,0.08f,0.08f,1.0f)); PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);
        BeginChild("md_code_block", ImVec2(GetContentRegionAvail().x,0), true);
        ImFont* mono = GetFontByFamily("Monospace", FontStyle::Regular);
        if(mono) PushFont(mono);
    }
    void endCodeBlock(){ if(GetFont()) PopFont(); EndChild(); PopStyleVar(); PopStyleColor(); Spacing(); in_code_block=false; code_lang.clear(); }

    void renderText(const MD_CHAR* text, MD_SIZE size){
        if(size==0) return;
        std::string s((const char*)text, (size_t)size);
        if(in_code_block){
            // render raw code line
            TextUnformatted(s.c_str());
            NewLine();
            return;
        }

        // Apply inline styles (span stack)
        // Choose font
        bool usedFont = false;
        if(!spans.empty()){
            auto t = spans.top();
            if(t == MD_SPAN_EM){ ImFont* it = GetFont(FontStyle::Italic); if(it){ PushFont(it); usedFont = true;} }
            else if(t == MD_SPAN_STRONG){ ImFont* bf = GetFont(FontStyle::Bold); if(bf){ PushFont(bf); usedFont = true;} }
            else if(t == MD_SPAN_CODE){ ImFont* mono = GetFontByFamily("Monospace", FontStyle::Regular); if(mono){ PushFont(mono); usedFont = true;} }
        }

        // Color for link
        if(!linkUrl.empty()){
            PushStyleColor(ImGuiCol_Text, ImVec4(0.26f,0.53f,0.96f,1.0f));
        }

        // Render (use wrapped text so paragraphs wrap properly)
        TextWrapped(s.c_str());

        if(!linkUrl.empty()) PopStyleColor();
        if(usedFont) PopFont();
    }

    // md4c callbacks
    static int enter_block(MD_BLOCKTYPE t, void* detail, void* userdata){
        MD4CRenderer* r = (MD4CRenderer*)userdata;
        r->blocks.push(t);
        switch(t){
            case MD_BLOCK_H: {
                int level = ((MD_BLOCK_H_DETAIL*)detail)->level;
                r->pushHeader(level);
                break;
            }
            case MD_BLOCK_P: r->beginParagraph(); break;
            case MD_BLOCK_CODE: r->beginCodeBlock(&((MD_BLOCK_CODE_DETAIL*)detail)->lang); break;
            case MD_BLOCK_HR: Separator(); break;
            default: break;
        }
        return 0;
    }
    static int leave_block(MD_BLOCKTYPE t, void* detail, void* userdata){
        MD4CRenderer* r = (MD4CRenderer*)userdata;
        if(r->blocks.empty()) return 0;
        r->blocks.pop();
        switch(t){
            case MD_BLOCK_H: r->popHeader(); Dummy(ImVec2(0,6)); break;
            case MD_BLOCK_P: r->endParagraph(); break;
            case MD_BLOCK_CODE: r->endCodeBlock(); break;
            default: break;
        }
        return 0;
    }
    static int enter_span(MD_SPANTYPE t, void* detail, void* userdata){
        MD4CRenderer* r = (MD4CRenderer*)userdata;
        r->spans.push(t);
        if(t == MD_SPAN_A){
            MD_SPAN_A_DETAIL* d = (MD_SPAN_A_DETAIL*)detail;
            if(d && d->href.text) r->linkUrl.assign(d->href.text, d->href.size); else r->linkUrl.clear();
        } else if(t == MD_SPAN_IMG){
            MD_SPAN_IMG_DETAIL* d = (MD_SPAN_IMG_DETAIL*)detail;
            std::string src;
            if(d && d->src.text) src.assign(d->src.text, d->src.size);
            std::string label = "[image]";
            try{ auto p = std::filesystem::path(src); if(!p.filename().empty()) label = p.filename().string(); } catch(...){}

            // If we have a Vault context, attempt inline rendering and caching
            if(r->ctx){
                Vault* v = reinterpret_cast<Vault*>(r->ctx);
                // vault attachment
                const std::string prefix = "vault://attachment/";
                if(src.rfind(prefix,0) == 0){
                    std::string idstr = src.substr(prefix.size());
                    try{
                        int64_t aid = std::stoll(idstr);
                        auto meta = v->getAttachmentMeta(aid);
                        auto isModelExt = [&](const std::string &n)->bool{
                            std::string ext;
                            try{ ext = std::filesystem::path(n).extension().string(); std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower); } catch(...){}
                            static const std::vector<std::string> models = {".obj",".fbx",".gltf",".glb",".ply",".dae",".stl"};
                            for(auto &m: models) if(ext == m) return true;
                            return (meta.mimeType.find("model") != std::string::npos);
                        };

                        if(isModelExt(meta.name)){
                            // Render inline model viewer (if available) or show a loading placeholder
                            ModelViewer* mv = v->getOrCreateModelViewerForSrc(src);
                            ImVec2 avail = ImVec2(GetContentRegionAvail().x, std::min(320.0f, GetContentRegionAvail().x * 0.6f));
                            if(mv && mv->isLoaded()){
                                mv->renderToRegion(avail);
                                // clicking inline opens full viewer
                                if(ImGui::IsItemClicked()) v->openModelFromSrc(src);
                            } else {
                                ImGui::Text("Model: %s (loading...)", meta.name.c_str()); ImGui::SameLine();
                                if(ImGui::SmallButton("Open")) v->openModelFromSrc(src);
                            }
                            return 0;
                        }

                        if(meta.size > 0){
                            auto data = v->getAttachmentData(aid);
                            if(!data.empty()){
                                auto tex = LoadTextureFromMemory(std::string("vault:att:") + std::to_string(aid), data);
                                if(tex.loaded){
                                    float availW = GetContentRegionAvail().x;
                                    float scale = 1.0f;
                                    if(tex.width > availW) scale = availW / static_cast<float>(tex.width);
                                    ImGui::Image((ImTextureID)(intptr_t)tex.textureID, ImVec2(tex.width*scale, tex.height*scale));
                                    NewLine();
                                    return 0;
                                }
                            }
                        } else {
                            // no data — try async fetch if ExternalPath present
                            if(!meta.externalPath.empty()) v->asyncFetchAndStoreAttachment(meta.id, meta.externalPath);
                        }
                    } catch(...){}
                    // fallback: show filename button that opens preview
                    if(ImGui::SmallButton(label.c_str())) v->openPreviewFromSrc(src);
                    return 0;
                }

                // http/https remote resource — check cache
                if(src.rfind("http://",0) == 0 || src.rfind("https://",0) == 0){
                    // If it's a model extension, create/queue and offer "View Model"
                    auto isModelUrl = [&](const std::string &u)->bool{
                        try{ auto ext = std::filesystem::path(u).extension().string(); std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower); static const std::vector<std::string> models = {".obj",".fbx",".gltf",".glb",".ply",".dae",".stl"}; for(auto &m: models) if(ext==m) return true; } catch(...){}
                        return false;
                    };
                    if(isModelUrl(src)){
                        // Try inline viewer
                        int64_t aid = v->findAttachmentByExternalPath(src);
                        if(aid == -1) aid = v->addAttachmentFromURL(src);
                        ModelViewer* mv = v->getOrCreateModelViewerForSrc(src);
                        ImVec2 avail = ImVec2(GetContentRegionAvail().x, std::min(320.0f, GetContentRegionAvail().x * 0.6f));
                        if(mv && mv->isLoaded()){
                            mv->renderToRegion(avail);
                            if(ImGui::IsItemClicked()) v->openModelFromSrc(src);
                        } else {
                            ImGui::Text("Model: %s", label.c_str()); ImGui::SameLine();
                            if(ImGui::Button("View Model")) v->openModelFromSrc(src);
                        }
                        return 0;
                    }

                    int64_t aid = v->findAttachmentByExternalPath(src);
                    if(aid == -1){
                        // create placeholder and start async fetch
                        aid = v->addAttachmentFromURL(src);
                        ImGui::Text("Fetching image: %s", label.c_str());
                        return 0;
                    }
                    auto meta = v->getAttachmentMeta(aid);
                    if(meta.size > 0){
                        auto data = v->getAttachmentData(aid);
                        if(!data.empty()){
                            auto tex = LoadTextureFromMemory(std::string("vault:url:") + std::to_string(aid), data);
                            if(tex.loaded){
                                float availW = GetContentRegionAvail().x;
                                float scale = 1.0f;
                                if(tex.width > availW) scale = availW / static_cast<float>(tex.width);
                                ImGui::Image((ImTextureID)(intptr_t)tex.textureID, ImVec2(tex.width*scale, tex.height*scale));
                                NewLine();
                                return 0;
                            }
                        }
                    }
                    ImGui::Text("Fetching image: %s", label.c_str());
                    return 0;
                }
            }

            // Fallback: show a placeholder small button (no context or unsupported scheme)
            if(ImGui::SmallButton(label.c_str())){
                // If no Vault context, we could open external viewer if path is local
                try{ std::string p(src); if(p.rfind("file://",0)==0) p = p.substr(7); if(std::filesystem::exists(p)){
                    // try to open preview using system default (not implemented)
                }} catch(...){}
            }
            return 0;
            // render a small button for images — clicking it opens preview if we have a Vault context
            if(ImGui::SmallButton(label.c_str())){
                if(r->ctx){
                    Vault* v = reinterpret_cast<Vault*>(r->ctx);
                    v->openPreviewFromSrc(src);
                } else {
                    // no context, try to open file directly by launching external viewer? not implemented
                }
            }
        }
        return 0;
    }
    static int leave_span(MD_SPANTYPE t, void* detail, void* userdata){
        MD4CRenderer* r = (MD4CRenderer*)userdata;
        if(!r->spans.empty()) r->spans.pop();
        if(t == MD_SPAN_A) r->linkUrl.clear();
        return 0;
    }
    static int textcb(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata){
        MD4CRenderer* r = (MD4CRenderer*)userdata;
        r->renderText(text, size);
        return 0;
    }
};

void MarkdownText(const char* text){ MarkdownText(text, nullptr); }

void MarkdownText(const char* text, void* context){
    if(!text || !text[0]) return;
    ImGuiWindow* w = GetCurrentWindow(); if(w->SkipItems) return;

    MD4CRenderer renderer;
    renderer.ctx = context;
    MD_PARSER parser = {0};
    parser.enter_block = MD4CRenderer::enter_block;
    parser.leave_block = MD4CRenderer::leave_block;
    parser.enter_span = MD4CRenderer::enter_span;
    parser.leave_span = MD4CRenderer::leave_span;
    parser.text = MD4CRenderer::textcb;

    // Set desired parser flags via the parser struct (md_parse takes only 4 args)
    parser.flags = MD_FLAG_TABLES;
    md_parse(text, strlen(text), &parser, &renderer);
}

void MarkdownText(const std::string& text){ MarkdownText(text.c_str()); }
void MarkdownText(const std::string& text, void* context){ MarkdownText(text.c_str(), context); }

}