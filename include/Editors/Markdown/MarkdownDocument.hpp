#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <cstddef>
#include <md4c.h>

namespace Markdown {

// Forward declarations
class Block;
class Span;

// ────────────────────────────────────────────────────────────────────
// Base AST Node
// ────────────────────────────────────────────────────────────────────

class Node {
public:
    virtual ~Node() = default;
    
    size_t sourceOffset = 0;  // byte offset in original source
    size_t sourceLength = 0;  // byte length in original source
};

// ────────────────────────────────────────────────────────────────────
// Inline/Span Nodes
// ────────────────────────────────────────────────────────────────────

enum class SpanType {
    Text,
    Emphasis,      // *italic* or _italic_
    Strong,        // **bold** or __bold__
    Code,          // `code`
    Link,          // [text](url)
    Image,         // ![alt](url)
    Strikethrough, // ~~text~~
    Effect,        // <fire>text</fire> custom effect tags
};

class Span : public Node {
public:
    SpanType type = SpanType::Text;
    std::string text;                   // for Text spans
    std::string url;                    // for Link/Image
    std::string title;                  // for Link/Image
    std::string effectName;             // for Effect spans (e.g., "fire", "rainbow")
    std::vector<std::pair<std::string, std::string>> effectParams;  // effect attributes
    std::vector<std::unique_ptr<Span>> children;
};

// ────────────────────────────────────────────────────────────────────
// Block Nodes
// ────────────────────────────────────────────────────────────────────

enum class BlockType {
    Document,
    Paragraph,
    Heading,
    CodeBlock,
    Quote,
    List,
    ListItem,
    HorizontalRule,
    Table,
    TableRow,
    TableCell,
    HTML,          // raw HTML blocks
};

class Block : public Node {
public:
    BlockType type = BlockType::Paragraph;
    
    // Block-specific data
    int headingLevel = 0;              // 1-6 for Heading
    std::string codeLanguage;          // for CodeBlock
    std::string codeContent;           // for CodeBlock (raw text)
    bool isOrderedList = false;        // for List
    int listStart = 1;                 // starting number for ordered lists
    bool isHeaderRow = false;          // for TableRow
    int tableColumnAlign = 0;          // for TableCell: 0=default, 1=left, 2=center, 3=right
    
    // Children
    std::vector<std::unique_ptr<Span>> inlineContent;  // for leaf blocks like Paragraph
    std::vector<std::unique_ptr<Block>> children;      // for container blocks
};

// ────────────────────────────────────────────────────────────────────
// MarkdownDocument - owns the parsed AST
// ────────────────────────────────────────────────────────────────────

class MarkdownDocument {
public:
    MarkdownDocument() = default;
    ~MarkdownDocument() = default;
    
    // Non-copyable, movable
    MarkdownDocument(const MarkdownDocument&) = delete;
    MarkdownDocument& operator=(const MarkdownDocument&) = delete;
    MarkdownDocument(MarkdownDocument&&) = default;
    MarkdownDocument& operator=(MarkdownDocument&&) = default;
    
    /// Parse markdown string and build AST
    void parseString(const std::string& markdown);
    
    /// Clear the document
    void clear();
    
    /// Check if document needs re-parsing (source changed)
    bool isDirty() const { return m_dirty; }
    
    /// Mark as dirty (call when source text changes)
    void markDirty() { m_dirty = true; }
    
    /// Get hash of last parsed source (for change detection)
    size_t sourceHash() const { return m_sourceHash; }
    
    /// Access the root document block
    Block& getRoot() { return m_root; }
    const Block& getRoot() const { return m_root; }
    
    /// Flat list of all top-level blocks for iteration
    const std::vector<Block*>& getBlocks() const { return m_blocks; }
    
    /// Visitor pattern for traversal
    using BlockVisitor = std::function<void(Block&, int depth)>;
    using SpanVisitor = std::function<void(Span&, int depth)>;
    void visitBlocks(const BlockVisitor& visitor);
    void visitSpans(Block& block, const SpanVisitor& visitor);

private:
    Block m_root;
    std::vector<Block*> m_blocks;  // flat view of top-level blocks
    size_t m_sourceHash = 0;
    bool m_dirty = true;
    
    // md4c parser state
    struct ParseState;
    static int enterBlock(MD_BLOCKTYPE type, void* detail, void* userdata);
    static int leaveBlock(MD_BLOCKTYPE type, void* detail, void* userdata);
    static int enterSpan(MD_SPANTYPE type, void* detail, void* userdata);
    static int leaveSpan(MD_SPANTYPE type, void* detail, void* userdata);
    static int textCallback(MD_TEXTTYPE type, const MD_CHAR* text, MD_SIZE size, void* userdata);
};

} // namespace Markdown
