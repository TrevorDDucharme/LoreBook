#include <MarkdownText.hpp>
#include <md4c.h>
#include <md4c-html.h>
#include <string>
#include <vector>
#include <stack>
#include <Fonts.hpp>
#include <plog/Log.h>
#include "Vault.hpp"
#include "Icons.hpp"

// Parse optional size suffixes appended with ::<width>x<height>
// Examples: "vault://Assets/model.glb::800x600" or "https://.../model.glb::640x480"
static bool parseSizeSuffix(const std::string &src, std::string &outBase, int &outW, int &outH)
{
    outW = -1;
    outH = -1;
    outBase = src;
    if (src.size() < 4)
        return false;
    size_t pos = src.rfind("::");
    if (pos == std::string::npos)
        return false;
    std::string suf = src.substr(pos + 2);
    // only allow digits and 'x' (e.g. 800x600)
    size_t xPos = suf.find('x');
    if (xPos == std::string::npos)
        return false;
    std::string ws = suf.substr(0, xPos);
    std::string hs = suf.substr(xPos + 1);
    if (ws.empty() && hs.empty())
        return false;
    try
    {
        if (!ws.empty())
            outW = std::stoi(ws);
        if (!hs.empty())
            outH = std::stoi(hs);
    }
    catch (...)
    {
        return false;
    }
    if (outW <= 0)
        outW = -1;
    if (outH <= 0)
        outH = -1;
    outBase = src.substr(0, pos);
    return true;
}

// Minimal md4c->ImGui renderer using callbacks
namespace ImGui
{

    struct MD4CRenderer
    {
        // State
        std::stack<MD_BLOCKTYPE> blocks;
        std::stack<MD_SPANTYPE> spans;
        std::string linkUrl;
        void *ctx = nullptr;
        bool in_code_block = false;
        std::string code_lang;

        // List handling state
        struct ListState { MD_BLOCKTYPE type; int index; };
        std::stack<ListState> lists;
        std::stack<bool> li_first_paragraph;
        // Track whether we've activated a wrap for the current LI (so we can close it when needed)
        std::stack<bool> li_wrap_active;
        // Running source offset (number of text bytes processed so far)
        size_t src_pos = 0;

        // Table state
        bool in_table = false;
        unsigned table_col_count = 0;
        bool table_in_header = false;
        std::vector<std::string> table_current_row;
        std::vector<std::vector<std::string>> table_header_rows;
        std::vector<std::vector<std::string>> table_body_rows;
        std::string table_cell_text;
        bool in_table_cell = false;

        // Helpers for list item paragraphs
        void beginListItemParagraph(bool ordered, int index)
        {
            if (ordered)
            {
                char buf[32];
                snprintf(buf, sizeof(buf), "%d.", index);
                TextUnformatted(buf);
            }
            else
            {
                TextUnformatted(IconTag("point").c_str());
                // add a small gap between icon and text
                SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
            }
            float wrapX = GetCursorPosX() + GetContentRegionAvail().x;
            PushTextWrapPos(wrapX);
        }
        void endListItemParagraph()
        {
            PopTextWrapPos();
            NewLine();
        }

        // Helpers
        void pushHeader(int level)
        {
            ImFont *headerFont = GetFont(FontStyle::Bold);
            PushFont(headerFont);
            float scale = 1.4f - (level - 1) * 0.1f;
            if (scale < 0.9f)
                scale = 0.9f;
            SetWindowFontScale(scale);
        }
        void popHeader()
        {
            SetWindowFontScale(1.0f);
            PopFont();
        }

        void beginParagraph() { PushTextWrapPos(GetContentRegionAvail().x); }
        void endParagraph()
        {
            PopTextWrapPos();
            NewLine();
        }

        void beginCodeBlock(const MD_ATTRIBUTE *langAttr)
        {
            in_code_block = true;
            code_lang.clear();
            if (langAttr && langAttr->text)
                code_lang.assign(langAttr->text, langAttr->size);
            Spacing();
            PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.08f, 0.08f, 0.08f, 1.0f));
            PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);
            BeginChild("md_code_block", ImVec2(GetContentRegionAvail().x, 0), true);
            ImFont *mono = GetFontByFamily("Monospace", FontStyle::Regular);
            if (mono)
                PushFont(mono);
        }
        void endCodeBlock()
        {
            if (GetFont())
                PopFont();
            EndChild();
            PopStyleVar();
            PopStyleColor();
            Spacing();
            in_code_block = false;
            code_lang.clear();
        }

        void renderText(const MD_CHAR *text, MD_SIZE size)
        {
            if (size == 0)
                return;
            std::string s((const char *)text, (size_t)size);

            // If we're inside a list item and the first paragraph hasn't started yet,
            // attempt to detect a task-list marker '[ ]' or '[x]' and render an interactive checkbox.
            // Otherwise, start a normal list item paragraph (bullet/number) on the first text encountered.
            size_t localPos = src_pos;
            if (!in_code_block && !lists.empty() && !li_first_paragraph.empty() && li_first_paragraph.top()) {
                // Look for a checkbox marker near the beginning of this text chunk
                size_t bracketPos = std::string::npos;
                for(size_t i=0;i<std::min<size_t>(s.size(),8);++i){ if(s[i]=='['){ bracketPos = i; break; } }
                if (bracketPos != std::string::npos && bracketPos + 2 < s.size() && s[bracketPos+2] == ']') {
                    char c = s[bracketPos+1];
                    bool checked = (c == 'x' || c == 'X');
                    // Render checkbox in place of a bullet
                    ImGui::PushID((void*)(intptr_t)(localPos + bracketPos));
                    bool v = checked;
                    if(ImGui::Checkbox("", &v)){
                        // Toggle persisted state in the vault content: replace '[ ]' with '[x]' or vice versa
                        size_t markerLen = 3;
                        if (bracketPos + 3 < s.size() && s[bracketPos+3] == ' ') markerLen = 4; // include trailing space
                        std::string repl = v ? std::string("[x]") : std::string("[ ]");
                        if(markerLen == 4) repl.push_back(' ');
                        if (ctx){
                            Vault *vptr = reinterpret_cast<Vault*>(ctx);
                            vptr->replaceCurrentContentRange(localPos + bracketPos, markerLen, repl);
                        }
                    }
                    ImGui::PopID();
                    // After checkbox, ensure text after the marker is wrapped and displayed
                    size_t after = bracketPos + ((bracketPos + 3 < s.size() && s[bracketPos+3]==' ') ? 4 : 3);
                    std::string rem;
                    if (after < s.size()) rem = s.substr(after);
                    // start a wrapped paragraph aligned with content region
                    float wrapX = GetCursorPosX() + GetContentRegionAvail().x;
                    PushTextWrapPos(wrapX);
                    TextWrapped(rem.c_str());
                    PopTextWrapPos();
                    // Mark paragraph started
                    li_first_paragraph.top() = false;
                    // We already rendered this chunk; advance src_pos and return
                    src_pos += size;
                    return;
                }
                else
                {
                    // No checkbox detected; start normal list item paragraph now
                    bool ordered = (lists.top().type == MD_BLOCK_OL);
                    beginListItemParagraph(ordered, lists.top().index);
                    li_first_paragraph.top() = false;
                    li_wrap_active.push(true);
                }
            }

            if (in_code_block)
            {
                // render raw code line
                TextUnformatted(s.c_str());
                NewLine();
                return;
            }

            // Apply inline styles (span stack)
            // Choose font
            bool usedFont = false;
            if (!spans.empty())
            {
                auto t = spans.top();
                if (t == MD_SPAN_EM)
                {
                    ImFont *it = GetFont(FontStyle::Italic);
                    if (it)
                    {
                        PushFont(it);
                        usedFont = true;
                    }
                }
                else if (t == MD_SPAN_STRONG)
                {
                    ImFont *bf = GetFont(FontStyle::Bold);
                    if (bf)
                    {
                        PushFont(bf);
                        usedFont = true;
                    }
                }
                else if (t == MD_SPAN_CODE)
                {
                    ImFont *mono = GetFontByFamily("Monospace", FontStyle::Regular);
                    if (mono)
                    {
                        PushFont(mono);
                        usedFont = true;
                    }
                }
            }

            // Definition list heuristics (inline 'Term: definition' or leading ': definition')
            if (!in_code_block && lists.empty() && !in_table && !s.empty()){
                // single-line 'Term: definition'
                size_t colon = s.find(':');
                size_t newline = s.find('\n');
                if (colon != std::string::npos && (newline == std::string::npos || newline > colon) && colon < 80){
                    // Render term bold then definition text
                    std::string term = s.substr(0, colon);
                    std::string def = (colon + 1 < s.size()) ? s.substr(colon + 1) : std::string();
                    // trim spaces
                    auto trim = [](std::string &t){ while(!t.empty() && isspace((unsigned char)t.front())) t.erase(t.begin()); while(!t.empty() && isspace((unsigned char)t.back())) t.pop_back(); };
                    trim(term); trim(def);
                    if(!term.empty() && !def.empty()){
                        ImFont *bf = GetFont(FontStyle::Bold); if(bf) PushFont(bf);
                        TextUnformatted(term.c_str());
                        if(bf) PopFont();
                        ImGui::SameLine();
                        TextWrapped(def.c_str());
                        src_pos += size; // consumed
                        if (!linkUrl.empty()) PopStyleColor(); if (usedFont) PopFont();
                        return;
                    }
                }
                // leading ': definition' -> render indented muted
                if(s.size() >= 2 && s[0] == ':' ){
                    std::string def = s.substr(1);
                    auto trimLeft = [](std::string &t){ while(!t.empty() && isspace((unsigned char)t.front())) t.erase(t.begin()); };
                    trimLeft(def);
                    PushStyleColor(ImGuiCol_Text, ImVec4(0.6f,0.6f,0.6f,1.0f));
                    ImGui::Indent(ImGui::GetFontSize() * 1.0f);
                    TextWrapped(def.c_str());
                    ImGui::Unindent(ImGui::GetFontSize() * 1.0f);
                    PopStyleColor();
                    src_pos += size;
                    if (!linkUrl.empty()) PopStyleColor(); if (usedFont) PopFont();
                    return;
                }
            }

            // Color for link
            if (!linkUrl.empty())
            {
                PushStyleColor(ImGuiCol_Text, ImVec4(0.26f, 0.53f, 0.96f, 1.0f));
            }

            // Render (use wrapped text so paragraphs wrap properly)
            TextWrapped(s.c_str());

            if (!linkUrl.empty())
                PopStyleColor();
            if (usedFont)
                PopFont();

            // Track source offset for mapping UI interactions back to original text
            src_pos += size;
        }

        // md4c callbacks
        static int enter_block(MD_BLOCKTYPE t, void *detail, void *userdata)
        {
            MD4CRenderer *r = (MD4CRenderer *)userdata;
            r->blocks.push(t);
            switch (t)
            {
            case MD_BLOCK_H:
            {
                int level = ((MD_BLOCK_H_DETAIL *)detail)->level;
                r->pushHeader(level);
                break;
            }
            case MD_BLOCK_UL:
                r->lists.push({MD_BLOCK_UL, 0});
                ImGui::Indent(ImGui::GetFontSize() * 1.5f);
                break;
            case MD_BLOCK_OL:
                r->lists.push({MD_BLOCK_OL, 0});
                ImGui::Indent(ImGui::GetFontSize() * 1.5f);
                break;
            case MD_BLOCK_LI:
                if (!r->lists.empty()) {
                    if (r->lists.top().type == MD_BLOCK_OL)
                        r->lists.top().index++;
                    r->li_first_paragraph.push(true);
                }
                break;
            case MD_BLOCK_P:
                // For paragraphs inside lists, defer starting the paragraph wrap until we get the text callback
                // so we can detect task-list markers and render checkboxes inline. For non-list paragraphs use default.
                if (r->lists.empty()) {
                    r->beginParagraph();
                }
                break;
            case MD_BLOCK_CODE:
                r->beginCodeBlock(&((MD_BLOCK_CODE_DETAIL *)detail)->lang);
                break;
            case MD_BLOCK_HR:
                Separator();
                break;
            case MD_BLOCK_TABLE:
            {
                MD_BLOCK_TABLE_DETAIL *d = (MD_BLOCK_TABLE_DETAIL *)detail;
                r->in_table = true;
                r->table_col_count = d ? d->col_count : 0;
                r->table_header_rows.clear();
                r->table_body_rows.clear();
                r->table_current_row.clear();
                r->table_cell_text.clear();
                r->in_table_cell = false;
                break;
            }
            case MD_BLOCK_THEAD:
                r->table_in_header = true;
                break;
            case MD_BLOCK_TBODY:
                r->table_in_header = false;
                break;
            case MD_BLOCK_TR:
                r->table_current_row.clear();
                break;
            case MD_BLOCK_TH:
            case MD_BLOCK_TD:
                r->in_table_cell = true;
                r->table_cell_text.clear();
                break;
            case MD_BLOCK_QUOTE:
                // blockquote styling
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f,0.6f,0.6f,1.0f));
                ImGui::Indent(ImGui::GetFontSize() * 1.0f);
                break;
            default:
                break;
            }
            return 0;
        }
        static int leave_block(MD_BLOCKTYPE t, void *detail, void *userdata)
        {
            MD4CRenderer *r = (MD4CRenderer *)userdata;
            if (r->blocks.empty())
                return 0;
            r->blocks.pop();
            switch (t)
            {
            case MD_BLOCK_H:
                r->popHeader();
                Dummy(ImVec2(0, 6));
                break;
            case MD_BLOCK_P:
                // If this paragraph belonged to a list and we opened a wrap for it, close it
                if (!r->li_wrap_active.empty() && r->li_wrap_active.top()) {
                    r->endListItemParagraph();
                    r->li_wrap_active.pop();
                } else {
                    r->endParagraph();
                }
                break;
            case MD_BLOCK_LI:
                if (!r->li_first_paragraph.empty()) {
                    bool pending = r->li_first_paragraph.top();
                    r->li_first_paragraph.pop();
                    if (!pending) {
                        // If a paragraph was started by raw text (no MD_BLOCK_P), close its wrap
                        if (!r->li_wrap_active.empty() && r->li_wrap_active.top()) {
                            r->endListItemParagraph();
                            r->li_wrap_active.pop();
                        }
                    }
                }
                break;
            case MD_BLOCK_TH:
            case MD_BLOCK_TD:
                // finalize current table cell
                if (r->in_table_cell) {
                    r->in_table_cell = false;
                    r->table_current_row.push_back(r->table_cell_text);
                    r->table_cell_text.clear();
                }
                break;
            case MD_BLOCK_TR:
                // finish table row
                if (r->in_table) {
                    if (r->table_in_header) r->table_header_rows.push_back(r->table_current_row);
                    else r->table_body_rows.push_back(r->table_current_row);
                    r->table_current_row.clear();
                }
                break;
            case MD_BLOCK_TABLE:
                // render table now
                if (r->in_table) {
                    ImGui::PushID((void*)r);
                    if (ImGui::BeginTable("md_table", r->table_col_count, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg)){
                        // header rows
                        for (auto &hr : r->table_header_rows){
                            ImGui::TableNextRow();
                            for (size_t i = 0; i < hr.size(); ++i){
                                ImGui::TableSetColumnIndex((int)i);
                                ImFont *bf = GetFont(FontStyle::Bold); if(bf) PushFont(bf);
                                TextUnformatted(hr[i].c_str());
                                if(bf) PopFont();
                            }
                        }
                        // body rows
                        for (auto &row : r->table_body_rows){
                            ImGui::TableNextRow();
                            for (size_t i = 0; i < row.size(); ++i){
                                ImGui::TableSetColumnIndex((int)i);
                                TextWrapped(row[i].c_str());
                            }
                        }
                        ImGui::EndTable();
                    }
                    ImGui::PopID();
                }
                // clear table state
                r->in_table = false; r->table_col_count = 0; r->table_in_header = false; r->table_current_row.clear(); r->table_header_rows.clear(); r->table_body_rows.clear();
                break;
            case MD_BLOCK_CODE:
                r->endCodeBlock();
                break;
            case MD_BLOCK_UL:
            case MD_BLOCK_OL:
                ImGui::Unindent(ImGui::GetFontSize() * 1.5f);
                if (!r->lists.empty()) r->lists.pop();
                break;
            default:
                break;
            }
            return 0;
        }
        static int enter_span(MD_SPANTYPE t, void *detail, void *userdata)
        {
            MD4CRenderer *r = (MD4CRenderer *)userdata;
            r->spans.push(t);
            if (t == MD_SPAN_A)
            {
                MD_SPAN_A_DETAIL *d = (MD_SPAN_A_DETAIL *)detail;
                if (d && d->href.text)
                    r->linkUrl.assign(d->href.text, d->href.size);
                else
                    r->linkUrl.clear();
            }
            else if (t == MD_SPAN_IMG)
            {
                MD_SPAN_IMG_DETAIL *d = (MD_SPAN_IMG_DETAIL *)detail;
                std::string src;
                if (d && d->src.text)
                    src.assign(d->src.text, d->src.size);
                std::string label = "[image]";
                try
                {
                    auto p = std::filesystem::path(src);
                    if (!p.filename().empty())
                        label = p.filename().string();
                }
                catch (...)
                {
                }
                PLOGV << "md:enter_img src='" << src << "' label='" << label << "' ctx=" << r->ctx;
                // support size suffix ::<W>x<H> (overrides meta)
                int urlW = -1, urlH = -1;
                std::string baseSrc = src;
                parseSizeSuffix(src, baseSrc, urlW, urlH);

                // If we have a Vault context, attempt inline rendering and caching
                if (r->ctx)
                {
                    Vault *v = reinterpret_cast<Vault *>(r->ctx);
                    // Use baseSrc for lookups (without size suffix)
                    src = baseSrc;
                    // vault attachment
                    const std::string prefix = "vault://attachment/";
                    if (src.rfind(prefix, 0) == 0)
                    {
                        std::string idstr = src.substr(prefix.size());
                        try
                        {
                            int64_t aid = std::stoll(idstr);
                            auto meta = v->getAttachmentMeta(aid);
                            auto isModelExt = [&](const std::string &n) -> bool
                            {
                                std::string ext;
                                try
                                {
                                    ext = std::filesystem::path(n).extension().string();
                                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                }
                                catch (...)
                                {
                                }
                                static const std::vector<std::string> models = {".obj", ".fbx", ".gltf", ".glb", ".ply", ".dae", ".stl"};
                                for (auto &m : models)
                                    if (ext == m)
                                        return true;
                                return (meta.mimeType.find("model") != std::string::npos);
                            };

                            if (isModelExt(meta.name))
                            {
                                // Render inline model viewer (if available) or show a loading placeholder
                                ModelViewer *mv = v->getOrCreateModelViewerForSrc(src);
                                float availW = GetContentRegionAvail().x;
                                // size precedence: URL suffix -> meta display -> defaults (cap to avail)
                                int desiredW = (urlW > 0) ? urlW : (meta.displayWidth > 0 ? meta.displayWidth : -1);
                                int desiredH = (urlH > 0) ? urlH : (meta.displayHeight > 0 ? meta.displayHeight : -1);
                                float width = (desiredW > 0) ? std::min(static_cast<float>(desiredW), availW) : std::min(400.0f, availW);
                                float height = (desiredH > 0) ? static_cast<float>(desiredH) : std::min(300.0f, width * 0.6f);
                                ImVec2 avail = ImVec2(width, height);
                                if (mv && mv->isLoaded())
                                {
                                    mv->renderToRegion(avail);
                                    // clicking inline opens full viewer
                                    if (ImGui::IsItemClicked())
                                        v->openModelFromSrc(src);
                                }
                                else
                                {
                                    if (mv && mv->loadFailed())
                                    {
                                        ImGui::Text("Failed to load model: %s", meta.name.c_str());
                                        ImGui::SameLine();
                                        ImGui::TextDisabled("(%lld bytes)", (long long)meta.size);
                                        ImGui::SameLine();
                                        if (ImGui::SmallButton("View Raw"))
                                            v->openPreviewFromSrc(src);
                                        ImGui::SameLine();
                                        if (ImGui::SmallButton("Open"))
                                            v->openModelFromSrc(src);
                                    }
                                    else if (mv && mv->isLoading())
                                    {
                                        ImGui::Text("Model: %s (loading...)", meta.name.c_str());
                                        ImGui::SameLine();
                                        if (ImGui::SmallButton("Open"))
                                            v->openModelFromSrc(src);
                                    }
                                    else
                                    {
                                        ImGui::Text("Model: %s", meta.name.c_str());
                                        ImGui::SameLine();
                                        if (ImGui::Button("View Model"))
                                            v->openModelFromSrc(src);
                                    }
                                }
                                return 0;
                            }

                            if (meta.size > 0)
                            {
                                std::string key = std::string("vault:att:") + std::to_string(aid);
                                IconTexture cached = GetDynamicTexture(key);
                                if (cached.loaded)
                                {
                                    float availW = GetContentRegionAvail().x;
                                    float width = availW;
                                    if (urlW > 0)
                                        width = std::min(static_cast<float>(urlW), availW);
                                    else if (meta.displayWidth > 0)
                                        width = std::min(static_cast<float>(meta.displayWidth), availW);
                                    else if (cached.width > availW)
                                        width = availW;
                                    else
                                        width = static_cast<float>(cached.width);
                                    float scale = width / static_cast<float>(cached.width);
                                    float height = (urlH > 0) ? static_cast<float>(urlH) : static_cast<float>(cached.height) * scale;
                                    ImGui::Image((ImTextureID)(intptr_t)cached.textureID, ImVec2(width, height));
                                    NewLine();
                                    return 0;
                                }
                                else
                                {
                                    // Schedule background read + main-thread texture creation
                                    std::thread([vaultPtr = v, aid, key]()
                                                {
                                    auto data = vaultPtr->getAttachmentData(aid);
                                    if(!data.empty()){
                                        auto dataPtr = std::make_shared<std::vector<uint8_t>>(std::move(data));
                                        vaultPtr->enqueueMainThreadTask([key, dataPtr, aid](){
                                            LoadTextureFromMemory(key, *dataPtr);
                                            PLOGV << "vault:loaded image aid=" << aid;
                                        });
                                    } })
                                        .detach();
                                }
                            }
                            else
                            {
                                // no data — try async fetch if ExternalPath present
                                if (!meta.externalPath.empty())
                                    v->asyncFetchAndStoreAttachment(meta.id, meta.externalPath);
                            }
                        }
                        catch (...)
                        {
                        }
                        // fallback: show filename button that opens preview
                        if (ImGui::SmallButton(label.c_str()))
                            v->openPreviewFromSrc(src);
                        return 0;
                    }

                    // vault://Assets/ namespace – resolve by ExternalPath
                    const std::string assetsPrefix = "vault://Assets/";
                    if (src.rfind(assetsPrefix, 0) == 0)
                    {
                        int64_t aid = v->findAttachmentByExternalPath(src);
                        PLOGV << "md:assets src='" << src << "' -> aid=" << aid;
                        if (aid != -1)
                        {
                            auto meta = v->getAttachmentMeta(aid);
                            std::string displayName = meta.name.empty() ? label : meta.name;
                            PLOGV << "md:assets meta id=" << meta.id << " name='" << meta.name << "' displayName='" << displayName << "' size=" << meta.size << " externalPath='" << meta.externalPath << "' mime='" << meta.mimeType << "'";
                            auto isModelExt2 = [&](const std::string &n) -> bool
                            {
                                std::string ext;
                                try
                                {
                                    ext = std::filesystem::path(n).extension().string();
                                    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                }
                                catch (...)
                                {
                                }
                                static const std::vector<std::string> models = {".obj", ".fbx", ".gltf", ".glb", ".ply", ".dae", ".stl"};
                                for (auto &m : models)
                                    if (ext == m)
                                        return true;
                                return (meta.mimeType.find("model") != std::string::npos);
                            };
                            if (isModelExt2(displayName))
                            {
                                PLOGV << "md:assets isModelExt2 name='" << displayName << "'";
                                ModelViewer *mv = v->getOrCreateModelViewerForSrc(src);
                                PLOGV << "md:assets mv=" << mv << " isLoaded=" << (mv ? mv->isLoaded() : false) << " loadFailed=" << (mv ? mv->loadFailed() : false);
                                float availW = GetContentRegionAvail().x;
                                // size precedence: URL suffix -> meta display -> defaults (cap to avail)
                                int desiredW = (urlW > 0) ? urlW : (meta.displayWidth > 0 ? meta.displayWidth : -1);
                                int desiredH = (urlH > 0) ? urlH : (meta.displayHeight > 0 ? meta.displayHeight : -1);
                                float width = (desiredW > 0) ? std::min(static_cast<float>(desiredW), availW) : std::min(400.0f, availW);
                                float height = (desiredH > 0) ? static_cast<float>(desiredH) : std::min(300.0f, width * 0.6f);
                                ImVec2 avail = ImVec2(width, height);
                                if (mv && mv->isLoaded())
                                {
                                    mv->renderToRegion(avail);
                                    if (ImGui::IsItemClicked())
                                        v->openModelFromSrc(src);
                                }
                                else
                                {
                                    if (mv && mv->loadFailed())
                                    {
                                        ImGui::Text("Failed to load model: %s", displayName.c_str());
                                        ImGui::SameLine();
                                        ImGui::TextDisabled("(%lld bytes)", (long long)meta.size);
                                        ImGui::SameLine();
                                        if (ImGui::SmallButton("View Raw"))
                                            v->openPreviewFromSrc(src);
                                        ImGui::SameLine();
                                        if (ImGui::Button("View Model"))
                                            v->openModelFromSrc(src);
                                    }
                                    else if (mv && mv->isLoading())
                                    {
                                        ImGui::Text("Model: %s (loading...)", displayName.c_str());
                                        ImGui::SameLine();
                                        if (ImGui::Button("View Model"))
                                            v->openModelFromSrc(src);
                                    }
                                    else
                                    {
                                        ImGui::Text("Model: %s", displayName.c_str());
                                        ImGui::SameLine();
                                        if (ImGui::Button("View Model"))
                                            v->openModelFromSrc(src);
                                    }
                                }
                                return 0;
                            }
                            if (meta.size > 0)
                            {
                                std::string key = std::string("vault:assets:") + std::to_string(aid);
                                IconTexture cached = GetDynamicTexture(key);
                                if (cached.loaded)
                                {
                                    float availW = GetContentRegionAvail().x;
                                    float width = availW;
                                    if (urlW > 0)
                                        width = std::min(static_cast<float>(urlW), availW);
                                    else if (meta.displayWidth > 0)
                                        width = std::min(static_cast<float>(meta.displayWidth), availW);
                                    else if (cached.width > availW)
                                        width = availW;
                                    else
                                        width = static_cast<float>(cached.width);
                                    float scale = width / static_cast<float>(cached.width);
                                    float height = (urlH > 0) ? static_cast<float>(urlH) : static_cast<float>(cached.height) * scale;
                                    ImGui::Image((ImTextureID)(intptr_t)cached.textureID, ImVec2(width, height));
                                    NewLine();
                                    return 0;
                                }
                                else
                                {
                                    std::thread([vaultPtr = v, aid, key]()
                                                {
                                    auto data = vaultPtr->getAttachmentData(aid);
                                    if(!data.empty()){
                                        auto dataPtr = std::make_shared<std::vector<uint8_t>>(std::move(data));
                                        vaultPtr->enqueueMainThreadTask([key, dataPtr, aid](){
                                            LoadTextureFromMemory(key, *dataPtr);
                                            PLOGV << "vault:loaded image aid=" << aid;
                                        });
                                    } })
                                        .detach();
                                }
                            }
                            else
                            {
                                if (!meta.externalPath.empty())
                                    v->asyncFetchAndStoreAttachment(meta.id, meta.externalPath);
                                else
                                {
                                    PLOGW << "md:assets missing data for aid=" << aid << " (no blob, no externalPath)";
                                    ImGui::Text("Asset: %s (no data)", displayName.c_str());
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Open"))
                                        v->openPreviewFromSrc(src);
                                }
                            }
                        }
                        else
                        {
                            if (ImGui::SmallButton((std::string("Asset: ") + label).c_str()))
                                v->openPreviewFromSrc(src);
                        }
                        return 0;
                    }

                    // http/https remote resource — check cache
                    if (src.rfind("http://", 0) == 0 || src.rfind("https://", 0) == 0)
                    {
                        // If it's a model extension, create/queue and offer "View Model"
                        auto isModelUrl = [&](const std::string &u) -> bool
                        {
                            try
                            {
                                auto ext = std::filesystem::path(u).extension().string();
                                std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
                                static const std::vector<std::string> models = {".obj", ".fbx", ".gltf", ".glb", ".ply", ".dae", ".stl"};
                                for (auto &m : models)
                                    if (ext == m)
                                        return true;
                            }
                            catch (...)
                            {
                            }
                            return false;
                        };
                        if (isModelUrl(src))
                        {
                            // strip size suffix if present for lookups
                            std::string modelBase = src;
                            int urlW2 = -1, urlH2 = -1;
                            parseSizeSuffix(src, modelBase, urlW2, urlH2);
                            src = modelBase;
                            // Try inline viewer
                            int64_t aid = v->findAttachmentByExternalPath(src);
                            if (aid == -1)
                                aid = v->addAttachmentFromURL(src);
                            auto meta = v->getAttachmentMeta(aid);
                            ModelViewer *mv = v->getOrCreateModelViewerForSrc(src);
                            float availW = GetContentRegionAvail().x;
                            // size precedence: URL suffix -> meta display -> defaults
                            int desiredW = (urlW2 > 0) ? urlW2 : (meta.displayWidth > 0 ? meta.displayWidth : -1);
                            int desiredH = (urlH2 > 0) ? urlH2 : (meta.displayHeight > 0 ? meta.displayHeight : -1);
                            float width = (desiredW > 0) ? std::min(static_cast<float>(desiredW), availW) : std::min(480.0f, availW);
                            float height = (desiredH > 0) ? static_cast<float>(desiredH) : std::min(std::min(320.0f, availW * 0.4f), ImGui::GetTextLineHeight() * 6.0f);
                            ImVec2 avail = ImVec2(width, height);
                            if (mv && mv->isLoaded())
                            {
                                mv->renderToRegion(avail);
                                if (ImGui::IsItemClicked())
                                    v->openModelFromSrc(src);
                            }
                            else
                            {
                                ImGui::Text("Model: %s", label.c_str());
                                ImGui::SameLine();
                                if (ImGui::Button("View Model"))
                                    v->openModelFromSrc(src);
                            }
                            return 0;
                        }

                        int64_t aid = v->findAttachmentByExternalPath(src);
                        if (aid == -1)
                        {
                            // create placeholder and start async fetch
                            aid = v->addAttachmentFromURL(src);
                            ImGui::Text("Fetching image: %s", label.c_str());
                            return 0;
                        }
                        auto meta = v->getAttachmentMeta(aid);
                        if (meta.size > 0)
                        {
                            std::string key = std::string("vault:url:") + std::to_string(aid);
                            IconTexture cached = GetDynamicTexture(key);
                            if (cached.loaded)
                            {
                                float availW = GetContentRegionAvail().x;
                                float width = availW;
                                if (urlW > 0)
                                    width = std::min(static_cast<float>(urlW), availW);
                                else if (meta.displayWidth > 0)
                                    width = std::min(static_cast<float>(meta.displayWidth), availW);
                                else if (cached.width > availW)
                                    width = availW;
                                else
                                    width = static_cast<float>(cached.width);
                                float scale = width / static_cast<float>(cached.width);
                                float height = (urlH > 0) ? static_cast<float>(urlH) : static_cast<float>(cached.height) * scale;
                                ImGui::Image((ImTextureID)(intptr_t)cached.textureID, ImVec2(width, height));
                                NewLine();
                                return 0;
                            }
                            else
                            {
                                std::thread([vaultPtr = v, aid, key]()
                                            {

                                auto data = vaultPtr->getAttachmentData(aid);
                                if(!data.empty()){
                                    auto dataPtr = std::make_shared<std::vector<uint8_t>>(std::move(data));
                                    vaultPtr->enqueueMainThreadTask([key, dataPtr, aid](){
                                        LoadTextureFromMemory(key, *dataPtr);
                                        PLOGV << "vault:loaded image aid=" << aid;
                                    });
                                } })
                                    .detach();
                            }
                        }
                        ImGui::Text("Fetching image: %s", label.c_str());
                        return 0;
                    }
                }

                // Fallback: show a placeholder small button (no context or unsupported scheme)
                if (ImGui::SmallButton(label.c_str()))
                {
                    // If no Vault context, we could open external viewer if path is local
                    try
                    {
                        std::string p(src);
                        if (p.rfind("file://", 0) == 0)
                            p = p.substr(7);
                        if (std::filesystem::exists(p))
                        {
                            // try to open preview using system default (not implemented)
                        }
                    }
                    catch (...)
                    {
                    }
                }
                return 0;
            }
            return 0;
        }
        static int leave_span(MD_SPANTYPE t, void *detail, void *userdata)
        {
            MD4CRenderer *r = (MD4CRenderer *)userdata;
            if (!r->spans.empty())
                r->spans.pop();
            if (t == MD_SPAN_A)
                r->linkUrl.clear();
            return 0;
        }
        static int textcb(MD_TEXTTYPE type, const MD_CHAR *text, MD_SIZE size, void *userdata)
        {
            MD4CRenderer *r = (MD4CRenderer *)userdata;
            r->renderText(text, size);
            return 0;
        }
    };

    void MarkdownText(const char *text) { MarkdownText(text, nullptr); }

    void MarkdownText(const char *text, void *context)
    {
        if (!text || !text[0])
            return;
        ImGuiWindow *w = GetCurrentWindow();
        if (w->SkipItems)
            return;

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

    void MarkdownText(const std::string &text) { MarkdownText(text.c_str()); }
    void MarkdownText(const std::string &text, void *context) { MarkdownText(text.c_str(), context); }

}