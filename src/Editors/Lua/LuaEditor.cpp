#include <Editors/Lua/LuaEditor.hpp>
#include <imgui.h>
#include <FileBackends/LocalFileBackend.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <unordered_set>

#include <LuaEngine.hpp>
#include <LuaVaultBindings.hpp>
#include <LuaImGuiBindings.hpp>
#include <FileBackends/VaultFileBackend.hpp>
#include <LuaCanvasBindings.hpp>

using namespace Lua;

LuaEditor::LuaEditor()
{
    // default to local backend
    if (!fileBackend)
        fileBackend = std::make_shared<LocalFileBackend>();
}

LuaEditor::~LuaEditor()
{
    if (fileWatcherThread.joinable())
        fileWatcherThread.join();
}

LuaEditor &LuaEditor::get()
{
    static LuaEditor instance;
    return instance;
}

ImVec2 LuaEditor::draw()
{
    // If no tabs show a simple placeholder area; otherwise render tab bar and editor
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("LuaEditorChild", avail, true, childFlags);

    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImVec2 childOrigin = ImGui::GetCursorScreenPos();

    if (ImGui::BeginTabBar("LuaEditorTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_TabListPopupButton))
    {
        for (int i = 0; i < tabs.size(); ++i)
        {
            bool isOpen = true;
            ImGuiTabItemFlags tabFlags = 0;
            if (tabs[i].editorState.isDirty)
                tabFlags |= ImGuiTabItemFlags_UnsavedDocument;

            if (ImGui::BeginTabItem(tabs[i].displayName.c_str(), &isOpen, tabFlags))
            {
                if (activeTabIndex != i)
                    setActiveTab(i);
                drawEditor();
                ImGui::EndTabItem();
            }

            if (!isOpen)
            {
                closeTab(i);
                break;
            }
        }

        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
        {
            static int untitledCounter = 1;
            std::string newFileName = "Untitled" + std::to_string(untitledCounter++) + ".lua";
            openTab(std::filesystem::path("scripts") / newFileName);
        }

        ImGui::EndTabBar();
    }

    if (tabs.empty())
    {
        ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x * 0.5f - 100);
        ImGui::SetCursorPosY(ImGui::GetContentRegionAvail().y * 0.5f - 50);
        ImGui::Text("No files open");
        ImGui::Text("Open a file to start editing");
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    return childOrigin;
}

ImVec2 LuaEditor::drawConsole()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("LuaConsoleChild", avail, true, childFlags);
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImVec2 childOrigin = ImGui::GetCursorScreenPos();

    if (ImGui::BeginTabBar("LuaConsoleTabs", ImGuiTabBarFlags_None))
    {
        if (ImGui::BeginTabItem("Live Console"))
        {
            drawLiveConsole();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();
    return childOrigin;
}

ImVec2 LuaEditor::drawEditor()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList *drawList = ImGui::GetWindowDrawList();

    // Update metrics and ensure we have active tab state
    updateFontMetrics();
    visibleHeight = avail.y;
    EditorTab *active = getActiveTab();
    if (!active)
    {
        // nothing to draw
        ImGui::Dummy(avail);
        return origin;
    }

    // If the editor requested focus, apply it
    if (editorState.wantsFocus)
    {
        ImGui::SetKeyboardFocusHere();
        editorState.wantsFocus = false;
        editorState.hasFocus = true;
    }

    // Draw background
    drawList->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), backgroundColor);

    // Reserve the area so ImGui can manage input
    ImGui::InvisibleButton("#LuaTextArea", avail);
    bool isHovered = ImGui::IsItemHovered();

    // Layout: left gutter for line numbers, right for text
    float gutterWidth = 60.0f;
    ImVec2 gutterOrigin = origin;
    ImVec2 textOrigin = ImVec2(origin.x + gutterWidth + 8.0f, origin.y + 4.0f);

    // Handle mouse clicks to place cursor / start selection
    ImVec2 mouse = ImGui::GetIO().MousePos;
    bool inTextArea = mouse.x >= textOrigin.x && mouse.x <= origin.x + avail.x && mouse.y >= origin.y && mouse.y <= origin.y + avail.y;
    if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        // compute clicked line (account for baseline offset used when drawing text)
        ImFont *font = ImGui::GetFont();
        float baselineOffset = (lineHeight - renderFontSize) * 0.5f;
        float relY = mouse.y - (origin.y + baselineOffset) + editorState.scrollY;
        int line = static_cast<int>(std::floor(relY / lineHeight));
        line = std::max(0, std::min(line, static_cast<int>(editorState.lines.size()) - 1));

        // approximate column by measuring text widths (reuse `font` declared above)
        int col = 0;
        if (line >= 0 && line < static_cast<int>(editorState.lines.size()))
        {
            const std::string &ln = editorState.lines[line];
            float x = textOrigin.x;
            for (size_t i = 0; i <= ln.size(); ++i)
            {
                float nextX = x;
                if (i < ln.size()) nextX += font ? font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f, ln.substr(i,1).c_str()).x : charWidth;
                float mid = (x + nextX) * 0.5f;
                if (mouse.x < mid) { col = static_cast<int>(i); break; }
                x = nextX;
                col = static_cast<int>(i+1);
            }
        }

        editorState.cursorLine = std::max(0, std::min(line, static_cast<int>(editorState.lines.size()) - 1));
        editorState.cursorColumn = std::max(0, std::min(col, static_cast<int>(editorState.lines[editorState.cursorLine].length())));
        editorState.hasSelection = false;
        editorState.selectionStartLine = editorState.cursorLine;
        editorState.selectionStartColumn = editorState.cursorColumn;
        editorState.selectionEndLine = editorState.cursorLine;
        editorState.selectionEndColumn = editorState.cursorColumn;
        editorState.needsScrollToCursor = true;
        editorState.hasFocus = true;
    }

    // Drag to select
    if (isHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        ImFont *font = ImGui::GetFont();
        float baselineOffset = (lineHeight - renderFontSize) * 0.5f;
        float relY = mouse.y - (origin.y + baselineOffset) + editorState.scrollY;
        int line = static_cast<int>(std::floor(relY / lineHeight));
        line = std::max(0, std::min(line, static_cast<int>(editorState.lines.size()) - 1));
        int col = 0;
        const std::string &ln = editorState.lines[line];
        float x = textOrigin.x;
        for (size_t i = 0; i <= ln.size(); ++i)
        {
            float nextX = x;
            if (i < ln.size()) nextX += font ? font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f, ln.substr(i,1).c_str()).x : charWidth;
            float mid = (x + nextX) * 0.5f;
            if (mouse.x < mid) { col = static_cast<int>(i); break; }
            x = nextX;
            col = static_cast<int>(i+1);
        }
        editorState.selectionEndLine = line;
        editorState.selectionEndColumn = col;
        editorState.hasSelection = !(editorState.selectionStartLine == editorState.selectionEndLine && editorState.selectionStartColumn == editorState.selectionEndColumn);
        editorState.cursorLine = editorState.selectionEndLine;
        editorState.cursorColumn = editorState.selectionEndColumn;
        editorState.needsScrollToCursor = true;
    }

    // Mouse wheel scrolling
    ImGuiIO &io = ImGui::GetIO();
    // When hovered, capture mouse input so the editor owns wheel/drag events.
    if (isHovered)
        io.WantCaptureMouse = true;

    // Ctrl+wheel: change editor scale; otherwise wheel scrolls the editor.
    if (isHovered && io.MouseWheel != 0.0f)
    {
        if (io.KeyCtrl)
        {
            // Adjust multiplier with a small step and clamp
            editorScaleMultiplier += io.MouseWheel * 0.05f;
            if (editorScaleMultiplier < 0.6f) editorScaleMultiplier = 0.6f;
            if (editorScaleMultiplier > 1.2f) editorScaleMultiplier = 1.2f;
            updateFontMetrics();
        }
        else
        {
            editorState.scrollY = std::max(0.0f, editorState.scrollY - io.MouseWheel * lineHeight * 3.0f);
        }
    }

    // Draw gutter and text
    drawLineNumbers(drawList, ImVec2(gutterOrigin.x + 4.0f, gutterOrigin.y + 4.0f), visibleHeight);
    drawTextContent(drawList, textOrigin, visibleHeight - 4.0f);
    drawSelection(drawList, textOrigin);
    drawCursor(drawList, textOrigin);
    drawSyntaxErrors(drawList, textOrigin);

    // Keyboard handling
    handleKeyboardInput();

    // Keep state in sync
    if (editorState.needsScrollToCursor) { ensureCursorVisible(); editorState.needsScrollToCursor = false; }
    syncEditorToActiveTab();

    // Completion popup
    drawCompletionPopup(textOrigin);

    // keep ImGui cursor at the end of the area
    ImGui::SetCursorScreenPos(ImVec2(origin.x + avail.x, origin.y + avail.y));
    return origin;
}

void LuaEditor::setSrc(std::filesystem::path source)
{
    src = std::move(source);
}

void LuaEditor::setClassPath(std::vector<std::filesystem::path> path)
{
    classPaths = std::move(path);
}

void LuaEditor::openFile(std::filesystem::path file)
{
    int idx = findTabByPath(file);
    if (idx >= 0)
    {
        setActiveTab(idx);
        return;
    }

    EditorTab t(file);
    tabs.push_back(std::move(t));
    // Load the content into the newly created tab before activating it so
    // that syncActiveTabToEditor() copies populated editorState into the
    // active editorState used by the UI.
    loadTabContent(tabs.back());
    setActiveTab(static_cast<int>(tabs.size()) - 1);
}



Lua::EditorTab *LuaEditor::getActiveTab()
{
    if (activeTabIndex < 0 || activeTabIndex >= static_cast<int>(tabs.size())) return nullptr;
    return &tabs[activeTabIndex];
}

const Lua::EditorTab *LuaEditor::getActiveTab() const
{
    if (activeTabIndex < 0 || activeTabIndex >= static_cast<int>(tabs.size())) return nullptr;
    return &tabs[activeTabIndex];
}

void LuaEditor::loadTabContent(EditorTab &tab)
{
    if (!fileBackend)
        fileBackend = std::make_shared<LocalFileBackend>();

    FileUri uri = FileUri::fromPath(tab.filePath);
    FileResult r = fileBackend->readFileSync(uri);
    tab.editorState.lines.clear();
    if (r.ok)
    {
        std::string txt = r.text();
        size_t pos = 0;
        while (pos < txt.size())
        {
            auto nl = txt.find('\n', pos);
            if (nl == std::string::npos)
            {
                tab.editorState.lines.push_back(txt.substr(pos));
                break;
            }
            tab.editorState.lines.push_back(txt.substr(pos, nl - pos));
            pos = nl + 1;
        }
        tab.editorState.isLoadedInMemory = true;
    }
    else
    {
        tab.editorState.lines.push_back(std::string());
        tab.editorState.isLoadedInMemory = false;
    }
    // Initialize editorState positions
    tab.editorState.cursorLine = 0;
    tab.editorState.cursorColumn = 0;
    tab.editorState.scrollY = 0.0f;
    tab.editorState.hasSelection = false;
    tab.editorState.currentFile = tab.filePath.string();
    tab.editorState.isDirty = false;
}

// --- Implementation of core editor behaviors ---

void LuaEditor::updateFileWatcher()
{
    programStructure.stopWatching();
    std::vector<std::filesystem::path> watchDirs;
    if (!src.empty() && std::filesystem::exists(src) && std::filesystem::is_directory(src))
        watchDirs.push_back(src);
    for (const auto &cp : classPaths)
        if (std::filesystem::exists(cp) && std::filesystem::is_directory(cp))
            watchDirs.push_back(cp);
        if (!watchDirs.empty())
        {
            for (const auto &p : watchDirs)
                programStructure.startWatching(p);
        }
}

void LuaEditor::updateFontMetrics()
{
    ImFont *font = ImGui::GetFont();
    if (font)
    {
        // scale by the editor multiplier to allow runtime adjustments (Ctrl+wheel)
        renderFontSize = font->FontSize * editorScaleMultiplier;
        charWidth = font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f, "M").x;
        lineHeight = renderFontSize + ImGui::GetStyle().ItemSpacing.y * 0.2f;
    }
    else
    {
        charWidth = 8.0f;
        lineHeight = 18.0f;
    }
}

void LuaEditor::saveEditorToFile()
{
    saveActiveTab();
}

void LuaEditor::saveActiveTab()
{
    EditorTab *activeTab = getActiveTab();
    if (!activeTab)
        return;

    // Join lines
    std::string out;
    for (size_t i = 0; i < activeTab->editorState.lines.size(); ++i)
    {
        out += activeTab->editorState.lines[i];
        if (i + 1 < activeTab->editorState.lines.size())
            out += '\n';
    }

    FileUri uri = FileUri::fromPath(activeTab->filePath);
    std::vector<uint8_t> data(out.begin(), out.end());
    if (!fileBackend)
        fileBackend = std::make_shared<LocalFileBackend>();

    FileResult r = fileBackend->writeFileSync(uri, data, true);
    if (r.ok)
    {
        activeTab->editorState.isDirty = false;
        activeTab->displayName = activeTab->filePath.filename().string();
        if (activeTab->isActive)
            editorState.isDirty = false;
    }
}

void LuaEditor::insertTextAtCursor(const std::string &text)
{
    if (editorState.hasSelection)
    {
        deleteSelection();
    }

    if (editorState.cursorLine >= editorState.lines.size())
        editorState.lines.resize(editorState.cursorLine + 1);

    if (text.find('\n') != std::string::npos)
    {
        std::vector<std::string> lines;
        std::string cur;
        for (char c : text)
        {
            if (c == '\n') { lines.push_back(cur); cur.clear(); }
            else cur.push_back(c);
        }
        lines.push_back(cur);

        std::string &line = editorState.lines[editorState.cursorLine];
        std::string after = line.substr(editorState.cursorColumn);
        line = line.substr(0, editorState.cursorColumn) + lines[0];
        for (size_t i = 1; i < lines.size(); ++i)
            editorState.lines.insert(editorState.lines.begin() + editorState.cursorLine + i, lines[i]);

        editorState.cursorLine += lines.size() - 1;
        editorState.cursorColumn = lines.back().length();
        editorState.lines[editorState.cursorLine] += after;
    }
    else
    {
        std::string &line = editorState.lines[editorState.cursorLine];
        line.insert(editorState.cursorColumn, text);
        editorState.cursorColumn += text.length();
    }

    editorState.isDirty = true;
    editorState.needsScrollToCursor = true;
    syncEditorToActiveTab();
    updateSyntaxErrors();
    updateLiveProgramStructure();
    updateCompletions();
}

void LuaEditor::deleteSelection()
{
    if (!editorState.hasSelection) return;

    size_t startLine = std::min(editorState.selectionStartLine, editorState.selectionEndLine);
    size_t endLine = std::max(editorState.selectionStartLine, editorState.selectionEndLine);
    size_t startCol = editorState.selectionStartLine < editorState.selectionEndLine ? editorState.selectionStartColumn : editorState.selectionEndColumn;
    size_t endCol = editorState.selectionStartLine < editorState.selectionEndLine ? editorState.selectionEndColumn : editorState.selectionStartColumn;

    if (startLine == endLine)
    {
        editorState.lines[startLine].erase(startCol, endCol - startCol);
    }
    else
    {
        std::string newLine = editorState.lines[startLine].substr(0, startCol) + editorState.lines[endLine].substr(endCol);
        editorState.lines[startLine] = newLine;
        editorState.lines.erase(editorState.lines.begin() + startLine + 1, editorState.lines.begin() + endLine + 1);
    }

    editorState.cursorLine = startLine;
    editorState.cursorColumn = startCol;
    editorState.hasSelection = false;
    editorState.isDirty = true;
    syncEditorToActiveTab();
    updateSyntaxErrors();
    updateLiveProgramStructure();
}

void LuaEditor::moveCursor(int deltaLine, int deltaColumn)
{
    int newLine = static_cast<int>(editorState.cursorLine) + deltaLine;
    int newColumn = static_cast<int>(editorState.cursorColumn) + deltaColumn;
    newLine = std::max(0, std::min(newLine, static_cast<int>(editorState.lines.size()) - 1));
    if (newLine >= 0 && newLine < static_cast<int>(editorState.lines.size()))
        newColumn = std::max(0, std::min(newColumn, static_cast<int>(editorState.lines[newLine].length())));
    else newColumn = 0;
    editorState.cursorLine = newLine;
    editorState.cursorColumn = newColumn;
    editorState.needsScrollToCursor = true;
    syncEditorToActiveTab();
}

void LuaEditor::handleKeyboardInput()
{
    ImGuiIO &io = ImGui::GetIO();
    if (!editorState.hasFocus) return;

    if (ImGui::IsKeyPressed(ImGuiKey_Enter))
    {
        if (showCompletions) showCompletions = false;
        if (editorState.hasSelection) deleteSelection();
        if (editorState.cursorLine >= editorState.lines.size()) editorState.lines.resize(editorState.cursorLine + 1);
        std::string &cur = editorState.lines[editorState.cursorLine];
        // simple indentation: preserve leading whitespace
        std::string indent;
        for (char c : cur) { if (c == ' ' || c == '\t') indent.push_back(c); else break; }
        std::string newLine = cur.substr(editorState.cursorColumn);
        cur = cur.substr(0, editorState.cursorColumn);
        if (!newLine.empty() || editorState.cursorColumn > 0) newLine = indent + newLine;
        editorState.lines.insert(editorState.lines.begin() + editorState.cursorLine + 1, newLine);
        editorState.cursorLine++;
        editorState.cursorColumn = indent.length();
        editorState.isDirty = true; editorState.needsScrollToCursor = true;
        updateSyntaxErrors(); updateLiveProgramStructure();
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace))
    {
        if (editorState.hasSelection) { deleteSelection(); }
        else if (editorState.cursorColumn > 0)
        {
            editorState.lines[editorState.cursorLine].erase(editorState.cursorColumn - 1, 1);
            editorState.cursorColumn--; editorState.isDirty = true; updateSyntaxErrors(); updateLiveProgramStructure();
        }
        else if (editorState.cursorLine > 0)
        {
            editorState.cursorColumn = editorState.lines[editorState.cursorLine - 1].length();
            editorState.lines[editorState.cursorLine - 1] += editorState.lines[editorState.cursorLine];
            editorState.lines.erase(editorState.lines.begin() + editorState.cursorLine);
            editorState.cursorLine--; editorState.isDirty = true; updateSyntaxErrors(); updateLiveProgramStructure(); scrollToCursorIfNeeded();
        }
        if (showCompletions) { updateCompletions(); if (completionItems.empty()) showCompletions = false; }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        if (editorState.hasSelection) deleteSelection();
        else if (editorState.cursorLine < editorState.lines.size())
        {
            if (editorState.cursorColumn < editorState.lines[editorState.cursorLine].length())
            {
                editorState.lines[editorState.cursorLine].erase(editorState.cursorColumn, 1);
                editorState.isDirty = true; updateSyntaxErrors(); updateLiveProgramStructure();
            }
            else if (editorState.cursorLine < editorState.lines.size() - 1)
            {
                editorState.lines[editorState.cursorLine] += editorState.lines[editorState.cursorLine + 1];
                editorState.lines.erase(editorState.lines.begin() + editorState.cursorLine + 1);
                editorState.isDirty = true; updateSyntaxErrors(); updateLiveProgramStructure();
            }
        }
    }

    // Arrows
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow)) { if (editorState.cursorColumn > 0) editorState.cursorColumn--; else if (editorState.cursorLine > 0) { editorState.cursorLine--; editorState.cursorColumn = editorState.lines[editorState.cursorLine].length(); } editorState.hasSelection = false; if (showCompletions) showCompletions = false; }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow)) { if (editorState.cursorLine < editorState.lines.size() && editorState.cursorColumn < editorState.lines[editorState.cursorLine].length()) editorState.cursorColumn++; else if (editorState.cursorLine < editorState.lines.size()-1) { editorState.cursorLine++; editorState.cursorColumn = 0; } editorState.hasSelection = false; if (showCompletions) showCompletions = false; }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow)) { if (editorState.cursorLine > 0) { editorState.cursorLine--; editorState.cursorColumn = std::min(editorState.cursorColumn, editorState.lines[editorState.cursorLine].length()); } editorState.hasSelection = false; if (showCompletions) showCompletions = false; }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow)) { if (editorState.cursorLine < editorState.lines.size()-1) { editorState.cursorLine++; editorState.cursorColumn = std::min(editorState.cursorColumn, editorState.lines[editorState.cursorLine].length()); } editorState.hasSelection = false; if (showCompletions) showCompletions = false; }

    if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !showCompletions)
    {
        if (editorState.hasSelection) deleteSelection();
        insertTextAtCursor("    ");
        return;
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S)) saveEditorToFile();

    if (showCompletions)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Tab))
        {
            if (selectedCompletion < completionItems.size())
            {
                const std::string &line = editorState.lines[editorState.cursorLine];
                size_t wordStart = editorState.cursorColumn;
                while (wordStart > 0 && (std::isalnum(line[wordStart-1]) || line[wordStart-1]=='_')) wordStart--;
                std::string &cur = editorState.lines[editorState.cursorLine];
                cur.erase(wordStart, editorState.cursorColumn - wordStart);
                cur.insert(wordStart, completionItems[selectedCompletion].text);
                editorState.cursorColumn = wordStart + completionItems[selectedCompletion].text.length();
                editorState.isDirty = true; updateLiveProgramStructure();
            }
            showCompletions = false;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape)) showCompletions = false;
    }

    for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
    {
        ImWchar c = io.InputQueueCharacters[i];
        if (c == '\t') { if (editorState.hasSelection) deleteSelection(); insertTextAtCursor("    "); }
        else if (c != 0 && c >= 32)
        {
            std::string s(1, static_cast<char>(c));
            bool shouldAutoPair = false; std::string closing;
            switch (c) { case '(' : shouldAutoPair=true; closing=")"; break; case '{': shouldAutoPair=true; closing="}"; break; case '[': shouldAutoPair=true; closing="]"; break; case '"': shouldAutoPair=true; closing="\""; break; case '\'': shouldAutoPair=true; closing="'"; break; }
            if (shouldAutoPair)
            {
                if (editorState.hasSelection) deleteSelection();
                insertTextAtCursor(s);
                size_t sl = editorState.cursorLine, sc = editorState.cursorColumn;
                insertTextAtCursor(closing);
                editorState.cursorLine = sl; editorState.cursorColumn = sc;
                syncEditorToActiveTab();
            }
            else { insertTextAtCursor(s); }
            if (c == '.' || std::isalpha(c)) updateCompletions();
        }
    }
}

void LuaEditor::updateCompletions()
{
    completionItems.clear();
    if (editorState.cursorLine >= editorState.lines.size()) return;
    std::string line = editorState.lines[editorState.cursorLine];
    if (editorState.cursorColumn == 0) return;

    updateLiveProgramStructureForAllTabs();
    std::string before = line.substr(0, editorState.cursorColumn);
    size_t lastDot = before.find_last_of('.');
    bool qualified = false; std::string objectName, prefix;
    if (lastDot != std::string::npos) { qualified = true; size_t objStart = lastDot; while (objStart>0 && (std::isalnum(before[objStart-1])||before[objStart-1]=='_')) objStart--; objectName = before.substr(objStart, lastDot-objStart); prefix = before.substr(lastDot+1); }
    else { size_t wordStart = editorState.cursorColumn; while (wordStart>0 && (std::isalnum(line[wordStart-1])||line[wordStart-1]=='_')) wordStart--; if (wordStart==editorState.cursorColumn) return; prefix = line.substr(wordStart, editorState.cursorColumn-wordStart); }
    generateContextAwareCompletions(prefix, qualified, objectName);
    showCompletions = !completionItems.empty(); selectedCompletion = 0;
}

void LuaEditor::updateSyntaxErrors()
{
    syntaxErrors.clear();
    int paren=0, brace=0, bracket=0; bool inString=false; bool inSingle=false; bool inMulti=false;
    for (size_t ln=0; ln<editorState.lines.size(); ++ln)
    {
        const std::string &l = editorState.lines[ln];
        inSingle = false;
        for (size_t c=0; c<l.size(); ++c)
        {
            char ch = l[c]; char next = (c+1<l.size()?l[c+1]:'\0'); char prev = (c>0?l[c-1]:'\0');
            if (!inString && !inSingle && !inMulti)
            {
                if (ch=='-' && next=='-') { inSingle=true; c++; continue; }
                if (ch=='[' && next=='[') { inMulti=true; c++; continue; }
            }
            if (inMulti) { if (ch==']' && next==']') { inMulti=false; c++; } continue; }
            if (inSingle) continue;
            if (!inString && ch=='"' && prev!='\\') { inString=true; continue; }
            else if (inString && ch=='"' && prev!='\\') { inString=false; continue; }
            if (inString) continue;
            switch (ch) { case '(' : paren++; break; case ')' : paren--; if (paren<0){ syntaxErrors.emplace_back(static_cast<int>(ln+1), static_cast<int>(c+1), "Unmatched closing parenthesis"); paren=0;} break; case '{': brace++; break; case '}': brace--; if (brace<0){ syntaxErrors.emplace_back(static_cast<int>(ln+1), static_cast<int>(c+1), "Unmatched closing brace"); brace=0;} break; case '[': bracket++; break; case ']': bracket--; if (bracket<0){ syntaxErrors.emplace_back(static_cast<int>(ln+1), static_cast<int>(c+1), "Unmatched closing bracket"); bracket=0;} break; }
        }
    }
    if (paren>0) syntaxErrors.emplace_back(static_cast<int>(editorState.lines.size()), 1, "Unclosed parenthesis");
    if (brace>0) syntaxErrors.emplace_back(static_cast<int>(editorState.lines.size()), 1, "Unclosed brace");
    if (bracket>0) syntaxErrors.emplace_back(static_cast<int>(editorState.lines.size()), 1, "Unclosed bracket");
}

// --- Tab management ---
int LuaEditor::findTabByPath(const std::filesystem::path &path)
{
    for (size_t i=0;i<tabs.size();++i) if (tabs[i].filePath==path) return static_cast<int>(i);
    return -1;
}

void LuaEditor::openTab(const std::filesystem::path &path)
{
    // forward to openFile which handles switching to existing tab or creating a new one
    openFile(path);
}

void LuaEditor::closeTab(int tabIndex)
{
    if (tabIndex<0 || tabIndex>=static_cast<int>(tabs.size())) return;
    tabs.erase(tabs.begin()+tabIndex);
    if (tabs.empty()) activeTabIndex=-1;
    else if (activeTabIndex>=tabs.size()) activeTabIndex = static_cast<int>(tabs.size())-1;
    else if (activeTabIndex>tabIndex) activeTabIndex--;
    if (activeTabIndex>=0 && activeTabIndex<tabs.size()) tabs[activeTabIndex].isActive=true;
}

void LuaEditor::setActiveTab(int tabIndex)
{
    if (tabIndex<0 || tabIndex>=static_cast<int>(tabs.size())) return;
    syncEditorToActiveTab();
    for (auto &t: tabs) t.isActive=false;
    activeTabIndex = tabIndex; tabs[activeTabIndex].isActive = true; syncActiveTabToEditor();
}

void LuaEditor::syncActiveTabToEditor()
{
    EditorTab *t = getActiveTab(); if (t) { editorState = t->editorState; syntaxErrors = t->syntaxErrors; }
}

void LuaEditor::syncEditorToActiveTab()
{
    EditorTab *t = getActiveTab(); if (t) { t->editorState = editorState; t->syntaxErrors = syntaxErrors; if (t->editorState.isDirty) { if (!t->displayName.empty() && t->displayName.back()!='*') t->displayName += "*"; } else { if (!t->displayName.empty() && t->displayName.back()=='*') t->displayName.pop_back(); } }
}

// --- Rendering helpers adapted from JavaEditor ---

void LuaEditor::drawLiveConsole()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGui::Text("Live Lua Console"); ImGui::SameLine(); ImGui::TextColored(ImVec4(0.7f,0.7f,0.7f,1.0f),"(Enter Lua expressions to execute)"); ImGui::Separator();
    ImGui::Text("Output:"); ImGui::BeginChild("LiveConsoleOutput", ImVec2(0, -ImGui::GetTextLineHeightWithSpacing()*4), true, ImGuiWindowFlags_HorizontalScrollbar);
    if (!liveConsoleOutput.empty()) ImGui::TextWrapped("%s", liveConsoleOutput.c_str()); else { ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150,150,150,255)); ImGui::Text("Enter Lua expressions to see results..."); ImGui::PopStyleColor(); }
    if (ImGui::GetScrollY()>=ImGui::GetScrollMaxY()) ImGui::SetScrollHereY(1.0f);
    ImGui::EndChild();
    ImGui::Text("Lua Expression:"); ImGui::PushItemWidth(-180);
    bool inputChanged = ImGui::InputTextMultiline("##LiveConsoleInput", &liveConsoleInput, ImVec2(0, ImGui::GetTextLineHeightWithSpacing()*2), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::BeginGroup(); bool shouldExecute = (ImGui::Button("Execute", ImVec2(80,0)) || inputChanged) && !liveConsoleInput.empty(); if (shouldExecute) { std::string result = executeLuaCode(liveConsoleInput); liveConsoleOutput += "Input: "+liveConsoleInput+"\n"; liveConsoleOutput += "Result: "+result+"\n\n"; liveConsoleHistory.push_back(liveConsoleInput); historyIndex = liveConsoleHistory.size(); liveConsoleInput.clear(); } if (ImGui::Button("Clear", ImVec2(80,0))) { liveConsoleOutput.clear(); liveConsoleInput.clear(); } ImGui::EndGroup(); if (!liveConsoleHistory.empty()) ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.0f), "Use Up/Down arrows to navigate history (%zu items)", liveConsoleHistory.size()); if (ImGui::IsItemFocused() && !liveConsoleHistory.empty()) { if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && historyIndex>0) { historyIndex--; liveConsoleInput = liveConsoleHistory[historyIndex]; } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && historyIndex < static_cast<int>(liveConsoleHistory.size())-1) { historyIndex++; liveConsoleInput = liveConsoleHistory[historyIndex]; } }
}

std::string LuaEditor::executeLuaCode(const std::string &code)
{
    // For now, just echo the input as a stubbed result
    return std::string("(eval) ") + code;
}

std::string LuaEditor::convertLuaResultToJson(const std::string &result)
{
    std::ostringstream ss; ss << "{\"result\": \"" << result << "\"}"; return ss.str();
}

// Simple syntax highlighting and drawing functions
void LuaEditor::drawLineNumbers(ImDrawList *drawList, const ImVec2 &origin, float visibleHeight)
{
    ImFont *font = ImGui::GetFont();
    float baselineOffset = (lineHeight - renderFontSize) * 0.5f;
    float y = origin.y - editorState.scrollY + baselineOffset;
    size_t startLine = static_cast<size_t>(std::max(0.0f, editorState.scrollY / lineHeight));
    size_t endLine = std::min(editorState.lines.size(), startLine + static_cast<size_t>(visibleHeight / lineHeight) + 2);
    for (size_t ln = startLine; ln < endLine; ++ln)
    {
        float lineY = y + ln * lineHeight;
        if (lineY > origin.y + visibleHeight) break;
        if (lineY + lineHeight < origin.y) continue;
        std::string num = std::to_string(ln+1);
        drawList->AddText(font, renderFontSize, ImVec2(origin.x + 5, lineY), lineNumberColor, num.c_str());
    }
}

void LuaEditor::drawTextContent(ImDrawList *drawList, const ImVec2 &origin, float visibleHeight)
{
    drawTextContentWithSyntaxHighlighting(drawList, origin, visibleHeight);
}

void LuaEditor::drawTextContentWithSyntaxHighlighting(ImDrawList *drawList, const ImVec2 &origin, float visibleHeight)
{
    static const std::unordered_set<std::string> luaKeywords = {"and","break","do","else","elseif","end","false","for","function","if","in","local","nil","not","or","repeat","return","then","true","until","while"};
    ImFont *font = ImGui::GetFont(); if (!font) return;
    float baselineOffset = (lineHeight - renderFontSize) * 0.5f;
    float y = origin.y - editorState.scrollY + baselineOffset;
    size_t startLine = static_cast<size_t>(std::max(0.0f, editorState.scrollY / lineHeight));
    size_t endLine = std::min(editorState.lines.size(), startLine + static_cast<size_t>(visibleHeight / lineHeight) + 2);
    for (size_t ln = startLine; ln < endLine; ++ln)
    {
        float lineY = y + ln * lineHeight; if (lineY > origin.y + visibleHeight) break; if (lineY + lineHeight < origin.y) continue;
        const std::string &line = editorState.lines[ln]; if (line.empty()) continue;
        float x = origin.x; size_t pos = 0;
        while (pos < line.length())
        {
            std::string token; ImU32 color = textColor;
            if (std::isspace(line[pos])) { size_t s = pos; while (s<line.length() && std::isspace(line[s])) s++; token = line.substr(pos, s-pos); color = textColor; pos = s; }
            else if (line[pos] == '"' || line[pos]=='\'') { char q=line[pos]; size_t s=pos+1; bool esc=false; while (s<line.length()) { if (!esc && line[s]==q) { s++; break; } esc = (line[s]=='\\' && !esc); s++; } token = line.substr(pos, s-pos); color = stringColor; pos = s; }
            else if (std::isdigit(line[pos])) { size_t s=pos; while (s<line.length() && (std::isdigit(line[s])||line[s]=='.')) s++; token=line.substr(pos,s-pos); color=numberColor; pos=s; }
            else if (std::isalpha(line[pos]) || line[pos]=='_') { size_t s=pos; while (s<line.length() && (std::isalnum(line[s])||line[s]=='_')) s++; token=line.substr(pos,s-pos); if (luaKeywords.count(token)) color = keywordColor; else color = textColor; pos=s; }
            else { token.push_back(line[pos]); color = operatorColor; pos++; }
            if (!token.empty()) {
                const char *b = token.c_str(); const char *e = b + token.size();
                drawList->AddText(font, renderFontSize, ImVec2(x, lineY), color, b, e);
                x += font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f, b, e).x;
            }
        }
    }
}

void LuaEditor::drawCursor(ImDrawList *drawList, const ImVec2 &origin)
{
    ImVec2 p = getCursorScreenPos(origin);
    drawList->AddLine(p, ImVec2(p.x, p.y + lineHeight), cursorColor, 2.0f);
}

void LuaEditor::drawSelection(ImDrawList *drawList, const ImVec2 &origin)
{
    if (!editorState.hasSelection) return;
    size_t startLine = std::min(editorState.selectionStartLine, editorState.selectionEndLine);
    size_t endLine = std::max(editorState.selectionStartLine, editorState.selectionEndLine);
    size_t startCol = editorState.selectionStartLine < editorState.selectionEndLine ? editorState.selectionStartColumn : editorState.selectionEndColumn;
    size_t endCol = editorState.selectionStartLine < editorState.selectionEndLine ? editorState.selectionEndColumn : editorState.selectionStartColumn;
    ImFont *font = ImGui::GetFont();
    float baselineOffset = (lineHeight - renderFontSize) * 0.5f;
    for (size_t ln = startLine; ln <= endLine; ++ln)
    {
        if (ln >= editorState.lines.size()) break;
        float lineY = origin.y + ln * lineHeight - editorState.scrollY + baselineOffset;
        size_t ls = (ln==startLine)?startCol:0; size_t le=(ln==endLine)?endCol:editorState.lines[ln].length();
        float startX = origin.x; float endX = origin.x;
        if (font)
        {
            if (ls>0) startX += font->CalcTextSizeA(renderFontSize, FLT_MAX,0.0f, editorState.lines[ln].substr(0, ls).c_str()).x;
            if (le>0) endX += font->CalcTextSizeA(renderFontSize, FLT_MAX,0.0f, editorState.lines[ln].substr(0, le).c_str()).x;
        }
        else { startX += ls*charWidth; endX += le*charWidth; }
        drawList->AddRectFilled(ImVec2(startX, lineY), ImVec2(endX, lineY+lineHeight), selectionColor);
    }
}

void LuaEditor::drawSyntaxErrors(ImDrawList *drawList, const ImVec2 &origin)
{
    ImFont *font = ImGui::GetFont();
    float baselineOffset = (lineHeight - renderFontSize) * 0.5f;
    for (const auto &err : syntaxErrors)
    {
        size_t ln = err.getLine()-1; if (ln>=editorState.lines.size()) continue;
        const std::string &line = editorState.lines[ln]; float lineY = origin.y + ln*lineHeight - editorState.scrollY + baselineOffset + lineHeight - 2;
        float startX = origin.x; if (font && err.getColumn()>1 && err.getColumn()-1<=line.length()) startX += font->CalcTextSizeA(renderFontSize,FLT_MAX,0.0f,line.substr(0, err.getColumn()-1).c_str()).x; else startX += (err.getColumn()-1)*charWidth;
        float endX = startX + charWidth*5; for (float x = startX; x<endX; x+=4.0f) { float wave = sin((x-startX)*0.5f)*2.0f; drawList->AddLine(ImVec2(x, lineY+wave), ImVec2(x+2, lineY-wave), errorColor, 1.0f); }
    }
}

void LuaEditor::drawCompletionPopup(const ImVec2 &textOrigin)
{
    if (completionItems.empty()) return;
    ImVec2 cursor = getCursorScreenPos(textOrigin);
    ImVec2 pos = ImVec2(cursor.x, cursor.y + lineHeight);
    ImGui::SetNextWindowPos(pos); ImGui::SetNextWindowSize(ImVec2(300, std::min(200.0f, completionItems.size()*20.0f)));
    if (ImGui::Begin("##LuaCompletions", nullptr, ImGuiWindowFlags_NoTitleBar|ImGuiWindowFlags_NoResize|ImGuiWindowFlags_NoMove))
    {
        for (size_t i=0;i<completionItems.size();++i)
        {
            bool sel = (i==selectedCompletion);
            if (sel) { ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255,255,255,255)); ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(100,150,200,255)); }
            if (ImGui::Selectable(completionItems[i].text.c_str(), sel))
            {
                selectedCompletion = i; const std::string &line = editorState.lines[editorState.cursorLine]; size_t ws = editorState.cursorColumn; while (ws>0 && (std::isalnum(line[ws-1])||line[ws-1]=='_')) ws--; std::string &cur = editorState.lines[editorState.cursorLine]; cur.erase(ws, editorState.cursorColumn-ws); cur.insert(ws, completionItems[i].text); editorState.cursorColumn = ws + completionItems[i].text.length(); editorState.isDirty = true; showCompletions = false;
            }
            if (sel) ImGui::PopStyleColor(2);
            ImGui::SameLine(); ImGui::TextDisabled(" - %s", completionItems[i].description.c_str());
        }
    }
    ImGui::End();
}

ImVec2 LuaEditor::getCursorScreenPos(const ImVec2 &origin) const
{
    float x = origin.x;
    if (editorState.cursorLine < editorState.lines.size()) {
        const std::string &line = editorState.lines[editorState.cursorLine];
        if (editorState.cursorColumn>0 && editorState.cursorColumn<=line.length()) {
            std::string to = line.substr(0, editorState.cursorColumn);
            ImFont *font = ImGui::GetFont();
            if (font) x += font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f, to.c_str()).x;
            else x += editorState.cursorColumn*charWidth;
        }
    }
    ImFont *font = ImGui::GetFont();
    float baselineOffset = (lineHeight - renderFontSize) * 0.5f;
    float y = origin.y + editorState.cursorLine*lineHeight - editorState.scrollY + baselineOffset;
    return ImVec2(x,y);
}

void LuaEditor::ensureCursorVisible()
{
    float cursorY = editorState.cursorLine * lineHeight; float margin = lineHeight*0.5f;
    if (cursorY < editorState.scrollY) { editorState.scrollY = std::max(0.0f, cursorY - margin); }
    else if (cursorY + lineHeight > editorState.scrollY + visibleHeight) { editorState.scrollY = std::max(0.0f, cursorY + lineHeight - visibleHeight + margin); }
}

bool LuaEditor::isCursorVisible() const
{
    float cursorY = editorState.cursorLine * lineHeight; float margin = lineHeight*2.0f; return (cursorY >= editorState.scrollY - margin && cursorY + lineHeight <= editorState.scrollY + visibleHeight + margin);
}

void LuaEditor::scrollToCursorIfNeeded()
{
    if (!isCursorVisible()) editorState.needsScrollToCursor = true;
}

// Stubs for program-structure related functions
void LuaEditor::updateLiveProgramStructure() { /* minimal: update current file in programStructure if needed */ }
void LuaEditor::updateLiveProgramStructureForAllTabs() { /* minimal placeholder */ }
bool LuaEditor::isInClassContext(size_t, size_t) const { return false; }
void LuaEditor::generateContextAwareCompletions(const std::string &prefix, bool isQualifiedAccess, const std::string &objectName)
{
    (void)isQualifiedAccess; (void)objectName;
    // simple keyword completions
    static const std::vector<std::string> kws = {"function","local","if","then","else","end","for","in","pairs","ipairs","return","require","while","repeat","until","true","false","nil"};
    for (const auto &k: kws)
        if (k.find(prefix) == 0)
        {
            CompletionItem it; it.text = k; it.description = "Lua keyword"; it.type = CompletionItem::KEYWORD; completionItems.push_back(it);
        }

    // Augment completions with registered binding globals by creating a temp LuaEngine,
    // registering bindings (ImGui and vault if available), and enumerating globals.
    try {
        LuaEngine eng;
        lua_State *L = eng.L();
        if (L) {
            // Register ImGui bindings to expose UI helper functions
            registerLuaImGuiBindings(L);
            // Also register canvas bindings with a default dummy region so canvas helpers
            // (e.g., drawing helpers) are available for completion discovery.
            registerLuaCanvasBindings(L, ImVec2(0,0), 300, 200);

            // If the file backend is a VaultFileBackend, register vault bindings too
            if (fileBackend)
            {
                auto vb = dynamic_cast<VaultFileBackend*>(fileBackend.get());
                if (vb)
                {
                    auto vaultPtr = vb->getVault();
                    if (vaultPtr)
                        registerLuaVaultBindings(L, vaultPtr.get());
                }
            }

            // If this is a qualified access (e.g. `canvas.draw` where objectName == "canvas" and prefix == "draw"),
            // enumerate members of that table instead of top-level globals.
            if (isQualifiedAccess && !objectName.empty())
            {
                // get global by name
                lua_getglobal(L, objectName.c_str());
                if (lua_istable(L, -1))
                {
                    lua_pushnil(L);
                    while (lua_next(L, -2) != 0)
                    {
                        // key at -2, value at -1
                        if (lua_type(L, -2) == LUA_TSTRING)
                        {
                            const char *mname = lua_tostring(L, -2);
                            if (mname && mname[0] != '_')
                            {
                                std::string sname(mname);
                                if (sname.rfind(prefix, 0) == 0)
                                {
                                    CompletionItem it; it.text = sname;
                                    int vtype = lua_type(L, -1);
                                    if (vtype == LUA_TFUNCTION) { it.type = CompletionItem::METHOD; it.description = "binding method"; }
                                    else { it.type = CompletionItem::VARIABLE; it.description = "binding field"; }
                                    completionItems.push_back(it);
                                }
                            }
                        }
                        lua_pop(L, 1); // pop value, keep key
                    }
                }
                lua_pop(L, 1); // pop the object
            }
            else
            {
                // Iterate globals for completion candidates
                lua_pushglobaltable(L);
                lua_pushnil(L);
                while (lua_next(L, -2) != 0)
                {
                    // key at -2, value at -1
                    if (lua_type(L, -2) == LUA_TSTRING)
                    {
                        const char *name = lua_tostring(L, -2);
                        if (name && name[0] != '_') // skip internal names
                        {
                            // only consider names that match the prefix
                            std::string sname(name);
                            if (sname.rfind(prefix, 0) == 0)
                            {
                                CompletionItem it;
                                it.text = sname;
                                int vtype = lua_type(L, -1);
                                if (vtype == LUA_TFUNCTION) { it.type = CompletionItem::METHOD; it.description = "binding function"; }
                                else { it.type = CompletionItem::VARIABLE; it.description = "binding"; }
                                completionItems.push_back(it);
                            }
                        }
                    }
                    lua_pop(L, 1); // pop value, keep key
                }
                lua_pop(L, 1); // pop global table
            }
        }
    }
    catch (...) { /* best-effort: ignore lua completion failures */ }
}



