#pragma once
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_stdlib.h>
#include <filesystem>
#include <vector>
#include <thread>
#include <string>
#include <Editors/Common/BaseTextEditor.hpp>
#include <Editors/Markdown/MarkdownPreview.hpp>

class MarkdownEditor : public Editors::BaseTextEditor
{
    std::filesystem::path src;
    std::thread fileWatcherThread;

    // 2.5D FBO Preview renderer
    Markdown::MarkdownPreview m_preview;
    std::string m_previewSourceCache;

    // Markdown-specific helpers
    void updateFileWatcher();
    void parseCurrentFileForContext();
    void handlePreviewMouseInput();
    void syncPreviewSource();

    // Tokenizer state for multi-line code blocks
    bool mdInCodeBlock = false;
    std::string mdCodeBlockLang;
    void tokenizeInlineMarkdown(std::vector<Editors::SyntaxToken>& tokens, const std::string& text, size_t offset);

protected:
    // BaseTextEditor overrides
    void beginTokenize(size_t startLine) override;
    std::vector<Editors::SyntaxToken> tokenizeLine(const std::string& line, size_t lineIndex) override;
    void updateCompletions() override;
    void updateSyntaxErrors() override;
    void generateContextAwareCompletions(const std::string& prefix, bool isQualifiedAccess, const std::string& objectName) override;
    void onTextChanged() override;
    std::string getNewFileName() const override { return "Untitled.md"; }
    std::string getTabBarId() const override { return "MarkdownEditorTabs"; }

public:
    MarkdownEditor();
    ~MarkdownEditor();

    ImVec2 draw();
    ImVec2 drawPreview();

    void setSrc(std::filesystem::path source);
    void openFile(std::filesystem::path file);
    void setWorkingFile(std::filesystem::path file);
    void updateVaultDirectories();
    void setScriptManager(LuaScriptManager* mgr) { m_preview.setScriptManager(mgr); }
    void setVault(Vault* v) { m_preview.setVault(v); }

    static MarkdownEditor& get();
};