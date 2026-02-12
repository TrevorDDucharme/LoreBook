#include <Editors/Common/BaseTextEditor.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <fstream>
#include <algorithm>
#include <cmath>
#include <cctype>
#include <unordered_set>

namespace Editors
{

BaseTextEditor::BaseTextEditor()
{
}

BaseTextEditor::~BaseTextEditor()
{
}

// ============================================================================
// Font Metrics — match LuaEditor exactly
// ============================================================================

void BaseTextEditor::updateFontMetrics()
{
    ImFont* font = ImGui::GetFont();
    if (font)
    {
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

float BaseTextEditor::getBaselineOffset() const
{
    return (lineHeight - renderFontSize) * 0.5f;
}

// ============================================================================
// Core Editor Operations — NO pushUndo inside; callers are responsible
// ============================================================================

void BaseTextEditor::insertTextAtCursor(const std::string& text)
{
    if (editorState.hasSelection)
        deleteSelection();

    if (editorState.cursorLine >= editorState.lines.size())
        editorState.lines.resize(editorState.cursorLine + 1);

    if (text.find('\n') != std::string::npos)
    {
        std::vector<std::string> newLines;
        std::string current;
        for (char c : text)
        {
            if (c == '\n') { newLines.push_back(current); current.clear(); }
            else current.push_back(c);
        }
        newLines.push_back(current);

        std::string& line = editorState.lines[editorState.cursorLine];
        std::string afterCursor = line.substr(editorState.cursorColumn);
        line = line.substr(0, editorState.cursorColumn) + newLines[0];

        for (size_t i = 1; i < newLines.size(); ++i)
            editorState.lines.insert(
                editorState.lines.begin() + editorState.cursorLine + i,
                newLines[i]);

        editorState.cursorLine += newLines.size() - 1;
        editorState.cursorColumn = newLines.back().length();
        editorState.lines[editorState.cursorLine] += afterCursor;
    }
    else
    {
        std::string& line = editorState.lines[editorState.cursorLine];
        line.insert(editorState.cursorColumn, text);
        editorState.cursorColumn += text.length();
    }

    editorState.isDirty = true;
    editorState.needsScrollToCursor = true;
    syncEditorToActiveTab();
    onTextChanged();
}

void BaseTextEditor::deleteSelection()
{
    if (!editorState.hasSelection) return;

    size_t startLine = std::min(editorState.selectionStartLine, editorState.selectionEndLine);
    size_t endLine = std::max(editorState.selectionStartLine, editorState.selectionEndLine);
    size_t startCol, endCol;

    if (startLine == endLine)
    {
        startCol = std::min(editorState.selectionStartColumn, editorState.selectionEndColumn);
        endCol = std::max(editorState.selectionStartColumn, editorState.selectionEndColumn);
    }
    else
    {
        startCol = (editorState.selectionStartLine < editorState.selectionEndLine)
            ? editorState.selectionStartColumn : editorState.selectionEndColumn;
        endCol = (editorState.selectionStartLine < editorState.selectionEndLine)
            ? editorState.selectionEndColumn : editorState.selectionStartColumn;
    }

    if (startLine == endLine)
    {
        editorState.lines[startLine].erase(startCol, endCol - startCol);
    }
    else
    {
        std::string newLine = editorState.lines[startLine].substr(0, startCol) +
                              editorState.lines[endLine].substr(endCol);
        editorState.lines[startLine] = newLine;
        editorState.lines.erase(editorState.lines.begin() + startLine + 1,
                                editorState.lines.begin() + endLine + 1);
    }

    editorState.cursorLine = startLine;
    editorState.cursorColumn = startCol;
    editorState.hasSelection = false;
    editorState.isDirty = true;
    syncEditorToActiveTab();
    onTextChanged();
}

void BaseTextEditor::moveCursor(int deltaLine, int deltaColumn)
{
    int newLine = static_cast<int>(editorState.cursorLine) + deltaLine;
    int newColumn = static_cast<int>(editorState.cursorColumn) + deltaColumn;
    
    newLine = std::max(0, std::min(newLine, static_cast<int>(editorState.lines.size()) - 1));
    
    if (newLine >= 0 && newLine < static_cast<int>(editorState.lines.size()))
        newColumn = std::max(0, std::min(newColumn, static_cast<int>(editorState.lines[newLine].length())));
    else
        newColumn = 0;
    
    editorState.cursorLine = newLine;
    editorState.cursorColumn = newColumn;
    editorState.needsScrollToCursor = true;
    syncEditorToActiveTab();
}

void BaseTextEditor::pushUndo()
{
    UndoEntry entry;
    entry.lines = editorState.lines;
    entry.cursorLine = editorState.cursorLine;
    entry.cursorColumn = editorState.cursorColumn;
    undoStack.push_back(std::move(entry));
    if (undoStack.size() > maxUndoHistory)
        undoStack.erase(undoStack.begin());
    redoStack.clear();
}

// ============================================================================
// Rendering — matches LuaEditor exactly
// ============================================================================

void BaseTextEditor::drawLineNumbers(ImDrawList* drawList, const ImVec2& origin, float height)
{
    ImFont* font = ImGui::GetFont();
    float baselineOffset = getBaselineOffset();
    float y = origin.y - editorState.scrollY + baselineOffset;

    size_t startLine = static_cast<size_t>(std::max(0.0f, editorState.scrollY / lineHeight));
    size_t endLine = std::min(editorState.lines.size(),
                              startLine + static_cast<size_t>(height / lineHeight) + 2);

    for (size_t ln = startLine; ln < endLine; ++ln)
    {
        float lineY = y + ln * lineHeight;
        if (lineY > origin.y + height) break;
        if (lineY + lineHeight < origin.y) continue;

        std::string num = std::to_string(ln + 1);
        drawList->AddText(font, renderFontSize, ImVec2(origin.x + 5, lineY),
                          lineNumberColor, num.c_str());
    }
}

void BaseTextEditor::drawTextContentWithSyntaxHighlighting(ImDrawList* drawList,
                                                           const ImVec2& origin,
                                                           float visibleHeight)
{
    ImFont* font = ImGui::GetFont();
    if (!font) return;

    float baselineOffset = getBaselineOffset();
    float y = origin.y - editorState.scrollY + baselineOffset;
    size_t startLine = static_cast<size_t>(std::max(0.0f, editorState.scrollY / lineHeight));
    size_t endLine = std::min(editorState.lines.size(),
                              startLine + static_cast<size_t>(visibleHeight / lineHeight) + 2);

    beginTokenize(startLine);

    for (size_t ln = startLine; ln < endLine; ++ln)
    {
        float lineY = y + ln * lineHeight;
        if (lineY > origin.y + visibleHeight) break;
        if (lineY + lineHeight < origin.y) continue;

        const std::string& line = editorState.lines[ln];
        if (line.empty()) { tokenizeLine(line, ln); continue; } // advance tokenizer state

        auto tokens = tokenizeLine(line, ln);
        float x = origin.x;

        if (tokens.empty())
        {
            drawList->AddText(font, renderFontSize, ImVec2(x, lineY), textColor,
                              line.c_str());
        }
        else
        {
            for (const auto& tok : tokens)
            {
                if (tok.start >= line.size()) break;
                size_t end = std::min(tok.start + tok.length, line.size());
                const char* b = line.c_str() + tok.start;
                const char* e = line.c_str() + end;
                drawList->AddText(font, renderFontSize, ImVec2(x, lineY), tok.color, b, e);
                x += font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f, b, e).x;
            }
        }
    }
}

void BaseTextEditor::drawCursor(ImDrawList* drawList, const ImVec2& origin)
{
    ImVec2 p = getCursorScreenPos(origin);
    drawList->AddLine(p, ImVec2(p.x, p.y + lineHeight), cursorColor, 2.0f);

    // Draw extra cursors
    ImFont* font = ImGui::GetFont();
    float baselineOffset = getBaselineOffset();
    for (const auto& ec : extraCursors)
    {
        float x = origin.x;
        if (ec.line < editorState.lines.size() && font)
        {
            const std::string& ln = editorState.lines[ec.line];
            size_t col = std::min(ec.column, ln.length());
            if (col > 0)
                x += font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f,
                                         ln.substr(0, col).c_str()).x;
        }
        float y = origin.y + ec.line * lineHeight - editorState.scrollY + baselineOffset;
        drawList->AddLine(ImVec2(x, y), ImVec2(x, y + lineHeight), cursorColor, 2.0f);
    }
}

void BaseTextEditor::drawSelection(ImDrawList* drawList, const ImVec2& origin)
{
    if (!editorState.hasSelection) return;

    size_t startLine = std::min(editorState.selectionStartLine, editorState.selectionEndLine);
    size_t endLine = std::max(editorState.selectionStartLine, editorState.selectionEndLine);
    size_t startCol, endCol;

    if (startLine == endLine)
    {
        startCol = std::min(editorState.selectionStartColumn, editorState.selectionEndColumn);
        endCol = std::max(editorState.selectionStartColumn, editorState.selectionEndColumn);
    }
    else
    {
        startCol = (editorState.selectionStartLine < editorState.selectionEndLine)
            ? editorState.selectionStartColumn : editorState.selectionEndColumn;
        endCol = (editorState.selectionStartLine < editorState.selectionEndLine)
            ? editorState.selectionEndColumn : editorState.selectionStartColumn;
    }

    ImFont* font = ImGui::GetFont();
    float baselineOffset = getBaselineOffset();

    for (size_t ln = startLine; ln <= endLine; ++ln)
    {
        if (ln >= editorState.lines.size()) break;
        float lineY = origin.y + ln * lineHeight - editorState.scrollY + baselineOffset;
        size_t ls = (ln == startLine) ? startCol : 0;
        size_t le = (ln == endLine) ? endCol : editorState.lines[ln].length();
        float startX = origin.x, endX = origin.x;
        if (font)
        {
            if (ls > 0)
                startX += font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f,
                    editorState.lines[ln].substr(0, ls).c_str()).x;
            if (le > 0)
                endX += font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f,
                    editorState.lines[ln].substr(0, le).c_str()).x;
        }
        else
        {
            startX += ls * charWidth;
            endX += le * charWidth;
        }
        drawList->AddRectFilled(ImVec2(startX, lineY),
                                ImVec2(endX, lineY + lineHeight), selectionColor);
    }
}

void BaseTextEditor::drawSyntaxErrors(ImDrawList* drawList, const ImVec2& origin)
{
    ImFont* font = ImGui::GetFont();
    float baselineOffset = getBaselineOffset();

    for (const auto& err : syntaxErrors)
    {
        size_t ln = err.getLine() - 1; // Convert to 0-based
        if (ln >= editorState.lines.size()) continue;

        const std::string& line = editorState.lines[ln];
        float lineY = origin.y + ln * lineHeight - editorState.scrollY +
                      baselineOffset + lineHeight - 2;

        float startX = origin.x;
        if (font && err.getColumn() > 1 &&
            static_cast<size_t>(err.getColumn() - 1) <= line.length())
        {
            startX += font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f,
                line.substr(0, err.getColumn() - 1).c_str()).x;
        }
        else
        {
            startX += (err.getColumn() - 1) * charWidth;
        }

        float endX = startX + charWidth * 5;
        for (float x = startX; x < endX; x += 4.0f)
        {
            float wave = sin((x - startX) * 0.5f) * 2.0f;
            drawList->AddLine(ImVec2(x, lineY + wave),
                              ImVec2(x + 2, lineY - wave), errorColor, 1.0f);
        }
    }
}

void BaseTextEditor::drawCompletionPopup(const ImVec2& textOrigin)
{
    if (!showCompletions || completionItems.empty()) return;

    ImVec2 cursor = getCursorScreenPos(textOrigin);
    ImVec2 pos = ImVec2(cursor.x, cursor.y + lineHeight);
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(ImVec2(300, std::min(200.0f,
        completionItems.size() * 20.0f)));

    if (ImGui::Begin("##Completions", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove))
    {
        for (size_t i = 0; i < completionItems.size(); ++i)
        {
            bool sel = (static_cast<int>(i) == selectedCompletion);
            if (sel)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
                ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(100, 150, 200, 255));
            }

            if (ImGui::Selectable(completionItems[i].text.c_str(), sel))
            {
                selectedCompletion = static_cast<int>(i);
                // Erase the partial word and insert the completion
                const std::string& line = editorState.lines[editorState.cursorLine];
                size_t ws = editorState.cursorColumn;
                while (ws > 0 && (std::isalnum(line[ws - 1]) || line[ws - 1] == '_'))
                    ws--;
                std::string& cur = editorState.lines[editorState.cursorLine];
                cur.erase(ws, editorState.cursorColumn - ws);
                cur.insert(ws, completionItems[i].text);
                editorState.cursorColumn = ws + completionItems[i].text.length();
                editorState.isDirty = true;
                showCompletions = false;
            }

            if (sel) ImGui::PopStyleColor(2);
            ImGui::SameLine();
            ImGui::TextDisabled(" - %s", completionItems[i].description.c_str());
        }
    }
    ImGui::End();
}

// ============================================================================
// Cursor Positioning
// ============================================================================

ImVec2 BaseTextEditor::getCursorScreenPos(const ImVec2& origin) const
{
    float x = origin.x;
    if (editorState.cursorLine < editorState.lines.size())
    {
        const std::string& line = editorState.lines[editorState.cursorLine];
        if (editorState.cursorColumn > 0 && editorState.cursorColumn <= line.length())
        {
            std::string to = line.substr(0, editorState.cursorColumn);
            ImFont* font = ImGui::GetFont();
            if (font)
                x += font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f, to.c_str()).x;
            else
                x += editorState.cursorColumn * charWidth;
        }
    }
    ImFont* font = ImGui::GetFont();
    float baselineOffset = (lineHeight - renderFontSize) * 0.5f;
    float y = origin.y + editorState.cursorLine * lineHeight - editorState.scrollY + baselineOffset;
    return ImVec2(x, y);
}

void BaseTextEditor::ensureCursorVisible()
{
    float cursorY = editorState.cursorLine * lineHeight;
    float margin = lineHeight * 0.5f;
    
    if (cursorY < editorState.scrollY)
    {
        editorState.scrollY = cursorY - margin;
        editorState.scrollY = std::max(0.0f, editorState.scrollY);
    }
    else if (cursorY + lineHeight > editorState.scrollY + visibleHeight)
    {
        editorState.scrollY = cursorY + lineHeight - visibleHeight + margin;
        editorState.scrollY = std::max(0.0f, editorState.scrollY);
    }

    // Horizontal visibility
    float cursorX = 0.0f;
    if (editorState.cursorLine < editorState.lines.size())
    {
        ImFont* font = ImGui::GetFont();
        const std::string& line = editorState.lines[editorState.cursorLine];
        std::string textToCursor = line.substr(0, std::min(editorState.cursorColumn, line.length()));
        cursorX = font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f, textToCursor.c_str()).x;
    }
    
    float marginX = charWidth * 4.0f;
    if (cursorX < editorState.scrollX + marginX)
    {
        editorState.scrollX = cursorX - marginX;
        editorState.scrollX = std::max(0.0f, editorState.scrollX);
    }
    else if (cursorX + charWidth > editorState.scrollX + visibleWidth - marginX)
    {
        editorState.scrollX = cursorX + charWidth - visibleWidth + marginX;
        editorState.scrollX = std::max(0.0f, editorState.scrollX);
    }
}

bool BaseTextEditor::isCursorVisible() const
{
    float cursorY = editorState.cursorLine * lineHeight;
    float margin = lineHeight * 2.0f;
    bool verticallyVisible = (cursorY >= editorState.scrollY - margin && 
                              cursorY + lineHeight <= editorState.scrollY + visibleHeight + margin);

    float cursorX = 0.0f;
    if (editorState.cursorLine < editorState.lines.size())
    {
        ImFont* font = ImGui::GetFont();
        const std::string& line = editorState.lines[editorState.cursorLine];
        std::string textToCursor = line.substr(0, std::min(editorState.cursorColumn, line.length()));
        cursorX = font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f, textToCursor.c_str()).x;
    }
    
    float marginX = charWidth * 4.0f;
    bool horizontallyVisible = (cursorX >= editorState.scrollX - marginX && 
                                cursorX + charWidth <= editorState.scrollX + visibleWidth + marginX);

    return verticallyVisible && horizontallyVisible;
}

void BaseTextEditor::scrollToCursorIfNeeded()
{
    if (!isCursorVisible())
        editorState.needsScrollToCursor = true;
}

// ============================================================================
// Mouse Input — matches LuaEditor midpoint-based column detection
// ============================================================================

void BaseTextEditor::handleMouseInput(const ImVec2& origin, const ImVec2& textOrigin,
                                       const ImVec2& size, bool isHovered)
{
    ImGuiIO& io = ImGui::GetIO();
    ImVec2 mouse = io.MousePos;
    float baselineOffset = getBaselineOffset();
    ImFont* font = ImGui::GetFont();

    // Click to place cursor (midpoint-based column detection, matching LuaEditor)
    if (isHovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        float relY = mouse.y - (origin.y + baselineOffset) + editorState.scrollY;
        int line = static_cast<int>(std::floor(relY / lineHeight));
        line = std::max(0, std::min(line, static_cast<int>(editorState.lines.size()) - 1));

        int col = 0;
        if (line >= 0 && line < static_cast<int>(editorState.lines.size()))
        {
            const std::string& ln = editorState.lines[line];
            float x = textOrigin.x;
            for (size_t i = 0; i <= ln.size(); ++i)
            {
                float nextX = x;
                if (i < ln.size())
                    nextX += font ? font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f,
                        ln.substr(i, 1).c_str()).x : charWidth;
                float mid = (x + nextX) * 0.5f;
                if (mouse.x < mid) { col = static_cast<int>(i); break; }
                x = nextX;
                col = static_cast<int>(i + 1);
            }
        }

        int clampedLine = std::max(0, std::min(line,
            static_cast<int>(editorState.lines.size()) - 1));
        int clampedCol = std::max(0, std::min(col,
            static_cast<int>(editorState.lines[clampedLine].length())));

        // Ctrl+Alt+Click: add an extra cursor
        if (io.KeyCtrl && io.KeyAlt)
        {
            ExtraCursor ec;
            ec.line = static_cast<size_t>(clampedLine);
            ec.column = static_cast<size_t>(clampedCol);
            extraCursors.push_back(ec);
        }
        else
        {
            extraCursors.clear();
            editorState.cursorLine = clampedLine;
            editorState.cursorColumn = clampedCol;
            editorState.hasSelection = false;
            editorState.selectionStartLine = editorState.cursorLine;
            editorState.selectionStartColumn = editorState.cursorColumn;
            editorState.selectionEndLine = editorState.cursorLine;
            editorState.selectionEndColumn = editorState.cursorColumn;
        }
        editorState.needsScrollToCursor = true;
        editorState.hasFocus = true;
    }

    // Drag to select (midpoint-based)
    if (isHovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left))
    {
        float relY = mouse.y - (origin.y + baselineOffset) + editorState.scrollY;
        int line = static_cast<int>(std::floor(relY / lineHeight));
        line = std::max(0, std::min(line, static_cast<int>(editorState.lines.size()) - 1));

        int col = 0;
        const std::string& ln = editorState.lines[line];
        float x = textOrigin.x;
        for (size_t i = 0; i <= ln.size(); ++i)
        {
            float nextX = x;
            if (i < ln.size())
                nextX += font ? font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f,
                    ln.substr(i, 1).c_str()).x : charWidth;
            float mid = (x + nextX) * 0.5f;
            if (mouse.x < mid) { col = static_cast<int>(i); break; }
            x = nextX;
            col = static_cast<int>(i + 1);
        }

        editorState.selectionEndLine = line;
        editorState.selectionEndColumn = col;
        editorState.hasSelection = !(editorState.selectionStartLine == editorState.selectionEndLine &&
                                     editorState.selectionStartColumn == editorState.selectionEndColumn);
        editorState.cursorLine = editorState.selectionEndLine;
        editorState.cursorColumn = editorState.selectionEndColumn;
        editorState.needsScrollToCursor = true;
    }

    // When hovered, capture mouse input
    if (isHovered)
        io.WantCaptureMouse = true;

    // Ctrl+wheel: scale; Shift+wheel: horizontal scroll; wheel: vertical scroll
    if (isHovered && io.MouseWheel != 0.0f)
    {
        if (io.KeyCtrl)
        {
            editorScaleMultiplier += io.MouseWheel * 0.05f;
            if (editorScaleMultiplier < 0.6f) editorScaleMultiplier = 0.6f;
            if (editorScaleMultiplier > 1.2f) editorScaleMultiplier = 1.2f;
            updateFontMetrics();
        }
        else if (io.KeyShift)
        {
            editorState.scrollX = std::max(0.0f,
                editorState.scrollX - io.MouseWheel * charWidth * 8.0f);
            // Clamp to content width
            float maxWidth = 0.0f;
            for (const auto& l : editorState.lines)
            {
                float w = font ? font->CalcTextSizeA(renderFontSize, FLT_MAX, 0.0f,
                    l.c_str()).x : l.length() * charWidth;
                if (w > maxWidth) maxWidth = w;
            }
            float maxScroll = std::max(0.0f, maxWidth - visibleWidth + charWidth * 4.0f);
            if (editorState.scrollX > maxScroll) editorState.scrollX = maxScroll;
        }
        else
        {
            editorState.scrollY = std::max(0.0f,
                editorState.scrollY - io.MouseWheel * lineHeight * 3.0f);
        }
    }
}

// ============================================================================
// Keyboard Input — matches LuaEditor exactly
// ============================================================================

void BaseTextEditor::handleKeyboardInput()
{
    ImGuiIO& io = ImGui::GetIO();
    if (!editorState.hasFocus) return;

    io.WantCaptureKeyboard = true;
    io.WantTextInput = true;

    // --- Clipboard shortcuts ---
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_A))
    {
        editorState.selectionStartLine = 0;
        editorState.selectionStartColumn = 0;
        editorState.selectionEndLine = editorState.lines.size() - 1;
        editorState.selectionEndColumn = editorState.lines.back().length();
        editorState.cursorLine = editorState.selectionEndLine;
        editorState.cursorColumn = editorState.selectionEndColumn;
        editorState.hasSelection = true;
        return;
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C))
    {
        if (editorState.hasSelection)
        {
            size_t sL = std::min(editorState.selectionStartLine, editorState.selectionEndLine);
            size_t eL = std::max(editorState.selectionStartLine, editorState.selectionEndLine);
            size_t sC = (editorState.selectionStartLine < editorState.selectionEndLine)
                ? editorState.selectionStartColumn
                : (editorState.selectionStartLine == editorState.selectionEndLine)
                    ? std::min(editorState.selectionStartColumn, editorState.selectionEndColumn)
                    : editorState.selectionEndColumn;
            size_t eC = (editorState.selectionStartLine < editorState.selectionEndLine)
                ? editorState.selectionEndColumn
                : (editorState.selectionStartLine == editorState.selectionEndLine)
                    ? std::max(editorState.selectionStartColumn, editorState.selectionEndColumn)
                    : editorState.selectionStartColumn;
            std::string selected;
            if (sL == eL) selected = editorState.lines[sL].substr(sC, eC - sC);
            else
            {
                selected = editorState.lines[sL].substr(sC) + "\n";
                for (size_t l = sL + 1; l < eL; ++l)
                    selected += editorState.lines[l] + "\n";
                selected += editorState.lines[eL].substr(0, eC);
            }
            ImGui::SetClipboardText(selected.c_str());
        }
        return;
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X))
    {
        if (editorState.hasSelection)
        {
            size_t sL = std::min(editorState.selectionStartLine, editorState.selectionEndLine);
            size_t eL = std::max(editorState.selectionStartLine, editorState.selectionEndLine);
            size_t sC = (editorState.selectionStartLine < editorState.selectionEndLine)
                ? editorState.selectionStartColumn
                : (editorState.selectionStartLine == editorState.selectionEndLine)
                    ? std::min(editorState.selectionStartColumn, editorState.selectionEndColumn)
                    : editorState.selectionEndColumn;
            size_t eC = (editorState.selectionStartLine < editorState.selectionEndLine)
                ? editorState.selectionEndColumn
                : (editorState.selectionStartLine == editorState.selectionEndLine)
                    ? std::max(editorState.selectionStartColumn, editorState.selectionEndColumn)
                    : editorState.selectionStartColumn;
            std::string selected;
            if (sL == eL) selected = editorState.lines[sL].substr(sC, eC - sC);
            else
            {
                selected = editorState.lines[sL].substr(sC) + "\n";
                for (size_t l = sL + 1; l < eL; ++l)
                    selected += editorState.lines[l] + "\n";
                selected += editorState.lines[eL].substr(0, eC);
            }
            ImGui::SetClipboardText(selected.c_str());
            pushUndo();
            deleteSelection();
        }
        return;
    }

    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V))
    {
        const char* clip = ImGui::GetClipboardText();
        if (clip && clip[0] != '\0')
        {
            pushUndo();
            if (editorState.hasSelection) deleteSelection();
            insertTextAtCursor(std::string(clip));
        }
        return;
    }

    // --- Undo: Ctrl+Z ---
    if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z))
    {
        if (!undoStack.empty())
        {
            UndoEntry redo;
            redo.lines = editorState.lines;
            redo.cursorLine = editorState.cursorLine;
            redo.cursorColumn = editorState.cursorColumn;
            redoStack.push_back(std::move(redo));

            const UndoEntry& u = undoStack.back();
            editorState.lines = u.lines;
            editorState.cursorLine = u.cursorLine;
            editorState.cursorColumn = u.cursorColumn;
            undoStack.pop_back();
            editorState.hasSelection = false;
            editorState.isDirty = true;
            extraCursors.clear();
            syncEditorToActiveTab();
            onTextChanged();
        }
        return;
    }

    // --- Redo: Ctrl+Y or Ctrl+Shift+Z ---
    if ((io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Y)) ||
        (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)))
    {
        if (!redoStack.empty())
        {
            UndoEntry u;
            u.lines = editorState.lines;
            u.cursorLine = editorState.cursorLine;
            u.cursorColumn = editorState.cursorColumn;
            undoStack.push_back(std::move(u));

            const UndoEntry& r = redoStack.back();
            editorState.lines = r.lines;
            editorState.cursorLine = r.cursorLine;
            editorState.cursorColumn = r.cursorColumn;
            redoStack.pop_back();
            editorState.hasSelection = false;
            editorState.isDirty = true;
            extraCursors.clear();
            syncEditorToActiveTab();
            onTextChanged();
        }
        return;
    }

    // --- Ctrl+S: Save ---
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
    {
        saveActiveTab();
        return;
    }

    // --- Enter ---
    if (ImGui::IsKeyPressed(ImGuiKey_Enter))
    {
        pushUndo();
        if (showCompletions) showCompletions = false;
        if (editorState.hasSelection) deleteSelection();
        if (editorState.cursorLine >= editorState.lines.size())
            editorState.lines.resize(editorState.cursorLine + 1);

        std::string& cur = editorState.lines[editorState.cursorLine];
        std::string indent;
        for (char c : cur) { if (c == ' ' || c == '\t') indent.push_back(c); else break; }

        std::string newLine = cur.substr(editorState.cursorColumn);
        cur = cur.substr(0, editorState.cursorColumn);
        if (!newLine.empty() || editorState.cursorColumn > 0) newLine = indent + newLine;

        editorState.lines.insert(editorState.lines.begin() + editorState.cursorLine + 1, newLine);
        editorState.cursorLine++;
        editorState.cursorColumn = indent.length();
        editorState.isDirty = true;
        editorState.needsScrollToCursor = true;
        onTextChanged();
    }

    // --- Backspace ---
    if (ImGui::IsKeyPressed(ImGuiKey_Backspace))
    {
        pushUndo();
        // Multi-cursor backspace: process extra cursors bottom-to-top
        if (!extraCursors.empty())
        {
            std::sort(extraCursors.begin(), extraCursors.end(),
                [](const ExtraCursor& a, const ExtraCursor& b) {
                    return a.line > b.line || (a.line == b.line && a.column > b.column);
                });
            for (auto& ec : extraCursors)
            {
                if (ec.column > 0 && ec.line < editorState.lines.size())
                {
                    editorState.lines[ec.line].erase(ec.column - 1, 1);
                    ec.column--;
                    if (ec.line == editorState.cursorLine && editorState.cursorColumn > ec.column)
                        editorState.cursorColumn--;
                }
            }
            editorState.isDirty = true;
        }

        if (editorState.hasSelection)
        {
            deleteSelection();
        }
        else if (editorState.cursorColumn > 0)
        {
            editorState.lines[editorState.cursorLine].erase(editorState.cursorColumn - 1, 1);
            editorState.cursorColumn--;
            editorState.isDirty = true;
            onTextChanged();
        }
        else if (editorState.cursorLine > 0)
        {
            editorState.cursorColumn = editorState.lines[editorState.cursorLine - 1].length();
            editorState.lines[editorState.cursorLine - 1] += editorState.lines[editorState.cursorLine];
            editorState.lines.erase(editorState.lines.begin() + editorState.cursorLine);
            editorState.cursorLine--;
            editorState.isDirty = true;
            onTextChanged();
            scrollToCursorIfNeeded();
            for (auto& ec : extraCursors)
                if (ec.line > editorState.cursorLine) ec.line--;
        }

        if (showCompletions)
        {
            updateCompletions();
            if (completionItems.empty()) showCompletions = false;
        }
    }

    // --- Delete ---
    if (ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        pushUndo();
        // Multi-cursor delete: process bottom-to-top
        if (!extraCursors.empty())
        {
            std::sort(extraCursors.begin(), extraCursors.end(),
                [](const ExtraCursor& a, const ExtraCursor& b) {
                    return a.line > b.line || (a.line == b.line && a.column > b.column);
                });
            for (auto& ec : extraCursors)
            {
                if (ec.line < editorState.lines.size() &&
                    ec.column < editorState.lines[ec.line].length())
                {
                    editorState.lines[ec.line].erase(ec.column, 1);
                    if (ec.line == editorState.cursorLine && editorState.cursorColumn > ec.column)
                        editorState.cursorColumn--;
                }
            }
            editorState.isDirty = true;
        }

        if (editorState.hasSelection) deleteSelection();
        else if (editorState.cursorLine < editorState.lines.size())
        {
            if (editorState.cursorColumn < editorState.lines[editorState.cursorLine].length())
            {
                editorState.lines[editorState.cursorLine].erase(editorState.cursorColumn, 1);
                editorState.isDirty = true;
                onTextChanged();
            }
            else if (editorState.cursorLine < editorState.lines.size() - 1)
            {
                editorState.lines[editorState.cursorLine] +=
                    editorState.lines[editorState.cursorLine + 1];
                editorState.lines.erase(editorState.lines.begin() + editorState.cursorLine + 1);
                editorState.isDirty = true;
                onTextChanged();
                for (auto& ec : extraCursors)
                    if (ec.line > editorState.cursorLine) ec.line--;
            }
        }
    }

    // --- Escape: clear extra cursors ---
    if (ImGui::IsKeyPressed(ImGuiKey_Escape) && !extraCursors.empty())
        extraCursors.clear();

    // --- Ctrl+Alt+Up/Down: add extra cursor ---
    if (io.KeyCtrl && io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_UpArrow))
    {
        size_t topLine = editorState.cursorLine;
        for (const auto& ec : extraCursors)
            if (ec.line < topLine) topLine = ec.line;
        if (topLine > 0)
        {
            ExtraCursor ec;
            ec.line = topLine - 1;
            ec.column = std::min(editorState.cursorColumn, editorState.lines[ec.line].length());
            extraCursors.push_back(ec);
        }
        return;
    }
    if (io.KeyCtrl && io.KeyAlt && ImGui::IsKeyPressed(ImGuiKey_DownArrow))
    {
        size_t botLine = editorState.cursorLine;
        for (const auto& ec : extraCursors)
            if (ec.line > botLine) botLine = ec.line;
        if (botLine + 1 < editorState.lines.size())
        {
            ExtraCursor ec;
            ec.line = botLine + 1;
            ec.column = std::min(editorState.cursorColumn, editorState.lines[ec.line].length());
            extraCursors.push_back(ec);
        }
        return;
    }

    // --- Completion navigation with Ctrl+Up/Down ---
    if (showCompletions && io.KeyCtrl)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
        {
            if (selectedCompletion > 0) selectedCompletion--;
            return;
        }
        if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
        {
            if (selectedCompletion < static_cast<int>(completionItems.size()) - 1)
                selectedCompletion++;
            return;
        }
    }

    // --- Arrow keys with Shift selection + extra cursor movement ---
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
    {
        if (io.KeyShift)
        {
            if (!editorState.hasSelection) {
                editorState.selectionStartLine = editorState.cursorLine;
                editorState.selectionStartColumn = editorState.cursorColumn;
                editorState.hasSelection = true;
            }
            if (editorState.cursorColumn > 0) editorState.cursorColumn--;
            else if (editorState.cursorLine > 0) {
                editorState.cursorLine--;
                editorState.cursorColumn = editorState.lines[editorState.cursorLine].length();
            }
            editorState.selectionEndLine = editorState.cursorLine;
            editorState.selectionEndColumn = editorState.cursorColumn;
        }
        else
        {
            if (editorState.cursorColumn > 0) editorState.cursorColumn--;
            else if (editorState.cursorLine > 0) {
                editorState.cursorLine--;
                editorState.cursorColumn = editorState.lines[editorState.cursorLine].length();
            }
            editorState.hasSelection = false;
        }
        for (auto& ec : extraCursors)
        {
            if (ec.column > 0) ec.column--;
            else if (ec.line > 0) { ec.line--; ec.column = editorState.lines[ec.line].length(); }
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
    {
        if (io.KeyShift)
        {
            if (!editorState.hasSelection) {
                editorState.selectionStartLine = editorState.cursorLine;
                editorState.selectionStartColumn = editorState.cursorColumn;
                editorState.hasSelection = true;
            }
            if (editorState.cursorLine < editorState.lines.size() &&
                editorState.cursorColumn < editorState.lines[editorState.cursorLine].length())
                editorState.cursorColumn++;
            else if (editorState.cursorLine < editorState.lines.size() - 1) {
                editorState.cursorLine++; editorState.cursorColumn = 0;
            }
            editorState.selectionEndLine = editorState.cursorLine;
            editorState.selectionEndColumn = editorState.cursorColumn;
        }
        else
        {
            if (editorState.cursorLine < editorState.lines.size() &&
                editorState.cursorColumn < editorState.lines[editorState.cursorLine].length())
                editorState.cursorColumn++;
            else if (editorState.cursorLine < editorState.lines.size() - 1) {
                editorState.cursorLine++; editorState.cursorColumn = 0;
            }
            editorState.hasSelection = false;
        }
        for (auto& ec : extraCursors)
        {
            if (ec.line < editorState.lines.size() &&
                ec.column < editorState.lines[ec.line].length()) ec.column++;
            else if (ec.line < editorState.lines.size() - 1) { ec.line++; ec.column = 0; }
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
    {
        if (io.KeyShift)
        {
            if (!editorState.hasSelection) {
                editorState.selectionStartLine = editorState.cursorLine;
                editorState.selectionStartColumn = editorState.cursorColumn;
                editorState.hasSelection = true;
            }
            if (editorState.cursorLine > 0) {
                editorState.cursorLine--;
                editorState.cursorColumn = std::min(editorState.cursorColumn,
                    editorState.lines[editorState.cursorLine].length());
            }
            editorState.selectionEndLine = editorState.cursorLine;
            editorState.selectionEndColumn = editorState.cursorColumn;
        }
        else
        {
            if (editorState.cursorLine > 0) {
                editorState.cursorLine--;
                editorState.cursorColumn = std::min(editorState.cursorColumn,
                    editorState.lines[editorState.cursorLine].length());
            }
            editorState.hasSelection = false;
        }
        for (auto& ec : extraCursors)
        {
            if (ec.line > 0) {
                ec.line--;
                ec.column = std::min(ec.column, editorState.lines[ec.line].length());
            }
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
    {
        if (io.KeyShift)
        {
            if (!editorState.hasSelection) {
                editorState.selectionStartLine = editorState.cursorLine;
                editorState.selectionStartColumn = editorState.cursorColumn;
                editorState.hasSelection = true;
            }
            if (editorState.cursorLine < editorState.lines.size() - 1) {
                editorState.cursorLine++;
                editorState.cursorColumn = std::min(editorState.cursorColumn,
                    editorState.lines[editorState.cursorLine].length());
            }
            editorState.selectionEndLine = editorState.cursorLine;
            editorState.selectionEndColumn = editorState.cursorColumn;
        }
        else
        {
            if (editorState.cursorLine < editorState.lines.size() - 1) {
                editorState.cursorLine++;
                editorState.cursorColumn = std::min(editorState.cursorColumn,
                    editorState.lines[editorState.cursorLine].length());
            }
            editorState.hasSelection = false;
        }
        for (auto& ec : extraCursors)
        {
            if (ec.line < editorState.lines.size() - 1) {
                ec.line++;
                ec.column = std::min(ec.column, editorState.lines[ec.line].length());
            }
        }
    }

    // Close completion popup if cursor moved to different line
    if (showCompletions && editorState.cursorLine != completionOriginLine)
    {
        showCompletions = false;
        completionItems.clear();
    }

    // --- Home/End with Shift + extra cursors ---
    if (ImGui::IsKeyPressed(ImGuiKey_Home))
    {
        if (io.KeyShift)
        {
            if (!editorState.hasSelection) {
                editorState.selectionStartLine = editorState.cursorLine;
                editorState.selectionStartColumn = editorState.cursorColumn;
                editorState.hasSelection = true;
            }
            editorState.cursorColumn = 0;
            editorState.selectionEndLine = editorState.cursorLine;
            editorState.selectionEndColumn = editorState.cursorColumn;
        }
        else
        {
            editorState.cursorColumn = 0;
            editorState.hasSelection = false;
        }
        for (auto& ec : extraCursors) ec.column = 0;
        editorState.needsScrollToCursor = true;
    }
    if (ImGui::IsKeyPressed(ImGuiKey_End))
    {
        size_t lineLen = editorState.cursorLine < editorState.lines.size()
            ? editorState.lines[editorState.cursorLine].length() : 0;
        if (io.KeyShift)
        {
            if (!editorState.hasSelection) {
                editorState.selectionStartLine = editorState.cursorLine;
                editorState.selectionStartColumn = editorState.cursorColumn;
                editorState.hasSelection = true;
            }
            editorState.cursorColumn = lineLen;
            editorState.selectionEndLine = editorState.cursorLine;
            editorState.selectionEndColumn = editorState.cursorColumn;
        }
        else
        {
            editorState.cursorColumn = lineLen;
            editorState.hasSelection = false;
        }
        for (auto& ec : extraCursors)
            ec.column = ec.line < editorState.lines.size()
                ? editorState.lines[ec.line].length() : 0;
        editorState.needsScrollToCursor = true;
    }

    // --- Tab (when completions not showing) ---
    if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !showCompletions)
    {
        pushUndo();
        if (editorState.hasSelection) deleteSelection();
        // Multi-cursor tab: insert at extra cursors bottom-to-top
        std::sort(extraCursors.begin(), extraCursors.end(),
            [](const ExtraCursor& a, const ExtraCursor& b) {
                return a.line > b.line || (a.line == b.line && a.column > b.column);
            });
        for (auto& ec : extraCursors)
        {
            if (ec.line < editorState.lines.size())
            {
                editorState.lines[ec.line].insert(ec.column, "    ");
                ec.column += 4;
            }
        }
        insertTextAtCursor("    ");
        io.InputQueueCharacters.clear();
        return;
    }

    // --- Completion acceptance: Tab or Enter ---
    if (showCompletions)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Tab) || ImGui::IsKeyPressed(ImGuiKey_Enter))
        {
            if (selectedCompletion < static_cast<int>(completionItems.size()))
            {
                const std::string& line = editorState.lines[editorState.cursorLine];
                size_t wordStart = editorState.cursorColumn;
                while (wordStart > 0 && (std::isalnum(line[wordStart - 1]) ||
                       line[wordStart - 1] == '_')) wordStart--;
                std::string& cur = editorState.lines[editorState.cursorLine];
                cur.erase(wordStart, editorState.cursorColumn - wordStart);
                cur.insert(wordStart, completionItems[selectedCompletion].text);
                editorState.cursorColumn = wordStart +
                    completionItems[selectedCompletion].text.length();
                editorState.isDirty = true;
                onTextChanged();
            }
            showCompletions = false;
            io.InputQueueCharacters.clear();
            return;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            showCompletions = false;
        }
    }

    // --- Character input ---
    // Snapshot undo once before processing typed characters
    if (io.InputQueueCharacters.Size > 0) pushUndo();

    for (int i = 0; i < io.InputQueueCharacters.Size; ++i)
    {
        ImWchar c = io.InputQueueCharacters[i];
        if (c == '\t')
        {
            if (editorState.hasSelection) deleteSelection();
            insertTextAtCursor("    ");
        }
        else if (c != 0 && c >= 32)
        {
            std::string s(1, static_cast<char>(c));
            bool shouldAutoPair = false;
            std::string closing;
            switch (c)
            {
                case '(': shouldAutoPair = true; closing = ")"; break;
                case '{': shouldAutoPair = true; closing = "}"; break;
                case '[': shouldAutoPair = true; closing = "]"; break;
                case '"': shouldAutoPair = true; closing = "\""; break;
                case '\'': shouldAutoPair = true; closing = "'"; break;
            }

            // Insert at extra cursors bottom-to-top
            std::sort(extraCursors.begin(), extraCursors.end(),
                [](const ExtraCursor& a, const ExtraCursor& b) {
                    return a.line > b.line || (a.line == b.line && a.column > b.column);
                });
            for (auto& ec : extraCursors)
            {
                if (ec.line < editorState.lines.size())
                {
                    std::string& eline = editorState.lines[ec.line];
                    if (shouldAutoPair)
                    {
                        eline.insert(ec.column, s + closing);
                        ec.column += 1;
                    }
                    else
                    {
                        eline.insert(ec.column, s);
                        ec.column += 1;
                    }
                }
            }

            if (shouldAutoPair)
            {
                if (editorState.hasSelection) deleteSelection();
                insertTextAtCursor(s);
                size_t sl = editorState.cursorLine, sc = editorState.cursorColumn;
                insertTextAtCursor(closing);
                editorState.cursorLine = sl;
                editorState.cursorColumn = sc;
                syncEditorToActiveTab();
            }
            else
            {
                insertTextAtCursor(s);
            }

            if (c == '.' || std::isalpha(c)) updateCompletions();
        }
    }
}

// ============================================================================
// Tab Management
// ============================================================================

int BaseTextEditor::findTabByPath(const std::filesystem::path& path)
{
    for (size_t i = 0; i < tabs.size(); ++i)
    {
        if (tabs[i].filePath == path) return static_cast<int>(i);
    }
    return -1;
}

void BaseTextEditor::openTab(const std::filesystem::path& path)
{
    int idx = findTabByPath(path);
    if (idx >= 0)
    {
        setActiveTab(idx);
        return;
    }

    EditorTab t(path);
    tabs.push_back(std::move(t));
    loadTabContent(tabs.back());
    setActiveTab(static_cast<int>(tabs.size()) - 1);
}

void BaseTextEditor::closeTab(int tabIndex)
{
    if (tabIndex < 0 || tabIndex >= static_cast<int>(tabs.size())) return;
    
    tabs.erase(tabs.begin() + tabIndex);
    
    if (tabs.empty())
    {
        activeTabIndex = -1;
    }
    else if (activeTabIndex >= static_cast<int>(tabs.size()))
    {
        activeTabIndex = static_cast<int>(tabs.size()) - 1;
    }
    else if (activeTabIndex > tabIndex)
    {
        activeTabIndex--;
    }
    
    if (activeTabIndex >= 0 && activeTabIndex < static_cast<int>(tabs.size()))
    {
        tabs[activeTabIndex].isActive = true;
        syncActiveTabToEditor();
    }
}

void BaseTextEditor::setActiveTab(int tabIndex)
{
    if (tabIndex < 0 || tabIndex >= static_cast<int>(tabs.size())) return;
    
    syncEditorToActiveTab();
    
    for (auto& t : tabs) t.isActive = false;
    
    activeTabIndex = tabIndex;
    tabs[activeTabIndex].isActive = true;
    syncActiveTabToEditor();
}

EditorTab* BaseTextEditor::getActiveTab()
{
    if (activeTabIndex < 0 || activeTabIndex >= static_cast<int>(tabs.size())) return nullptr;
    return &tabs[activeTabIndex];
}

const EditorTab* BaseTextEditor::getActiveTab() const
{
    if (activeTabIndex < 0 || activeTabIndex >= static_cast<int>(tabs.size())) return nullptr;
    return &tabs[activeTabIndex];
}

void BaseTextEditor::syncActiveTabToEditor()
{
    EditorTab* t = getActiveTab();
    if (t)
    {
        editorState = t->editorState;
        syntaxErrors = t->syntaxErrors;
    }
}

void BaseTextEditor::syncEditorToActiveTab()
{
    EditorTab* t = getActiveTab();
    if (t)
    {
        t->editorState = editorState;
        t->syntaxErrors = syntaxErrors;
        // Dirty indicator: append/remove '*' suffix on display name
        if (t->editorState.isDirty)
        {
            if (!t->displayName.empty() && t->displayName.back() != '*')
                t->displayName += "*";
        }
        else
        {
            if (!t->displayName.empty() && t->displayName.back() == '*')
                t->displayName.pop_back();
        }
    }
}

void BaseTextEditor::loadTabContent(EditorTab& tab)
{
    tab.editorState.lines.clear();
    
    std::ifstream file(tab.filePath);
    if (file.is_open())
    {
        std::string line;
        while (std::getline(file, line))
        {
            tab.editorState.lines.push_back(line);
        }
        file.close();
        tab.editorState.isLoadedInMemory = true;
    }
    else
    {
        tab.editorState.lines.push_back("");
        tab.editorState.isLoadedInMemory = true;
        tab.editorState.isDirty = true;
    }
    
    if (tab.editorState.lines.empty())
        tab.editorState.lines.push_back("");
    
    tab.editorState.cursorLine = 0;
    tab.editorState.cursorColumn = 0;
    tab.editorState.scrollY = 0.0f;
    tab.editorState.scrollX = 0.0f;
    tab.editorState.hasSelection = false;
    tab.editorState.currentFile = tab.filePath.string();
    tab.editorState.isDirty = false;
}

void BaseTextEditor::saveActiveTab()
{
    EditorTab* activeTab = getActiveTab();
    if (!activeTab) return;

    std::string out;
    for (size_t i = 0; i < activeTab->editorState.lines.size(); ++i)
    {
        out += activeTab->editorState.lines[i];
        if (i < activeTab->editorState.lines.size() - 1) out += '\n';
    }

    std::ofstream file(activeTab->filePath);
    if (file.is_open())
    {
        file << out;
        file.close();
        activeTab->editorState.isDirty = false;
        activeTab->displayName = activeTab->filePath.filename().string();
        syncActiveTabToEditor();
    }
}

// ============================================================================
// Content Access (for Vault integration)
// ============================================================================

void BaseTextEditor::setContent(const std::string& content)
{
    // Ensure there is an active tab so drawEditor() won't bail out
    if (tabs.empty())
    {
        EditorTab t(std::filesystem::path(""));
        t.displayName = "Content";
        tabs.push_back(std::move(t));
        activeTabIndex = 0;
    }

    editorState.lines.clear();
    
    if (content.empty())
    {
        editorState.lines.push_back("");
    }
    else
    {
        std::string line;
        for (char c : content)
        {
            if (c == '\n')
            {
                editorState.lines.push_back(line);
                line.clear();
            }
            else
            {
                line += c;
            }
        }
        editorState.lines.push_back(line);
    }
    
    editorState.cursorLine = 0;
    editorState.cursorColumn = 0;
    editorState.scrollY = 0.0f;
    editorState.scrollX = 0.0f;
    editorState.hasSelection = false;
    editorState.isDirty = false;
    editorState.isLoadedInMemory = true;
    
    undoStack.clear();
    redoStack.clear();

    syncEditorToActiveTab();
}

std::string BaseTextEditor::getEditorContent() const
{
    std::string content;
    for (size_t i = 0; i < editorState.lines.size(); ++i)
    {
        content += editorState.lines[i];
        if (i < editorState.lines.size() - 1) content += '\n';
    }
    return content;
}

// ============================================================================
// Main Drawing Function
// ============================================================================

ImVec2 BaseTextEditor::drawEditor()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    updateFontMetrics();
    visibleHeight = avail.y;

    EditorTab* active = getActiveTab();
    if (!active)
    {
        ImGui::Dummy(avail);
        return origin;
    }

    if (editorState.wantsFocus)
    {
        ImGui::SetKeyboardFocusHere();
        editorState.wantsFocus = false;
        editorState.hasFocus = true;
    }

    // Draw background
    drawList->AddRectFilled(origin,
        ImVec2(origin.x + avail.x, origin.y + avail.y), backgroundColor);

    // Reserve area for input
    ImGui::InvisibleButton("##TextArea", avail);
    bool isHovered = ImGui::IsItemHovered();

    // Layout: gutter + text
    float gutterWidth = 60.0f;
    ImVec2 gutterOrigin = origin;
    float baseTextX = origin.x + gutterWidth + 8.0f;
    visibleWidth = std::max(0.0f, avail.x - gutterWidth - 8.0f);
    ImVec2 textOrigin = ImVec2(baseTextX - editorState.scrollX, origin.y + 4.0f);

    // Handle mouse input
    handleMouseInput(origin, textOrigin, avail, isHovered);

    // Draw components
    drawLineNumbers(drawList, ImVec2(gutterOrigin.x + 4.0f, gutterOrigin.y + 4.0f), visibleHeight);
    drawTextContentWithSyntaxHighlighting(drawList, textOrigin, visibleHeight - 4.0f);
    drawSelection(drawList, textOrigin);
    drawCursor(drawList, textOrigin);
    drawSyntaxErrors(drawList, textOrigin);

    // Handle keyboard input
    handleKeyboardInput();

    // Scroll to cursor if needed
    if (editorState.needsScrollToCursor)
    {
        ensureCursorVisible();
        editorState.needsScrollToCursor = false;
    }

    syncEditorToActiveTab();

    // Subclass overlay hook (e.g., API docs button)
    drawEditorOverlay(textOrigin);

    // Completion popup
    drawCompletionPopup(textOrigin);

    ImGui::SetCursorScreenPos(ImVec2(origin.x + avail.x, origin.y + avail.y));
    return origin;
}

} // namespace Editors
