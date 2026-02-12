#pragma once
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_stdlib.h>
#include <filesystem>
#include <vector>
#include <string>
#include <memory>
#include <Editors/Common/SyntaxError.hpp>

namespace Editors
{

// Common editor state used by all text editors
struct EditorState
{
    std::vector<std::string> lines;
    size_t cursorLine = 0;
    size_t cursorColumn = 0;
    size_t selectionStartLine = 0;
    size_t selectionStartColumn = 0;
    size_t selectionEndLine = 0;
    size_t selectionEndColumn = 0;
    bool hasSelection = false;
    float scrollY = 0.0f;
    float scrollX = 0.0f; // Horizontal scroll offset (pixels)
    bool needsScrollToCursor = false;
    std::string currentFile;
    bool isDirty = false;
    bool hasFocus = false;
    bool wantsFocus = false;
    bool isLoadedInMemory = false;
};

// Completion item for autocomplete
struct CompletionItem
{
    std::string text;
    std::string description;
    enum Type { CLASS, METHOD, VARIABLE, KEYWORD } type;
};

// Extra cursor for multi-cursor editing
struct ExtraCursor
{
    size_t line = 0;
    size_t column = 0;
};

// Undo/Redo snapshot
struct UndoEntry
{
    std::vector<std::string> lines;
    size_t cursorLine = 0;
    size_t cursorColumn = 0;
};

// Syntax highlighting token — child editors produce these per line
struct SyntaxToken
{
    size_t start;   // Column offset (0-based byte position)
    size_t length;  // Number of bytes
    ImU32 color;    // Rendering color
};

// Editor tab containing file state
struct EditorTab
{
    std::filesystem::path filePath;
    EditorState editorState;
    std::vector<SyntaxError> syntaxErrors;
    std::string displayName;
    bool isActive = false;
    
    EditorTab() = default;
    EditorTab(const std::filesystem::path& path) : filePath(path)
    {
        displayName = path.filename().string();
        if (displayName.empty())
        {
            displayName = "Untitled";
        }
    }
};

// Base class for text editors with common rendering and input handling
class BaseTextEditor
{
protected:
    // Tab management
    std::vector<EditorTab> tabs;
    int activeTabIndex = -1;
    
    // Working copies for the active tab
    EditorState editorState;
    std::vector<SyntaxError> syntaxErrors;
    std::vector<CompletionItem> completionItems;
    bool showCompletions = false;
    int selectedCompletion = 0;
    size_t completionOriginLine = 0;
    size_t completionOriginColumn = 0;
    
    // Multi-cursor state
    std::vector<ExtraCursor> extraCursors;
    
    // Undo/redo state
    std::vector<UndoEntry> undoStack;
    std::vector<UndoEntry> redoStack;
    static constexpr size_t maxUndoHistory = 200;
    
    // Editor settings - font metrics
    float lineHeight = 18.0f;
    float charWidth = 8.0f;
    float renderFontSize = 14.0f;           // actual font size used for rendering (scaled)
    float editorScaleMultiplier = 0.85f;    // user-adjustable multiplier applied to ImGui font size
    float visibleHeight = 0.0f;
    float visibleWidth = 0.0f;
    
    // Colors
    ImU32 textColor = IM_COL32(220, 220, 220, 255);
    ImU32 backgroundColor = IM_COL32(30, 30, 30, 255);
    ImU32 lineNumberColor = IM_COL32(100, 100, 100, 255);
    ImU32 cursorColor = IM_COL32(255, 255, 255, 255);
    ImU32 selectionColor = IM_COL32(100, 150, 200, 100);
    ImU32 errorColor = IM_COL32(255, 100, 100, 255);
    
    // Syntax highlighting colors
    ImU32 keywordColor = IM_COL32(86, 156, 214, 255);
    ImU32 stringColor = IM_COL32(206, 145, 120, 255);
    ImU32 commentColor = IM_COL32(106, 153, 85, 255);
    ImU32 numberColor = IM_COL32(181, 206, 168, 255);
    ImU32 classColor = IM_COL32(78, 201, 176, 255);
    ImU32 methodColor = IM_COL32(220, 220, 170, 255);
    ImU32 variableColor = IM_COL32(156, 220, 254, 255);
    ImU32 operatorColor = IM_COL32(180, 180, 180, 255);

    // Core editor operations — callers push undo before destructive ops
    void insertTextAtCursor(const std::string& text);
    void deleteSelection();
    void moveCursor(int deltaLine, int deltaColumn);
    void pushUndo();
    // Font metrics
    void updateFontMetrics();
    float getBaselineOffset() const;
    
    // Rendering helpers
    void drawLineNumbers(ImDrawList* drawList, const ImVec2& origin, float visibleHeight);
    void drawTextContentWithSyntaxHighlighting(ImDrawList* drawList, const ImVec2& origin, float visibleHeight);
    void drawCursor(ImDrawList* drawList, const ImVec2& origin);
    void drawSelection(ImDrawList* drawList, const ImVec2& origin);
    void drawSyntaxErrors(ImDrawList* drawList, const ImVec2& origin);
    void drawCompletionPopup(const ImVec2& textOrigin);
    
    // Cursor positioning
    ImVec2 getCursorScreenPos(const ImVec2& origin) const;
    void ensureCursorVisible();
    bool isCursorVisible() const;
    void scrollToCursorIfNeeded();
    
    // Input handling
    void handleKeyboardInput();
    void handleMouseInput(const ImVec2& origin, const ImVec2& textOrigin, const ImVec2& size, bool isHovered);
    
    // Tab management
    int findTabByPath(const std::filesystem::path& path);
    void closeTab(int tabIndex);
    void setActiveTab(int tabIndex);
    EditorTab* getActiveTab();
    const EditorTab* getActiveTab() const;
    void syncActiveTabToEditor();
    void syncEditorToActiveTab();
    
    // Virtual methods for language-specific behavior
    virtual void beginTokenize(size_t startLine) {} // Prepare tokenizer multi-line state up to startLine
    virtual std::vector<SyntaxToken> tokenizeLine(const std::string& line, size_t lineIndex) = 0;
    virtual void updateCompletions() = 0;
    virtual void updateSyntaxErrors() = 0;
    virtual void generateContextAwareCompletions(const std::string& prefix, bool isQualifiedAccess, const std::string& objectName) = 0;
    
    // Optional overrides
    virtual void onTextChanged() {} // Called after text modifications
    virtual void loadTabContent(EditorTab& tab); // Can be overridden for custom file backends
    virtual void saveActiveTab(); // Can be overridden for custom file backends
    virtual std::string getNewFileName() const { return "Untitled.txt"; }
    virtual std::string getTabBarId() const { return "EditorTabs"; }
    virtual void drawEditorOverlay(const ImVec2& textOrigin) {} // Hook for subclass UI in editor area

public:
    BaseTextEditor();
    virtual ~BaseTextEditor();
    
    // Main drawing function - draws tab bar and editor
    ImVec2 drawEditor();
    
    // Content access for vault integration
    void setContent(const std::string& content);
    std::string getEditorContent() const;
    bool isDirty() const { return editorState.isDirty; }
    void clearDirtyFlag() { editorState.isDirty = false; }
    
    // Tab operations
    void openTab(const std::filesystem::path& path);
    bool hasOpenTabs() const { return !tabs.empty(); }
    int getActiveTabIndex() const { return activeTabIndex; }
};

} // namespace Editors
