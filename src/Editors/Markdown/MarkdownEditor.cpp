#include <Editors/Markdown/MarkdownEditor.hpp>
#include <fstream>
#include <cmath>
#include <algorithm>
#include <regex>
#include <set>
#include <unordered_set>
#include <sstream>
#include <ctime>

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

    // Add all classpath directories that exist
    for (const auto &classPath : classPaths)
    {
        if (std::filesystem::exists(classPath) && std::filesystem::is_directory(classPath))
        {
            watchDirectories.push_back(classPath);
        }
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

// Tab management methods
int MarkdownEditor::findTabByPath(const std::filesystem::path &path)
{
    for (int i = 0; i < tabs.size(); ++i)
    {
        if (tabs[i].filePath == path)
        {
            return i;
        }
    }
    return -1;
}

void MarkdownEditor::openTab(const std::filesystem::path &path)
{
    // Check if tab already exists
    int existingTabIndex = findTabByPath(path);
    if (existingTabIndex != -1)
    {
        setActiveTab(existingTabIndex);
        return;
    }

    // Create new tab
    tabs.emplace_back(path);
    int newTabIndex = tabs.size() - 1;

    // Load content for the new tab
    loadTabContent(tabs[newTabIndex]);

    // Set as active tab
    setActiveTab(newTabIndex);
}

void MarkdownEditor::closeTab(int tabIndex)
{
    if (tabIndex < 0 || tabIndex >= tabs.size())
    {
        return;
    }

    // Remove the tab
    tabs.erase(tabs.begin() + tabIndex);

    // Update active tab index
    if (tabs.empty())
    {
        activeTabIndex = -1;
    }
    else if (activeTabIndex >= tabs.size())
    {
        activeTabIndex = tabs.size() - 1;
    }
    else if (activeTabIndex > tabIndex)
    {
        activeTabIndex--;
    }

    // If we closed the active tab, make sure activeTabIndex points to a valid tab
    if (activeTabIndex >= 0 && activeTabIndex < tabs.size())
    {
        tabs[activeTabIndex].isActive = true;
    }
}

void MarkdownEditor::setActiveTab(int tabIndex)
{
    if (tabIndex < 0 || tabIndex >= tabs.size())
    {
        return;
    }

    // Save current editor state to the previously active tab
    syncEditorToActiveTab();

    // Deactivate all tabs
    for (auto &tab : tabs)
    {
        tab.isActive = false;
    }

    // Activate the selected tab
    activeTabIndex = tabIndex;
    tabs[activeTabIndex].isActive = true;

    // Load the active tab state into the editor
    syncActiveTabToEditor();
}

EditorTab *MarkdownEditor::getActiveTab()
{
    if (activeTabIndex >= 0 && activeTabIndex < tabs.size())
    {
        return &tabs[activeTabIndex];
    }
    return nullptr;
}

const EditorTab *MarkdownEditor::getActiveTab() const
{
    if (activeTabIndex >= 0 && activeTabIndex < tabs.size())
    {
        return &tabs[activeTabIndex];
    }
    return nullptr;
}

void MarkdownEditor::loadTabContent(EditorTab &tab)
{

    // Prefer reading via Vault if available through the file backend; otherwise, use filesystem.
    std::filesystem::path fullPath;
    bool useFilesystem = true;

    // Attempt to resolve via Vault if a Vault API exists.
    // If not available, fall back to local filesystem path relative to workspace.
    try {
        // If the project exposes a Vault pointer or manager, integrate here.
        // For now, fall back to filesystem-based behavior.
        fullPath = tab.filePath;
        useFilesystem = true;
    }
    catch (...) {
        fullPath = tab.filePath;
        useFilesystem = true;
    }

    tab.editorState.lines.clear();
    tab.editorState.cursorLine = 0;
    tab.editorState.cursorColumn = 0;
    tab.editorState.scrollY = 0.0f;
    tab.editorState.hasSelection = false;
    tab.editorState.currentFile = tab.filePath.string();
    tab.editorState.isDirty = false;
    tab.editorState.isLoadedInMemory = false;

    if (useFilesystem && std::filesystem::exists(fullPath))
    {
        std::ifstream file(fullPath);
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
            tab.editorState.lines.push_back("// Error: Cannot open file");
            tab.editorState.isLoadedInMemory = true;
        }
    }
    else
    {
        // Create new file (or vault-backed missing file)
        tab.editorState.lines.push_back("");
        tab.editorState.isLoadedInMemory = true;
        tab.editorState.isDirty = true;
    }

    if (tab.editorState.lines.empty())
    {
        tab.editorState.lines.push_back("");
    }
}

void MarkdownEditor::saveActiveTab()
{
    EditorTab *activeTab = getActiveTab();
    if (!activeTab || activeTab->editorState.currentFile.empty())
    {
        return;
    }

    // Save to filesystem by default. Integrate with FileBackend/VaultFileBackend later.
    std::filesystem::path fullPath = activeTab->filePath;

    // If editorState.currentFile is a relative path, prefer using that relative to cwd
    if (!activeTab->editorState.currentFile.empty() && activeTab->filePath.empty()) {
        fullPath = std::filesystem::path(activeTab->editorState.currentFile);
    }

    // Create directory if it doesn't exist
    std::filesystem::create_directories(fullPath.parent_path());

    std::ofstream file(fullPath);
    if (file.is_open())
    {
        for (size_t i = 0; i < activeTab->editorState.lines.size(); ++i)
        {
            file << activeTab->editorState.lines[i];
            if (i < activeTab->editorState.lines.size() - 1)
            {
                file << "\n";
            }
        }
        file.close();
        activeTab->editorState.isDirty = false;

        // Update the tab's display name to remove dirty indicator
        activeTab->displayName = activeTab->filePath.filename().string();
        
        // If this is the active tab, also update the main editor state
        if (activeTab->isActive)
        {
            editorState.isDirty = false;
        }
    }
}

void MarkdownEditor::openFile(std::filesystem::path file)
{
    openTab(file);
}

void MarkdownEditor::syncActiveTabToEditor()
{
    EditorTab *activeTab = getActiveTab();
    if (activeTab)
    {
        editorState = activeTab->editorState;
        syntaxErrors = activeTab->syntaxErrors;
    }
}

void MarkdownEditor::syncEditorToActiveTab()
{
    EditorTab *activeTab = getActiveTab();
    if (activeTab)
    {
        activeTab->editorState = editorState;
        activeTab->syntaxErrors = syntaxErrors;

        // Update display name to show dirty indicator
        if (activeTab->editorState.isDirty && !activeTab->displayName.empty() && activeTab->displayName.back() != '*')
        {
            activeTab->displayName += "*";
        }
        else if (!activeTab->editorState.isDirty && !activeTab->displayName.empty() && activeTab->displayName.back() == '*')
        {
            activeTab->displayName.pop_back();
        }
    }
}

void MarkdownEditor::updateFontMetrics()
{
    // Get current font from ImGui
    ImFont *font = ImGui::GetFont();
    if (font)
    {
        // Calculate character width based on a typical character ('M' is often used for monospace width)
        charWidth = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, "M").x;
        lineHeight = font->FontSize + ImGui::GetStyle().ItemSpacing.y * 0.5f;
    }
    else
    {
        // Fallback values if no font is available
        charWidth = 8.0f;
        lineHeight = 18.0f;
    }
}

void MarkdownEditor::forceReloadFromDisk()
{
    EditorTab *activeTab = getActiveTab();
    if (activeTab)
    {
        activeTab->editorState.isLoadedInMemory = false; // Force reload
        loadTabContent(*activeTab);
        // Update the main editor state
        editorState = activeTab->editorState;
        syntaxErrors = activeTab->syntaxErrors;
    }
}

void MarkdownEditor::saveEditorToFile()
{
    saveActiveTab();
}

void MarkdownEditor::insertTextAtCursor(const std::string &text)
{
    if (editorState.hasSelection)
    {
        deleteSelection();
    }

    if (editorState.cursorLine >= editorState.lines.size())
    {
        editorState.lines.resize(editorState.cursorLine + 1);
    }

    // Handle multi-line text insertion (e.g., pasted text)
    if (text.find('\n') != std::string::npos)
    {
        std::vector<std::string> lines;
        std::string currentLine;
        for (char c : text)
        {
            if (c == '\n')
            {
                lines.push_back(currentLine);
                currentLine.clear();
            }
            else
            {
                currentLine += c;
            }
        }
        lines.push_back(currentLine);

        // Insert the text
        std::string &line = editorState.lines[editorState.cursorLine];
        std::string afterCursor = line.substr(editorState.cursorColumn);
        line = line.substr(0, editorState.cursorColumn) + lines[0];

        // Insert additional lines if needed
        for (size_t i = 1; i < lines.size(); ++i)
        {
            editorState.lines.insert(editorState.lines.begin() + editorState.cursorLine + i, lines[i]);
        }

        // Update cursor position
        editorState.cursorLine += lines.size() - 1;
        editorState.cursorColumn = lines.back().length();

        // Append the text that was after the cursor
        editorState.lines[editorState.cursorLine] += afterCursor;
    }
    else
    {
        // Single line text insertion
        std::string &line = editorState.lines[editorState.cursorLine];
        line.insert(editorState.cursorColumn, text);
        editorState.cursorColumn += text.length();
    }

    editorState.isDirty = true;
    editorState.needsScrollToCursor = true;

    // Sync changes back to active tab
    syncEditorToActiveTab();

    updateSyntaxErrors();
    updateLiveProgramStructure(); // Update program structure with live content
    updateCompletions();
}

void MarkdownEditor::deleteSelection()
{
    if (!editorState.hasSelection)
    {
        return;
    }

    size_t startLine = std::min(editorState.selectionStartLine, editorState.selectionEndLine);
    size_t endLine = std::max(editorState.selectionStartLine, editorState.selectionEndLine);
    size_t startCol = editorState.selectionStartLine < editorState.selectionEndLine ? editorState.selectionStartColumn : editorState.selectionEndColumn;
    size_t endCol = editorState.selectionStartLine < editorState.selectionEndLine ? editorState.selectionEndColumn : editorState.selectionStartColumn;

    if (startLine == endLine)
    {
        // Single line deletion
        editorState.lines[startLine].erase(startCol, endCol - startCol);
    }
    else
    {
        // Multi-line deletion
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

    // Sync changes back to active tab
    syncEditorToActiveTab();

    updateSyntaxErrors();
    updateLiveProgramStructure(); // Update program structure with live content
}

void MarkdownEditor::moveCursor(int deltaLine, int deltaColumn)
{
    int newLine = static_cast<int>(editorState.cursorLine) + deltaLine;
    int newColumn = static_cast<int>(editorState.cursorColumn) + deltaColumn;

    newLine = std::max(0, std::min(newLine, static_cast<int>(editorState.lines.size()) - 1));

    if (newLine < static_cast<int>(editorState.lines.size()))
    {
        newColumn = std::max(0, std::min(newColumn, static_cast<int>(editorState.lines[newLine].length())));
    }
    else
    {
        newColumn = 0;
    }

    editorState.cursorLine = newLine;
    editorState.cursorColumn = newColumn;
    editorState.needsScrollToCursor = true;

    // Sync cursor position back to active tab
    syncEditorToActiveTab();
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
    updateLiveProgramStructure();
}

void MarkdownEditor::updateLiveProgramStructure()
{
    EditorTab *activeTab = getActiveTab();
    if (!activeTab)
    {
        return;
    }

    // Join all lines into a single string
    std::string content;
    for (size_t i = 0; i < activeTab->editorState.lines.size(); ++i)
    {
        content += activeTab->editorState.lines[i];
        if (i < activeTab->editorState.lines.size() - 1)
        {
            content += "\n";
        }
    }
}

void MarkdownEditor::updateLiveProgramStructureForAllTabs()
{
    // Update active tab first
    updateLiveProgramStructure();

    // Update all other tabs
    for (const auto &tab : tabs)
    {
        if (tab.isActive)
            continue; // Skip active tab (already updated above)

        // Join tab content
        std::string tabContent;
        for (size_t i = 0; i < tab.editorState.lines.size(); ++i)
        {
            tabContent += tab.editorState.lines[i];
            if (i < tab.editorState.lines.size() - 1)
            {
                tabContent += "\n";
            }
        }

        // Extract class name and package from tab
        std::string tabClassName;
        std::string tabPackageName;

        for (const auto &tabLine : tab.editorState.lines)
        {
            // Look for package declaration
            std::regex packageRegex(R"(package\s+([\w\.]+)\s*;)");
            std::smatch packageMatch;
            if (std::regex_search(tabLine, packageMatch, packageRegex))
            {
                tabPackageName = packageMatch[1].str();
            }

            // Look for class declaration
            std::regex classRegex(R"((?:public\s+)?(?:abstract\s+)?(?:final\s+)?class\s+(\w+))");
            std::smatch classMatch;
            if (std::regex_search(tabLine, classMatch, classRegex))
            {
                tabClassName = classMatch[1].str();
            }
        }

        if (!tabClassName.empty())
        {
            // Update program structure with this tab's content
            
        }
    }
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
        "sparkle", "snow", "blood", "ice", "magic", "ghost", "underwater",
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

ImVec2 MarkdownEditor::drawEditor()
{
    // Update font metrics first
    updateFontMetrics();

    // Get the current drawing context (we're already inside a child window from draw())
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImVec2 childOrigin = ImGui::GetCursorScreenPos();
    ImVec2 avail = ImGui::GetContentRegionAvail();

    // Create an invisible button that covers the entire editor area for input capture
    ImGui::SetCursorScreenPos(childOrigin);
    ImGui::InvisibleButton("##editorInput", avail, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    bool isHovered = ImGui::IsItemHovered();
    bool isClicked = ImGui::IsItemClicked(0);

    // Update focus state
    if (isClicked)
    {
        editorState.wantsFocus = true;
    }

    if (editorState.wantsFocus)
    {
        ImGui::SetKeyboardFocusHere(-1); // Focus the invisible button
        editorState.hasFocus = true;
        editorState.wantsFocus = false;
    }

    // Check if we still have focus
    editorState.hasFocus = ImGui::IsItemFocused() || isHovered;

    // Handle keyboard input only when focused
    if (editorState.hasFocus)
    {
        handleKeyboardInput();
    }

    // Calculate visible area
    float lineNumberWidth = 60.0f;
    ImVec2 textOrigin = ImVec2(childOrigin.x + lineNumberWidth, childOrigin.y);
    visibleHeight = avail.y; // Store for use in cursor visibility checks

    // Background
    drawList->AddRectFilled(childOrigin, ImVec2(childOrigin.x + avail.x, childOrigin.y + avail.y), backgroundColor);

    // Line numbers background
    drawList->AddRectFilled(childOrigin, ImVec2(childOrigin.x + lineNumberWidth, childOrigin.y + avail.y), IM_COL32(40, 40, 40, 255));

    // Separator line
    drawList->AddLine(ImVec2(childOrigin.x + lineNumberWidth, childOrigin.y),
                      ImVec2(childOrigin.x + lineNumberWidth, childOrigin.y + avail.y),
                      IM_COL32(80, 80, 80, 255));

    // Ensure cursor is visible
    if (editorState.needsScrollToCursor)
    {
        ensureCursorVisible();
        editorState.needsScrollToCursor = false;
    }

    // Draw components
    drawLineNumbers(drawList, childOrigin, visibleHeight);
    drawSelection(drawList, textOrigin);
    drawTextContent(drawList, textOrigin, visibleHeight);
    drawCursor(drawList, textOrigin);
    drawSyntaxErrors(drawList, textOrigin);

    // Handle mouse click for cursor positioning
    if (isClicked)
    {
        ImVec2 mousePos = ImGui::GetMousePos();
        // Convert mouse position to text coordinates
        float relativeX = mousePos.x - textOrigin.x;
        float relativeY = mousePos.y - textOrigin.y + editorState.scrollY;

        size_t clickedLine = static_cast<size_t>(relativeY / lineHeight);

        if (clickedLine < editorState.lines.size())
        {
            const std::string &line = editorState.lines[clickedLine];

            // Find the closest character position by measuring text width
            size_t clickedColumn = 0;
            ImFont *font = ImGui::GetFont();

            if (font && !line.empty())
            {
                // Binary search or linear search to find the closest character position
                float bestDistance = FLT_MAX;
                for (size_t col = 0; col <= line.length(); ++col)
                {
                    std::string textToCol = line.substr(0, col);
                    float textWidth = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, textToCol.c_str()).x;
                    float distance = std::abs(textWidth - relativeX);

                    if (distance < bestDistance)
                    {
                        bestDistance = distance;
                        clickedColumn = col;
                    }
                    else
                    {
                        break; // We've passed the closest point
                    }
                }
            }
            else
            {
                // Fallback to simple calculation
                clickedColumn = static_cast<size_t>(relativeX / charWidth);
                clickedColumn = std::min(clickedColumn, line.length());
            }

            editorState.cursorLine = clickedLine;
            editorState.cursorColumn = clickedColumn;
            editorState.hasSelection = false;
            // Don't set needsScrollToCursor for mouse clicks since user clicked in visible area
        }
    }

    // Handle scrolling
    float scroll = ImGui::GetIO().MouseWheel;
    if (scroll != 0.0f && isHovered)
    {
        editorState.scrollY -= scroll * lineHeight * 3;
        editorState.scrollY = std::max(0.0f, editorState.scrollY);
    }

    // Check for error tooltips
    if (isHovered)
    {
        ImVec2 mousePos = ImGui::GetMousePos();
        for (const auto &error : syntaxErrors)
        {
            size_t errorLine = error.getLine() - 1;
            size_t errorCol = error.getColumn() - 1;

            if (errorLine < editorState.lines.size())
            {
                float errorX = textOrigin.x + errorCol * charWidth;
                float errorY = textOrigin.y + errorLine * lineHeight - editorState.scrollY;
                float errorWidth = charWidth * 5; // Error underline width

                if (mousePos.x >= errorX && mousePos.x <= errorX + errorWidth &&
                    mousePos.y >= errorY && mousePos.y <= errorY + lineHeight)
                {
                    ImGui::SetTooltip("%s", error.getMessage().c_str());
                    break;
                }
            }
        }
    }

    // Draw completion popup
    if (showCompletions)
    {
        float lineNumberWidth = 60.0f;
        ImVec2 textOrigin = ImVec2(childOrigin.x + lineNumberWidth, childOrigin.y);
        drawCompletionPopup(textOrigin);
    }

    return childOrigin;
}

#include <Util/ErrorStream.hpp>
#include <Util/StandardStream.hpp>

std::string MarkdownEditor::getEditorContent() const
{
    std::string content;
    for (size_t i = 0; i < editorState.lines.size(); ++i) {
        content += editorState.lines[i];
        if (i < editorState.lines.size() - 1) {
            content += '\n';
        }
    }
    return content;
}

void MarkdownEditor::setContent(const std::string& content)
{
    // Clear existing lines
    editorState.lines.clear();
    
    // Parse content into lines
    if (content.empty()) {
        editorState.lines.push_back("");
    } else {
        std::string line;
        for (char c : content) {
            if (c == '\n') {
                editorState.lines.push_back(line);
                line.clear();
            } else {
                line += c;
            }
        }
        // Add the last line (or empty string if content ended with newline)
        editorState.lines.push_back(line);
    }
    
    // Reset cursor and scroll
    editorState.cursorLine = 0;
    editorState.cursorColumn = 0;
    editorState.scrollY = 0.0f;
    editorState.hasSelection = false;
    editorState.isDirty = false;
    editorState.isLoadedInMemory = true;
    
    // Clear preview cache to force re-sync
    m_previewSourceCache.clear();
}

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

void MarkdownEditor::handleKeyboardInput()
{
    ImGuiIO &io = ImGui::GetIO();

    // Only handle input if we have focus
    if (!editorState.hasFocus)
    {
        return;
    }

    // Handle special keys
    if (ImGui::IsKeyPressed(ImGuiKey_Enter))
    {
        // Hide completions when Enter is pressed
        if (showCompletions)
        {
            showCompletions = false;
        }

        if (editorState.hasSelection)
        {
            deleteSelection();
        }

        // Split the current line at cursor position
        if (editorState.cursorLine >= editorState.lines.size())
        {
            editorState.lines.resize(editorState.cursorLine + 1);
        }

        std::string &currentLine = editorState.lines[editorState.cursorLine];
        
        // Calculate indentation of the current line
        std::string indentation = "";
        for (size_t i = 0; i < currentLine.length(); ++i)
        {
            if (currentLine[i] == ' ' || currentLine[i] == '\t')
            {
                indentation += currentLine[i];
            }
            else
            {
                break; // Stop at first non-whitespace character
            }
        }
        
        std::string newLine = currentLine.substr(editorState.cursorColumn);
        currentLine = currentLine.substr(0, editorState.cursorColumn);

        // Add indentation to the new line if it's not empty or if we're not at the start
        if (!newLine.empty() || editorState.cursorColumn > 0)
        {
            newLine = indentation + newLine;
        }

        // Insert new line
        editorState.lines.insert(editorState.lines.begin() + editorState.cursorLine + 1, newLine);
        editorState.cursorLine++;
        editorState.cursorColumn = indentation.length(); // Position cursor after indentation
        editorState.isDirty = true;
        editorState.needsScrollToCursor = true;

        updateSyntaxErrors();
        updateLiveProgramStructure(); // Update program structure with live content
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Backspace))
    {
        if (editorState.hasSelection)
        {
            deleteSelection();
        }
        else if (editorState.cursorColumn > 0)
        {
            editorState.lines[editorState.cursorLine].erase(editorState.cursorColumn - 1, 1);
            editorState.cursorColumn--;
            editorState.isDirty = true;
            updateSyntaxErrors();
            updateLiveProgramStructure(); // Update program structure with live content
        }
        else if (editorState.cursorLine > 0)
        {
            // Merge with previous line
            editorState.cursorColumn = editorState.lines[editorState.cursorLine - 1].length();
            editorState.lines[editorState.cursorLine - 1] += editorState.lines[editorState.cursorLine];
            editorState.lines.erase(editorState.lines.begin() + editorState.cursorLine);
            editorState.cursorLine--;
            editorState.isDirty = true;
            updateSyntaxErrors();
            updateLiveProgramStructure(); // Update program structure with live content
            scrollToCursorIfNeeded();
        }
        
        // Update or dismiss completions after backspace
        if (showCompletions)
        {
            updateCompletions(); // Recalculate completions with new content
            if (completionItems.empty())
            {
                showCompletions = false; // Dismiss if no completions available
            }
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_Delete))
    {
        if (editorState.hasSelection)
        {
            deleteSelection();
        }
        else if (editorState.cursorLine < editorState.lines.size())
        {
            if (editorState.cursorColumn < editorState.lines[editorState.cursorLine].length())
            {
                // Delete character at cursor
                editorState.lines[editorState.cursorLine].erase(editorState.cursorColumn, 1);
                editorState.isDirty = true;
                updateSyntaxErrors();
                updateLiveProgramStructure(); // Update program structure with live content
            }
            else if (editorState.cursorLine < editorState.lines.size() - 1)
            {
                // At end of line, merge with next line
                editorState.lines[editorState.cursorLine] += editorState.lines[editorState.cursorLine + 1];
                editorState.lines.erase(editorState.lines.begin() + editorState.cursorLine + 1);
                editorState.isDirty = true;
                updateSyntaxErrors();
                updateLiveProgramStructure(); // Update program structure with live content
            }
        }
    }

    // Arrow keys
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
    {
        if (editorState.cursorColumn > 0)
        {
            editorState.cursorColumn--;
        }
        else if (editorState.cursorLine > 0)
        {
            editorState.cursorLine--;
            editorState.cursorColumn = editorState.lines[editorState.cursorLine].length();
        }
        // Only scroll if cursor moves outside current viewport (strict check)
        float cursorY = editorState.cursorLine * lineHeight;
        if (cursorY < editorState.scrollY || cursorY + lineHeight > editorState.scrollY + visibleHeight)
        {
            editorState.needsScrollToCursor = true;
        }
        editorState.hasSelection = false;
        // Hide completions when cursor moves
        if (showCompletions)
        {
            showCompletions = false;
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
    {
        if (editorState.cursorLine < editorState.lines.size() &&
            editorState.cursorColumn < editorState.lines[editorState.cursorLine].length())
        {
            editorState.cursorColumn++;
        }
        else if (editorState.cursorLine < editorState.lines.size() - 1)
        {
            editorState.cursorLine++;
            editorState.cursorColumn = 0;
        }
        // Only scroll if cursor moves outside current viewport (strict check)
        float cursorY = editorState.cursorLine * lineHeight;
        if (cursorY < editorState.scrollY || cursorY + lineHeight > editorState.scrollY + visibleHeight)
        {
            editorState.needsScrollToCursor = true;
        }
        editorState.hasSelection = false;
        // Hide completions when cursor moves
        if (showCompletions)
        {
            showCompletions = false;
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
    {
        if (showCompletions && io.KeyCtrl)
        {
            // Navigate completions when Ctrl is held
            selectedCompletion = (selectedCompletion > 0) ? selectedCompletion - 1 : completionItems.size() - 1;
        }
        else
        {
            // Move cursor normally
            if (editorState.cursorLine > 0)
            {
                editorState.cursorLine--;
                if (editorState.cursorLine < editorState.lines.size())
                {
                    editorState.cursorColumn = std::min(editorState.cursorColumn, editorState.lines[editorState.cursorLine].length());
                }
                // Only scroll if cursor moves above visible area (strict check)
                float cursorY = editorState.cursorLine * lineHeight;
                if (cursorY < editorState.scrollY)
                {
                    editorState.needsScrollToCursor = true;
                }
            }
            editorState.hasSelection = false;
            // Hide completions when cursor moves
            if (showCompletions)
            {
                showCompletions = false;
            }
        }
    }

    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
    {
        if (showCompletions && io.KeyCtrl)
        {
            // Navigate completions when Ctrl is held
            selectedCompletion = (selectedCompletion + 1) % completionItems.size();
        }
        else
        {
            // Move cursor normally
            if (editorState.cursorLine < editorState.lines.size() - 1)
            {
                editorState.cursorLine++;
                if (editorState.cursorLine < editorState.lines.size())
                {
                    editorState.cursorColumn = std::min(editorState.cursorColumn, editorState.lines[editorState.cursorLine].length());
                }
                // Only scroll if cursor moves below visible area (strict check)
                float cursorY = editorState.cursorLine * lineHeight;
                if (cursorY + lineHeight > editorState.scrollY + visibleHeight)
                {
                    editorState.needsScrollToCursor = true;
                }
            }
            editorState.hasSelection = false;
            // Hide completions when cursor moves
            if (showCompletions)
            {
                showCompletions = false;
            }
        }
    }

    // Handle Tab key for indentation
    if (ImGui::IsKeyPressed(ImGuiKey_Tab) && !showCompletions)
    {
        if (editorState.hasSelection)
        {
            deleteSelection();
        }
        insertTextAtCursor("    "); // Insert 4 spaces for tab
        return;                     // Don't process other input after tab
    }

    // Handle Ctrl+S for save
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S))
    {
        saveEditorToFile();
    }

    // Handle completion navigation
    if (showCompletions)
    {
        if (ImGui::IsKeyPressed(ImGuiKey_Tab))
        {
            if (selectedCompletion < completionItems.size())
            {
                // Find the word being completed
                const std::string &line = editorState.lines[editorState.cursorLine];
                size_t wordStart = editorState.cursorColumn;
                while (wordStart > 0 && (std::isalnum(line[wordStart - 1]) || line[wordStart - 1] == '_'))
                {
                    wordStart--;
                }

                // Replace the partial word with the completion
                std::string &currentLine = editorState.lines[editorState.cursorLine];
                currentLine.erase(wordStart, editorState.cursorColumn - wordStart);
                currentLine.insert(wordStart, completionItems[selectedCompletion].text);
                editorState.cursorColumn = wordStart + completionItems[selectedCompletion].text.length();
                editorState.isDirty = true;
                updateLiveProgramStructure(); // Update program structure with live content
            }
            showCompletions = false;
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        {
            showCompletions = false;
        }
    }

    // Handle text input
    for (int i = 0; i < io.InputQueueCharacters.Size; i++)
    {
        ImWchar c = io.InputQueueCharacters[i];
        if (c == '\t')
        {
            // Handle tab character explicitly
            if (editorState.hasSelection)
            {
                deleteSelection();
            }
            insertTextAtCursor("    "); // Insert 4 spaces for tab
        }
        else if (c != 0 && c >= 32)
        {
            std::string input(1, static_cast<char>(c));
            
            // Handle auto-pairing for brackets, quotes, and backticks
            bool shouldAutoPair = false;
            std::string closingChar = "";
            
            switch (c)
            {
                case '(':
                    shouldAutoPair = true;
                    closingChar = ")";
                    break;
                case '{':
                    shouldAutoPair = true;
                    closingChar = "}";
                    break;
                case '[':
                    shouldAutoPair = true;
                    closingChar = "]";
                    break;
                case '"':
                    shouldAutoPair = true;
                    closingChar = "\"";
                    break;
                case '\'':
                    shouldAutoPair = true;
                    closingChar = "'";
                    break;
                case '`':
                    shouldAutoPair = true;
                    closingChar = "`";
                    break;
            }
            
            if (shouldAutoPair)
            {
                if (editorState.hasSelection)
                {
                    deleteSelection();
                }
                
                // Insert opening character
                insertTextAtCursor(input);
                
                // Store cursor position before inserting closing character
                size_t savedCursorLine = editorState.cursorLine;
                size_t savedCursorColumn = editorState.cursorColumn;
                
                // Insert closing character
                insertTextAtCursor(closingChar);
                
                // Move cursor back between the paired characters
                editorState.cursorLine = savedCursorLine;
                editorState.cursorColumn = savedCursorColumn;
                
                // Sync changes back to active tab
                syncEditorToActiveTab();
            }
            else
            {
                insertTextAtCursor(input);
            }

            // Trigger completions on certain characters
            if (c == '.' || std::isalpha(c))
            {
                updateCompletions();
            }
        }
    }
}

void MarkdownEditor::drawLineNumbers(ImDrawList *drawList, const ImVec2 &origin, float visibleHeight)
{
    float y = origin.y - editorState.scrollY;
    size_t startLine = static_cast<size_t>(std::max(0.0f, editorState.scrollY / lineHeight));
    size_t endLine = std::min(editorState.lines.size(), startLine + static_cast<size_t>(visibleHeight / lineHeight) + 2);

    for (size_t lineNum = startLine; lineNum < endLine; ++lineNum)
    {
        float lineY = y + lineNum * lineHeight;
        if (lineY > origin.y + visibleHeight)
            break;
        if (lineY + lineHeight < origin.y)
            continue;

        std::string lineNumStr = std::to_string(lineNum + 1);
        drawList->AddText(ImVec2(origin.x + 5, lineY), lineNumberColor, lineNumStr.c_str());
    }
}

void MarkdownEditor::drawTextContent(ImDrawList *drawList, const ImVec2 &origin, float visibleHeight)
{
    drawTextContentWithSyntaxHighlighting(drawList, origin, visibleHeight);
}

void MarkdownEditor::renderInlineMarkdown(ImDrawList *drawList, const std::string &text, float startX, float lineY, ImFont *font)
{
    float x = startX;
    size_t pos = 0;
    
    while (pos < text.length())
    {
        // Check for effect tags like <fire>, </fire>, etc.
        if (text[pos] == '<')
        {
            size_t tagEnd = text.find('>', pos);
            if (tagEnd != std::string::npos)
            {
                std::string tag = text.substr(pos, tagEnd - pos + 1);
                // Check if it looks like an effect tag
                bool isEffectTag = false;
                if (tag.length() >= 3)
                {
                    std::string tagName = tag.substr(1, tag.length() - 2);
                    if (tagName[0] == '/') tagName = tagName.substr(1);
                    
                    static const std::unordered_set<std::string> effectTags = {
                        "fire", "electric", "rainbow", "shake", "wave", "glow", "neon",
                        "sparkle", "snow", "blood", "ice", "magic", "ghost", "underwater",
                        "golden", "toxic", "crystal", "storm", "ethereal", "lava", "frost",
                        "void", "holy", "matrix", "disco", "glitch"
                    };
                    
                    isEffectTag = effectTags.count(tagName) > 0;
                }
                
                if (isEffectTag)
                {
                    drawList->AddText(ImVec2(x, lineY), classColor, tag.c_str());
                    x += font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, tag.c_str()).x;
                    pos = tagEnd + 1;
                    continue;
                }
            }
        }
        
        // Check for inline code with backticks
        if (text[pos] == '`')
        {
            size_t endBacktick = text.find('`', pos + 1);
            if (endBacktick != std::string::npos)
            {
                std::string code = text.substr(pos, endBacktick - pos + 1);
                drawList->AddText(ImVec2(x, lineY), stringColor, code.c_str());
                x += font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, code.c_str()).x;
                pos = endBacktick + 1;
                continue;
            }
        }
        
        // Check for bold with **
        if (pos + 1 < text.length() && text[pos] == '*' && text[pos + 1] == '*')
        {
            size_t endBold = text.find("**", pos + 2);
            if (endBold != std::string::npos)
            {
                std::string boldText = text.substr(pos, endBold - pos + 2);
                drawList->AddText(ImVec2(x, lineY), keywordColor, boldText.c_str());
                x += font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, boldText.c_str()).x;
                pos = endBold + 2;
                continue;
            }
        }
        
        // Check for bold with __
        if (pos + 1 < text.length() && text[pos] == '_' && text[pos + 1] == '_')
        {
            size_t endBold = text.find("__", pos + 2);
            if (endBold != std::string::npos)
            {
                std::string boldText = text.substr(pos, endBold - pos + 2);
                drawList->AddText(ImVec2(x, lineY), keywordColor, boldText.c_str());
                x += font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, boldText.c_str()).x;
                pos = endBold + 2;
                continue;
            }
        }
        
        // Check for italic with single *
        if (text[pos] == '*' && (pos + 1 >= text.length() || text[pos + 1] != '*'))
        {
            size_t endItalic = pos + 1;
            while (endItalic < text.length())
            {
                if (text[endItalic] == '*' && (endItalic + 1 >= text.length() || text[endItalic + 1] != '*'))
                    break;
                endItalic++;
            }
            if (endItalic < text.length())
            {
                std::string italicText = text.substr(pos, endItalic - pos + 1);
                drawList->AddText(ImVec2(x, lineY), methodColor, italicText.c_str());
                x += font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, italicText.c_str()).x;
                pos = endItalic + 1;
                continue;
            }
        }
        
        // Check for italic with single _
        if (text[pos] == '_' && (pos + 1 >= text.length() || text[pos + 1] != '_'))
        {
            size_t endItalic = pos + 1;
            while (endItalic < text.length())
            {
                if (text[endItalic] == '_' && (endItalic + 1 >= text.length() || text[endItalic + 1] != '_'))
                    break;
                endItalic++;
            }
            if (endItalic < text.length())
            {
                std::string italicText = text.substr(pos, endItalic - pos + 1);
                drawList->AddText(ImVec2(x, lineY), methodColor, italicText.c_str());
                x += font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, italicText.c_str()).x;
                pos = endItalic + 1;
                continue;
            }
        }
        
        // Check for links [text](url)
        if (text[pos] == '[')
        {
            size_t closeBracket = text.find(']', pos);
            if (closeBracket != std::string::npos && closeBracket + 1 < text.length() && text[closeBracket + 1] == '(')
            {
                size_t closeParen = text.find(')', closeBracket + 2);
                if (closeParen != std::string::npos)
                {
                    std::string linkText = text.substr(pos, closeParen - pos + 1);
                    drawList->AddText(ImVec2(x, lineY), numberColor, linkText.c_str());
                    x += font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, linkText.c_str()).x;
                    pos = closeParen + 1;
                    continue;
                }
            }
        }
        
        // Regular text - collect until we hit a special character
        size_t nextSpecial = pos;
        while (nextSpecial < text.length())
        {
            char c = text[nextSpecial];
            if (c == '*' || c == '_' || c == '`' || c == '[' || c == '<')
                break;
            nextSpecial++;
        }
        
        if (nextSpecial > pos)
        {
            std::string normalText = text.substr(pos, nextSpecial - pos);
            drawList->AddText(ImVec2(x, lineY), textColor, normalText.c_str());
            x += font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, normalText.c_str()).x;
            pos = nextSpecial;
        }
        else
        {
            // Single special character that didn't match any pattern
            std::string ch(1, text[pos]);
            drawList->AddText(ImVec2(x, lineY), textColor, ch.c_str());
            x += font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, ch.c_str()).x;
            pos++;
        }
    }
}

void MarkdownEditor::drawTextContentWithSyntaxHighlighting(ImDrawList *drawList, const ImVec2 &origin, float visibleHeight)
{
    float y = origin.y - editorState.scrollY;
    size_t startLine = static_cast<size_t>(std::max(0.0f, editorState.scrollY / lineHeight));
    size_t endLine = std::min(editorState.lines.size(), startLine + static_cast<size_t>(visibleHeight / lineHeight) + 2);

    ImFont *font = ImGui::GetFont();
    if (!font) return;

    // Track code block state across lines
    bool inCodeBlock = false;
    std::string codeBlockLang;
    
    // First pass: determine code block state from beginning
    for (size_t lineNum = 0; lineNum < startLine && lineNum < editorState.lines.size(); ++lineNum)
    {
        const std::string &line = editorState.lines[lineNum];
        std::string trimmed = line;
        size_t start = trimmed.find_first_not_of(' ');
        if (start != std::string::npos) trimmed = trimmed.substr(start);
        
        if (trimmed.rfind("```", 0) == 0)
        {
            inCodeBlock = !inCodeBlock;
            if (inCodeBlock && trimmed.length() > 3)
            {
                codeBlockLang = trimmed.substr(3);
            }
        }
    }

    // Second pass: render visible lines
    for (size_t lineNum = startLine; lineNum < endLine; ++lineNum)
    {
        float lineY = y + lineNum * lineHeight;
        if (lineY > origin.y + visibleHeight) break;
        if (lineY + lineHeight < origin.y) continue;

        const std::string &line = editorState.lines[lineNum];
        if (line.empty()) continue;

        float x = origin.x;
        
        // Check for code block delimiter
        std::string trimmed = line;
        size_t indent = line.find_first_not_of(' ');
        if (indent != std::string::npos) trimmed = line.substr(indent);
        
        if (trimmed.rfind("```", 0) == 0)
        {
            // Draw the entire line as code delimiter
            drawList->AddText(ImVec2(x, lineY), commentColor, line.c_str());
            inCodeBlock = !inCodeBlock;
            if (inCodeBlock && trimmed.length() > 3)
            {
                codeBlockLang = trimmed.substr(3);
            }
            continue;
        }
        
        // Inside code block - render as code
        if (inCodeBlock)
        {
            drawList->AddText(ImVec2(x, lineY), stringColor, line.c_str());
            continue;
        }

        // Check for heading (#, ##, ###, etc.)
        if (!line.empty() && line[0] == '#')
        {
            size_t headingLevel = 0;
            while (headingLevel < line.length() && line[headingLevel] == '#') headingLevel++;
            
            if (headingLevel <= 6 && headingLevel < line.length() && line[headingLevel] == ' ')
            {
                // Draw # symbols
                std::string hashes = line.substr(0, headingLevel + 1);
                drawList->AddText(ImVec2(x, lineY), keywordColor, hashes.c_str());
                x += font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, hashes.c_str()).x;
                
                // Draw heading text in bold color
                std::string headingText = line.substr(headingLevel + 1);
                drawList->AddText(ImVec2(x, lineY), classColor, headingText.c_str());
                continue;
            }
        }
        
        // Check for blockquote
        if (!line.empty() && line[0] == '>')
        {
            drawList->AddText(ImVec2(x, lineY), commentColor, line.c_str());
            continue;
        }
        
        // Check for list item (-, *, +, or numbered)
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
            // Draw list marker
            std::string marker = line.substr(0, listMarkerEnd);
            drawList->AddText(ImVec2(x, lineY), keywordColor, marker.c_str());
            x += font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, marker.c_str()).x;
            
            // Continue with inline formatting for rest of line
            std::string rest = line.substr(listMarkerEnd);
            renderInlineMarkdown(drawList, rest, x, lineY, font);
            continue;
        }
        
        // Check for horizontal rule
        if (trimmed == "---" || trimmed == "***" || trimmed == "___")
        {
            drawList->AddText(ImVec2(x, lineY), operatorColor, line.c_str());
            continue;
        }
        
        // Regular paragraph - render with inline formatting
        renderInlineMarkdown(drawList, line, x, lineY, font);
    }
}

void MarkdownEditor::drawCursor(ImDrawList *drawList, const ImVec2 &origin)
{
    ImVec2 cursorPos = getCursorScreenPos(origin);
    drawList->AddLine(cursorPos, ImVec2(cursorPos.x, cursorPos.y + lineHeight), cursorColor, 2.0f);
}

void MarkdownEditor::drawSelection(ImDrawList *drawList, const ImVec2 &origin)
{
    if (!editorState.hasSelection)
    {
        return;
    }

    size_t startLine = std::min(editorState.selectionStartLine, editorState.selectionEndLine);
    size_t endLine = std::max(editorState.selectionStartLine, editorState.selectionEndLine);
    size_t startCol = editorState.selectionStartLine < editorState.selectionEndLine ? editorState.selectionStartColumn : editorState.selectionEndColumn;
    size_t endCol = editorState.selectionStartLine < editorState.selectionEndLine ? editorState.selectionEndColumn : editorState.selectionStartColumn;

    ImFont *font = ImGui::GetFont();

    for (size_t lineNum = startLine; lineNum <= endLine; ++lineNum)
    {
        if (lineNum >= editorState.lines.size())
            break;

        float lineY = origin.y + lineNum * lineHeight - editorState.scrollY;
        size_t lineStartCol = (lineNum == startLine) ? startCol : 0;
        size_t lineEndCol = (lineNum == endLine) ? endCol : editorState.lines[lineNum].length();

        const std::string &line = editorState.lines[lineNum];

        float startX = origin.x;
        float endX = origin.x;

        if (font)
        {
            // Calculate accurate positions using text measurement
            if (lineStartCol > 0 && lineStartCol <= line.length())
            {
                std::string textToStart = line.substr(0, lineStartCol);
                startX += font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, textToStart.c_str()).x;
            }

            if (lineEndCol > 0 && lineEndCol <= line.length())
            {
                std::string textToEnd = line.substr(0, lineEndCol);
                endX += font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, textToEnd.c_str()).x;
            }
            else if (lineEndCol > line.length())
            {
                endX += font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, line.c_str()).x;
            }
        }
        else
        {
            // Fallback to simple calculation
            startX += lineStartCol * charWidth;
            endX += lineEndCol * charWidth;
        }

        drawList->AddRectFilled(ImVec2(startX, lineY), ImVec2(endX, lineY + lineHeight), selectionColor);
    }
}

void MarkdownEditor::drawSyntaxErrors(ImDrawList *drawList, const ImVec2 &origin)
{
    ImFont *font = ImGui::GetFont();

    for (const auto &error : syntaxErrors)
    {
        size_t lineNum = error.getLine() - 1; // Convert to 0-based
        if (lineNum >= editorState.lines.size())
            continue;

        const std::string &line = editorState.lines[lineNum];
        float lineY = origin.y + lineNum * lineHeight - editorState.scrollY + lineHeight - 2;

        float startX = origin.x;
        if (font && error.getColumn() > 1 && error.getColumn() - 1 <= line.length())
        {
            std::string textToError = line.substr(0, error.getColumn() - 1);
            startX += font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, textToError.c_str()).x;
        }
        else
        {
            startX += (error.getColumn() - 1) * charWidth; // Fallback
        }

        float endX = startX + charWidth * 5; // Draw a short underline

        // Draw wavy red line under the error
        for (float x = startX; x < endX; x += 4.0f)
        {
            float wave = sin((x - startX) * 0.5f) * 2.0f;
            drawList->AddLine(ImVec2(x, lineY + wave), ImVec2(x + 2, lineY - wave), errorColor, 1.0f);
        }
    }
}

void MarkdownEditor::drawCompletionPopup(const ImVec2 &textOrigin)
{
    if (completionItems.empty())
    {
        return;
    }

    ImVec2 cursorScreenPos = getCursorScreenPos(textOrigin);
    ImVec2 popupPos = ImVec2(cursorScreenPos.x, cursorScreenPos.y + lineHeight);

    ImGui::SetNextWindowPos(popupPos);
    ImGui::SetNextWindowSize(ImVec2(300, std::min(200.0f, completionItems.size() * 20.0f)));

    if (ImGui::Begin("##Completions", nullptr, ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove))
    {
        for (size_t i = 0; i < completionItems.size(); ++i)
        {
            bool isSelected = (i == selectedCompletion);
            if (isSelected)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 255, 255, 255));
                ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(100, 150, 200, 255));
            }

            if (ImGui::Selectable(completionItems[i].text.c_str(), isSelected))
            {
                selectedCompletion = i;
                // Apply the completion
                const std::string &line = editorState.lines[editorState.cursorLine];
                size_t wordStart = editorState.cursorColumn;
                while (wordStart > 0 && (std::isalnum(line[wordStart - 1]) || line[wordStart - 1] == '_'))
                {
                    wordStart--;
                }

                std::string &currentLine = editorState.lines[editorState.cursorLine];
                currentLine.erase(wordStart, editorState.cursorColumn - wordStart);
                currentLine.insert(wordStart, completionItems[i].text);
                editorState.cursorColumn = wordStart + completionItems[i].text.length();
                editorState.isDirty = true;
                showCompletions = false;
            }

            if (isSelected)
            {
                ImGui::PopStyleColor(2);
            }

            ImGui::SameLine();
            ImGui::TextDisabled(" - %s", completionItems[i].description.c_str());
        }
    }
    ImGui::End();
}

ImVec2 MarkdownEditor::getCursorScreenPos(const ImVec2 &origin) const
{
    // Calculate X position by measuring the actual text width up to cursor position
    float x = origin.x;
    if (editorState.cursorLine < editorState.lines.size())
    {
        const std::string &line = editorState.lines[editorState.cursorLine];
        if (editorState.cursorColumn > 0 && editorState.cursorColumn <= line.length())
        {
            std::string textToCursor = line.substr(0, editorState.cursorColumn);
            ImFont *font = ImGui::GetFont();
            if (font)
            {
                x += font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, textToCursor.c_str()).x;
            }
            else
            {
                x += editorState.cursorColumn * charWidth; // Fallback
            }
        }
    }

    float y = origin.y + editorState.cursorLine * lineHeight - editorState.scrollY;
    return ImVec2(x, y);
}

void MarkdownEditor::ensureCursorVisible()
{
    float cursorY = editorState.cursorLine * lineHeight;

    // Only scroll if cursor is completely outside visible area
    // Add a small margin to prevent scrolling when cursor is just at the edge
    float margin = lineHeight * 0.5f;

    if (cursorY < editorState.scrollY)
    {
        // Cursor is above visible area - scroll up just enough to show it
        editorState.scrollY = cursorY - margin;
        editorState.scrollY = std::max(0.0f, editorState.scrollY);
    }
    else if (cursorY + lineHeight > editorState.scrollY + visibleHeight)
    {
        // Cursor is below visible area - scroll down just enough to show it
        editorState.scrollY = cursorY + lineHeight - visibleHeight + margin;
        editorState.scrollY = std::max(0.0f, editorState.scrollY);
    }
    // If cursor is within visible area, don't change scroll position
}

bool MarkdownEditor::isCursorVisible() const
{
    float cursorY = editorState.cursorLine * lineHeight;

    // Be more conservative - only consider cursor invisible if it's well outside the viewport
    float margin = lineHeight * 2.0f; // Larger margin to avoid premature scrolling

    return (cursorY >= editorState.scrollY - margin &&
            cursorY + lineHeight <= editorState.scrollY + visibleHeight + margin);
}

void MarkdownEditor::scrollToCursorIfNeeded()
{
    if (!isCursorVisible())
    {
        editorState.needsScrollToCursor = true;
    }
}