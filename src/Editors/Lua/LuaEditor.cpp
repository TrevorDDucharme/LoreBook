#include <Editors/Lua/LuaEditor.hpp>
#include <imgui.h>
#include <FileBackends/LocalFileBackend.hpp>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <unordered_set>
#include <map>
#include <set>

#include <LuaEngine.hpp>
#include <LuaVaultBindings.hpp>
#include <LuaImGuiBindings.hpp>
#include <LuaFSBindings.hpp>
#include <FileBackends/VaultFileBackend.hpp>
#include <LuaCanvasBindings.hpp>
#include <LuaBindingDocs.hpp>
#include <imgui_internal.h>

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
    float baseTextX = origin.x + gutterWidth + 8.0f;
    // compute visible text area width and apply horizontal scroll offset
    visibleWidth = std::max(0.0f, avail.x - gutterWidth - 8.0f);
    ImVec2 textOrigin = ImVec2(baseTextX - editorState.scrollX, origin.y + 4.0f);

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

    // Ctrl+wheel: change editor scale; Shift+wheel: horizontal scroll; otherwise wheel scrolls vertically.
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
        else if (io.KeyShift)
        {
            // Horizontal scroll (Shift + wheel)
            editorState.scrollX = std::max(0.0f, editorState.scrollX - io.MouseWheel * charWidth * 8.0f);
            // Clamp to content width so we don't overscroll
            ImFont *font = ImGui::GetFont();
            float maxWidth = 0.0f;
            for (const auto &l : editorState.lines)
            {
                float w = font ? font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f, l.c_str()).x : l.length() * charWidth;
                if (w > maxWidth) maxWidth = w;
            }
            float maxScroll = std::max(0.0f, maxWidth - visibleWidth + charWidth * 4.0f);
            if (editorState.scrollX > maxScroll) editorState.scrollX = maxScroll;
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

    // small API docs toggle
    ImGui::SameLine(); ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 120);
    if (ImGui::Button("API Docs", ImVec2(100, 0))) showDocViewer = !showDocViewer;

    // Completion popup
    drawCompletionPopup(textOrigin);

    // show docs viewer if requested
    if (showDocViewer) drawClassInvestigator();

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

    ImGui::SameLine(); ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 260);
    if (ImGui::Button("Show API Docs", ImVec2(120, 0))) showDocViewer = !showDocViewer;
    if (showDocViewer) drawClassInvestigator();

    ImGui::Text("Lua Expression:"); ImGui::PushItemWidth(-180);
    bool inputChanged = ImGui::InputTextMultiline("##LiveConsoleInput", &liveConsoleInput, ImVec2(0, ImGui::GetTextLineHeightWithSpacing()*2), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth(); ImGui::SameLine(); ImGui::BeginGroup(); bool shouldExecute = (ImGui::Button("Execute", ImVec2(80,0)) || inputChanged) && !liveConsoleInput.empty(); if (shouldExecute) { std::string result = executeLuaCode(liveConsoleInput); liveConsoleOutput += "Input: "+liveConsoleInput+"\n"; liveConsoleOutput += "Result: "+result+"\n\n"; liveConsoleHistory.push_back(liveConsoleInput); historyIndex = liveConsoleHistory.size(); liveConsoleInput.clear(); } if (ImGui::Button("Clear", ImVec2(80,0))) { liveConsoleOutput.clear(); liveConsoleInput.clear(); } ImGui::EndGroup(); if (!liveConsoleHistory.empty()) ImGui::TextColored(ImVec4(0.6f,0.6f,0.6f,1.0f), "Use Up/Down arrows to navigate history (%zu items)", liveConsoleHistory.size()); if (ImGui::IsItemFocused() && !liveConsoleHistory.empty()) { if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && historyIndex>0) { historyIndex--; liveConsoleInput = liveConsoleHistory[historyIndex]; } else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && historyIndex < static_cast<int>(liveConsoleHistory.size())-1) { historyIndex++; liveConsoleInput = liveConsoleHistory[historyIndex]; } }
}

std::string LuaEditor::executeLuaCode(const std::string &code)
{
    EditorTab *t = getActiveTab();
    if (!t) return std::string("(no active tab)");

    // Gather current editor text
    std::string src;
    for (size_t i = 0; i < t->editorState.lines.size(); ++i) { src += t->editorState.lines[i]; if (i + 1 < t->editorState.lines.size()) src += '\n'; }

    // Create or reload per-tab engine when missing or file changed
    if (!t->liveEngine || t->editorState.isDirty)
    {
        t->liveEngine = std::make_unique<LuaEngine>();
        // Register bindings BEFORE loading the script, since top-level code may reference os, vault, etc.
        lua_State *L = t->liveEngine->L();
        Vault *fsVault = nullptr;
        if (L)
        {
            registerLuaImGuiBindings(L);
            if (fileBackend)
            {
                auto vb = dynamic_cast<VaultFileBackend *>(fileBackend.get());
                if (vb)
                {
                    auto vaultPtr = vb->getVault();
                    if (vaultPtr)
                    {
                        registerLuaVaultBindings(L, vaultPtr.get());
                        fsVault = vaultPtr.get();
                    }
                }
            }
            registerLuaFSBindings(L, fsVault);
        }
        if (!t->liveEngine->loadScript(src))
        {
            std::string err = t->liveEngine->lastError();
            return std::string("(load error) ") + err;
        }
    }

    LuaEngine *eng = t->liveEngine.get();
    lua_State *L = eng ? eng->L() : nullptr;
    if (!L) return std::string("(no lua state)");

    // Ensure vault bindings are present in the live engine if a vault is available
    lua_getglobal(L, "vault");
    bool hasVaultGlobal = !lua_isnil(L, -1);
    lua_pop(L, 1);
    if (!hasVaultGlobal && fileBackend) {
        auto vb = dynamic_cast<VaultFileBackend *>(fileBackend.get());
        if (vb) {
            auto vaultPtr = vb->getVault();
            if (vaultPtr) registerLuaVaultBindings(L, vaultPtr.get());
        }
    }

    // Try to evaluate as expression: 'return <code>' first
    std::string expr = std::string("return ") + code;
    lua_settop(L, 0);
    if (luaL_loadstring(L, expr.c_str()) != LUA_OK)
    {
        // If expression load failed, try as statement
        lua_settop(L, 0);
        if (luaL_loadstring(L, code.c_str()) != LUA_OK)
        {
            const char *msg = lua_tostring(L, -1);
            std::string err = msg ? msg : "(compile error)";
            lua_pop(L, 1);
            return std::string("(compile error) ") + err;
        }
            // Run statement
        if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK)
        {
            const char *msg = lua_tostring(L, -1);
            std::string err = msg ? msg : "(runtime error)";
            lua_pop(L, 1);
            std::string printed = eng->takeStdout();
            if (!printed.empty()) return printed + std::string("(runtime error) ") + err;
            return std::string("(runtime error) ") + err;
        }
        {
            std::string printed = eng->takeStdout();
            if (!printed.empty()) return printed;
        }
        return std::string("(ok)");
    }
    // Run expression
    if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK)
    {
        const char *msg = lua_tostring(L, -1);
        std::string err = msg ? msg : "(runtime error)";
        lua_pop(L, 1);
        std::string printed = eng->takeStdout();
        if (!printed.empty()) return printed + std::string("(runtime error) ") + err;
        return std::string("(runtime error) ") + err;
    }

    std::string printed = eng->takeStdout();
    int n = lua_gettop(L);
    std::ostringstream ss;
    for (int i = 1; i <= n; ++i)
    {
        const char *s = luaL_tolstring(L, i, nullptr);
        if (s) ss << s;
        else ss << "(non-string)";
        if (i < n) ss << "\t";
        lua_pop(L, 1); // pop string pushed by luaL_tolstring
    }
    lua_settop(L, 0);
    std::string result = ss.str();
    if (!printed.empty() && (result.empty() || result == "nil")) return printed;
    if (!printed.empty()) return printed + "=> " + result;
    if (result.empty()) return std::string("(nil)");
    return result;
}

std::string LuaEditor::convertLuaResultToJson(const std::string &result)
{
    std::ostringstream ss; ss << "{\"result\": \"" << result << "\"}"; return ss.str();
}

// Run an example snippet either in the active tab's engine or in a temporary engine.
// Returns the captured output or any errors.// Run an example snippet either in the active tab's engine or in a temporary engine.
// Returns the captured output or any errors.
std::string LuaEditor::runExampleSnippet(const std::string &code)
{
    // If we have an active tab with a live engine, reuse that environment
    EditorTab *t = getActiveTab();
    if (t && t->liveEngine && !t->editorState.isDirty) {
        // Use executeLuaCode which uses the active tab engine
        return executeLuaCode(code);
    }

    // No active tab or no live engine: create a temporary engine and register bindings
    try {
        LuaEngine eng;
        lua_State *L = eng.L();
        if (!L) return std::string("(no lua state)");

        // Register common bindings so examples can run
        registerLuaImGuiBindings(L);
        registerLuaCanvasBindings(L, ImVec2(0,0), 320, 240);

        // If file backend is a VaultFileBackend, also register vault bindings so examples can access vault (and possibly modify it)
        Vault *fsVault2 = nullptr;
        if (fileBackend) {
            auto vb = dynamic_cast<VaultFileBackend*>(fileBackend.get());
            if (vb) {
                auto vaultPtr = vb->getVault();
                if (vaultPtr) {
                    registerLuaVaultBindings(L, vaultPtr.get());
                    fsVault2 = vaultPtr.get();
                }
            }
        }
        registerLuaFSBindings(L, fsVault2);

        // Snapshot ImGui state so we can restore on errors (save CurrentWindow only)
        ImGuiErrorRecoveryState errState;
        ImGui::ErrorRecoveryStoreState(&errState);
        // disable asserts while running user scripts; we will recover state explicitly on error
        ImGuiIO &io = ImGui::GetIO();
        bool oldAssert = io.ConfigErrorRecoveryEnableAssert;
        bool oldTooltip = io.ConfigErrorRecoveryEnableTooltip;
        io.ConfigErrorRecoveryEnableAssert = false;
        io.ConfigErrorRecoveryEnableTooltip = true;

        // Evaluate the snippet (first try as expression using `return`, otherwise as statement)
        std::string expr = std::string("return ") + code;
        lua_settop(L, 0);
        if (luaL_loadstring(L, expr.c_str()) != LUA_OK)
        {
            // try as statement
            lua_settop(L, 0);
            if (luaL_loadstring(L, code.c_str()) != LUA_OK)
            {
                const char *msg = lua_tostring(L, -1);
                std::string err = msg ? msg : "(compile error)";
                lua_pop(L, 1);
                ImGui::ErrorRecoveryTryToRecoverState(&errState);
                ImGui::GetIO().ConfigErrorRecoveryEnableAssert = oldAssert;
                ImGui::GetIO().ConfigErrorRecoveryEnableTooltip = oldTooltip;
                return std::string("(compile error) ") + err;
            }
            if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK)
            {
                const char *msg = lua_tostring(L, -1);
                std::string err = msg ? msg : "(runtime error)";
                lua_pop(L, 1);
                std::string printed = eng.takeStdout();
                ImGui::ErrorRecoveryTryToRecoverState(&errState);
                ImGui::GetIO().ConfigErrorRecoveryEnableAssert = oldAssert;
                ImGui::GetIO().ConfigErrorRecoveryEnableTooltip = oldTooltip;
                if (!printed.empty()) return printed + std::string("(runtime error) ") + err;
                return std::string("(runtime error) ") + err;
            }
            std::string printed = eng.takeStdout();
            ImGui::ErrorRecoveryTryToRecoverState(&errState);
            ImGui::GetIO().ConfigErrorRecoveryEnableAssert = oldAssert;
            ImGui::GetIO().ConfigErrorRecoveryEnableTooltip = oldTooltip;
            if (!printed.empty()) return printed;
            return std::string("(ok)");
        }
        if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK)
        {
            const char *msg = lua_tostring(L, -1);
            std::string err = msg ? msg : "(runtime error)";
            lua_pop(L, 1);
            std::string printed = eng.takeStdout();
            if (!printed.empty()) return printed + std::string("(runtime error) ") + err;
            return std::string("(runtime error) ") + err;
        }

        std::string printed = eng.takeStdout();
        int n = lua_gettop(L);
        std::ostringstream ss;
        for (int i = 1; i <= n; ++i)
        {
            const char *s = luaL_tolstring(L, i, nullptr);
            if (s) ss << s;
            else ss << "(non-string)";
            if (i < n) ss << "\t";
            lua_pop(L, 1);
        }
        lua_settop(L, 0);
        std::string result = ss.str();
        if (!printed.empty() && (result.empty() || result == "nil")) return printed;
        if (!printed.empty()) return printed + "=> " + result;
        if (result.empty()) return std::string("(nil)");
        return result;
    }
    catch (const std::exception &ex) {
        return std::string("(exception) ") + ex.what();
    }
}

// Detect whether the snippet provides a Config() and return the parsed ScriptConfig
ScriptConfig LuaEditor::detectSnippetConfig(const std::string &code)
{
    ScriptConfig out;
    try {
        LuaEngine eng;
        // register minimal bindings so user config calls like canvas.width work if invoked during Config()
        lua_State *L = eng.L();
        if (!L) return out;
        registerLuaCanvasBindings(L, ImVec2(0,0), 320, 240);
        registerLuaImGuiBindings(L);
        Vault *cfgVault = nullptr;
        if (fileBackend) {
            auto vb = dynamic_cast<VaultFileBackend*>(fileBackend.get());
            if (vb) {
                auto vaultPtr = vb->getVault();
                if (vaultPtr) {
                    registerLuaVaultBindings(L, vaultPtr.get());
                    cfgVault = vaultPtr.get();
                }
            }
        }
        registerLuaFSBindings(L, cfgVault);
        if (!eng.loadScript(code)) return out;
        out = eng.callConfig();
        return out;
    }
    catch (...) { }
    return out;
}

// Start a persistent preview engine that runs every frame
bool LuaEditor::startPreview(const std::string &code, ImVec2 origin, int width, int height, ScriptConfig *outConfig)
{
    stopPreview();
    try {
        previewEngine = std::make_unique<LuaEngine>();
        lua_State *L = previewEngine->L();
        if (!L) { stopPreview(); return false; }

        // Register bindings (initially with provided dims)
        registerLuaCanvasBindings(L, origin, width, height);
        registerLuaImGuiBindings(L);
        Vault *pvVault = nullptr;
        if (fileBackend) {
            auto vb = dynamic_cast<VaultFileBackend*>(fileBackend.get());
            if (vb) {
                auto vaultPtr = vb->getVault();
                if (vaultPtr) {
                    registerLuaVaultBindings(L, vaultPtr.get());
                    pvVault = vaultPtr.get();
                }
            }
        }
        registerLuaFSBindings(L, pvVault);

        if (!previewEngine->loadScript(code)) {
            previewOutput = previewEngine->lastError();
            stopPreview();
            return false;
        }

        // Call Config() if present
        ScriptConfig cfg = previewEngine->callConfig();
        if (cfg.type != ScriptConfig::Type::None) {
            previewType = (cfg.type == ScriptConfig::Type::Canvas) ? PreviewType::Canvas : PreviewType::UI;
            if (cfg.width > 0) width = cfg.width;
            if (cfg.height > 0) height = cfg.height;
            // re-register canvas with exact config dims
            registerLuaCanvasBindings(L, origin, width, height);
        } else {
            previewType = PreviewType::Other;
        }

        previewOrigin = origin;
        previewWidth = width;
        previewHeight = height;
        previewRunning = true;
        previewLastTime = ImGui::GetTime();

        if (outConfig) *outConfig = cfg;
        previewOutput.clear();
        return true;
    }
    catch (const std::exception &ex) { previewOutput = std::string("(exception) ") + ex.what(); stopPreview(); return false; }
}

void LuaEditor::stopPreview()
{
    previewEngine.reset();
    previewRunning = false;
    previewType = PreviewType::None;
    previewOutput.clear();
}

void LuaEditor::previewTick()
{
    if (!previewRunning || !previewEngine) return;
    try {
        double now = ImGui::GetTime();
        double dt = now - previewLastTime; if (dt < 0.0) dt = 0.016;
        previewLastTime = now;
        lua_State *L = previewEngine->L();
        if (!L) { stopPreview(); return; }

        if (previewType == PreviewType::Canvas) {
            // Render canvas via FBO and display the texture in ImGui
            unsigned int texID = previewEngine->renderCanvasFrame("preview", previewWidth, previewHeight, (float)dt);
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImGui::Image((ImTextureID)(intptr_t)texID, avail);
        } else if (previewType == PreviewType::UI) {
            // For UI preview, open a local ImGui area; the UI() function will call ui.* bindings
            previewEngine->callUI();
        } else {
            // For 'other' types we just execute once and stop
            std::string out = runPreviewSnippet(previewCode, previewOrigin, previewWidth, previewHeight);
            previewOutput = out;
            stopPreview();
            return;
        }

        std::string printed = previewEngine->takeStdout();
        if (!printed.empty()) previewOutput = printed;
    }
    catch (const std::exception &ex) { previewOutput = std::string("(exception) ") + ex.what(); stopPreview(); }
}
// Run a preview snippet inside a given origin / area so that canvas drawing happens inside the preview child
std::string LuaEditor::runPreviewSnippet(const std::string &code, ImVec2 origin, int width, int height)
{
    // For one-shot previews we use a temporary engine that draws into the provided child origin
    try {
        LuaEngine eng;
        lua_State *L = eng.L();
        if (!L) return std::string("(no lua state)");

        // Register bindings with actual origin so draw calls appear at the right place
        registerLuaCanvasBindings(L, origin, width, height);
        registerLuaImGuiBindings(L);

        Vault *snippetVault = nullptr;
        if (fileBackend) {
            auto vb = dynamic_cast<VaultFileBackend*>(fileBackend.get());
            if (vb) {
                auto vaultPtr = vb->getVault();
                if (vaultPtr) {
                    registerLuaVaultBindings(L, vaultPtr.get());
                    snippetVault = vaultPtr.get();
                }
            }
        }
        registerLuaFSBindings(L, snippetVault);

        // Setup ImGui error-recovery state (so we can recover on errors) and temporarily disable asserts/tooltips
        ImGuiErrorRecoveryState errState;
        ImGui::ErrorRecoveryStoreState(&errState);
        ImGuiIO &io = ImGui::GetIO();
        bool oldAssert = io.ConfigErrorRecoveryEnableAssert;
        bool oldTooltip = io.ConfigErrorRecoveryEnableTooltip;
        io.ConfigErrorRecoveryEnableAssert = false;
        io.ConfigErrorRecoveryEnableTooltip = true;

        // Evaluate as expression then statement (one-shot)
        std::string expr = std::string("return ") + code;
        lua_settop(L, 0);
        if (luaL_loadstring(L, expr.c_str()) != LUA_OK)
        {
            lua_settop(L, 0);
            if (luaL_loadstring(L, code.c_str()) != LUA_OK)
            {
                const char *msg = lua_tostring(L, -1);
                std::string err = msg ? msg : "(compile error)";
                lua_pop(L, 1);
                ImGui::ErrorRecoveryTryToRecoverState(&errState);
                ImGui::GetIO().ConfigErrorRecoveryEnableAssert = oldAssert;
                ImGui::GetIO().ConfigErrorRecoveryEnableTooltip = oldTooltip;
                return std::string("(compile error) ") + err;
            }
            if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK)
            {
                const char *msg = lua_tostring(L, -1);
                std::string err = msg ? msg : "(runtime error)";
                lua_pop(L, 1);
                std::string printed = eng.takeStdout();
                ImGui::ErrorRecoveryTryToRecoverState(&errState);
                ImGui::GetIO().ConfigErrorRecoveryEnableAssert = oldAssert;
                ImGui::GetIO().ConfigErrorRecoveryEnableTooltip = oldTooltip;
                if (!printed.empty()) return printed + std::string("(runtime error) ") + err;
                return std::string("(runtime error) ") + err;
            }
            std::string printed = eng.takeStdout();
            ImGui::ErrorRecoveryTryToRecoverState(&errState);
            ImGui::GetIO().ConfigErrorRecoveryEnableAssert = oldAssert;
            ImGui::GetIO().ConfigErrorRecoveryEnableTooltip = oldTooltip;
            if (!printed.empty()) return printed;
            return std::string("(ok)");
        }
        if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK)
        {
            const char *msg = lua_tostring(L, -1);
            std::string err = msg ? msg : "(runtime error)";
            lua_pop(L, 1);
            std::string printed = eng.takeStdout();
            ImGui::ErrorRecoveryTryToRecoverState(&errState);
            ImGui::GetIO().ConfigErrorRecoveryEnableAssert = oldAssert;
            ImGui::GetIO().ConfigErrorRecoveryEnableTooltip = oldTooltip;
            if (!printed.empty()) return printed + std::string("(runtime error) ") + err;
            return std::string("(runtime error) ") + err;
        }

        std::string printed = eng.takeStdout();
        int n = lua_gettop(L);
        std::ostringstream ss;
        for (int i = 1; i <= n; ++i)
        {
            const char *s = luaL_tolstring(L, i, nullptr);
            if (s) ss << s;
            else ss << "(non-string)";
            if (i < n) ss << "\t";
            lua_pop(L, 1);
        }
        lua_settop(L, 0);
        std::string result = ss.str();
        if (!printed.empty() && (result.empty() || result == "nil")) return printed;
        if (!printed.empty()) return printed + "=> " + result;
        if (result.empty()) return std::string("(nil)");
        return result;
    }
    catch (const std::exception &ex) {
        return std::string("(exception) ") + ex.what();
    }
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

    bool inMultiComment = false; // track --[[ ... ]] across lines within this render

    for (size_t ln = startLine; ln < endLine; ++ln)
    {
        float lineY = y + ln * lineHeight; if (lineY > origin.y + visibleHeight) break; if (lineY + lineHeight < origin.y) continue;
        const std::string &line = editorState.lines[ln]; if (line.empty()) { inMultiComment = false; continue; }
        float x = origin.x; size_t pos = 0;

        // If we're inside a multi-line comment from previous line, handle it first
        if (inMultiComment)
        {
            size_t end = line.find("]]", pos);
            if (end == std::string::npos)
            {
                // whole line is comment
                drawList->AddText(font, renderFontSize, ImVec2(x, lineY), commentColor, line.c_str());
                // stay in multi-line comment
                continue;
            }
            else
            {
                std::string tok = line.substr(0, end + 2);
                drawList->AddText(font, renderFontSize, ImVec2(x, lineY), commentColor, tok.c_str());
                x += font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f, tok.c_str()).x;
                pos = end + 2;
                inMultiComment = false;
            }
        }

        while (pos < line.length())
        {
            // detect single-line '--' and multi-line '--[[' comments
            if (line[pos] == '-' && pos + 1 < line.length() && line[pos+1] == '-')
            {
                // check for '--[[' start
                if (pos + 3 < line.length() && line[pos+2] == '[' && line[pos+3] == '[')
                {
                    size_t end = line.find("]]", pos + 4);
                    if (end == std::string::npos)
                    {
                        std::string tok = line.substr(pos);
                        drawList->AddText(font, renderFontSize, ImVec2(x, lineY), commentColor, tok.c_str());
                        inMultiComment = true; // continues to next lines
                        break; // rest of line handled
                    }
                    else
                    {
                        std::string tok = line.substr(pos, end - pos + 2);
                        drawList->AddText(font, renderFontSize, ImVec2(x, lineY), commentColor, tok.c_str());
                        x += font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f, tok.c_str()).x;
                        pos = end + 2;
                        continue;
                    }
                }
                else
                {
                    // simple single-line comment; consume rest of line
                    std::string tok = line.substr(pos);
                    drawList->AddText(font, renderFontSize, ImVec2(x, lineY), commentColor, tok.c_str());
                    break;
                }
            }

            std::string token; ImU32 color = textColor;
            if (std::isspace((unsigned char)line[pos])) { size_t s = pos; while (s<line.length() && std::isspace((unsigned char)line[s])) s++; token = line.substr(pos, s-pos); color = textColor; pos = s; }
            else if (line[pos] == '"' || line[pos]=='\'') { char q=line[pos]; size_t s=pos+1; bool esc=false; while (s<line.length()) { if (!esc && line[s]==q) { s++; break; } esc = (line[s]=='\\' && !esc); s++; } token = line.substr(pos, s-pos); color = stringColor; pos = s; }
            else if (std::isdigit((unsigned char)line[pos])) { size_t s=pos; while (s<line.length() && (std::isdigit((unsigned char)line[s])||line[s]=='.')) s++; token=line.substr(pos,s-pos); color=numberColor; pos=s; }
            else if (std::isalpha((unsigned char)line[pos]) || line[pos]=='_') { size_t s=pos; while (s<line.length() && (std::isalnum((unsigned char)line[s])||line[s]=='_')) s++; token=line.substr(pos,s-pos); if (luaKeywords.count(token)) color = keywordColor; else color = textColor; pos=s; }
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

// API docs window controls (open/close/query/render)
void LuaEditor::openApiDocs() { showDocViewer = true; }
void LuaEditor::closeApiDocs() { showDocViewer = false; }
bool LuaEditor::isApiDocsOpen() const { return showDocViewer; }
void LuaEditor::renderApiDocsIfOpen() { if (showDocViewer) drawClassInvestigator(); }
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
    float cursorY = editorState.cursorLine * lineHeight;
    float margin = lineHeight * 0.5f;
    if (cursorY < editorState.scrollY)
        editorState.scrollY = std::max(0.0f, cursorY - margin);
    else if (cursorY + lineHeight > editorState.scrollY + visibleHeight)
        editorState.scrollY = std::max(0.0f, cursorY + lineHeight - visibleHeight + margin);

    // Horizontal visibility
    float cursorX = 0.0f;
    if (editorState.cursorLine < editorState.lines.size())
    {
        const std::string &line = editorState.lines[editorState.cursorLine];
        std::string to = line.substr(0, editorState.cursorColumn);
        ImFont *font = ImGui::GetFont();
        cursorX = font ? font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f, to.c_str()).x : editorState.cursorColumn * charWidth;
    }
    float marginX = charWidth * 4.0f;
    if (cursorX < editorState.scrollX + marginX)
        editorState.scrollX = std::max(0.0f, cursorX - marginX);
    else if (cursorX + charWidth > editorState.scrollX + visibleWidth - marginX)
    {
        float target = std::max(0.0f, cursorX + charWidth - visibleWidth + marginX);
        editorState.scrollX = target;
    }
}

bool LuaEditor::isCursorVisible() const
{
    float cursorY = editorState.cursorLine * lineHeight;
    float margin = lineHeight * 2.0f;
    bool verticallyVisible = (cursorY >= editorState.scrollY - margin && cursorY + lineHeight <= editorState.scrollY + visibleHeight + margin);

    // Horizontal visibility
    float cursorX = 0.0f;
    if (editorState.cursorLine < editorState.lines.size())
    {
        const std::string &line = editorState.lines[editorState.cursorLine];
        std::string to = line.substr(0, editorState.cursorColumn);
        ImFont *font = ImGui::GetFont();
        cursorX = font ? font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f, to.c_str()).x : editorState.cursorColumn * charWidth;
    }
    float marginX = charWidth * 4.0f;
    bool horizontallyVisible = (cursorX >= editorState.scrollX - marginX && cursorX + charWidth <= editorState.scrollX + visibleWidth + marginX);

    return verticallyVisible && horizontallyVisible;
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
            Vault *acVault = nullptr;
            if (fileBackend)
            {
                auto vb = dynamic_cast<VaultFileBackend*>(fileBackend.get());
                if (vb)
                {
                    auto vaultPtr = vb->getVault();
                    if (vaultPtr)
                    {
                        registerLuaVaultBindings(L, vaultPtr.get());
                        acVault = vaultPtr.get();
                    }
                }
            }
            registerLuaFSBindings(L, acVault);

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

void LuaEditor::drawClassInvestigator()
{
    // Enhanced API docs viewer: search highlight, grouping by module, example runner, copy buttons
    static std::string filter;
    static int selected = -1;
    static std::string lastExampleOutput;

    ImGui::SetNextWindowSize(ImVec2(620, 480), ImGuiCond_Appearing);
    // Force the API Docs to appear in the main document dock by default and use the main viewport
    ImGui::SetNextWindowDockID(ImGui::GetID("MyDockSpace"), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGuiWindowFlags flags = ImGuiWindowFlags_None; // Allow docking by default
    if (!ImGui::Begin("API Docs", &showDocViewer, flags)) { ImGui::End(); return; }

    // Top controls: filter + module selector
    ImGui::BeginGroup(); ImGui::Text("Filter:"); ImGui::SameLine(); ImGui::PushItemWidth(260); ImGui::InputText("##ApiFilter", &filter); ImGui::PopItemWidth(); ImGui::SameLine();
    // Build module list
    auto all = LuaBindingDocs::get().listAll();
    std::set<std::string> modules;
    for (const auto &e : all)
    {
        auto pos = e.name.find('.');
        if (pos == std::string::npos) modules.insert("(global)"); else modules.insert(e.name.substr(0, pos));
    }
    std::vector<std::string> modList;
    modList.push_back("All");
    for (const auto &m : modules) modList.push_back(m);
    static int moduleIndex = 0; // 0 == All
    if (ImGui::BeginCombo("Module", modList[moduleIndex].c_str()))
    {
        for (int i = 0; i < (int)modList.size(); ++i)
        {
            bool sel = (i == moduleIndex);
            if (ImGui::Selectable(modList[i].c_str(), sel)) moduleIndex = i;
        }
        ImGui::EndCombo();
    }
    ImGui::EndGroup();
    ImGui::Separator();

    // Build filtered list grouped by module
    std::string lfilter = filter; std::transform(lfilter.begin(), lfilter.end(), lfilter.begin(), ::tolower);
    std::map<std::string, std::vector<int>> groups;
    for (size_t i = 0; i < all.size(); ++i)
    {
        const auto &e = all[i];
        std::string key = e.name + " " + e.signature + " " + e.summary;
        std::string lkey = key; std::transform(lkey.begin(), lkey.end(), lkey.begin(), ::tolower);
        if (!lfilter.empty() && lkey.find(lfilter) == std::string::npos) continue;
        std::string mod = "(global)";
        auto pos = e.name.find('.'); if (pos != std::string::npos) mod = e.name.substr(0, pos);
        if (moduleIndex != 0 && mod != modList[moduleIndex]) continue; // module filter
        groups[mod].push_back((int)i);
    }

    // Left list: grouped
    ImGui::BeginChild("DocList", ImVec2(280, -ImGui::GetFrameHeightWithSpacing()));
    for (const auto &kv : groups)
    {
        bool open = ImGui::CollapsingHeader(kv.first.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        if (!open) continue;
        for (int idx : kv.second)
        {
            const auto &e = all[idx];
            std::string label = e.name + " - " + e.signature;
            // highlight whole item if it contains the filter
            bool contains = lfilter.empty() || (std::string(label).find(filter) != std::string::npos) || ([&](){ std::string l = label; std::transform(l.begin(), l.end(), l.begin(), ::tolower); return l.find(lfilter) != std::string::npos; })();
            if (contains) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            bool sel = (selected == idx);
            if (ImGui::Selectable(label.c_str(), sel)) { selected = idx; lastExampleOutput.clear(); }
            if (contains) ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // Right detail pane
    ImGui::BeginChild("DocDetail", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
    if (selected >= 0 && selected < (int)all.size())
    {
        const auto &e = all[selected];
        ImGui::TextDisabled("%s", e.name.c_str());
        ImGui::Separator();

        // Helper to render highlighted wrapped text (case-insensitive)
        auto renderHighlighted = [&](const std::string &text, const std::string &flt, const ImVec4 &hlColor){
            if (flt.empty()) { ImGui::TextWrapped("%s", text.c_str()); return; }
            std::string lower = text; std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
            std::string lflt = flt; std::transform(lflt.begin(), lflt.end(), lflt.begin(), ::tolower);
            size_t pos = 0; size_t start = 0;
            while ((pos = lower.find(lflt, start)) != std::string::npos)
            {
                if (pos > start) { std::string seg = text.substr(start, pos - start); ImGui::TextUnformatted(seg.c_str()); ImGui::SameLine(0,0); }
                std::string match = text.substr(pos, lflt.size()); ImGui::PushStyleColor(ImGuiCol_Text, hlColor); ImGui::TextUnformatted(match.c_str()); ImGui::PopStyleColor(); ImGui::SameLine(0,0);
                start = pos + lflt.size();
            }
            if (start < text.size()) { std::string seg = text.substr(start); ImGui::TextUnformatted(seg.c_str()); }
            else ImGui::NewLine();
        };

        ImGui::TextWrapped("%s", "Summary:");
        renderHighlighted(e.summary, filter, ImVec4(0.9f, 0.8f, 0.3f, 1.0f));
        ImGui::Separator();
        ImGui::TextDisabled("Signature:"); ImGui::SameLine(); ImGui::Text("%s", e.signature.c_str());
        ImGui::SameLine(); if (ImGui::SmallButton("Copy Signature")) ImGui::SetClipboardText(e.signature.c_str());
        if (!e.sourceFile.empty()) { ImGui::SameLine(); if (ImGui::SmallButton("View Source")) { /* Future: open source in editor */ } }

        if (!e.example.empty())
        {
            ImGui::Separator();
            ImGui::TextDisabled("Example (editable):");
            // Prefill previewCode when selection changes
            static int prevSelected = -2; // impossible initial value
            if (selected != prevSelected) {
                // Stop any running preview when switching examples/docs to avoid stale previews
                if (previewRunning) stopPreview();
                previewExecuteRequested = false;
                previewCode = e.example;
                previewOutput.clear();
                lastExampleOutput.clear();
                prevSelected = selected;
            }
            // When the user edits the example we may need to re-run Config() detection
            bool detectionDirty = false;

            ImGui::BeginChild("ExampleEdit", ImVec2(0, 120), true);
            ImGui::PushItemWidth(-1);
            bool edited = ImGui::InputTextMultiline("##PreviewCode", &previewCode, ImVec2(-1, -1));
            ImGui::PopItemWidth();
            ImGui::EndChild();

            // Live-update behavior: when the example is edited, update previews.
            if (edited)
            {
                // If a persistent preview is running, restart it with the new code so changes appear immediately.
                if (previewRunning)
                {
                    startPreview(previewCode, previewOrigin, previewWidth, previewHeight);
                }
                else
                {
                    // Request a one-shot preview run for the edited code (will be executed when the preview area is rendered).
                    previewExecuteRequested = true;
                }
                // Mark detection dirty so we re-run Config() detection below (statics declared later)
                detectionDirty = true;
            }

            ImGui::BeginGroup();
            if (ImGui::Button("Copy Example")) ImGui::SetClipboardText(previewCode.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Insert into Console")) { liveConsoleInput = previewCode; ImGui::SetKeyboardFocusHere(); }
            ImGui::SameLine();

            // Determine whether the snippet provides a Config() by running a quick detection when selection changed
            static int _lastDetectSel = -1;
            static ScriptConfig _cachedCfg;
            bool hasConfig = false;
            if (selected != _lastDetectSel || detectionDirty) { _cachedCfg = detectSnippetConfig(previewCode); _lastDetectSel = selected; }
            if (_cachedCfg.type != ScriptConfig::Type::None) hasConfig = true;

            if (hasConfig)
            {
                // Show toggle for continuous preview
                if (!previewRunning)
                {
                    if (ImGui::Button("Run Preview")) { pendingExample = previewCode; previewRunConfirmOpen = true; ImGui::OpenPopup("Run Preview Confirmation"); }
                }
                else
                {
                    if (ImGui::Button("Stop Preview")) { stopPreview(); }
                }
            }
            else
            {
                // Non-config examples are one-shot runs
                if (ImGui::Button("Run Example")) { pendingExample = previewCode; runExampleConfirmOpen = true; ImGui::OpenPopup("Run Example Confirmation"); }
            }
            ImGui::EndGroup();

            // Run Example confirmation modal (full app run)
            // Ensure this important confirmation is centered on the main viewport
            {
                ImGuiViewport* main_viewport = ImGui::GetMainViewport();
                ImVec2 center = main_viewport->GetCenter();
                ImGui::SetNextWindowViewport(main_viewport->ID);
                ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            }
            if (ImGui::BeginPopupModal("Run Example Confirmation", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextWrapped("Warning: Running examples may modify your vault or other application state (e.g., via `vault.*` bindings). Only run examples from trusted sources. Continue?");
                ImGui::Separator();
                if (ImGui::Button("Run")) { lastExampleOutput = runExampleSnippet(pendingExample); pendingExample.clear(); runExampleConfirmOpen = false; ImGui::CloseCurrentPopup(); }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) { pendingExample.clear(); runExampleConfirmOpen = false; ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }

            // Preview confirmation modal (local preview run inside child)
            // Ensure this confirmation is centered on the main viewport as well
            {
                ImGuiViewport* main_viewport = ImGui::GetMainViewport();
                ImVec2 center = main_viewport->GetCenter();
                ImGui::SetNextWindowViewport(main_viewport->ID);
                ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
            }
            if (ImGui::BeginPopupModal("Run Preview Confirmation", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
            {
                ImGui::TextWrapped("Preview runs code inside a local preview area and may still execute bindings that modify state (e.g., `vault.*`). Only run previews for trusted snippets. Continue?");
                ImGui::Separator();
                if (ImGui::Button("Start Preview")) { previewRunConfirmOpen = false; ImGui::CloseCurrentPopup(); 
                    // Start persistent preview engine
                    ImVec2 childOrigin = ImGui::GetCursorScreenPos(); ImVec2 childSize = ImGui::GetContentRegionAvail();
                    ScriptConfig cfg = detectSnippetConfig(previewCode);
                    int w = (int)childSize.x, h = (int)childSize.y;
                    if (cfg.type == ScriptConfig::Type::Canvas && cfg.width>0) { w = cfg.width; h = cfg.height; }
                    startPreview(previewCode, childOrigin, w, h, &cfg);
                }
                ImGui::SameLine();
                if (ImGui::Button("Cancel")) { previewRunConfirmOpen = false; previewExecuteRequested = false; ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }

                    // Preview area for canvas/ui modules (only for examples that provide a Config())
                    if (hasConfig)
                    {
                        auto pos = ImGui::GetCursorScreenPos();
                        ImGui::Separator();
                        ImGui::TextDisabled("Preview:");
                        ImGui::BeginChild("PreviewArea", ImVec2(360, 240), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NavFlattened);
                        ImVec2 childOrigin = ImGui::GetCursorScreenPos();
                        ImVec2 childSize = ImGui::GetContentRegionAvail();

                        // If a persistent preview is running we need to tick it each frame so Render/UI is invoked
                        if (previewRunning && previewEngine)
                        {
                            // Ensure preview engine has vault bindings if a vault was opened after the preview started
                            lua_State *pvL = previewEngine->L();
                            if (pvL) {
                                lua_getglobal(pvL, "vault");
                                bool pvHasVault = !lua_isnil(pvL, -1);
                                lua_pop(pvL, 1);
                                if (!pvHasVault && fileBackend) {
                                    auto vb = dynamic_cast<VaultFileBackend*>(fileBackend.get());
                                    if (vb) {
                                        auto vaultPtr = vb->getVault();
                                        if (vaultPtr) {
                                            registerLuaVaultBindings(pvL, vaultPtr.get());
                                            // Also register FS bindings if not already present
                                            lua_getglobal(pvL, "fs");
                                            bool hasFsGlobal = !lua_isnil(pvL, -1);
                                            lua_pop(pvL, 1);
                                            if (!hasFsGlobal)
                                                registerLuaFSBindings(pvL, vaultPtr.get());
                                        }
                                    }
                                }
                            }
                            // Update preview origin/size in case the child moved
                            previewOrigin = childOrigin;
                            previewWidth = (int)childSize.x;
                            previewHeight = (int)childSize.y;
                            previewTick();
                        }

                        // Execute a one-shot preview run if requested (for non-persistent examples or one-off preview)
                        if (previewExecuteRequested && !previewRunning)
                        {
                            previewOutput = runPreviewSnippet(previewCode, childOrigin, (int)childSize.x, (int)childSize.y);
                            previewExecuteRequested = false; // reset
                        }

                        ImGui::EndChild();

                        if (!previewOutput.empty()) {
                            ImGui::Separator(); ImGui::TextDisabled("Preview Output:");
                            ImGui::BeginChild("PreviewOutput", ImVec2(0, 80), true);
                            ImGui::TextWrapped("%s", previewOutput.c_str());
                            ImGui::EndChild();
                        }
                    }
                    else
                    {
                        // For non-UI examples we don't show a preview area; clear previewOutput and any pending preview request
                        previewOutput.clear();
                        previewExecuteRequested = false;
                    }

            // If a non-UI / non-persistent example was run, show its stdout/returned output here
            if (!lastExampleOutput.empty()) {
                ImGui::Separator(); ImGui::TextDisabled("Example Output:");
                ImGui::BeginChild("ExampleOutput", ImVec2(0, 80), true);
                ImGui::TextWrapped("%s", lastExampleOutput.c_str());
                ImGui::EndChild();
            }
        }
        else
        {
            ImGui::TextDisabled("No example available for this entry.");
        }

        // Footer: signature + source
        ImGui::Separator();
        if (!e.sourceFile.empty()) ImGui::TextDisabled("Binding source: %s", e.sourceFile.c_str());
    }
    else
    {
        ImGui::TextDisabled("No selection");
        ImGui::TextWrapped("Use the filter to find API functions and click to view details and examples.");
    }
    ImGui::EndChild();

    ImGui::End();
}



