#include <Editors/Markdown/MarkdownEditor.hpp>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <regex>
#include <set>
#include <unordered_set>
#include <sstream>
#include <ctime>

using Editors::EditorTab;
using Editors::CompletionItem;
using Editors::EditorState;

MarkdownEditor::MarkdownEditor()
{
}

MarkdownEditor::~MarkdownEditor()
{
}

void MarkdownEditor::setSrc(std::filesystem::path source)
{
    src = source;

    // Update the file watcher when source directory changes
    updateFileWatcher();
}

void MarkdownEditor::updateFileWatcher()
{
    // Stop existing watcher

    // Collect all directories to watch
    std::vector<std::filesystem::path> watchDirectories;

    // Add source directory if it exists
    if (!src.empty() && std::filesystem::exists(src) && std::filesystem::is_directory(src))
    {
        watchDirectories.push_back(src);
    }

    // Start watching the collected directories
    if (!watchDirectories.empty())
    {
    }
}

// Use the FileBackend / VaultFileBackend instead of global vaultPath/getProgramPath
// This keeps MarkdownEditor storage access consistent with new editor backends.
#include <FileBackend.hpp>
#include <FileBackends/VaultFileBackend.hpp>

void MarkdownEditor::updateVaultDirectories()
{
    // Attempt to locate vault scripts directory via the Vault API if available.
    // Fallback: keep current src/classpath settings untouched if no vault is available.
    std::vector<std::filesystem::path> newClassPaths;

    try {
        // If a Vault is present, construct a VaultFileBackend to query for standard directories
        // We don't require a global `vaultPath` or `getProgramPath()` here.
        // Find a Vault via Vault::Open is heavy; instead, check for known Vault helpers if present.
        // As a conservative approach, do nothing here — other code paths should set src/classPaths
        // via `setSrc()` or `setClassPath()` using the FileBackend.
    }
    catch (...) {
        // Ignore any exceptions — keep editor state as-is
    }

    // No automatic changes performed here. If callers want to update to vault-backed
    // directories they should call `setSrc()` and `setClassPath()` with paths resolved
    // through a FileBackend or VaultFileBackend.
}

void MarkdownEditor::setWorkingFile(std::filesystem::path file)
{
    // Legacy method - now delegates to openFile
    openFile(file);
}
void MarkdownEditor::openFile(std::filesystem::path file)
{
    openTab(file);
}
void MarkdownEditor::updateCompletions()
{
    completionItems.clear();

    if (editorState.cursorLine >= editorState.lines.size())
    {
        showCompletions = false;
        return;
    }

    const std::string &line = editorState.lines[editorState.cursorLine];
    if (editorState.cursorColumn == 0)
    {
        showCompletions = false;
        return;
    }

    std::string beforeCursor = line.substr(0, editorState.cursorColumn);
    
    // Check for effect tag completion (after '<')
    size_t lastOpen = beforeCursor.rfind('<');
    if (lastOpen != std::string::npos && beforeCursor.find('>', lastOpen) == std::string::npos)
    {
        std::string tagPrefix = beforeCursor.substr(lastOpen + 1);
        // Remove leading '/' if it's a closing tag
        bool isClosingTag = !tagPrefix.empty() && tagPrefix[0] == '/';
        if (isClosingTag) tagPrefix = tagPrefix.substr(1);
        
        generateContextAwareCompletions(tagPrefix, true, isClosingTag ? "closing" : "effect");
        showCompletions = !completionItems.empty();
        selectedCompletion = 0;
        return;
    }
    
    // Check for link/image completion
    if (beforeCursor.find('[') != std::string::npos && beforeCursor.rfind(']') == std::string::npos)
    {
        // Inside link text - no completions needed
        showCompletions = false;
        return;
    }
    
    // Line-start completions (for empty lines or start of line)
    size_t nonSpace = beforeCursor.find_first_not_of(' ');
    if (nonSpace == std::string::npos || nonSpace == beforeCursor.length() - 1)
    {
        generateContextAwareCompletions("", false, "linestart");
        showCompletions = !completionItems.empty();
        selectedCompletion = 0;
        return;
    }

    showCompletions = false;
}

void MarkdownEditor::parseCurrentFileForContext()
{
    // No additional context parsing needed for markdown
}
void MarkdownEditor::generateContextAwareCompletions(const std::string &prefix, bool isQualifiedAccess, const std::string &context)
{
    completionItems.clear();
    
    // Effect tag completions
    if (context == "effect" || context == "closing")
    {
        static const std::vector<std::pair<std::string, std::string>> effectTags = {
            {"fire", "Fire effect with particles"},
            {"electric", "Electric/lightning effect"},
            {"rainbow", "Rainbow color cycling"},
            {"shake", "Shaking text"},
            {"wave", "Wave motion effect"},
            {"glow", "Glowing text"},
            {"neon", "Neon glow effect"},
            {"sparkle", "Sparkle particles"},
            {"snow", "Falling snow particles"},
            {"blood", "Dripping blood effect"},
            {"water", "Water / liquid fluid"},
            {"honey", "Thick honey / goo"},
            {"toxic_goo", "Toxic goo (neon green)"},
            {"ice", "Ice/frost effect"},
            {"magic", "Magical particles"},
            {"ghost", "Ghostly fade effect"},
            {"underwater", "Underwater bubbles"},
            {"golden", "Golden glow"},
            {"toxic", "Toxic/poison effect"},
            {"crystal", "Crystal shimmer"},
            {"storm", "Storm/lightning"},
            {"ethereal", "Ethereal glow"},
            {"lava", "Lava/magma effect"},
            {"frost", "Frost effect"},
            {"void", "Dark void effect"},
            {"holy", "Holy light effect"},
            {"matrix", "Matrix style"},
            {"disco", "Disco colors"},
            {"glitch", "Glitch effect"},
        };
        
        std::string lowerPrefix = prefix;
        std::transform(lowerPrefix.begin(), lowerPrefix.end(), lowerPrefix.begin(), ::tolower);
        
        for (const auto& [tag, desc] : effectTags)
        {
            if (lowerPrefix.empty() || tag.find(lowerPrefix) == 0)
            {
                CompletionItem item;
                if (context == "closing")
                {
                    item.text = "/" + tag + ">";
                }
                else
                {
                    item.text = tag + ">";
                }
                item.description = desc;
                item.type = CompletionItem::KEYWORD;
                completionItems.push_back(item);
            }
        }
    }
    // Line-start completions
    else if (context == "linestart")
    {
        static const std::vector<std::pair<std::string, std::string>> lineStarts = {
            {"# ", "Heading 1"},
            {"## ", "Heading 2"},
            {"### ", "Heading 3"},
            {"#### ", "Heading 4"},
            {"##### ", "Heading 5"},
            {"###### ", "Heading 6"},
            {"- ", "Unordered list item"},
            {"* ", "Unordered list item (alt)"},
            {"1. ", "Ordered list item"},
            {"> ", "Blockquote"},
            {"```", "Code block"},
            {"---", "Horizontal rule"},
            {"| ", "Table row"},
            {"[ ] ", "Checkbox (unchecked)"},
            {"[x] ", "Checkbox (checked)"},
        };
        
        for (const auto& [text, desc] : lineStarts)
        {
            CompletionItem item;
            item.text = text;
            item.description = desc;
            item.type = CompletionItem::KEYWORD;
            completionItems.push_back(item);
        }
    }
}

void MarkdownEditor::updateSyntaxErrors()
{
    syntaxErrors.clear();

    // Check for Markdown-specific syntax issues
    bool inCodeBlock = false;
    size_t codeBlockStartLine = 0;
    
    // Track open effect tags: tag name -> (line number, column)
    std::vector<std::tuple<std::string, size_t, size_t>> openEffectTags;
    
    static const std::unordered_set<std::string> validEffectTags = {
        "fire", "electric", "rainbow", "shake", "wave", "glow", "neon",
        "sparkle", "snow", "blood", "water", "honey", "toxic_goo", "ice", "magic", "ghost", "underwater",
        "golden", "toxic", "crystal", "storm", "ethereal", "lava", "frost",
        "void", "holy", "matrix", "disco", "glitch"
    };

    for (size_t lineNum = 0; lineNum < editorState.lines.size(); ++lineNum)
    {
        const std::string &line = editorState.lines[lineNum];
        
        // Check for code block toggles
        std::string trimmed = line;
        size_t trimStart = line.find_first_not_of(' ');
        if (trimStart != std::string::npos) trimmed = line.substr(trimStart);
        
        if (trimmed.rfind("```", 0) == 0)
        {
            if (!inCodeBlock)
            {
                inCodeBlock = true;
                codeBlockStartLine = lineNum;
            }
            else
            {
                inCodeBlock = false;
            }
            continue;
        }
        
        // Skip effect tag checking inside code blocks
        if (inCodeBlock) continue;
        
        // Scan for effect tags
        size_t pos = 0;
        while (pos < line.length())
        {
            size_t tagStart = line.find('<', pos);
            if (tagStart == std::string::npos) break;
            
            size_t tagEnd = line.find('>', tagStart);
            if (tagEnd == std::string::npos)
            {
                pos = tagStart + 1;
                continue;
            }
            
            std::string tagContent = line.substr(tagStart + 1, tagEnd - tagStart - 1);
            bool isClosing = !tagContent.empty() && tagContent[0] == '/';
            std::string tagName = isClosing ? tagContent.substr(1) : tagContent;
            
            // Check if it's a valid effect tag
            if (validEffectTags.count(tagName))
            {
                if (isClosing)
                {
                    // Find matching open tag
                    bool found = false;
                    for (auto it = openEffectTags.rbegin(); it != openEffectTags.rend(); ++it)
                    {
                        if (std::get<0>(*it) == tagName)
                        {
                            openEffectTags.erase(std::next(it).base());
                            found = true;
                            break;
                        }
                    }
                    if (!found)
                    {
                        SyntaxError error(lineNum + 1, tagStart + 1, "Unmatched closing tag </" + tagName + ">");
                        syntaxErrors.push_back(error);
                    }
                }
                else
                {
                    openEffectTags.push_back({tagName, lineNum, tagStart});
                }
            }
            
            pos = tagEnd + 1;
        }
    }
    
    // Report unclosed code block
    if (inCodeBlock)
    {
        SyntaxError error(codeBlockStartLine + 1, 1, "Unclosed code block");
        syntaxErrors.push_back(error);
    }
    
    // Report unclosed effect tags
    for (const auto& [tagName, line, col] : openEffectTags)
    {
        SyntaxError error(line + 1, col + 1, "Unclosed effect tag <" + tagName + ">");
        syntaxErrors.push_back(error);
    }
}

ImVec2 MarkdownEditor::draw()
{
    // Check if vault has changed and update directories accordingly
    

    // Use internal gridOrigin
    // Setup child window with flags to ensure mouse events are captured and no padding
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("MarkdownEditorChild", avail, true, childFlags);
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImVec2 childOrigin = ImGui::GetCursorScreenPos();
    ImVec2 childSize = avail;

    // Draw tab bar
    if (ImGui::BeginTabBar("MarkdownEditorTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_TabListPopupButton))
    {

        // Draw existing tabs
        for (int i = 0; i < tabs.size(); ++i)
        {
            bool isOpen = true;
            ImGuiTabItemFlags tabFlags = 0;
            if (tabs[i].editorState.isDirty)
            {
                tabFlags |= ImGuiTabItemFlags_UnsavedDocument;
            }

            if (ImGui::BeginTabItem(tabs[i].displayName.c_str(), &isOpen, tabFlags))
            {
                if (activeTabIndex != i)
                {
                    setActiveTab(i);
                }

                // Draw the editor content for the active tab
                drawEditor();

                ImGui::EndTabItem();
            }

            // Handle tab close
            if (!isOpen)
            {
                closeTab(i);
                break; // Break to avoid iterating invalid indices
            }
        }

        // Add a "+" button to create new tab
        if (ImGui::TabItemButton("+", ImGuiTabItemFlags_Trailing | ImGuiTabItemFlags_NoTooltip))
        {
            // Create a new untitled file
            static int untitledCounter = 1;
            std::string newFileName = "Untitled" + std::to_string(untitledCounter++) + ".markdown";
            openTab(std::filesystem::path("src") / newFileName);
        }

        ImGui::EndTabBar();
    }

    // If no tabs are open, show a welcome message
    if (tabs.empty())
    {
        ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x * 0.5f - 100);
        ImGui::SetCursorPosY(ImGui::GetContentRegionAvail().y * 0.5f - 50);
        ImGui::Text("No files open");
        ImGui::Text("Use the + button or open a file to start editing");

        // Display some program structure information
        ImGui::Separator();
        
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();

    // Return the screen position of the grid for drop coordinate calculation
    return childOrigin;
}
#include <Util/ErrorStream.hpp>
#include <Util/StandardStream.hpp>
void MarkdownEditor::syncPreviewSource()
{
    std::string content = getEditorContent();
    if (content != m_previewSourceCache) {
        m_previewSourceCache = content;
        m_preview.setSource(content);
    }
}

ImVec2 MarkdownEditor::drawPreview()
{
    // Sync editor content to preview
    syncPreviewSource();
    
    // Render the 2.5D FBO preview
    return m_preview.render();
}

void MarkdownEditor::handlePreviewMouseInput()
{
    ImGuiIO &io = ImGui::GetIO();
    bool isHovered = ImGui::IsItemHovered();
    bool isClicked = ImGui::IsItemClicked(0);

    if (isClicked)
    {
        // For future interactivity, e.g., clicking on links in the preview
    }
}

static MarkdownEditor instance;
MarkdownEditor &MarkdownEditor::get()
{
    return instance;
}

void MarkdownEditor::tokenizeInlineMarkdown(std::vector<Editors::SyntaxToken>& tokens,
                                             const std::string& text, size_t offset)
{
    size_t pos = 0;
    while (pos < text.length())
    {
        // Effect tags like <fire>, </fire>
        if (text[pos] == '<')
        {
            size_t tagEnd = text.find('>', pos);
            if (tagEnd != std::string::npos)
            {
                std::string tagContent = text.substr(pos + 1, tagEnd - pos - 1);
                std::string tagName = tagContent;
                if (!tagName.empty() && tagName[0] == '/') tagName = tagName.substr(1);

                static const std::unordered_set<std::string> effectTags = {
                    "fire", "electric", "rainbow", "shake", "wave", "glow", "neon",
                    "sparkle", "snow", "blood", "ice", "magic", "ghost", "underwater",
                    "golden", "toxic", "crystal", "storm", "ethereal", "lava", "frost",
                    "void", "holy", "matrix", "disco", "glitch"
                };

                if (effectTags.count(tagName))
                {
                    tokens.push_back({offset + pos, tagEnd - pos + 1, classColor});
                    pos = tagEnd + 1;
                    continue;
                }
            }
        }

        // Inline code `code`
        if (text[pos] == '`')
        {
            size_t endBt = text.find('`', pos + 1);
            if (endBt != std::string::npos)
            {
                tokens.push_back({offset + pos, endBt - pos + 1, stringColor});
                pos = endBt + 1;
                continue;
            }
        }

        // Bold **text**
        if (pos + 1 < text.length() && text[pos] == '*' && text[pos + 1] == '*')
        {
            size_t end = text.find("**", pos + 2);
            if (end != std::string::npos)
            {
                tokens.push_back({offset + pos, end - pos + 2, keywordColor});
                pos = end + 2;
                continue;
            }
        }

        // Bold __text__
        if (pos + 1 < text.length() && text[pos] == '_' && text[pos + 1] == '_')
        {
            size_t end = text.find("__", pos + 2);
            if (end != std::string::npos)
            {
                tokens.push_back({offset + pos, end - pos + 2, keywordColor});
                pos = end + 2;
                continue;
            }
        }

        // Italic *text*
        if (text[pos] == '*' && (pos + 1 >= text.length() || text[pos + 1] != '*'))
        {
            size_t end = pos + 1;
            while (end < text.length())
            {
                if (text[end] == '*' && (end + 1 >= text.length() || text[end + 1] != '*'))
                    break;
                end++;
            }
            if (end < text.length())
            {
                tokens.push_back({offset + pos, end - pos + 1, methodColor});
                pos = end + 1;
                continue;
            }
        }

        // Italic _text_
        if (text[pos] == '_' && (pos + 1 >= text.length() || text[pos + 1] != '_'))
        {
            size_t end = pos + 1;
            while (end < text.length())
            {
                if (text[end] == '_' && (end + 1 >= text.length() || text[end + 1] != '_'))
                    break;
                end++;
            }
            if (end < text.length())
            {
                tokens.push_back({offset + pos, end - pos + 1, methodColor});
                pos = end + 1;
                continue;
            }
        }

        // Links [text](url)
        if (text[pos] == '[')
        {
            size_t closeBracket = text.find(']', pos);
            if (closeBracket != std::string::npos && closeBracket + 1 < text.length() && text[closeBracket + 1] == '(')
            {
                size_t closeParen = text.find(')', closeBracket + 2);
                if (closeParen != std::string::npos)
                {
                    tokens.push_back({offset + pos, closeParen - pos + 1, numberColor});
                    pos = closeParen + 1;
                    continue;
                }
            }
        }

        // Regular text – collect until next special character
        size_t next = pos;
        while (next < text.length())
        {
            char c = text[next];
            if (c == '*' || c == '_' || c == '`' || c == '[' || c == '<')
                break;
            next++;
        }

        if (next > pos)
        {
            tokens.push_back({offset + pos, next - pos, textColor});
            pos = next;
        }
        else
        {
            // Single special char that didn't match any pattern
            tokens.push_back({offset + pos, 1, textColor});
            pos++;
        }
    }
}

void MarkdownEditor::beginTokenize(size_t startLine)
{
    mdInCodeBlock = false;
    mdCodeBlockLang.clear();
    // Pre-scan lines before the visible range for code-block state
    for (size_t i = 0; i < startLine && i < editorState.lines.size(); ++i)
    {
        const std::string& line = editorState.lines[i];
        std::string trimmed = line;
        size_t indent = line.find_first_not_of(' ');
        if (indent != std::string::npos) trimmed = line.substr(indent);
        if (trimmed.rfind("```", 0) == 0)
        {
            mdInCodeBlock = !mdInCodeBlock;
            if (mdInCodeBlock && trimmed.length() > 3)
                mdCodeBlockLang = trimmed.substr(3);
        }
    }
}

std::vector<Editors::SyntaxToken> MarkdownEditor::tokenizeLine(const std::string& line, size_t lineIndex)
{
    using Editors::SyntaxToken;
    std::vector<SyntaxToken> tokens;
    if (line.empty()) return tokens;

    // Check for code-block delimiter
    std::string trimmed = line;
    size_t indent = line.find_first_not_of(' ');
    if (indent != std::string::npos) trimmed = line.substr(indent);

    if (trimmed.rfind("```", 0) == 0)
    {
        tokens.push_back({0, line.size(), commentColor});
        mdInCodeBlock = !mdInCodeBlock;
        if (mdInCodeBlock && trimmed.length() > 3)
            mdCodeBlockLang = trimmed.substr(3);
        return tokens;
    }

    // Inside code block – whole line as string
    if (mdInCodeBlock)
    {
        tokens.push_back({0, line.size(), stringColor});
        return tokens;
    }

    // Heading (# … ######)
    if (!line.empty() && line[0] == '#')
    {
        size_t level = 0;
        while (level < line.length() && line[level] == '#') level++;
        if (level <= 6 && level < line.length() && line[level] == ' ')
        {
            tokens.push_back({0, level + 1, keywordColor});
            if (level + 1 < line.size())
                tokens.push_back({level + 1, line.size() - level - 1, classColor});
            return tokens;
        }
    }

    // Blockquote
    if (!line.empty() && line[0] == '>')
    {
        tokens.push_back({0, line.size(), commentColor});
        return tokens;
    }

    // Horizontal rule
    if (trimmed == "---" || trimmed == "***" || trimmed == "___")
    {
        tokens.push_back({0, line.size(), operatorColor});
        return tokens;
    }

    // List items (-, *, +, 1.)
    std::string listTrimmed = line;
    size_t listIndent = line.find_first_not_of(' ');
    if (listIndent != std::string::npos) listTrimmed = line.substr(listIndent);

    bool isList = false;
    size_t listMarkerEnd = 0;
    if (!listTrimmed.empty())
    {
        if ((listTrimmed[0] == '-' || listTrimmed[0] == '*' || listTrimmed[0] == '+') &&
            listTrimmed.length() > 1 && listTrimmed[1] == ' ')
        {
            isList = true;
            listMarkerEnd = listIndent + 2;
        }
        else if (std::isdigit(listTrimmed[0]))
        {
            size_t dotPos = listTrimmed.find(". ");
            if (dotPos != std::string::npos && dotPos < 4)
            {
                isList = true;
                listMarkerEnd = listIndent + dotPos + 2;
            }
        }
    }

    if (isList)
    {
        tokens.push_back({0, listMarkerEnd, keywordColor});
        if (listMarkerEnd < line.size())
            tokenizeInlineMarkdown(tokens, line.substr(listMarkerEnd), listMarkerEnd);
        return tokens;
    }

    // Regular paragraph – inline markdown
    tokenizeInlineMarkdown(tokens, line, 0);
    return tokens;
}

void MarkdownEditor::onTextChanged()
{
    updateSyntaxErrors();
}
