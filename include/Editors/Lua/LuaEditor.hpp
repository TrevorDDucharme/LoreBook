#pragma once
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_stdlib.h>
#include <filesystem>
#include <vector>
#include <thread>
#include <string>
#include <Editors/Common/SyntaxError.hpp>
#include <Editors/Lua/ProgramStructure/ProgramStructure.hpp>
#include <Editors/Lua/ProgramStructure/Class.hpp>
#include <Editors/Lua/ProgramStructure/Method.hpp>

// Lua editor does not use JNI; keep Lua-only APIs
#include <memory>
#include <FileBackend.hpp>
#include <LuaEngine.hpp> // used for per-tab live evaluation

namespace Lua
{

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
        bool isLoadedInMemory = false; // Track if file content is loaded in memory
    };

    struct CompletionItem
    {
        std::string text;
        std::string description;
        enum Type
        {
            METHOD,
            VARIABLE,
            KEYWORD
        } type;
    };

    struct EditorTab
    {
        std::filesystem::path filePath;
        EditorState editorState;
        std::vector<SyntaxError> syntaxErrors;
        std::string displayName;
        bool isActive = false;
        // Per-tab transient Lua engine used by the live console (created on demand)
        std::unique_ptr<LuaEngine> liveEngine;

        EditorTab(const std::filesystem::path &path) : filePath(path)
        {
            displayName = path.filename().string();
            if (displayName.empty())
            {
                displayName = "Untitled";
            }
        }
    };

    class LuaEditor
    {
        std::filesystem::path src;                     // Path to the Lua source file
        std::vector<std::filesystem::path> classPaths; // Path to the compiled class files
        ProgramStructure programStructure;             // Program structure for managing types, packages, etc.
        std::thread fileWatcherThread;                 // Thread for watching file and directory changes
        std::shared_ptr<FileBackend> fileBackend;      // pluggable file backend (local or vault)

        // Tab management
        std::vector<EditorTab> tabs;
        int activeTabIndex = -1;

        // Working copies for the active tab (synced with active tab)
        EditorState editorState;
        std::vector<SyntaxError> syntaxErrors;
        std::vector<CompletionItem> completionItems;
        bool showCompletions = false;
        int selectedCompletion = 0;

        // Live console state
        std::string liveConsoleInput;
        std::string liveConsoleOutput;
        std::vector<std::string> liveConsoleHistory;
        int historyIndex = -1;

        // REPLACE WITH LUA-SPECIFIC TABLE INVESTIGATOR
        // // Class investigator state
        // std::string classSearchFilter;
        // std::string selectedClassName;
        // std::string classInfoOutput;
        // std::vector<std::string> availableClasses;
        // std::vector<std::string> availablePackages;

        // Editor settings
        float lineHeight = 18.0f;
        float charWidth = 8.0f;
        float renderFontSize = 14.0f; // actual font size used for rendering (scaled)
        float editorScaleMultiplier = 0.85f; // user-adjustable multiplier applied to ImGui font size
        float visibleHeight = 0.0f; // Store visible height for visibility calculations
        float visibleWidth = 0.0f;  // Width of the text area for horizontal visibility
        ImU32 textColor = IM_COL32(220, 220, 220, 255);
        ImU32 backgroundColor = IM_COL32(30, 30, 30, 255);
        ImU32 lineNumberColor = IM_COL32(100, 100, 100, 255);
        ImU32 cursorColor = IM_COL32(255, 255, 255, 255);
        ImU32 selectionColor = IM_COL32(100, 150, 200, 100);
        ImU32 errorColor = IM_COL32(255, 100, 100, 255);

        // Syntax highlighting colors
        ImU32 keywordColor = IM_COL32(86, 156, 214, 255);   // Blue for keywords
        ImU32 stringColor = IM_COL32(206, 145, 120, 255);   // Orange for strings
        ImU32 commentColor = IM_COL32(106, 153, 85, 255);   // Green for comments
        ImU32 numberColor = IM_COL32(181, 206, 168, 255);   // Light green for numbers
        ImU32 classColor = IM_COL32(78, 201, 176, 255);     // Cyan for class names
        ImU32 methodColor = IM_COL32(220, 220, 170, 255);   // Yellow for method names
        ImU32 variableColor = IM_COL32(156, 220, 254, 255); // Light blue for variables
        ImU32 operatorColor = IM_COL32(180, 180, 180, 255); // Gray for operators

        // Private helper methods
        void updateFileWatcher(); // Update the file watcher with current paths

        // Editor methods
        void loadFileIntoEditor(const std::filesystem::path &filePath);
        void forceReloadFromDisk(); // Force reload current file from disk
        void saveEditorToFile();
        void insertTextAtCursor(const std::string &text);
        void deleteSelection();
        void moveCursor(int deltaLine, int deltaColumn);
        void handleKeyboardInput();
        void updateCompletions();
        void updateFontMetrics();                                // Update character width and line height based on current font
        void updateLiveProgramStructure();                       // Update program structure with live content
        void updateLiveProgramStructureForAllTabs();             // Update program structure for all open tabs
        bool isInClassContext(size_t line, size_t column) const; // Check if cursor is within a class
        void generateContextAwareCompletions(const std::string &prefix, bool isQualifiedAccess, const std::string &objectName);
        void drawLineNumbers(ImDrawList *drawList, const ImVec2 &origin, float visibleHeight);
        void drawTextContent(ImDrawList *drawList, const ImVec2 &origin, float visibleHeight);
        void drawTextContentWithSyntaxHighlighting(ImDrawList *drawList, const ImVec2 &origin, float visibleHeight);
        void drawCursor(ImDrawList *drawList, const ImVec2 &origin);
        void drawSelection(ImDrawList *drawList, const ImVec2 &origin);
        void drawSyntaxErrors(ImDrawList *drawList, const ImVec2 &origin);
        void drawCompletionPopup(const ImVec2 &textOrigin);
        ImVec2 getCursorScreenPos(const ImVec2 &origin) const;
        void ensureCursorVisible();
        bool isCursorVisible() const;
        void scrollToCursorIfNeeded();
        void updateSyntaxErrors();
        std::string getCurrentTableName() const;                       // Get table/module name from current file
        std::string getCurrentModuleName() const;                     // Get module name from current file
        void parseCurrentFileForContext();                             // Parse current file content for completion context
        std::vector<std::string> getCurrentImports() const;            // Get import statements from current file
        const Table *resolveTable(const std::string &tableName) const; // Resolve table/module using imports

        // Tab management methods
        int findTabByPath(const std::filesystem::path &path);
        void openTab(const std::filesystem::path &path);
        void closeTab(int tabIndex);
        void setActiveTab(int tabIndex);
        EditorTab *getActiveTab();
        const EditorTab *getActiveTab() const;
        void loadTabContent(EditorTab &tab);
        void saveActiveTab();
        void syncActiveTabToEditor(); // Sync active tab state to main editor state
        void syncEditorToActiveTab(); // Sync main editor state to active tab

        // Live console methods
        void drawLiveConsole();
        std::string executeLuaCode(const std::string &code);
        std::string convertLuaResultToJson(const std::string &result);

        // Class investigation methods / API docs viewer
        void drawClassInvestigator();
        std::vector<std::string> getAvailableClassPaths();
        std::vector<std::string> getClassesInPackage(const std::string &packageName);
        std::string getClassInfo(const std::string &className);
        std::string executeStaticMethod(const std::string &className, const std::string &methodName, const std::vector<std::string> &args);

        // Toggle for showing API docs
        bool showDocViewer = false;

        // Run example helper state
        // When running an example from the docs viewer we may not have an active tab. In that case we
        // create a temporary LuaEngine and run the snippet. Running examples may modify vault state.
        std::string pendingExample;
        bool runExampleConfirmOpen = false;
        // Execute an example snippet in either the active tab or a temporary engine (returns output string)
        std::string runExampleSnippet(const std::string &code);

        // Preview helper state (used for canvas/ui preview area in the docs viewer)
        std::string previewCode;                // editable snippet for preview
        std::string previewOutput;              // output or error from last preview run
        bool previewLive = false;               // if true, re-run on change (manual Run Preview recommended)
        bool previewRunConfirmOpen = false;     // confirmation modal for preview
        bool previewExecuteRequested = false;   // set when the user requested a preview run

        // Persistent preview engine state (for continuous runs)
        std::unique_ptr<LuaEngine> previewEngine; // persistent engine while preview is running
        enum class PreviewType { None, Canvas, UI, Other } previewType = PreviewType::None;
        bool previewRunning = false;
        ImVec2 previewOrigin = ImVec2(0,0);
        int previewWidth = 320;
        int previewHeight = 240;
        double previewLastTime = 0.0;

        // Utility: detect script config (calls Config() if present) without mutating app state
        ScriptConfig detectSnippetConfig(const std::string &code);

        // Start/stop preview. startPreview will call loadScript, register bindings, and set up persistent engine.
        bool startPreview(const std::string &code, ImVec2 origin, int width, int height, ScriptConfig *outConfig = nullptr);
        void stopPreview();
        // Called every frame inside the preview child to invoke Render(dt) / UI()
        void previewTick();
        // Run a preview snippet inside a given screen origin / area so canvas drawing happens inside the preview child
        std::string runPreviewSnippet(const std::string &code, ImVec2 origin, int width, int height);
    public:
        LuaEditor();
        ~LuaEditor();

        // API Docs control (open/close/query/render)
        void openApiDocs();
        void closeApiDocs();
        bool isApiDocsOpen() const;
        void renderApiDocsIfOpen();

        ImVec2 draw();
        ImVec2 drawConsole();
        ImVec2 drawEditor(); // New custom text editor

        // Set the Lua source code to edit
        void setSrc(std::filesystem::path source);
        void setClassPath(std::vector<std::filesystem::path> path);

        // Pluggable backend for file operations (set before opening files)
        void setFileBackend(std::shared_ptr<FileBackend> backend) { fileBackend = std::move(backend); }

        void openFile(std::filesystem::path file);       // Open file in new tab or switch to existing tab
        void setWorkingFile(std::filesystem::path file); // Deprecated, use openFile instead

        // Update directories based on current project
        void updateProjectDirectories();

        // Access to the program structure
        const ProgramStructure &getProgramStructure() const { return programStructure; }

        static LuaEditor &get();
    };

};