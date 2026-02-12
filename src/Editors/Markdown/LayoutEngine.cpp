#include <Editors/Markdown/LayoutEngine.hpp>
#include <Editors/Markdown/Effect.hpp>
#include <Fonts.hpp>
#include <imgui.h>
#include <plog/Log.h>
#include <cmath>
#include <cstdio>
#include <algorithm>
#include <string>

namespace Markdown {

LayoutEngine::LayoutEngine() = default;

void LayoutEngine::resetState(float wrapWidth) {
    m_curX = 0;
    m_curY = 0;
    m_indentX = 0;
    m_wrapWidth = wrapWidth;
    m_contentHeight = 0;
    
    // Get fonts from global font system
    m_font = GetFont(FontStyle::Regular);
    m_boldFont = GetFont(FontStyle::Bold);
    m_italicFont = GetFont(FontStyle::Italic);
    m_monoFont = GetFont(FontStyle::Regular);  // No dedicated mono, use regular
    
    if (!m_font) {
        m_font = ImGui::GetFont();
    }
    if (!m_boldFont) m_boldFont = m_font;
    if (!m_italicFont) m_italicFont = m_font;
    if (!m_monoFont) m_monoFont = m_font;
    
    m_scale = 1.0f;
    m_lineHeight = m_font ? m_font->FontSize : 16.0f;
    m_color = {1, 1, 1, 1};
    
    m_fontStack.clear();
    m_fontStack.push_back(m_font);
    
    m_colorStack.clear();
    m_colorStack.push_back(m_color);
    
    m_effectStack.clear();
    m_inlineEffects.clear();
    m_clonedEffects.clear();
}

void LayoutEngine::layout(const MarkdownDocument& doc, float wrapWidth,
                          std::vector<LayoutGlyph>& outGlyphs,
                          std::vector<OverlayWidget>& outWidgets) {
    outGlyphs.clear();
    outWidgets.clear();
    
    m_outGlyphs = &outGlyphs;
    m_outWidgets = &outWidgets;
    
    resetState(wrapWidth);
    
    // Layout all children of the root document block
    const Block& root = doc.getRoot();
    for (const auto& child : root.children) {
        layoutBlock(*child, 0);
    }
    
    m_contentHeight = m_curY;
    
    m_outGlyphs = nullptr;
    m_outWidgets = nullptr;
}

void LayoutEngine::layoutBlock(const Block& block, int depth) {
    switch (block.type) {
        case BlockType::Document:
            for (const auto& child : block.children) {
                layoutBlock(*child, depth);
            }
            break;
            
        case BlockType::Paragraph:
            layoutParagraph(block);
            break;
            
        case BlockType::Heading:
            layoutHeading(block);
            break;
            
        case BlockType::CodeBlock:
            layoutCodeBlock(block);
            break;
            
        case BlockType::Quote:
            layoutQuote(block, depth);
            break;
            
        case BlockType::List:
            layoutList(block, depth);
            break;
            
        case BlockType::HorizontalRule:
            layoutHorizontalRule();
            break;
            
        case BlockType::Table:
            layoutTable(block);
            break;
            
        case BlockType::HTML:
            // Skip raw HTML blocks for now
            break;
            
        default:
            // For unknown block types, try to layout children
            for (const auto& child : block.children) {
                layoutBlock(*child, depth + 1);
            }
            break;
    }
}

void LayoutEngine::layoutHeading(const Block& block) {
    // Push larger font scale based on heading level
    float scales[] = {2.0f, 1.75f, 1.5f, 1.25f, 1.1f, 1.0f};
    int level = std::clamp(block.headingLevel, 1, 6) - 1;
    float oldScale = m_scale;
    m_scale = scales[level];
    
    // Use bold font for headings
    m_fontStack.push_back(m_boldFont);
    
    addVerticalSpace(HEADING_SPACING);
    
    // Layout inline content
    layoutSpans(block.inlineContent);
    
    lineBreak();
    addVerticalSpace(HEADING_SPACING * 0.5f);
    
    m_fontStack.pop_back();
    m_scale = oldScale;
}

void LayoutEngine::layoutParagraph(const Block& block) {
    m_curX = m_indentX;
    
    layoutSpans(block.inlineContent);
    
    lineBreak();
    addVerticalSpace(PARAGRAPH_SPACING);
}

void LayoutEngine::layoutList(const Block& block, int depth) {
    int index = block.listStart;
    
    for (const auto& child : block.children) {
        if (child->type == BlockType::ListItem) {
            layoutListItem(*child, index, block.isOrderedList, depth);
            index++;
        }
    }
    
    addVerticalSpace(PARAGRAPH_SPACING * 0.5f);
}

void LayoutEngine::layoutListItem(const Block& block, int listIndex, bool ordered, int depth) {
    float oldIndent = m_indentX;
    m_indentX += LIST_INDENT;
    m_curX = m_indentX;
    
    // Emit bullet or number
    std::string marker;
    if (ordered) {
        marker = std::to_string(listIndex) + ". ";
    } else {
        // Use different bullets for different depths
        const char* bullets[] = {"• ", "◦ ", "▪ "};
        marker = bullets[depth % 3];
    }
    
    // Position marker to the left of the indent
    float markerWidth = 0;
    if (m_font) {
        ImVec2 sz = m_font->CalcTextSizeA(m_font->FontSize * m_scale, FLT_MAX, 0, marker.c_str());
        markerWidth = sz.x;
    }
    
    float savedX = m_curX;
    m_curX = m_indentX - markerWidth - 4;
    layoutText(marker, block.sourceOffset);
    m_curX = savedX;
    
    // Layout content - check both inline content and children
    // md4c may put text directly in ListItem's inline content (tight lists)
    // or wrap it in a Paragraph child (loose lists)
    if (!block.inlineContent.empty()) {
        layoutSpans(block.inlineContent);
        lineBreak();
    }
    
    for (const auto& child : block.children) {
        if (child->type == BlockType::Paragraph) {
            // Inline the first paragraph
            layoutSpans(child->inlineContent);
            lineBreak();
        } else {
            layoutBlock(*child, depth + 1);
        }
    }
    
    m_indentX = oldIndent;
}

void LayoutEngine::layoutCodeBlock(const Block& block) {
    addVerticalSpace(CODE_PADDING);
    
    float oldIndent = m_indentX;
    m_indentX += CODE_PADDING;
    m_curX = m_indentX;
    
    // Use monospace font
    m_fontStack.push_back(m_monoFont);
    
    // Change color for code
    m_colorStack.push_back({0.9f, 0.9f, 0.8f, 1.0f});
    
    // TODO: Emit background quad for code block
    
    // Layout code content line by line
    const std::string& code = block.codeContent;
    size_t offset = block.sourceOffset;
    
    for (size_t i = 0; i < code.size(); ) {
        size_t lineEnd = code.find('\n', i);
        if (lineEnd == std::string::npos) lineEnd = code.size();
        
        std::string line = code.substr(i, lineEnd - i);
        layoutText(line, offset + i);
        lineBreak();
        
        i = lineEnd + 1;
    }
    
    m_colorStack.pop_back();
    m_fontStack.pop_back();
    
    m_indentX = oldIndent;
    addVerticalSpace(CODE_PADDING);
}

void LayoutEngine::layoutQuote(const Block& block, int depth) {
    float oldIndent = m_indentX;
    m_indentX += QUOTE_INDENT;
    
    // Tint color for quotes
    glm::vec4 quoteColor = m_color * 0.7f;
    quoteColor.a = 1.0f;
    m_colorStack.push_back(quoteColor);
    
    // TODO: Emit left border line for quote
    
    for (const auto& child : block.children) {
        layoutBlock(*child, depth + 1);
    }
    
    m_colorStack.pop_back();
    m_indentX = oldIndent;
}

void LayoutEngine::layoutHorizontalRule() {
    addVerticalSpace(PARAGRAPH_SPACING);
    
    // TODO: Emit horizontal line glyph/quad
    
    addVerticalSpace(PARAGRAPH_SPACING);
}

void LayoutEngine::layoutTable(const Block& block) {
    // Simple table layout - calculate column widths first
    std::vector<int> colWidths;
    
    // Find max width per column (simplified)
    for (const auto& row : block.children) {
        if (row->type != BlockType::TableRow) continue;
        
        int col = 0;
        for (const auto& cell : row->children) {
            if (cell->type != BlockType::TableCell) continue;
            
            // Estimate width from content
            float width = 100.0f;  // Default width
            
            if (col >= static_cast<int>(colWidths.size())) {
                colWidths.push_back(static_cast<int>(width));
            } else {
                colWidths[col] = std::max(colWidths[col], static_cast<int>(width));
            }
            col++;
        }
    }
    
    // Layout rows
    for (const auto& row : block.children) {
        if (row->type == BlockType::TableRow) {
            layoutTableRow(*row, colWidths, row->isHeaderRow);
        }
    }
    
    addVerticalSpace(PARAGRAPH_SPACING);
}

void LayoutEngine::layoutTableRow(const Block& block, const std::vector<int>& colWidths, bool isHeader) {
    float startX = m_curX;
    
    if (isHeader) {
        m_fontStack.push_back(m_boldFont);
    }
    
    int col = 0;
    for (const auto& cell : block.children) {
        if (cell->type != BlockType::TableCell) continue;
        
        float cellWidth = col < static_cast<int>(colWidths.size()) ? colWidths[col] : 100.0f;
        
        // Layout cell content
        float oldWrap = m_wrapWidth;
        m_wrapWidth = m_curX + cellWidth - 8;
        
        layoutSpans(cell->inlineContent);
        
        m_wrapWidth = oldWrap;
        m_curX = startX + (col + 1) * cellWidth;
        col++;
    }
    
    if (isHeader) {
        m_fontStack.pop_back();
    }
    
    lineBreak();
}

void LayoutEngine::layoutSpans(const std::vector<std::unique_ptr<Span>>& spans) {
    for (const auto& span : spans) {
        layoutSpan(*span);
    }
}

void LayoutEngine::layoutSpan(const Span& span) {
    switch (span.type) {
        case SpanType::Text:
            layoutTextSpan(span);
            break;
            
        case SpanType::Emphasis:
            m_fontStack.push_back(m_italicFont);
            layoutSpans(span.children);
            m_fontStack.pop_back();
            break;
            
        case SpanType::Strong:
            m_fontStack.push_back(m_boldFont);
            layoutSpans(span.children);
            m_fontStack.pop_back();
            break;
            
        case SpanType::Code:
            layoutCodeSpan(span);
            break;
            
        case SpanType::Link:
            layoutLinkSpan(span);
            break;
            
        case SpanType::Image:
            layoutImageSpan(span);
            break;
            
        case SpanType::Strikethrough:
            // TODO: Add strikethrough style flag
            layoutSpans(span.children);
            break;
            
        case SpanType::Effect:
            layoutEffectSpan(span);
            break;
    }
}

void LayoutEngine::layoutTextSpan(const Span& span) {
    layoutText(span.text, span.sourceOffset);
}

void LayoutEngine::layoutLinkSpan(const Span& span) {
    // Save start position for overlay widget
    float startX = m_curX;
    float startY = m_curY;
    
    // Links are blue
    m_colorStack.push_back({0.4f, 0.6f, 1.0f, 1.0f});
    
    layoutSpans(span.children);
    
    m_colorStack.pop_back();
    
    // Record overlay widget for click handling
    if (m_outWidgets) {
        OverlayWidget widget;
        widget.type = OverlayWidget::Link;
        widget.docPos = {startX, startY};
        widget.size = {m_curX - startX, m_lineHeight * m_scale};
        widget.data = span.url;
        widget.sourceOffset = span.sourceOffset;
        m_outWidgets->push_back(widget);
    }
}

void LayoutEngine::layoutImageSpan(const Span& span) {
    // Record image as overlay widget
    if (m_outWidgets) {
        OverlayWidget widget;
        widget.type = OverlayWidget::Image;
        widget.docPos = {m_curX, m_curY};
        widget.size = {200, 150};  // Default size, will be adjusted when image loads
        widget.data = span.url;
        widget.altText = span.title;
        widget.sourceOffset = span.sourceOffset;
        m_outWidgets->push_back(widget);
        
        // Reserve space
        lineBreak();
        m_curY += 150;  // Placeholder height
    }
}

void LayoutEngine::layoutCodeSpan(const Span& span) {
    m_fontStack.push_back(m_monoFont);
    m_colorStack.push_back({0.9f, 0.8f, 0.7f, 1.0f});
    
    // Layout the text content
    for (const auto& child : span.children) {
        if (child->type == SpanType::Text) {
            layoutText(child->text, child->sourceOffset);
        }
    }
    
    m_colorStack.pop_back();
    m_fontStack.pop_back();
}

void LayoutEngine::layoutEffectSpan(const Span& span) {
    // Look up base effect definition
    EffectDef* effect = nullptr;
    if (m_effectSystem) {
        effect = m_effectSystem->getEffect(span.effectName);
    }
    
    // If there are per-tag attribute overrides and we have an Effect object, apply them
    if (effect && effect->effect && !span.effectParams.empty()) {
        // Clone the Effect so we can modify params for this span only
        auto cloned = effect->effect->clone();
        
        for (const auto& [key, val] : span.effectParams) {
            if (key == "color" && val.size() >= 7 && val[0] == '#') {
                unsigned int hex = 0;
                if (std::sscanf(val.c_str() + 1, "%x", &hex) == 1) {
                    if (val.size() == 7) {
                        cloned->color1 = {
                            ((hex >> 16) & 0xFF) / 255.0f,
                            ((hex >> 8) & 0xFF) / 255.0f,
                            (hex & 0xFF) / 255.0f,
                            1.0f
                        };
                    } else if (val.size() == 9) {
                        cloned->color1 = {
                            ((hex >> 24) & 0xFF) / 255.0f,
                            ((hex >> 16) & 0xFF) / 255.0f,
                            ((hex >> 8) & 0xFF) / 255.0f,
                            (hex & 0xFF) / 255.0f
                        };
                    }
                }
            } else if (key == "intensity") {
                try { cloned->intensity = std::stof(val); } catch (...) {}
            } else if (key == "speed") {
                try { cloned->speed = std::stof(val); } catch (...) {}
            } else if (key == "scale") {
                try { cloned->scale = std::stof(val); } catch (...) {}
            } else if (key == "amplitude") {
                try { cloned->amplitude = std::stof(val); } catch (...) {}
            } else if (key == "frequency") {
                try { cloned->frequency = std::stof(val); } catch (...) {}
            }
        }
        
        auto custom = std::make_unique<EffectDef>(*effect);
        custom->effect = cloned.get();
        m_clonedEffects.push_back(std::move(cloned));
        
        effect = custom.get();
        m_inlineEffects.push_back(std::move(custom));
    }
    
    // Push effect onto stack
    if (effect) {
        m_effectStack.push(effect, span.sourceOffset);
    }
    
    // Layout children
    layoutSpans(span.children);
    
    // Pop effect
    if (effect) {
        m_effectStack.pop(span.sourceOffset + span.sourceLength);
    }
}

void LayoutEngine::layoutText(const std::string& text, size_t sourceOffset) {
    if (!m_font || text.empty()) return;
    
    ImFont* font = m_fontStack.empty() ? m_font : m_fontStack.back();
    glm::vec4 color = m_colorStack.empty() ? m_color : m_colorStack.back();
    float fontSize = font->FontSize * m_scale;
    
    const char* p = text.c_str();
    const char* end = p + text.size();
    
    while (p < end) {
        // Find word wrap break point
        float availWidth = m_wrapWidth - m_curX;
        if (availWidth < fontSize) {
            lineBreak();
            availWidth = m_wrapWidth - m_indentX;
        }
        
        const char* wrapPos = font->CalcWordWrapPositionA(m_scale, p, end, availWidth);
        if (wrapPos == p) {
            // Can't fit even one character, force at least one
            uint32_t cp;
            wrapPos = p + decodeUTF8(p, &cp);
        }
        
        // Emit glyphs from p to wrapPos
        const char* c = p;
        while (c < wrapPos) {
            uint32_t codepoint;
            int bytes = decodeUTF8(c, &codepoint);
            
            const ImFontGlyph* g = font->FindGlyph(codepoint);
            if (g && codepoint != '\n' && codepoint != '\r') {
                LayoutGlyph lg;
                lg.pos = {m_curX + g->X0 * m_scale, m_curY + g->Y0 * m_scale, 0.0f};
                lg.size = {(g->X1 - g->X0) * m_scale, (g->Y1 - g->Y0) * m_scale};
                lg.uvMin = {g->U0, g->V0};
                lg.uvMax = {g->U1, g->V1};
                lg.color = color;
                lg.effect = getCompositeEffect();
                lg.sourceOffset = sourceOffset + (c - text.c_str());
                
                if (m_outGlyphs) {
                    m_outGlyphs->push_back(lg);
                }
                
                m_curX += g->AdvanceX * m_scale;
            } else if (codepoint == '\n') {
                lineBreak();
            }
            
            c += bytes;
        }
        
        // Line break if we wrapped
        if (wrapPos < end && *wrapPos != '\n') {
            lineBreak();
        }
        
        // Skip whitespace at wrap point
        p = wrapPos;
        while (p < end && (*p == ' ' || *p == '\t')) ++p;
    }
}

void LayoutEngine::emitGlyph(uint32_t codepoint, size_t sourceOffset) {
    if (!m_font) return;
    
    ImFont* font = m_fontStack.empty() ? m_font : m_fontStack.back();
    glm::vec4 color = m_colorStack.empty() ? m_color : m_colorStack.back();
    
    const ImFontGlyph* g = font->FindGlyph(codepoint);
    if (!g) return;
    
    LayoutGlyph lg;
    lg.pos = {m_curX + g->X0 * m_scale, m_curY + g->Y0 * m_scale, 0.0f};
    lg.size = {(g->X1 - g->X0) * m_scale, (g->Y1 - g->Y0) * m_scale};
    lg.uvMin = {g->U0, g->V0};
    lg.uvMax = {g->U1, g->V1};
    lg.color = color;
    lg.effect = getCompositeEffect();
    lg.sourceOffset = sourceOffset;
    
    if (m_outGlyphs) {
        m_outGlyphs->push_back(lg);
    }
    
    m_curX += g->AdvanceX * m_scale;
}

void LayoutEngine::lineBreak() {
    m_curX = m_indentX;
    m_curY += m_lineHeight * m_scale;
}

void LayoutEngine::addVerticalSpace(float space) {
    m_curY += space;
}

EffectDef* LayoutEngine::getCompositeEffect() {
    if (m_effectStack.empty()) return nullptr;
    if (m_effectStack.size() == 1) return m_effectStack.currentEffect();
    
    // Multiple effects stacked — compose a single EffectDef with:
    // - effectStack: all Effect* instances for shader composition
    // - stackSignature: cache key for composite shader
    // - particle/bloom merged from the full stack
    const auto& stack = m_effectStack.getStack();
    EffectDef* innermost = m_effectStack.currentEffect();
    
    // Build effectStack (outer→inner) and stackSignature
    std::vector<Effect*> effectPtrs;
    std::string signature;
    bool needsBloomMerge = false;
    bool needsParticleMerge = false;
    Effect* bloomFx = nullptr;
    
    for (const auto& ae : stack) {
        if (!ae.def || !ae.def->effect) continue;
        effectPtrs.push_back(ae.def->effect);
        
        if (!signature.empty()) signature += "+";
        signature += ae.def->name;
        
        // Check bloom from any effect in stack
        if (ae.def->effect->getCapabilities().contributesToBloom) {
            if (!innermost->effect || !innermost->effect->getCapabilities().contributesToBloom) {
                needsBloomMerge = true;
                bloomFx = ae.def->effect;
            }
        }
        
        // Check particles from outer effects
        if (ae.def != innermost && ae.def->hasParticles && !innermost->hasParticles) {
            needsParticleMerge = true;
        }
    }
    
    // Always create a composite EffectDef for stacked effects
    auto composite = std::make_unique<EffectDef>(*innermost);
    composite->effectStack = std::move(effectPtrs);
    composite->stackSignature = std::move(signature);
    
    if (needsBloomMerge && bloomFx) {
        composite->bloomEffect = bloomFx;
    }
    
    if (needsParticleMerge) {
        for (const auto& ae : stack) {
            if (ae.def && ae.def->hasParticles) {
                composite->hasParticles = true;
                composite->emission = ae.def->emission;
                composite->effectKernel = ae.def->effectKernel;
                composite->effectProgram = ae.def->effectProgram;
                composite->effect = ae.def->effect;
                break;
            }
        }
    }
    
    EffectDef* ptr = composite.get();
    m_inlineEffects.push_back(std::move(composite));
    return ptr;
}

int LayoutEngine::decodeUTF8(const char* p, uint32_t* outCodepoint) {
    unsigned char c = static_cast<unsigned char>(*p);
    
    if (c < 0x80) {
        *outCodepoint = c;
        return 1;
    }
    
    if ((c & 0xE0) == 0xC0) {
        *outCodepoint = ((c & 0x1F) << 6) | (p[1] & 0x3F);
        return 2;
    }
    
    if ((c & 0xF0) == 0xE0) {
        *outCodepoint = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F);
        return 3;
    }
    
    if ((c & 0xF8) == 0xF0) {
        *outCodepoint = ((c & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F);
        return 4;
    }
    
    // Invalid UTF-8, return replacement character
    *outCodepoint = 0xFFFD;
    return 1;
}

} // namespace Markdown
