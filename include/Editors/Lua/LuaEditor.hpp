#pragma once
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_stdlib.h>
#include <filesystem>
#include <vector>
#include <thread>
#include <string>
#include <memory>
#include <unordered_map>
#include <Editors/Common/BaseTextEditor.hpp>
#include <Editors/Lua/ProgramStructure/ProgramStructure.hpp>
#include <Editors/Lua/ProgramStructure/Class.hpp>
#include <Editors/Lua/ProgramStructure/Method.hpp>
#include <FileBackend.hpp>
#include <LuaEngine.hpp>

namespace Lua
{

class LuaEditor : public Editors::BaseTextEditor
{
    std::filesystem::path src;
    std::vector<std::filesystem::path> classPaths;
    ProgramStructure programStructure;
    std::thread fileWatcherThread;
    std::shared_ptr<FileBackend> fileBackend;

    // Per-tab Lua engines keyed by filepath string (for live console)
    std::unordered_map<std::string, std::unique_ptr<LuaEngine>> tabEngines;

    // Live console state
    std::string liveConsoleInput;
    std::string liveConsoleOutput;
    std::vector<std::string> liveConsoleHistory;
    int historyIndex = -1;

    bool showDocViewer = false;

    // Run example state
    std::string pendingExample;
    bool runExampleConfirmOpen = false;

    // Preview state
    std::string previewCode;
    std::string previewOutput;
    bool previewRunConfirmOpen = false;
    bool previewExecuteRequested = false;
    std::unique_ptr<LuaEngine> previewEngine;
    enum class PreviewType { None, Canvas, UI, Other } previewType = PreviewType::None;
    bool previewRunning = false;
    ImVec2 previewOrigin = ImVec2(0,0);
    int previewWidth = 320;
    int previewHeight = 240;
    double previewLastTime = 0.0;

    // Lua-specific helpers
    void updateFileWatcher();
    void updateLiveProgramStructure();
    void updateLiveProgramStructureForAllTabs();
    bool isInClassContext(size_t line, size_t column) const;
    std::string getCurrentTableName() const;
    std::string getCurrentModuleName() const;
    void parseCurrentFileForContext();
    std::vector<std::string> getCurrentImports() const;
    const Table *resolveTable(const std::string &tableName) const;

    void drawLiveConsole();
    std::string executeLuaCode(const std::string &code);
    std::string convertLuaResultToJson(const std::string &result);
    void drawClassInvestigator();
    std::string runExampleSnippet(const std::string &code);
    ScriptConfig detectSnippetConfig(const std::string &code);
    bool startPreview(const std::string &code, ImVec2 origin, int width, int height, ScriptConfig *outConfig = nullptr);
    void stopPreview();
    void previewTick();
    std::string runPreviewSnippet(const std::string &code, ImVec2 origin, int width, int height);

    // Get or create the per-tab LuaEngine for the active tab
    LuaEngine* getOrCreateTabEngine();

protected:
    // BaseTextEditor overrides
    void drawTextContentWithSyntaxHighlighting(ImDrawList *drawList, const ImVec2 &origin, float visibleHeight) override;
    void updateCompletions() override;
    void updateSyntaxErrors() override;
    void generateContextAwareCompletions(const std::string &prefix, bool isQualifiedAccess, const std::string &objectName) override;
    void onTextChanged() override;
    void loadTabContent(Editors::EditorTab &tab) override;
    void saveActiveTab() override;
    std::string getNewFileName() const override { return "Untitled.lua"; }
    std::string getTabBarId() const override { return "LuaEditorTabs"; }
    void drawEditorOverlay(const ImVec2 &textOrigin) override;

public:
    LuaEditor();
    ~LuaEditor();

    void openApiDocs();
    void closeApiDocs();
    bool isApiDocsOpen() const;
    void renderApiDocsIfOpen();

    ImVec2 draw();
    ImVec2 drawConsole();

    void setSrc(std::filesystem::path source);
    void setClassPath(std::vector<std::filesystem::path> path);
    void setFileBackend(std::shared_ptr<FileBackend> backend) { fileBackend = std::move(backend); }

    void openFile(std::filesystem::path file);
    void setWorkingFile(std::filesystem::path file);
    void forceReloadFromDisk();
    void updateProjectDirectories();

    const ProgramStructure &getProgramStructure() const { return programStructure; }

    static LuaEditor &get();
};

} // namespace Lua