#pragma once
#include <Editors/Markdown/MarkdownDocument.hpp>
#include <Editors/Markdown/PreviewEffectSystem.hpp>
#include <imgui.h>
#include <glm/glm.hpp>
#include <vector>
#include <string>
#include <memory>

namespace Markdown {

// ────────────────────────────────────────────────────────────────────
// Overlay Widget (interactive elements rendered via ImGui overlay)
// ────────────────────────────────────────────────────────────────────

struct OverlayWidget {
    enum Type { 
        Checkbox, 
        Link, 
        Image,
        ModelViewer, 
        LuaCanvas, 
        WorldMap 
    };
    
    Type type = Link;
    glm::vec2 docPos;        // position in document space
    glm::vec2 size;          // widget size
    size_t sourceOffset = 0;
    std::string data;        // URL, script path, etc.
    std::string altText;     // for images
    bool checked = false;    // for checkboxes
};

// ────────────────────────────────────────────────────────────────────
// LayoutEngine - converts AST to positioned glyphs
// ────────────────────────────────────────────────────────────────────

class LayoutEngine {
public:
    LayoutEngine();
    ~LayoutEngine() = default;
    
    /// Set the effect system for resolving effect names
    void setEffectSystem(PreviewEffectSystem* system) { m_effectSystem = system; }
    
    /// Perform layout on a document
    void layout(const MarkdownDocument& doc, float wrapWidth,
                std::vector<LayoutGlyph>& outGlyphs,
                std::vector<OverlayWidget>& outWidgets);
    
    /// Get the total content height after layout
    float getContentHeight() const { return m_contentHeight; }

private:
    // Layout state
    void resetState(float wrapWidth);
    
    // Block layouters
    void layoutBlock(const Block& block, int depth);
    void layoutHeading(const Block& block);
    void layoutParagraph(const Block& block);
    void layoutList(const Block& block, int depth);
    void layoutListItem(const Block& block, int listIndex, bool ordered, int depth);
    void layoutCodeBlock(const Block& block);
    void layoutQuote(const Block& block, int depth);
    void layoutHorizontalRule();
    void layoutTable(const Block& block);
    void layoutTableRow(const Block& block, const std::vector<int>& colWidths, bool isHeader);
    
    // Span layouters
    void layoutSpans(const std::vector<std::unique_ptr<Span>>& spans);
    void layoutSpan(const Span& span);
    void layoutTextSpan(const Span& span);
    void layoutLinkSpan(const Span& span);
    void layoutImageSpan(const Span& span);
    void layoutCodeSpan(const Span& span);
    void layoutEffectSpan(const Span& span);
    
    // Text layout helpers
    void layoutText(const std::string& text, size_t sourceOffset);
    void emitGlyph(uint32_t codepoint, size_t sourceOffset);
    void lineBreak();
    void addVerticalSpace(float space);
    
    /// Get the current composite effect (merges stacked effects)
    EffectDef* getCompositeEffect();
    
    // UTF-8 helpers
    static int decodeUTF8(const char* p, uint32_t* outCodepoint);
    
    // State
    float m_curX = 0;
    float m_curY = 0;
    float m_indentX = 0;
    float m_wrapWidth = 0;
    float m_contentHeight = 0;
    
    // Font state
    ImFont* m_font = nullptr;
    ImFont* m_boldFont = nullptr;
    ImFont* m_italicFont = nullptr;
    ImFont* m_monoFont = nullptr;
    float m_scale = 1.0f;
    float m_lineHeight = 0;
    glm::vec4 m_color = {1, 1, 1, 1};
    
    // Style stacks
    std::vector<ImFont*> m_fontStack;
    std::vector<glm::vec4> m_colorStack;
    
    // Effect stack
    EffectStack m_effectStack;
    PreviewEffectSystem* m_effectSystem = nullptr;
    
    // Inline custom effect defs (for per-tag parameter overrides)
    std::vector<std::unique_ptr<EffectDef>> m_inlineEffects;
    
    // Output
    std::vector<LayoutGlyph>* m_outGlyphs = nullptr;
    std::vector<OverlayWidget>* m_outWidgets = nullptr;
    
    // Constants
    static constexpr float PARAGRAPH_SPACING = 12.0f;
    static constexpr float HEADING_SPACING = 16.0f;
    static constexpr float LIST_INDENT = 24.0f;
    static constexpr float QUOTE_INDENT = 20.0f;
    static constexpr float CODE_PADDING = 8.0f;
};

} // namespace Markdown
