#include <MarkdownText.hpp>
#include <md4c.h>
#include <md4c-html.h>
#include <string>
#include <vector>
#include <stack>
#include <chrono>
#include <unordered_set>
#include <Fonts.hpp>
#include <plog/Log.h>
#include "Vault.hpp"
#include "Icons.hpp"
#include <stringUtils.hpp>
#include <WorldMaps/WorldMap.hpp>
#include "LuaScriptManager.hpp"
#include "LuaCanvasBindings.hpp"
#include "LuaImGuiBindings.hpp"
#include "LuaEngine.hpp"
#include "ResourceExplorer.hpp"
#include <GL/gl.h>
#include "TextEffectsOverlay.hpp"
#include "TextEffectSystem.hpp"
#include <cctype>
#include <algorithm>
#include <cstdlib>

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
        // Per-render unique embed counter for disambiguating multiple identical embeds in the same document
        int embedCounter = 0;
        // Pointer to original source buffer base used to compute absolute byte offsets for md4c callbacks
        const MD_CHAR *source_base = nullptr; 

        // ── Text effects system ──
        struct ActiveEffect {
            EffectParams params;
            TextEffectDef effectDef;
            std::string tagName;
        };
        std::vector<ActiveEffect> effectStack;
        TextEffectsOverlay *effectsOverlay = nullptr;
        // Document-space offset: used to convert screen positions to document coords
        ImVec2 contentOrigin = ImVec2(0,0);
        float docScrollY = 0.0f;
        // Flag: an inline effect tag was just consumed, so the next text chunk should
        // continue on the same visual line via SameLine(0,0).
        bool sameLineNext = false;
        // Flag: visible text has been rendered in the current block. Used to gate
        // sameLineNext so that opening tags at the start of a paragraph don't
        // cause SameLine against the previous paragraph's content.
        bool hasRenderedTextInBlock = false;

        // Parse an effect HTML tag and return the params + TextEffectDef. Returns true if it was an effect tag.
        static bool parseEffectTag(const std::string &html, EffectParams &out, TextEffectDef &outDef,
                                   std::string &tagName, bool &isClosing)
        {
            isClosing = false;
            tagName.clear();
            if (html.empty() || html[0] != '<') return false;
            size_t i = 1;
            if (i < html.size() && html[i] == '/') { isClosing = true; i++; }
            // Extract tag name
            while (i < html.size() && html[i] != ' ' && html[i] != '>' && html[i] != '/') {
                tagName.push_back((char)std::tolower((unsigned char)html[i]));
                i++;
            }
            // Check if tag is a known modifier or the generic "effect" compose tag
            bool isEffectTag = (tagName == "effect");
            bool isKnownMod = (TextEffectSystem::parseModName(tagName) != FXMod::Count);
            if (!isEffectTag && !isKnownMod) return false;
            if (isClosing) return true; // closing tags just need the tag name for stack matching

            // Parse key=value attributes and bare words (modifier names without values)
            std::vector<std::pair<std::string,std::string>> attrs;
            std::vector<std::string> bareWords;

            while (i < html.size() && html[i] != '>') {
                while (i < html.size() && (html[i] == ' ' || html[i] == '\t')) i++;
                if (i >= html.size() || html[i] == '>' || html[i] == '/') break;

                std::string key;
                while (i < html.size() && html[i] != '=' && html[i] != ' ' && html[i] != '>') {
                    key.push_back((char)std::tolower((unsigned char)html[i]));
                    i++;
                }

                if (i < html.size() && html[i] == '=') {
                    i++; // skip '='
                    std::string val;
                    if (i < html.size() && (html[i] == '\"' || html[i] == '\'')) {
                        char q = html[i]; i++;
                        while (i < html.size() && html[i] != q) { val.push_back(html[i]); i++; }
                        if (i < html.size()) i++; // skip closing quote
                    } else {
                        while (i < html.size() && html[i] != ' ' && html[i] != '>') {
                            val.push_back(html[i]); i++;
                        }
                    }
                    attrs.push_back({key, val});
                } else if (!key.empty()) {
                    bareWords.push_back(key);
                }
            }

            // Build the composable TextEffectDef via TextEffectSystem
            outDef = TextEffectSystem::buildFromTag(tagName, attrs, bareWords);

            // Derive legacy EffectParams for overlay compatibility
            out = EffectParams();
            if (!outDef.modifiers.empty()) {
                auto &first = outDef.modifiers[0];
                switch (first.type) {
                case FXMod::Pulse:     out.type = TextEffectType::Pulse; break;
                case FXMod::Glow:      out.type = TextEffectType::Glow; break;
                case FXMod::Shake:     out.type = TextEffectType::Shake; break;
                case FXMod::FireEmit:  out.type = TextEffectType::Fire; break;
                case FXMod::FairyDust: out.type = TextEffectType::Sparkle; break;
                case FXMod::Rainbow:   out.type = TextEffectType::Rainbow; break;
                default:               out.type = TextEffectType::None; break;
                }
                out.cycleSec = 1.0f / std::max(0.01f, first.params.speed);
                out.intensity = first.params.amplitude;
                out.color = first.params.color;
                out.radius = first.params.radius;
                out.speed = first.params.speed;
                out.density = first.params.density;
                out.lifetimeSec = first.params.lifetime;
            }
            return true;
        }

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
        // If suppressBullet is true, do not print the bullet/number nor start the wrapping; caller
        // will render a custom widget (e.g., a checkbox) and then start the wrap inline.
        void beginListItemParagraph(bool ordered, int index, bool suppressBullet = false)
        {
            if (!suppressBullet) {
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
            } else {
                // suppressed bullet -> do nothing here (caller will render inline widget and start wrap)
            }
        }
        void endListItemParagraph()
        {
            PopTextWrapPos();
            // Do NOT add a NewLine() here — avoid inserting extra blank lines that were not
            // present in the original markdown source. The caller/leave_block will manage
            // block spacing according to the document structure.
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
            // Intentionally avoid calling NewLine() here to prevent inserting extra newline
            // characters at paragraph boundaries that the original source did not include.
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
            size_t localPos = 0;
            // md4c may deliver text callbacks that omit list markers (e.g. "- "), so compute absolute byte offset
            // from the original source pointer when available.
            if (source_base && text) {
                localPos = (size_t)((const char*)text - (const char*)source_base);
            } else {
                localPos = src_pos;
            }
            if (!in_code_block && !lists.empty() && !li_first_paragraph.empty() && li_first_paragraph.top()) {
                // Look for a checkbox marker near the beginning of this text chunk
                size_t bracketPos = std::string::npos;
                for(size_t i=0;i<std::min<size_t>(s.size(),8);++i){ if(s[i]=='['){ bracketPos = i; break; } }
                if (bracketPos != std::string::npos && bracketPos + 2 < s.size() && s[bracketPos+2] == ']') {
                    char c = s[bracketPos+1];
                    bool checked = (c == 'x' || c == 'X');

                    // Prepare list item paragraph but suppress the bullet/number since we render a checkbox.
                    bool ordered = (lists.top().type == MD_BLOCK_OL);
                    // Defer the actual wrap until after the checkbox is rendered so the text stays inline
                    beginListItemParagraph(ordered, lists.top().index, true);
                    li_first_paragraph.top() = false;
                    li_wrap_active.push(true);

                    // If this is a root-level list (depth == 1), temporarily outdent so checkbox is left-aligned
                    bool didUnindent = false;
                    if (lists.size() == 1) {
                        ImGui::Unindent(ImGui::GetFontSize() * 1.5f);
                        didUnindent = true;
                    }

                    // Render checkbox in place of bullet
                    ImGui::PushID((void*)(intptr_t)(localPos + bracketPos));
                    // Reduce vertical spacing for the checkbox so it lines up compactly with text
                    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0.0f, 1.0f));
                    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(ImGui::GetStyle().ItemSpacing.x, 2.0f));
                    ImGui::AlignTextToFramePadding();
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
                    ImGui::PopStyleVar(2);
                    ImGui::PopID();

                    // Restore indent if we temporarily unindented
                    if (didUnindent) {
                        ImGui::Indent(ImGui::GetFontSize() * 1.5f);
                    }

                    // Put remaining text on the same line, with same spacing as normal list items
                    SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
                    size_t after = bracketPos + ((bracketPos + 3 < s.size() && s[bracketPos+3]==' ') ? 4 : 3);
                    std::string rem;
                    if (after < s.size()) rem = s.substr(after);

                    // Now start a wrapped paragraph aligned with the post-checkbox cursor position
                    // Keep the wrap active (do NOT PopTextWrapPos here) so leave_block will close it correctly.
                    float wrapX = GetCursorPosX() + GetContentRegionAvail().x;
                    PushTextWrapPos(wrapX);
                    TextWrapped(rem.c_str());

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

            // Color for link — push BEFORE definition list heuristic so early returns can pop it
            bool pushedLinkColor = false;
            if (!linkUrl.empty())
            {
                PushStyleColor(ImGuiCol_Text, ImVec4(0.26f, 0.53f, 0.96f, 1.0f));
                pushedLinkColor = true;
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
                        if (pushedLinkColor) PopStyleColor(); if (usedFont) PopFont();
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
                    if (pushedLinkColor) PopStyleColor(); if (usedFont) PopFont();
                    return;
                }
            }

            // Render (use wrapped text so paragraphs wrap properly)
            // Continue inline text flow after consumed HTML effect tags (SameLine
            // keeps chunks like "The <shake>word</shake> rest" on one visual line)
            if (sameLineNext) {
                ImGui::SameLine(0, 0);
                sameLineNext = false;
            }

            // Record cursor position before rendering for glyph data capture
            ImVec2 preRenderPos = GetCursorScreenPos();

            // ── Capture draw list vertex count before rendering text ──
            ImDrawList *dl = GetWindowDrawList();
            int vtxCountBefore = dl->VtxBuffer.Size;

            TextWrapped(s.c_str());
            ImVec2 postRenderPos = GetCursorScreenPos();

            // ── Per-character text effects via TextEffectSystem ──
            // Merge all stacked effects and apply (position → color → alpha → additive)
            if (!effectStack.empty())
            {
                ImTextureID atlasID = GetIO().Fonts->TexID;
                TextEffectDef combined;
                for (auto &ae : effectStack)
                    combined.modifiers.insert(combined.modifiers.end(),
                        ae.effectDef.modifiers.begin(), ae.effectDef.modifiers.end());
                TextEffectSystem::applyEffect(dl, vtxCountBefore, dl->VtxBuffer.Size,
                                              atlasID, (float)GetTime(), combined);
            }

            // ── Record glyph data for text effects overlay ──
            // Push one region per active effect so stacked effects all get overlay data
            if (!effectStack.empty() && effectsOverlay)
            {
                ImFont *font = GetFont();
                float fontSize = font->FontSize;
                ImFontAtlas *atlas = GetIO().Fonts;
                float atlasW = (float)atlas->TexWidth;
                float atlasH = (float)atlas->TexHeight;

                // Build glyph list once, reuse for all stacked effects
                std::vector<EffectGlyphInfo> glyphList;
                ImVec2 bMin(preRenderPos.x - contentOrigin.x,
                            preRenderPos.y - contentOrigin.y + docScrollY);
                ImVec2 bMax = bMin;

                float curX = preRenderPos.x;
                float curY = preRenderPos.y;
                float wrapWidth = GetContentRegionAvail().x + GetCursorPosX();
                int charIdx = 0;

                const char *p = s.c_str();
                const char *end = p + s.size();
                while (p < end)
                {
                    unsigned int cp = 0;
                    int charLen = ImTextCharFromUtf8(&cp, p, end);
                    if (charLen == 0) break;

                    const ImFontGlyph *glyph = font->FindGlyph((ImWchar)cp);
                    if (glyph)
                    {
                        float glyphW = (glyph->X1 - glyph->X0) * (fontSize / font->FontSize);
                        float glyphH = (glyph->Y1 - glyph->Y0) * (fontSize / font->FontSize);
                        float advance = glyph->AdvanceX * (fontSize / font->FontSize);

                        // Simple word wrap approximation
                        if (cp == '\n') { curX = preRenderPos.x; curY += fontSize; }

                        EffectGlyphInfo gi;
                        gi.docPos = ImVec2(curX - contentOrigin.x,
                                           curY - contentOrigin.y + docScrollY);
                        gi.uvMin = ImVec2(glyph->U0, glyph->V0);
                        gi.uvMax = ImVec2(glyph->U1, glyph->V1);
                        gi.advanceX = advance;
                        gi.glyphW = glyphW;
                        gi.glyphH = glyphH;
                        gi.charIndex = charIdx;

                        glyphList.push_back(gi);

                        // Expand bounds
                        bMax.x = std::max(bMax.x, gi.docPos.x + glyphW);
                        bMax.y = std::max(bMax.y, gi.docPos.y + glyphH);
                        bMin.x = std::min(bMin.x, gi.docPos.x);
                        bMin.y = std::min(bMin.y, gi.docPos.y);

                        curX += advance;
                    }

                    p += charLen;
                    charIdx++;
                }

                if (!glyphList.empty())
                {
                    for (auto &ae : effectStack)
                    {
                        EffectRegion region;
                        region.params = ae.params;
                        region.boundsMin = bMin;
                        region.boundsMax = bMax;
                        region.glyphs = glyphList;
                        effectsOverlay->addEffectRegion(region);
                    }
                }
            }

            if (pushedLinkColor)
                PopStyleColor();
            if (usedFont)
                PopFont();

            hasRenderedTextInBlock = true;
            // Track source offset for mapping UI interactions back to original text
            src_pos += size;
        }

        // md4c callbacks
        static int enter_block(MD_BLOCKTYPE t, void *detail, void *userdata)
        {
            MD4CRenderer *r = (MD4CRenderer *)userdata;
            r->sameLineNext = false;
            r->hasRenderedTextInBlock = false;
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
                r->effectStack.clear();
                r->popHeader();
                Dummy(ImVec2(0, 6));
                break;
            case MD_BLOCK_P:
                // Clear any unclosed effect tags at paragraph boundary
                r->effectStack.clear();
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
                r->effectStack.clear();
                r->endCodeBlock();
                break;
            case MD_BLOCK_QUOTE:
                r->effectStack.clear();
                ImGui::Unindent(ImGui::GetFontSize() * 1.0f);
                ImGui::PopStyleColor();
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
                    // vault://World/ namespace
                    const std::string worldPrefix = "vault://World/";
                    if (src.rfind(worldPrefix, 0) == 0)
                    {
                        //get the string after the prefix
                        std::string worldPath = src.substr(worldPrefix.size());
                        std::vector<std::string> parts = splitBracketAware(worldPath, "/");
                        if (parts.size() == 2)
                        {
                            try{
                            std::string worldName="";
                            std::string config= "";
                            splitNameConfig(parts[0], worldName, config);
                            std::string projection= parts[1];
                            //convert projection to lowercase
                            std::transform(projection.begin(), projection.end(), projection.begin(), ::tolower);
                            PLOGV << "md:world src='" << src << "' world='" << worldName << "' config='" << config << "' projection='" << projection << "'";
                            ImVec2 size = GetContentRegionAvail();
                            struct CachedWorld {
                                World world;
                                std::string config;
                                std::chrono::steady_clock::time_point last_used;
                                CachedWorld(const std::string &cfg) : world(cfg), config(cfg), last_used(std::chrono::steady_clock::now()) {}
                            };
                            static std::unordered_map<std::string, CachedWorld> worldCache;
                            auto now = std::chrono::steady_clock::now();
                            // cleanup stale entries not used in last 5 seconds
                            constexpr auto kWorldCacheLifetime = std::chrono::seconds(5);
                            for (auto itc = worldCache.begin(); itc != worldCache.end();) {
                                if (now - itc->second.last_used > kWorldCacheLifetime) {
                                    PLOGV << "md:world evicting '" << itc->first << "' from cache";
                                    itc = worldCache.erase(itc);
                                } else {
                                    ++itc;
                                }
                            }
                            auto it = worldCache.try_emplace(worldName, config).first;
                            CachedWorld &cw = it->second;
                            // If stored config differs from requested config, re-parse on existing world
                            if (cw.config != config) {
                                PLOGV << "md:world config changed for '" << worldName << "' - re-parsing config";
                                try {
                                    cw.world.parseConfig(config);
                                    cw.config = config;
                                } catch (const std::exception &e) {
                                    PLOGW << "md:world parseConfig failed for '" << worldName << "': " << e.what();
                                }
                            }
                            cw.last_used = now;
                            World &map = cw.world;
                            if(projection=="mercator"){
                                size.y= size.x * 0.5f;
                                mercatorMap(worldPath.c_str(),size,map);
                                return 0;
                            }
                            else if(projection=="globe"){
                                size.y= size.x;
                                globeMap(worldPath.c_str(),size,map);
                                return 0;
                            }
                            }
                            catch(std::exception &e){
                                ImGui::TextColored(ImVec4(1.0f,0.5f,0.0f,1.0f),"Error rendering world map for src='%s'",src.c_str());
                                ImGui::TextColored(ImVec4(1.0f,0.5f,0.0f,1.0f),"%s",e.what());
                                PLOGW << "md:world failed to render world map for src='" << src << "'";
                                PLOGW << "  exception: " << e.what();
                            }
                        }
                        else
                        {
                            ImGui::TextColored(ImVec4(1.0f,0.5f,0.0f,1.0f),"Invalid world map src format: '%s'",src.c_str());
                            PLOGW << "md:world invalid src format: '" << src << "'";
                        }
                        return 0;
                    }

                    // vault://Scripts/ namespace -> lua scripts
                    const std::string scriptsPrefix = "vault://Scripts/";
                    if (src.rfind(scriptsPrefix, 0) == 0)
                    {
                        // scriptPath is the path portion after the prefix (e.g., 'magic_circle.lua')
                        std::string scriptName = src.substr(scriptsPrefix.size());
                        // Create a per-instance embed ID using the source offset plus a per-render counter
                        std::string embedID = std::to_string(r->src_pos) + ":" + std::to_string(++r->embedCounter);
                        auto mgr = v->getScriptManager();
                        if (!mgr)
                        {
                            ImGui::TextColored(ImVec4(1,0,0,1), "[Script: no script manager]");
                            return 0;
                        }
                        LuaEngine *eng = mgr->getOrCreateEngine(scriptName, embedID, v->getSelectedItemID());
                        if (!eng)
                        {
                            ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Script failed to load: %s", scriptName.c_str());
                            // Try to fetch any recorded load error from the script manager
                            std::string loadErr = mgr ? mgr->getLastError(scriptName, embedID) : std::string();
                            if (!loadErr.empty()){
                                ImGui::TextWrapped("%s", loadErr.c_str());
                                if (ImGui::SmallButton("Copy Error")) {
                                    ImGui::SetClipboardText(loadErr.c_str());
                                }
                            } else {
                                ImGui::TextWrapped("No additional error details available.");
                            }

                            ImGui::SameLine();
                            if (ImGui::SmallButton("Open in Explorer"))
                                RequestOpenResourceExplorer(std::string("vault://Scripts/") + scriptName);

                            // one-time error log so we don't spam the logs repeatedly while preview remains open
                            static std::unordered_set<std::string> s_logged;
                            std::string skey = scriptName + "::" + embedID + ":load";
                            if (!loadErr.empty() && s_logged.find(skey) == s_logged.end()) {
                                PLOGE << "md:script '" << scriptName << "' embed=" << embedID << " failed to load: " << loadErr;
                                s_logged.insert(skey);
                            }

                            return 0;
                        }

                        // If script has error state, show the full error in the preview and log it once
                        if (!eng->lastError().empty())
                        {
                            std::string err = eng->lastError();
                            ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Script error: %s", scriptName.c_str());
                            ImGui::TextWrapped("%s", err.c_str());
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Copy Error")) ImGui::SetClipboardText(err.c_str());
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Open in Explorer"))
                                RequestOpenResourceExplorer(std::string("vault://Scripts/") + scriptName);

                            static std::unordered_set<std::string> s_logged_state;
                            std::string skey_state = scriptName + "::" + embedID + ":state";
                            if (s_logged_state.find(skey_state) == s_logged_state.end()) {
                                PLOGE << "md:script '" << scriptName << "' embed=" << embedID << " error: " << err;
                                s_logged_state.insert(skey_state);
                            }

                            return 0;
                        }

                        ScriptConfig cfg = eng->callConfig();
                        int width = (urlW > 0) ? urlW : cfg.width;
                        int height = (urlH > 0) ? urlH : cfg.height;

                        ImGui::PushID(embedID.c_str());
                        if (cfg.type == ScriptConfig::Type::Canvas)
                        {
                            //no padding, we want the canvas to take the full size specified by the script or URL suffix
                            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
                            ImGui::BeginChild("lua_canvas", ImVec2((float)width, (float)height), true);
                            ImVec2 pos = ImGui::GetCursorScreenPos();
                            // Use the window content region origin (absolute screen coords) so the canvas
                            // coordinate space matches what ImGui actually draws for the child content.
                            ImVec2 winPos = ImGui::GetWindowPos();
                            ImVec2 contentMin = ImGui::GetWindowContentRegionMin(); // relative to window
                            ImVec2 origin = ImVec2(winPos.x + contentMin.x, winPos.y + contentMin.y);


                            // Render canvas frame via engine (handles FBO creation, binding, GL canvas, flush)
                            float dt = ImGui::GetIO().DeltaTime;
                            unsigned int texID = eng->renderCanvasFrame(embedID, width, height, dt);

                            // Render the resulting texture into the ImGui child region
                            ImVec2 avail = ImGui::GetContentRegionAvail();
                            ImGui::Image((ImTextureID)(intptr_t)texID, avail, ImVec2(0,1), ImVec2(1,0));

                            // IMMEDIATELY place invisible button covering the canvas to capture all input
                            // (must be placed right after Image, before any other widgets that might interfere)
                            ImGui::SetCursorScreenPos(pos);
                            ImGui::InvisibleButton("canvas_click", ImVec2((float)width, (float)height));
                            ImGuiIO &io = ImGui::GetIO();
                            bool isHover = ImGui::IsItemHovered();

                            // Mouse button events: down/up/click/doubleclick
                            for (int b = 0; b < 3; ++b)
                            {
                                // mousedown (pressed)
                                if (isHover && ImGui::IsMouseClicked((ImGuiMouseButton)b))
                                {
                                    ImVec2 clickPos = io.MouseClickedPos[b];
                                    ImVec2 rel = ImVec2(clickPos.x - origin.x, clickPos.y - origin.y);
                                    LuaEngine::CanvasEvent ev; ev.type = "mousedown";
                                    ev.data["button"] = std::to_string(b);
                                    ev.data["x"] = std::to_string(rel.x);
                                    ev.data["y"] = std::to_string(rel.y);
                                    ev.data["ctrl"] = io.KeyCtrl ? "1" : "0";
                                    ev.data["shift"] = io.KeyShift ? "1" : "0";
                                    ev.data["alt"] = io.KeyAlt ? "1" : "0";
                                    eng->callOnCanvasEvent(ev);
                                }
                                // mouseup (released)
                                if (isHover && ImGui::IsMouseReleased((ImGuiMouseButton)b))
                                {
                                    ImVec2 rel = ImVec2(io.MousePos.x - origin.x, io.MousePos.y - origin.y);
                                    LuaEngine::CanvasEvent ev; ev.type = "mouseup";
                                    ev.data["button"] = std::to_string(b);
                                    ev.data["x"] = std::to_string(rel.x);
                                    ev.data["y"] = std::to_string(rel.y);
                                    eng->callOnCanvasEvent(ev);
                                }
                                // double click
                                if (isHover && ImGui::IsMouseDoubleClicked((ImGuiMouseButton)b))
                                {
                                    ImVec2 clickPos = io.MouseClickedPos[b];
                                    ImVec2 rel = ImVec2(clickPos.x - origin.x, clickPos.y - origin.y);
                                    LuaEngine::CanvasEvent ev; ev.type = "doubleclick";
                                    ev.data["button"] = std::to_string(b);
                                    ev.data["x"] = std::to_string(rel.x);
                                    ev.data["y"] = std::to_string(rel.y);
                                    eng->callOnCanvasEvent(ev);
                                }
                            }

                            // Scroll events
                            if (isHover && (io.MouseWheel != 0.0f || io.MouseWheelH != 0.0f))
                            {
                                LuaEngine::CanvasEvent ev; ev.type = "scroll";
                                ev.data["dx"] = std::to_string(io.MouseWheelH);
                                ev.data["dy"] = std::to_string(io.MouseWheel);
                                ev.data["x"] = std::to_string(io.MousePos.x - origin.x);
                                ev.data["y"] = std::to_string(io.MousePos.y - origin.y);
                                eng->callOnCanvasEvent(ev);
                            }

                            // Movement / hover update (keep sending while hovered)
                            if (isHover)
                            {
                                ImVec2 rel = ImVec2(io.MousePos.x - origin.x, io.MousePos.y - origin.y);
                                LuaEngine::CanvasEvent ev; ev.type = "mousemove";
                                ev.data["x"] = std::to_string(rel.x);
                                ev.data["y"] = std::to_string(rel.y);
                                // indicate which buttons are currently down
                                ev.data["left"] = io.MouseDown[0] ? "1" : "0";
                                ev.data["right"] = io.MouseDown[1] ? "1" : "0";
                                ev.data["middle"] = io.MouseDown[2] ? "1" : "0";
                                eng->callOnCanvasEvent(ev);
                            }

                            ImGui::EndChild();
                            ImGui::PopStyleVar();

                            // Diagnostic information displayed below the canvas (outside child window to avoid clipping)
                            // Runtime error display
                            if (!eng->lastError().empty())
                            {
                                std::string rerr = eng->lastError();
                                ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Runtime error: %s", scriptName.c_str());
                                ImGui::TextWrapped("%s", rerr.c_str());
                                if (ImGui::SmallButton("Copy Error")) ImGui::SetClipboardText(rerr.c_str());

                                static std::unordered_set<std::string> s_logged_runtime;
                                std::string skey_rt = scriptName + "::" + embedID + ":runtime";
                                if (s_logged_runtime.find(skey_rt) == s_logged_runtime.end()) {
                                    PLOGE << "md:script '" << scriptName << "' embed=" << embedID << " runtime error: " << rerr;
                                    s_logged_runtime.insert(skey_rt);
                                }
                            }

                            // Draw call count diagnostic
                            {
                                int drawCount = eng->canvasDrawCount();
                                ImGui::TextColored(ImVec4(0.6f,1.0f,0.6f,1.0f), "Canvas draw calls: %d", drawCount);
                                PLOGI << "md:script '" << scriptName << "' embed=" << embedID << " drawCalls=" << drawCount;

                                if (drawCount == 0)
                                {
                                    ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Canvas produced no draw calls — check Render() or script");
                                    ImGui::SameLine();
                                    if (ImGui::SmallButton("Open in Explorer"))
                                        RequestOpenResourceExplorer(std::string("vault://Scripts/") + scriptName);
                                }
                            }
                        }
                        else if (cfg.type == ScriptConfig::Type::UI)
                        {
                            ImGui::BeginChild("lua_ui", ImVec2((float)width, (float)height), true);
                            // UI bindings already registered at engine creation
                            eng->callUI();
                            ImGui::EndChild();
                        }
                        else
                        {
                            ImGui::TextColored(ImVec4(1,0.5f,0,1), "[Script has no Config() or unknown type]");
                        }
                        ImGui::PopID();
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
                                        ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Failed to load model: %s", displayName.c_str());
                                        ImGui::SameLine();
                                        ImGui::TextDisabled("(%lld bytes)", (long long)meta.size);
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
                                    if (!meta.externalPath.empty())
                                        v->asyncFetchAndStoreAttachment(meta.id, meta.externalPath);
                                    else
                                    {
                                        PLOGW << "md:assets missing data for aid=" << aid << " (no blob, no externalPath)";
                                        ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Asset missing data: %s", displayName.c_str());
                                    }
                                }
                            }
                        }
                        else
                        {
                            ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Asset not found: %s", label.c_str());
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

                // Fallback: unsupported scheme or no preview available
                ImGui::TextColored(ImVec4(1,0.3f,0.3f,1), "Cannot preview resource: %s", label.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Open in Explorer")){
                    RequestOpenResourceExplorer(src);
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

            // Soft/hard breaks reset inline continuation so the next text chunk
            // starts on a new line (ImGui naturally places items below each other).
            if (type == MD_TEXT_SOFTBR || type == MD_TEXT_BR)
            {
                r->sameLineNext = false;
                r->src_pos += size;
                return 0;
            }

            // Intercept HTML tags for text effects
            if (type == MD_TEXT_HTML && !r->in_code_block)
            {
                std::string html((const char *)text, (size_t)size);
                EffectParams params;
                TextEffectDef effectDef;
                std::string tagName;
                bool isClosing = false;
                if (parseEffectTag(html, params, effectDef, tagName, isClosing))
                {
                    if (isClosing) {
                        // Pop matching effect from stack
                        for (int i = (int)r->effectStack.size() - 1; i >= 0; i--) {
                            if (r->effectStack[i].tagName == tagName) {
                                r->effectStack.erase(r->effectStack.begin() + i);
                                break;
                            }
                        }
                    } else {
                        // Push new effect
                        r->effectStack.push_back({params, effectDef, tagName});
                    }
                    // Consume the HTML tag silently — don't render.
                    // Signal that the next text chunk should continue inline,
                    // but only if visible text was already rendered in this block.
                    r->src_pos += size;
                    if (r->hasRenderedTextInBlock)
                        r->sameLineNext = true;
                    return 0;
                }
            }

            r->renderText(text, size);
            return 0;
        }
    };

    void MarkdownText(const char *text) { MarkdownText(text, nullptr); }

    // Static pointer to the active text effects overlay (set by the Vault preview before rendering)
    static TextEffectsOverlay *s_activeEffectsOverlay = nullptr;
    static ImVec2 s_activeContentOrigin = ImVec2(0,0);
    static float s_activeDocScrollY = 0.0f;

    void MarkdownTextSetEffectsOverlay(TextEffectsOverlay *overlay, ImVec2 contentOrigin, float scrollY)
    {
        s_activeEffectsOverlay = overlay;
        s_activeContentOrigin = contentOrigin;
        s_activeDocScrollY = scrollY;
    }

    void MarkdownText(const char *text, void *context)
    {
        if (!text || !text[0])
            return;
        ImGuiWindow *w = GetCurrentWindow();
        if (w->SkipItems)
            return;

        MD4CRenderer renderer;
        renderer.ctx = context;
        renderer.source_base = (const MD_CHAR*)text;
        renderer.effectsOverlay = s_activeEffectsOverlay;
        renderer.contentOrigin = s_activeContentOrigin;
        renderer.docScrollY = s_activeDocScrollY;
        MD_PARSER parser = {0};
        parser.enter_block = MD4CRenderer::enter_block; 
        parser.leave_block = MD4CRenderer::leave_block;
        parser.enter_span = MD4CRenderer::enter_span;
        parser.leave_span = MD4CRenderer::leave_span;
        parser.text = MD4CRenderer::textcb;

        // Set desired parser flags via the parser struct (md_parse takes only 4 args)
        parser.flags = MD_FLAG_TABLES | MD_FLAG_NOHTMLBLOCKS;
        md_parse(text, strlen(text), &parser, &renderer);
    }

    void MarkdownText(const std::string &text) { MarkdownText(text.c_str()); }
    void MarkdownText(const std::string &text, void *context) { MarkdownText(text.c_str(), context); }

}