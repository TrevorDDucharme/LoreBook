#include <Editors/Markdown/MarkdownDocument.hpp>
#include <stack>
#include <functional>
#include <cstring>
#include <algorithm>
#include <unordered_set>
#include <plog/Log.h>

namespace Markdown {

// ────────────────────────────────────────────────────────────────────
// Known effect tag names (shared across preprocessor and parser)
// ────────────────────────────────────────────────────────────────────

static const std::unordered_set<std::string>& getKnownEffectTags() {
    static const std::unordered_set<std::string> tags = {
        "fire", "rainbow", "shake", "wave", "glow", "dissolve",
        "sparkle", "snow", "blood", "neon", "glitch", "bounce",
        "pulse", "typewriter", "shadow", "outline", "gradient",
        "electric", "ice", "magic", "ghost", "underwater", "golden",
        "toxic", "crystal", "storm", "ethereal", "lava", "frost",
        "void", "holy", "matrix", "disco", "particle", "effect"
    };
    return tags;
}

// ────────────────────────────────────────────────────────────────────
// Effect tag record (from pre-processing)
// ────────────────────────────────────────────────────────────────────

struct EffectTagRecord {
    std::string tagName;       // raw tag name ("glow", "particle", "effect")
    std::string effectiveName; // resolved name ("fire" for <particle effect=fire>)
    std::vector<std::pair<std::string, std::string>> params;
    bool isOpening = true;
};

// ────────────────────────────────────────────────────────────────────
// Parse State for md4c callbacks
// ────────────────────────────────────────────────────────────────────

struct MDParseState {
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
    
    // Pre-processed effect tag records (indexed by marker ID)
    std::vector<EffectTagRecord> effectRecords;
    
    // Stack of open effect tags for proper nesting
    struct EffectTagState {
        std::string tagName;
        Span* span = nullptr;
    };
    std::vector<EffectTagState> effectTagStack;
};

// Alias so `MarkdownDocument::ParseState` is satisfied for the header declaration
struct MarkdownDocument::ParseState : MDParseState {};

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
    
    size_t i = 0;
    while (i < tag.size() && (tag[i] == '<' || std::isspace(tag[i]))) ++i;
    while (i < tag.size() && !std::isspace(tag[i]) && tag[i] != '>' && tag[i] != '/') {
        outName += tag[i++];
    }
    
    while (i < tag.size() && tag[i] != '>') {
        while (i < tag.size() && std::isspace(tag[i])) ++i;
        if (i >= tag.size() || tag[i] == '>' || tag[i] == '/') break;
        
        std::string attrName;
        while (i < tag.size() && tag[i] != '=' && !std::isspace(tag[i]) && tag[i] != '>') {
            attrName += tag[i++];
        }
        while (i < tag.size() && std::isspace(tag[i])) ++i;
        
        std::string attrValue;
        if (i < tag.size() && tag[i] == '=') {
            ++i;
            while (i < tag.size() && std::isspace(tag[i])) ++i;
            char quote = 0;
            if (i < tag.size() && (tag[i] == '"' || tag[i] == '\'')) {
                quote = tag[i++];
            }
            while (i < tag.size()) {
                if (quote && tag[i] == quote) { ++i; break; }
                else if (!quote && (std::isspace(tag[i]) || tag[i] == '>')) break;
                attrValue += tag[i++];
            }
        }
        if (!attrName.empty()) outParams.emplace_back(attrName, attrValue);
    }
}

// ────────────────────────────────────────────────────────────────────
// Pre-processor: replace effect tags with byte markers before md4c
//
// This avoids relying on md4c's HTML handling for custom effect tags.
// Each <tag ...> is replaced with \x02O{hex_id}\x03 + padding spaces
// Each </tag>   is replaced with \x02C{hex_id}\x03 + padding spaces
// The replacement has the same byte length as the original tag.
// ────────────────────────────────────────────────────────────────────

static std::string preprocessEffectTags(const std::string& source,
                                         std::vector<EffectTagRecord>& records) {
    records.clear();
    std::string result = source;
    const auto& knownTags = getKnownEffectTags();
    
    // Track code fence regions to skip
    std::vector<std::pair<size_t, size_t>> fenceRegions;
    {
        bool inFence = false;
        size_t fenceStart = 0;
        size_t p = 0;
        while (p < result.size()) {
            // Find line start
            size_t ls = p;
            while (p < result.size() && result[p] == ' ') ++p;
            if (p + 2 < result.size() && result[p] == '`' && result[p+1] == '`' && result[p+2] == '`') {
                if (!inFence) {
                    fenceStart = ls;
                    inFence = true;
                } else {
                    while (p < result.size() && result[p] != '\n') ++p;
                    fenceRegions.push_back({fenceStart, p});
                    inFence = false;
                }
            }
            while (p < result.size() && result[p] != '\n') ++p;
            if (p < result.size()) ++p;
        }
        // Unclosed fence extends to end
        if (inFence) fenceRegions.push_back({fenceStart, result.size()});
    }
    auto inCodeFence = [&](size_t pos) -> bool {
        for (const auto& [s, e] : fenceRegions)
            if (pos >= s && pos < e) return true;
        return false;
    };
    
    size_t pos = 0;
    while (pos < result.size()) {
        if (result[pos] != '<' || inCodeFence(pos)) {
            ++pos;
            continue;
        }
        
        size_t tagStart = pos;
        size_t p = pos + 1;
        
        bool isClosing = false;
        if (p < result.size() && result[p] == '/') {
            isClosing = true;
            ++p;
        }
        
        // Read tag name
        size_t nameStart = p;
        while (p < result.size() && !std::isspace(result[p]) && result[p] != '>' && result[p] != '/') {
            ++p;
        }
        if (p == nameStart) { ++pos; continue; }
        
        std::string tagName(result, nameStart, p - nameStart);
        // Case-insensitive match
        std::string lowerTag = tagName;
        std::transform(lowerTag.begin(), lowerTag.end(), lowerTag.begin(), ::tolower);
        
        if (knownTags.find(lowerTag) == knownTags.end()) {
            ++pos;
            continue;
        }
        
        // Find closing '>'
        while (p < result.size() && result[p] != '>') ++p;
        if (p >= result.size()) break;
        ++p; // include '>'
        
        size_t tagEnd = p;
        size_t tagLen = tagEnd - tagStart;
        
        // Parse attributes for opening tags
        std::vector<std::pair<std::string, std::string>> params;
        if (!isClosing) {
            std::string fullTag = result.substr(tagStart, tagLen);
            std::string parsedName;
            parseEffectTag(fullTag, parsedName, params);
        }
        
        // Build record
        EffectTagRecord record;
        record.tagName = lowerTag;
        record.isOpening = !isClosing;
        record.params = params;
        record.effectiveName = lowerTag;
        if (!isClosing) {
            for (const auto& [key, val] : params) {
                if ((lowerTag == "particle" && key == "effect") ||
                    (lowerTag == "effect" && key == "preset")) {
                    record.effectiveName = val;
                    break;
                }
            }
        }
        
        size_t recordIdx = records.size();
        records.push_back(record);
        
        // Replace tag bytes with marker (same length)
        // Marker format: \x02 + 'O'|'C' + hex1 + hex2 + \x03 + padding spaces
        if (tagLen >= 5 && recordIdx < 256) {
            result[tagStart]     = '\x02';
            result[tagStart + 1] = isClosing ? 'C' : 'O';
            result[tagStart + 2] = "0123456789abcdef"[(recordIdx >> 4) & 0xF];
            result[tagStart + 3] = "0123456789abcdef"[recordIdx & 0xF];
            result[tagStart + 4] = '\x03';
            for (size_t i = tagStart + 5; i < tagEnd; ++i)
                result[i] = ' ';
        }
        
        pos = tagEnd;
    }
    
    return result;
}

// ────────────────────────────────────────────────────────────────────
// Helper: emit a text span into the current context
// ────────────────────────────────────────────────────────────────────

static void emitTextSpan(void* userdata,
                         const MD_CHAR* text, size_t size) {
    if (size == 0) return;
    auto* state = static_cast<MDParseState*>(userdata);
    
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
}

// ────────────────────────────────────────────────────────────────────
// Helper: process text containing effect markers
// Scans for \x02O{id}\x03 / \x02C{id}\x03 sequences and creates
// Effect spans, emitting plain Text spans for content in between.
// ────────────────────────────────────────────────────────────────────

static void processTextWithMarkers(void* userdata,
                                    const MD_CHAR* text, MD_SIZE size) {
    auto* state = static_cast<MDParseState*>(userdata);
    size_t pos = 0;
    while (pos < size) {
        // Look for next marker
        size_t markerPos = pos;
        while (markerPos < size && text[markerPos] != '\x02') ++markerPos;
        
        // Emit text before marker
        if (markerPos > pos) {
            emitTextSpan(userdata, text + pos, markerPos - pos);
        }
        
        if (markerPos >= size) break; // no more markers
        
        // Parse marker: \x02 + O/C + hex1 + hex2 + \x03 [+ spaces]
        if (markerPos + 4 < size && text[markerPos + 4] == '\x03') {
            bool isOpening = text[markerPos + 1] == 'O';
            char h1 = text[markerPos + 2], h2 = text[markerPos + 3];
            int idx = ((h1 >= 'a' ? h1 - 'a' + 10 : h1 - '0') << 4) |
                       (h2 >= 'a' ? h2 - 'a' + 10 : h2 - '0');
            
            if (idx >= 0 && idx < static_cast<int>(state->effectRecords.size())) {
                const auto& record = state->effectRecords[idx];
                
                if (isOpening) {
                    // Create Effect span and push onto stacks
                    auto span = std::make_unique<Span>();
                    span->type = SpanType::Effect;
                    span->effectName = record.effectiveName;
                    span->effectParams = record.params;
                    span->sourceOffset = (text + markerPos) - state->sourceBase;
                    
                    Span* spanPtr = span.get();
                    
                    if (!state->spanStack.empty()) {
                        state->spanStack.top()->children.push_back(std::move(span));
                    } else if (!state->blockStack.empty()) {
                        state->blockStack.top()->inlineContent.push_back(std::move(span));
                    }
                    
                    state->spanStack.push(spanPtr);
                    state->effectTagStack.push_back({record.tagName, spanPtr});
                } else {
                    // Close matching Effect span
                    for (int si = static_cast<int>(state->effectTagStack.size()) - 1; si >= 0; --si) {
                        if (state->effectTagStack[si].tagName == record.tagName) {
                            Span* targetSpan = state->effectTagStack[si].span;
                            while (!state->spanStack.empty() && state->spanStack.top() != targetSpan) {
                                state->spanStack.pop();
                            }
                            if (!state->spanStack.empty()) {
                                state->spanStack.pop();
                            }
                            state->effectTagStack.erase(
                                state->effectTagStack.begin() + si,
                                state->effectTagStack.end());
                            break;
                        }
                    }
                }
            }
            
            // Skip marker bytes + any trailing padding spaces
            pos = markerPos + 5;
            while (pos < size && text[pos] == ' ') ++pos;
        } else {
            // Malformed marker, emit as text and continue
            emitTextSpan(userdata, text + markerPos, 1);
            pos = markerPos + 1;
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
    
    // Code block content: append directly, don't process markers
    if (!state->blockStack.empty() && state->blockStack.top()->type == BlockType::CodeBlock) {
        state->blockStack.top()->codeContent.append(text, size);
        return 0;
    }
    
    // For all text (normal AND html), scan for our effect markers
    // Effect tags were pre-processed into \x02O/C{id}\x03 sequences
    processTextWithMarkers(userdata, text, size);
    
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
    
    // Pre-process: extract effect tags and replace with byte markers
    // This bypasses md4c's HTML handling entirely for our custom tags
    std::string processedSource = preprocessEffectTags(markdown, state.effectRecords);
    
    state.sourceBase = processedSource.c_str();
    state.sourceLen = processedSource.size();
    
    MD_PARSER parser = {};
    parser.abi_version = 0;
    parser.flags = MD_FLAG_STRIKETHROUGH | MD_FLAG_TABLES | MD_FLAG_PERMISSIVEURLAUTOLINKS | MD_FLAG_NOHTMLBLOCKS;
    parser.enter_block = enterBlock;
    parser.leave_block = leaveBlock;
    parser.enter_span = enterSpan;
    parser.leave_span = leaveSpan;
    parser.text = textCallback;
    
    int result = md_parse(processedSource.c_str(), static_cast<MD_SIZE>(processedSource.size()), &parser, &state);
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
