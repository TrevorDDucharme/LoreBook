#include <Editors/Markdown/MarkdownDocument.hpp>
#include <stack>
#include <functional>
#include <cstring>
#include <plog/Log.h>

namespace Markdown {

// ────────────────────────────────────────────────────────────────────
// Parse State for md4c callbacks
// ────────────────────────────────────────────────────────────────────

struct MarkdownDocument::ParseState {
    MarkdownDocument* doc = nullptr;
    const char* sourceBase = nullptr;
    size_t sourceLen = 0;
    
    // Stack of current block context
    std::stack<Block*> blockStack;
    
    // Stack of current span context
    std::stack<Span*> spanStack;
    
    // Current text accumulator
    std::string textAccum;
    size_t textStartOffset = 0;
    
    // Track if we're inside an effect tag (HTML span)
    bool inEffectTag = false;
    std::string currentEffectName;
    std::vector<std::pair<std::string, std::string>> currentEffectParams;
};

// ────────────────────────────────────────────────────────────────────
// Helper: compute simple hash
// ────────────────────────────────────────────────────────────────────

static size_t computeHash(const std::string& s) {
    size_t hash = 0;
    for (char c : s) {
        hash = hash * 31 + static_cast<unsigned char>(c);
    }
    return hash;
}

// ────────────────────────────────────────────────────────────────────
// Helper: parse effect tag attributes
// ────────────────────────────────────────────────────────────────────

static void parseEffectTag(const std::string& tag, std::string& outName,
                           std::vector<std::pair<std::string, std::string>>& outParams) {
    outName.clear();
    outParams.clear();
    
    // Simple parser for <tagname attr="value" attr2="value2">
    size_t i = 0;
    
    // Skip leading '<'
    while (i < tag.size() && (tag[i] == '<' || std::isspace(tag[i]))) ++i;
    
    // Read tag name
    while (i < tag.size() && !std::isspace(tag[i]) && tag[i] != '>' && tag[i] != '/') {
        outName += tag[i++];
    }
    
    // Parse attributes
    while (i < tag.size() && tag[i] != '>') {
        // Skip whitespace
        while (i < tag.size() && std::isspace(tag[i])) ++i;
        if (i >= tag.size() || tag[i] == '>' || tag[i] == '/') break;
        
        // Read attribute name
        std::string attrName;
        while (i < tag.size() && tag[i] != '=' && !std::isspace(tag[i]) && tag[i] != '>') {
            attrName += tag[i++];
        }
        
        // Skip to '='
        while (i < tag.size() && std::isspace(tag[i])) ++i;
        
        std::string attrValue;
        if (i < tag.size() && tag[i] == '=') {
            ++i;
            // Skip whitespace
            while (i < tag.size() && std::isspace(tag[i])) ++i;
            
            // Read value (quoted or unquoted)
            char quote = 0;
            if (i < tag.size() && (tag[i] == '"' || tag[i] == '\'')) {
                quote = tag[i++];
            }
            
            while (i < tag.size()) {
                if (quote && tag[i] == quote) {
                    ++i;
                    break;
                } else if (!quote && (std::isspace(tag[i]) || tag[i] == '>')) {
                    break;
                }
                attrValue += tag[i++];
            }
        }
        
        if (!attrName.empty()) {
            outParams.emplace_back(attrName, attrValue);
        }
    }
}

// ────────────────────────────────────────────────────────────────────
// md4c callbacks
// ────────────────────────────────────────────────────────────────────

int MarkdownDocument::enterBlock(MD_BLOCKTYPE type, void* detail, void* userdata) {
    auto* state = static_cast<ParseState*>(userdata);
    
    auto block = std::make_unique<Block>();
    Block* blockPtr = block.get();
    
    switch (type) {
        case MD_BLOCK_DOC:
            block->type = BlockType::Document;
            break;
        case MD_BLOCK_P:
            block->type = BlockType::Paragraph;
            break;
        case MD_BLOCK_H: {
            auto* h = static_cast<MD_BLOCK_H_DETAIL*>(detail);
            block->type = BlockType::Heading;
            block->headingLevel = h->level;
            break;
        }
        case MD_BLOCK_CODE: {
            auto* c = static_cast<MD_BLOCK_CODE_DETAIL*>(detail);
            block->type = BlockType::CodeBlock;
            if (c->lang.text && c->lang.size > 0) {
                block->codeLanguage.assign(c->lang.text, c->lang.size);
            }
            break;
        }
        case MD_BLOCK_QUOTE:
            block->type = BlockType::Quote;
            break;
        case MD_BLOCK_UL:
            block->type = BlockType::List;
            block->isOrderedList = false;
            break;
        case MD_BLOCK_OL: {
            auto* ol = static_cast<MD_BLOCK_OL_DETAIL*>(detail);
            block->type = BlockType::List;
            block->isOrderedList = true;
            block->listStart = ol->start;
            break;
        }
        case MD_BLOCK_LI:
            block->type = BlockType::ListItem;
            break;
        case MD_BLOCK_HR:
            block->type = BlockType::HorizontalRule;
            break;
        case MD_BLOCK_TABLE:
            block->type = BlockType::Table;
            break;
        case MD_BLOCK_THEAD:
        case MD_BLOCK_TBODY:
            // These are implicit containers, skip
            return 0;
        case MD_BLOCK_TR: {
            block->type = BlockType::TableRow;
            break;
        }
        case MD_BLOCK_TH:
        case MD_BLOCK_TD: {
            auto* td = static_cast<MD_BLOCK_TD_DETAIL*>(detail);
            block->type = BlockType::TableCell;
            block->tableColumnAlign = static_cast<int>(td->align);
            break;
        }
        case MD_BLOCK_HTML:
            block->type = BlockType::HTML;
            break;
        default:
            break;
    }
    
    // Add to parent
    if (!state->blockStack.empty()) {
        Block* parent = state->blockStack.top();
        parent->children.push_back(std::move(block));
    } else {
        // Root level
        state->doc->m_root.children.push_back(std::move(block));
        state->doc->m_blocks.push_back(blockPtr);
    }
    
    state->blockStack.push(blockPtr);
    return 0;
}

int MarkdownDocument::leaveBlock(MD_BLOCKTYPE type, void* detail, void* userdata) {
    auto* state = static_cast<ParseState*>(userdata);
    
    // Skip implicit containers
    if (type == MD_BLOCK_THEAD || type == MD_BLOCK_TBODY) {
        return 0;
    }
    
    if (!state->blockStack.empty()) {
        state->blockStack.pop();
    }
    return 0;
}

int MarkdownDocument::enterSpan(MD_SPANTYPE type, void* detail, void* userdata) {
    auto* state = static_cast<ParseState*>(userdata);
    
    auto span = std::make_unique<Span>();
    Span* spanPtr = span.get();
    
    switch (type) {
        case MD_SPAN_EM:
            span->type = SpanType::Emphasis;
            break;
        case MD_SPAN_STRONG:
            span->type = SpanType::Strong;
            break;
        case MD_SPAN_CODE:
            span->type = SpanType::Code;
            break;
        case MD_SPAN_A: {
            auto* a = static_cast<MD_SPAN_A_DETAIL*>(detail);
            span->type = SpanType::Link;
            if (a->href.text && a->href.size > 0) {
                span->url.assign(a->href.text, a->href.size);
            }
            if (a->title.text && a->title.size > 0) {
                span->title.assign(a->title.text, a->title.size);
            }
            break;
        }
        case MD_SPAN_IMG: {
            auto* img = static_cast<MD_SPAN_IMG_DETAIL*>(detail);
            span->type = SpanType::Image;
            if (img->src.text && img->src.size > 0) {
                span->url.assign(img->src.text, img->src.size);
            }
            if (img->title.text && img->title.size > 0) {
                span->title.assign(img->title.text, img->title.size);
            }
            break;
        }
        case MD_SPAN_DEL:
            span->type = SpanType::Strikethrough;
            break;
        default:
            span->type = SpanType::Text;
            break;
    }
    
    // Add to parent span or block
    if (!state->spanStack.empty()) {
        state->spanStack.top()->children.push_back(std::move(span));
    } else if (!state->blockStack.empty()) {
        state->blockStack.top()->inlineContent.push_back(std::move(span));
    }
    
    state->spanStack.push(spanPtr);
    return 0;
}

int MarkdownDocument::leaveSpan(MD_SPANTYPE type, void* detail, void* userdata) {
    auto* state = static_cast<ParseState*>(userdata);
    
    if (!state->spanStack.empty()) {
        state->spanStack.pop();
    }
    return 0;
}

int MarkdownDocument::textCallback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata) {
    auto* state = static_cast<ParseState*>(userdata);
    
    if (type == MD_TEXT_HTML) {
        // Check for effect tags
        std::string html(text, size);
        
        // Check for opening effect tag
        if (html.size() > 1 && html[0] == '<' && html[1] != '/') {
            std::string tagName;
            std::vector<std::pair<std::string, std::string>> params;
            parseEffectTag(html, tagName, params);
            
            // Check if it's a known effect tag
            // Common effect tags: fire, rainbow, shake, wave, glow, etc.
            static const std::vector<std::string> effectTags = {
                "fire", "rainbow", "shake", "wave", "glow", "dissolve",
                "sparkle", "snow", "blood", "neon", "glitch", "bounce",
                "pulse", "typewriter", "shadow", "outline", "gradient",
                "electric", "ice", "magic", "ghost", "underwater", "golden",
                "toxic", "crystal", "storm", "ethereal", "lava", "frost",
                "void", "holy", "matrix", "disco",
                "effect" // generic effect tag
            };
            
            bool isEffect = false;
            for (const auto& et : effectTags) {
                if (tagName == et) {
                    isEffect = true;
                    break;
                }
            }
            
            if (isEffect) {
                auto span = std::make_unique<Span>();
                span->type = SpanType::Effect;
                span->effectName = tagName;
                span->effectParams = params;
                
                Span* spanPtr = span.get();
                
                if (!state->spanStack.empty()) {
                    state->spanStack.top()->children.push_back(std::move(span));
                } else if (!state->blockStack.empty()) {
                    state->blockStack.top()->inlineContent.push_back(std::move(span));
                }
                
                state->spanStack.push(spanPtr);
                state->inEffectTag = true;
                state->currentEffectName = tagName;
                return 0;
            }
        }
        // Check for closing effect tag
        else if (html.size() > 2 && html[0] == '<' && html[1] == '/') {
            std::string closingTag;
            for (size_t i = 2; i < html.size() && html[i] != '>'; ++i) {
                if (!std::isspace(html[i])) {
                    closingTag += html[i];
                }
            }
            
            if (state->inEffectTag && closingTag == state->currentEffectName) {
                if (!state->spanStack.empty()) {
                    state->spanStack.pop();
                }
                state->inEffectTag = false;
                state->currentEffectName.clear();
                return 0;
            }
        }
    }
    
    // Regular text
    auto span = std::make_unique<Span>();
    span->type = SpanType::Text;
    span->text.assign(text, size);
    span->sourceOffset = text - state->sourceBase;
    span->sourceLength = size;
    
    if (!state->spanStack.empty()) {
        state->spanStack.top()->children.push_back(std::move(span));
    } else if (!state->blockStack.empty()) {
        Block* block = state->blockStack.top();
        if (block->type == BlockType::CodeBlock) {
            block->codeContent.append(text, size);
        } else {
            block->inlineContent.push_back(std::move(span));
        }
    }
    
    return 0;
}

// ────────────────────────────────────────────────────────────────────
// Public API
// ────────────────────────────────────────────────────────────────────

void MarkdownDocument::parseString(const std::string& markdown) {
    size_t newHash = computeHash(markdown);
    if (newHash == m_sourceHash && !m_dirty) {
        return;  // No change
    }
    
    clear();
    m_sourceHash = newHash;
    m_dirty = false;
    
    m_root.type = BlockType::Document;
    
    ParseState state;
    state.doc = this;
    state.sourceBase = markdown.c_str();
    state.sourceLen = markdown.size();
    
    MD_PARSER parser = {};
    parser.abi_version = 0;
    parser.flags = MD_FLAG_STRIKETHROUGH | MD_FLAG_TABLES | MD_FLAG_PERMISSIVEURLAUTOLINKS;
    parser.enter_block = enterBlock;
    parser.leave_block = leaveBlock;
    parser.enter_span = enterSpan;
    parser.leave_span = leaveSpan;
    parser.text = textCallback;
    
    int result = md_parse(markdown.c_str(), static_cast<MD_SIZE>(markdown.size()), &parser, &state);
    if (result != 0) {
        PLOG_WARNING << "md4c parse error: " << result;
    }
}

void MarkdownDocument::clear() {
    m_root.children.clear();
    m_root.inlineContent.clear();
    m_blocks.clear();
    m_sourceHash = 0;
    m_dirty = true;
}

void MarkdownDocument::visitBlocks(const BlockVisitor& visitor) {
    std::function<void(Block&, int)> visit = [&](Block& block, int depth) {
        visitor(block, depth);
        for (auto& child : block.children) {
            visit(*child, depth + 1);
        }
    };
    
    visit(m_root, 0);
}

void MarkdownDocument::visitSpans(Block& block, const SpanVisitor& visitor) {
    std::function<void(Span&, int)> visit = [&](Span& span, int depth) {
        visitor(span, depth);
        for (auto& child : span.children) {
            visit(*child, depth + 1);
        }
    };
    
    for (auto& span : block.inlineContent) {
        visit(*span, 0);
    }
}

} // namespace Markdown
