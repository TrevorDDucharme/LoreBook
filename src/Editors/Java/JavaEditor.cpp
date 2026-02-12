#include <Editors/Java/JavaEditor.hpp>
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

JavaEditor::JavaEditor()
{
}

JavaEditor::~JavaEditor()
{
    // Stop the file watcher when the editor is destroyed
    programStructure.stopWatching();
}

void JavaEditor::setSrc(std::filesystem::path source)
{
    src = source;

    // Update the file watcher when source directory changes
    updateFileWatcher();
}

void JavaEditor::setClassPath(std::vector<std::filesystem::path> path)
{
    classPaths = path;

    // Update the file watcher when classpath changes
    updateFileWatcher();
}

void JavaEditor::updateFileWatcher()
{
    // Stop existing watcher
    programStructure.stopWatching();

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
        programStructure.startWatching(watchDirectories);
    }
}

// Use the FileBackend / VaultFileBackend instead of global vaultPath/getProgramPath
// This keeps JavaEditor storage access consistent with new editor backends.
#include <FileBackend.hpp>
#include <FileBackends/VaultFileBackend.hpp>

void JavaEditor::updateVaultDirectories()
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

void JavaEditor::setWorkingFile(std::filesystem::path file)
{
    // Legacy method - now delegates to openFile
    openFile(file);
}
void JavaEditor::openFile(std::filesystem::path file)
{
    openTab(file);
}
void JavaEditor::updateCompletions()
{
    completionItems.clear();

    if (editorState.cursorLine >= editorState.lines.size())
    {
        return;
    }

    const std::string &line = editorState.lines[editorState.cursorLine];
    if (editorState.cursorColumn == 0)
    {
        return;
    }

    // Update program structure with live content from all tabs
    updateLiveProgramStructureForAllTabs();

    // Analyze the context around the cursor
    std::string beforeCursor = line.substr(0, editorState.cursorColumn);

    // Check if we're after a dot (qualified access)
    size_t lastDot = beforeCursor.find_last_of('.');
    bool isQualifiedAccess = false;
    std::string objectName;
    std::string prefix;

    if (lastDot != std::string::npos)
    {
        // We're typing after a dot - this is qualified access
        isQualifiedAccess = true;

        // Find the start of the object name
        size_t objectStart = lastDot;
        while (objectStart > 0 && (std::isalnum(beforeCursor[objectStart - 1]) || beforeCursor[objectStart - 1] == '_'))
        {
            objectStart--;
        }
        objectName = beforeCursor.substr(objectStart, lastDot - objectStart);
        prefix = beforeCursor.substr(lastDot + 1);
    }
    else
    {
        // Regular word completion
        size_t wordStart = editorState.cursorColumn;
        while (wordStart > 0 && (std::isalnum(line[wordStart - 1]) || line[wordStart - 1] == '_'))
        {
            wordStart--;
        }

        if (wordStart == editorState.cursorColumn)
        {
                return;
        }

        prefix = line.substr(wordStart, editorState.cursorColumn - wordStart);
    }

    // Generate context-aware completions
    generateContextAwareCompletions(prefix, isQualifiedAccess, objectName);

    showCompletions = !completionItems.empty();
    selectedCompletion = 0;
}

std::string JavaEditor::getCurrentClassName() const
{
    const EditorTab *activeTab = getActiveTab();
    if (!activeTab || activeTab->editorState.lines.empty())
    {
        return "";
    }

    // Look for class declaration in the current file
    for (const auto &line : activeTab->editorState.lines)
    {
        std::regex classRegex(R"((?:public\s+)?(?:abstract\s+)?(?:final\s+)?class\s+(\w+))");
        std::smatch match;
        if (std::regex_search(line, match, classRegex))
        {
            return match[1].str();
        }
    }

    return "";
}

std::string JavaEditor::getCurrentPackageName() const
{
    const EditorTab *activeTab = getActiveTab();
    if (!activeTab || activeTab->editorState.lines.empty())
    {
        return "";
    }

    // Look for package declaration in the current file
    for (const auto &line : activeTab->editorState.lines)
    {
        std::regex packageRegex(R"(package\s+([\w\.]+)\s*;)");
        std::smatch match;
        if (std::regex_search(line, match, packageRegex))
        {
            return match[1].str();
        }
    }

    return "";
}

std::vector<std::string> JavaEditor::getCurrentImports() const
{
    std::vector<std::string> imports;
    const EditorTab *activeTab = getActiveTab();
    if (!activeTab || activeTab->editorState.lines.empty())
    {
        return imports;
    }

    // Look for import statements in the current file
    for (const auto &line : activeTab->editorState.lines)
    {
        std::regex importRegex(R"(import\s+([\w\.]+)\s*;)");
        std::smatch match;
        if (std::regex_search(line, match, importRegex))
        {
            imports.push_back(match[1].str());
        }
    }

    return imports;
}

const Class *JavaEditor::resolveClass(const std::string &className) const
{
    // First, try to find in current package
    std::string currentPackage = getCurrentPackageName();
    const Class *cls = programStructure.findClass(className, currentPackage);
    if (cls)
    {
        return cls;
    }

    // Then, look through imports
    auto imports = getCurrentImports();
    for (const auto &importPath : imports)
    {
        // Check if this import ends with our class name
        if (importPath.length() > className.length() &&
            importPath.substr(importPath.length() - className.length()) == className)
        {
            // Extract package from import path
            size_t lastDot = importPath.find_last_of('.');
            if (lastDot != std::string::npos)
            {
                std::string packageName = importPath.substr(0, lastDot);
                cls = programStructure.findClass(className, packageName);
                if (cls)
                {
                    return cls;
                }
            }
        }
    }

    // Finally, try to find in any package (fallback)
    return programStructure.findClass(className);
}

void JavaEditor::parseCurrentFileForContext()
{
    updateLiveProgramStructure();
}

void JavaEditor::updateLiveProgramStructure()
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

    // Get current package and class names
    std::string packageName = getCurrentPackageName();
    std::string className = getCurrentClassName();

    if (className.empty())
    {
        return;
    }

    // Update the program structure with live content only if syntax is valid
    const_cast<ProgramStructure &>(programStructure).updateClassFromLiveContent(content, className, packageName);
}

void JavaEditor::updateLiveProgramStructureForAllTabs()
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
            const_cast<ProgramStructure &>(programStructure).updateClassFromLiveContent(tabContent, tabClassName, tabPackageName);
        }
    }
}

bool JavaEditor::isInClassContext(size_t line, size_t column) const
{
    const EditorTab *activeTab = getActiveTab();
    if (!activeTab || activeTab->editorState.lines.empty())
    {
        return false;
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

    auto context = programStructure.analyzeCursorContext(content, line, column);
    return context.isInClass;
}

void JavaEditor::generateContextAwareCompletions(const std::string &prefix, bool isQualifiedAccess, const std::string &objectName)
{
    if (isQualifiedAccess)
    {
        // Handle qualified access (e.g., MainMenuInterface.method)
        const Class *targetClass = resolveClass(objectName);
        if (targetClass)
        {
            // Get cursor context to determine static vs instance access
            const EditorTab *activeTab = getActiveTab();
            if (activeTab)
            {
                std::string content;
                for (size_t i = 0; i < activeTab->editorState.lines.size(); ++i)
                {
                    content += activeTab->editorState.lines[i];
                    if (i < activeTab->editorState.lines.size() - 1)
                    {
                        content += "\n";
                    }
                }

                auto context = programStructure.analyzeCursorContext(content, editorState.cursorLine, editorState.cursorColumn);

                // Get all accessible methods (includes inherited ones recursively)
                auto accessibleMethods = programStructure.getAccessibleMethods(targetClass, true); // Static context for qualified access
                for (const auto *method : accessibleMethods)
                {
                    if (method->isStatic && method->name.find(prefix) == 0)
                    {
                        CompletionItem item;
                        item.text = method->name;

                        // Determine if method is from the target class or inherited
                        bool isFromTargetClass = false;
                        for (const auto &targetMethod : targetClass->methods)
                        {
                            if (targetMethod.name == method->name)
                            {
                                isFromTargetClass = true;
                                break;
                            }
                        }

                        if (isFromTargetClass)
                        {
                            item.description = method->returnType + " " + method->name + "() (static)";
                        }
                        else
                        {
                            item.description = method->returnType + " " + method->name + "() (inherited static)";
                        }
                        item.type = CompletionItem::METHOD;
                        completionItems.push_back(item);
                    }
                }

                // Get all accessible variables (includes inherited ones recursively)
                auto accessibleVariables = programStructure.getAccessibleVariables(targetClass, true); // Static context
                for (const auto *variable : accessibleVariables)
                {
                    if (variable->isStatic && variable->name.find(prefix) == 0)
                    {
                        CompletionItem item;
                        item.text = variable->name;

                        // Determine if variable is from the target class or inherited
                        bool isFromTargetClass = false;
                        for (const auto &targetVar : targetClass->variables)
                        {
                            if (targetVar.name == variable->name)
                            {
                                isFromTargetClass = true;
                                break;
                            }
                        }

                        if (isFromTargetClass)
                        {
                            item.description = variable->type + " " + variable->name + " (static)";
                        }
                        else
                        {
                            item.description = variable->type + " " + variable->name + " (inherited static)";
                        }
                        item.type = CompletionItem::VARIABLE;
                        completionItems.push_back(item);
                    }
                }
            }
        }
    }
    else
    {
        // Regular completion - determine context
        const EditorTab *activeTab = getActiveTab();
        if (!activeTab)
            return;

        std::string content;
        for (size_t i = 0; i < activeTab->editorState.lines.size(); ++i)
        {
            content += activeTab->editorState.lines[i];
            if (i < activeTab->editorState.lines.size() - 1)
            {
                content += "\n";
            }
        }

        auto context = programStructure.analyzeCursorContext(content, editorState.cursorLine, editorState.cursorColumn);

        // Add Java keywords
        std::vector<std::string> keywords = {
            "abstract", "assert", "boolean", "break", "byte", "case", "catch", "char",
            "class", "const", "continue", "default", "do", "double", "else", "enum",
            "extends", "final", "finally", "float", "for", "goto", "if", "implements",
            "import", "instanceof", "int", "interface", "long", "native", "new",
            "package", "private", "protected", "public", "return", "short", "static",
            "strictfp", "super", "switch", "synchronized", "this", "throw", "throws",
            "transient", "try", "void", "volatile", "while"};

        for (const auto &keyword : keywords)
        {
            if (keyword.find(prefix) == 0)
            {
                CompletionItem item;
                item.text = keyword;
                item.description = "Java keyword";
                item.type = CompletionItem::KEYWORD;
                completionItems.push_back(item);
            }
        }

        // Add classes from all packages
        auto allPackages = programStructure.getAllPackagesFlat();
        for (const auto *package : allPackages)
        {
            std::string fullPackageName = programStructure.getFullPackageName(package);
            for (const auto &cls : package->classes)
            {
                if (cls.name.find(prefix) == 0)
                {
                    CompletionItem item;
                    item.text = cls.name;
                    item.description = "Class in package " + fullPackageName;
                    item.type = CompletionItem::CLASS;
                    completionItems.push_back(item);
                }
            }
        }

        // If we're in a class context, add all accessible methods and variables with full inheritance support
        if (context.isInClass && !context.currentClassName.empty())
        {
            const Class *currentClass = resolveClass(context.currentClassName);
            if (currentClass)
            {
                // Get all accessible methods (includes full inheritance chain recursively)
                auto accessibleMethods = programStructure.getAccessibleMethods(currentClass, context.hasStaticContext);
                for (const auto *method : accessibleMethods)
                {
                    // Filter by visibility - exclude private methods from other classes
                    bool shouldInclude = true;
                    bool isFromCurrentClass = false;

                    // Check if method is from current class
                    for (const auto &currentMethod : currentClass->methods)
                    {
                        if (currentMethod.name == method->name &&
                            currentMethod.returnType == method->returnType)
                        {
                            isFromCurrentClass = true;
                            break;
                        }
                    }

                    // Include private methods only if they're from the current class
                    if (method->isPrivate && !isFromCurrentClass)
                    {
                        shouldInclude = false;
                    }

                    if (shouldInclude && method->name.find(prefix) == 0)
                    {
                        CompletionItem item;
                        item.text = method->name;

                        if (isFromCurrentClass)
                        {
                            item.description = method->returnType + " " + method->name + "()";
                        }
                        else
                        {
                            // This is an inherited method, let's identify the source
                            item.description = method->returnType + " " + method->name + "() (inherited)";
                        }

                        if (method->isStatic)
                        {
                            item.description += " (static)";
                        }

                        item.type = CompletionItem::METHOD;
                        completionItems.push_back(item);
                    }
                }

                // Get all accessible variables (includes full inheritance chain recursively)
                auto accessibleVariables = programStructure.getAccessibleVariables(currentClass, context.hasStaticContext);
                for (const auto *variable : accessibleVariables)
                {
                    // Filter by visibility - exclude private variables from other classes
                    bool shouldInclude = true;
                    bool isFromCurrentClass = false;

                    // Check if variable is from current class
                    for (const auto &currentVar : currentClass->variables)
                    {
                        if (currentVar.name == variable->name &&
                            currentVar.type == variable->type)
                        {
                            isFromCurrentClass = true;
                            break;
                        }
                    }

                    // Include private variables only if they're from the current class
                    if (variable->isPrivate && !isFromCurrentClass)
                    {
                        shouldInclude = false;
                    }

                    if (shouldInclude && variable->name.find(prefix) == 0)
                    {
                        CompletionItem item;
                        item.text = variable->name;

                        if (isFromCurrentClass)
                        {
                            item.description = variable->type + " " + variable->name;
                        }
                        else
                        {
                            // This is an inherited variable
                            item.description = variable->type + " " + variable->name + " (inherited)";
                        }

                        if (variable->isStatic)
                        {
                            item.description += " (static)";
                        }

                        item.type = CompletionItem::VARIABLE;
                        completionItems.push_back(item);
                    }
                }
            }
        }

        // Add public methods and variables from other classes (only if appropriate)
        if (!context.hasStaticContext || !context.isInMethod)
        {
            for (const auto *package : allPackages)
            {
                for (const auto &cls : package->classes)
                {
                    for (const auto &method : cls.methods)
                    {
                        if (method.isPublic && method.name.find(prefix) == 0)
                        {
                            CompletionItem item;
                            item.text = method.name;
                            item.description = method.returnType + " " + method.name + "() in " + cls.name;
                            item.type = CompletionItem::METHOD;
                            completionItems.push_back(item);
                        }
                    }

                    for (const auto &variable : cls.variables)
                    {
                        if (variable.isPublic && variable.name.find(prefix) == 0)
                        {
                            CompletionItem item;
                            item.text = variable.name;
                            item.description = variable.type + " " + variable.name + " in " + cls.name;
                            item.type = CompletionItem::VARIABLE;
                            completionItems.push_back(item);
                        }
                    }
                }
            }
        }
    }
}

void JavaEditor::updateSyntaxErrors()
{
    syntaxErrors.clear();

    // Multi-line syntax checking for proper bracket matching
    int totalBraceCount = 0;
    int totalParenCount = 0;
    int totalBracketCount = 0;
    bool inString = false;
    bool inChar = false;
    bool inSingleLineComment = false;
    bool inMultiLineComment = false;

    for (size_t lineNum = 0; lineNum < editorState.lines.size(); ++lineNum)
    {
        const std::string &line = editorState.lines[lineNum];
        inSingleLineComment = false; // Reset for each line

        for (size_t col = 0; col < line.length(); ++col)
        {
            char c = line[col];
            char nextC = (col + 1 < line.length()) ? line[col + 1] : '\0';
            char prevC = (col > 0) ? line[col - 1] : '\0';

            // Handle comment states
            if (!inString && !inChar && !inSingleLineComment && !inMultiLineComment)
            {
                if (c == '/' && nextC == '/')
                {
                    inSingleLineComment = true;
                    col++; // Skip next character
                    continue;
                }
                else if (c == '/' && nextC == '*')
                {
                    inMultiLineComment = true;
                    col++; // Skip next character
                    continue;
                }
            }

            if (inMultiLineComment)
            {
                if (c == '*' && nextC == '/')
                {
                    inMultiLineComment = false;
                    col++; // Skip next character
                }
                continue;
            }

            if (inSingleLineComment)
            {
                continue; // Skip rest of line
            }

            // Handle string and character literals
            if (!inString && !inChar && c == '"' && prevC != '\\')
            {
                inString = true;
                continue;
            }
            else if (inString && c == '"' && prevC != '\\')
            {
                inString = false;
                continue;
            }
            else if (!inString && !inChar && c == '\'' && prevC != '\\')
            {
                inChar = true;
                continue;
            }
            else if (inChar && c == '\'' && prevC != '\\')
            {
                inChar = false;
                continue;
            }

            // Skip bracket checking inside strings, characters, or comments
            if (inString || inChar)
            {
                continue;
            }

            // Check brackets only in actual code
            switch (c)
            {
            case '{':
                totalBraceCount++;
                break;
            case '}':
                totalBraceCount--;
                if (totalBraceCount < 0)
                {
                    SyntaxError error(lineNum + 1, col + 1, "Unmatched closing brace");
                    syntaxErrors.push_back(error);
                    totalBraceCount = 0; // Reset to avoid cascading errors
                }
                break;
            case '(':
                totalParenCount++;
                break;
            case ')':
                totalParenCount--;
                if (totalParenCount < 0)
                {
                    SyntaxError error(lineNum + 1, col + 1, "Unmatched closing parenthesis");
                    syntaxErrors.push_back(error);
                    totalParenCount = 0;
                }
                break;
            case '[':
                totalBracketCount++;
                break;
            case ']':
                totalBracketCount--;
                if (totalBracketCount < 0)
                {
                    SyntaxError error(lineNum + 1, col + 1, "Unmatched closing bracket");
                    syntaxErrors.push_back(error);
                    totalBracketCount = 0;
                }
                break;
            }
        }
    }

    // Check for unclosed brackets at the end of file
    if (totalBraceCount > 0)
    {
        SyntaxError error(editorState.lines.size(), 1, "Unclosed brace(s)");
        syntaxErrors.push_back(error);
    }
    if (totalParenCount > 0)
    {
        SyntaxError error(editorState.lines.size(), 1, "Unclosed parenthesis(es)");
        syntaxErrors.push_back(error);
    }
    if (totalBracketCount > 0)
    {
        SyntaxError error(editorState.lines.size(), 1, "Unclosed bracket(s)");
        syntaxErrors.push_back(error);
    }
}

ImVec2 JavaEditor::draw()
{
    // Check if vault has changed and update directories accordingly
    

    // Use internal gridOrigin
    // Setup child window with flags to ensure mouse events are captured and no padding
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("JavaEditorChild", avail, true, childFlags);
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImVec2 childOrigin = ImGui::GetCursorScreenPos();
    ImVec2 childSize = avail;

    // Draw tab bar
    if (ImGui::BeginTabBar("JavaEditorTabs", ImGuiTabBarFlags_Reorderable | ImGuiTabBarFlags_AutoSelectNewTabs | ImGuiTabBarFlags_TabListPopupButton))
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
            std::string newFileName = "Untitled" + std::to_string(untitledCounter++) + ".java";
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
        if (programStructure.isCurrentlyWatching())
        {
            ImGui::Text("File watcher: Active");
            ImGui::Text("Packages: %zu", programStructure.getPackages().size());
        }
        else
        {
            ImGui::Text("File watcher: Inactive");
        }
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();

    // Return the screen position of the grid for drop coordinate calculation
    return childOrigin;
}
#include <Util/ErrorStream.hpp>
#include <Util/StandardStream.hpp>

ImVec2 JavaEditor::drawConsole()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImGuiWindowFlags childFlags = ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoScrollbar;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::BeginChild("JavaConsoleChild", avail, true, childFlags);
    ImDrawList *drawList = ImGui::GetWindowDrawList();
    ImVec2 childOrigin = ImGui::GetCursorScreenPos();
    ImVec2 childSize = avail;

    // Draw tab bar for console outputs
    if (ImGui::BeginTabBar("JavaConsoleTabs", ImGuiTabBarFlags_None))
    {
        // Error Stream Tab
        if (ImGui::BeginTabItem("Errors"))
        {
            ImGui::BeginChild("ErrorScrollRegion", ImVec2(0, -ImGui::GetTextLineHeightWithSpacing()), false, ImGuiWindowFlags_HorizontalScrollbar);
            
            ErrorStream& errorStream = ErrorStream::getInstance();
            
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 100, 100, 255)); // Red color for errors
                ImGui::TextWrapped("%s", errorStream.getBuffer().c_str());
                ImGui::PopStyleColor();
            
            
            // Auto-scroll to bottom if new content is added
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
                
            ImGui::EndChild();
            
            // Clear button
            if (ImGui::Button("Clear Errors"))
            {
                errorStream.clearBuffer();
            }
            
            ImGui::EndTabItem();
        }
        
        // Standard Output Tab
        if (ImGui::BeginTabItem("Output"))
        {
            ImGui::BeginChild("OutputScrollRegion", ImVec2(0, -ImGui::GetTextLineHeightWithSpacing()), false, ImGuiWindowFlags_HorizontalScrollbar);
            
            StandardStream& standardStream = StandardStream::getInstance();
           
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 200, 200, 255)); // Gray color for standard output
            ImGui::TextWrapped("%s", standardStream.getBuffer().c_str());
            ImGui::PopStyleColor();
            
            // Auto-scroll to bottom if new content is added
            if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
                ImGui::SetScrollHereY(1.0f);
                
            ImGui::EndChild();
            
            // Clear button
            if (ImGui::Button("Clear Output"))
            {
                standardStream.clearBuffer();
            }
            
            ImGui::EndTabItem();
        }
        
        // Live Console Tab
        if (ImGui::BeginTabItem("Live Console"))
        {
            drawLiveConsole();
            ImGui::EndTabItem();
        }
        
        // Class Investigator Tab
        if (ImGui::BeginTabItem("Class Investigator"))
        {
            drawClassInvestigator();
            ImGui::EndTabItem();
        }
        
        ImGui::EndTabBar();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar();

    return childOrigin;
}

void JavaEditor::drawLiveConsole()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    
    // Title and help text
    ImGui::Text("Live Java Console");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Enter Java expressions to execute)");
    ImGui::Separator();
    
    // Output display area
    ImGui::Text("Output:");
    ImGui::BeginChild("LiveConsoleOutput", ImVec2(0, -ImGui::GetTextLineHeightWithSpacing() * 4), true, ImGuiWindowFlags_HorizontalScrollbar);
    
    if (!liveConsoleOutput.empty())
    {
        // Parse and display output with color coding
        std::istringstream iss(liveConsoleOutput);
        std::string line;
        while (std::getline(iss, line))
        {
            if (line.find("Input:") == 0)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 255, 255)); // Blue for input
                ImGui::Text("%s", line.c_str());
                ImGui::PopStyleColor();
            }
            else if (line.find("Result:") == 0)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 255, 100, 255)); // Green for result
                ImGui::Text("%s", line.c_str());
                ImGui::PopStyleColor();
            }
            else if (line.find("error") != std::string::npos)
            {
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 100, 100, 255)); // Red for errors
                ImGui::Text("%s", line.c_str());
                ImGui::PopStyleColor();
            }
            else
            {
                ImGui::Text("%s", line.c_str());
            }
        }
    }
    else
    {
        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(150, 150, 150, 255)); // Gray color for placeholder
        ImGui::Text("Enter Java expressions below to see results...");
        ImGui::Text("Examples:");
        ImGui::Text("  5 + 3");
        ImGui::Text("  10.5 * 2.0");
        ImGui::Text("  System.out.println(\"Hello World\")");
        ImGui::Text("  int x = 42");
        ImGui::PopStyleColor();
    }
    
    // Auto-scroll to bottom
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY())
        ImGui::SetScrollHereY(1.0f);
        
    ImGui::EndChild();
    
    // Input area
    ImGui::Text("Java Expression:");
    
    // Text input for Java code using std::string directly
    ImGui::PushItemWidth(-180);
    
    // Use ImGui's string support for InputTextMultiline
    bool inputChanged = ImGui::InputTextMultiline("##LiveConsoleInput", &liveConsoleInput, 
                                                 ImVec2(0, ImGui::GetTextLineHeightWithSpacing() * 2),
                                                 ImGuiInputTextFlags_EnterReturnsTrue);
    
    ImGui::PopItemWidth();
    
    ImGui::SameLine();
    
    // Button column
    ImGui::BeginGroup();
    
    // Execute button (can be triggered by button click or Enter key)
    bool shouldExecute = (ImGui::Button("Execute", ImVec2(80, 0)) || inputChanged) && !liveConsoleInput.empty();
    
    if (shouldExecute)
    {
        std::string result = executeJavaCode(liveConsoleInput);
        liveConsoleOutput += "Input: " + liveConsoleInput + "\n";
        liveConsoleOutput += "Result: " + result + "\n\n";
        
        // Add to history
        liveConsoleHistory.push_back(liveConsoleInput);
        historyIndex = liveConsoleHistory.size();
        
        // Clear input after successful execution
        liveConsoleInput.clear();
    }
    
    // Clear button
    if (ImGui::Button("Clear", ImVec2(80, 0)))
    {
        liveConsoleOutput.clear();
        liveConsoleInput.clear();
    }
    
    ImGui::EndGroup();
    
    // History navigation hint
    if (!liveConsoleHistory.empty())
    {
        ImGui::TextColored(ImVec4(0.6f, 0.6f, 0.6f, 1.0f), "Use Up/Down arrows to navigate history (%zu items)", liveConsoleHistory.size());
    }
    
    // Handle history navigation
    if (ImGui::IsItemFocused() && !liveConsoleHistory.empty())
    {
        if (ImGui::IsKeyPressed(ImGuiKey_UpArrow) && historyIndex > 0)
        {
            historyIndex--;
            liveConsoleInput = liveConsoleHistory[historyIndex];
        }
        else if (ImGui::IsKeyPressed(ImGuiKey_DownArrow) && historyIndex < static_cast<int>(liveConsoleHistory.size()) - 1)
        {
            historyIndex++;
            liveConsoleInput = liveConsoleHistory[historyIndex];
        }
    }
}

std::string JavaEditor::executeJavaCode(const std::string& code)
{
    JNIEnv* env = getJavaEnv();
    if (!env)
    {
        return "{\"error\": \"Java VM not initialized\"}";
    }
    
    try
    {
        std::ostringstream json;
        json << "{\n";
        json << "  \"input\": \"" << code << "\",\n";
        
        // Handle System.out.println specially
        if (code.find("System.out.println") != std::string::npos)
        {
            // Extract the content between parentheses
            size_t start = code.find("(");
            size_t end = code.find_last_of(")");
            if (start != std::string::npos && end != std::string::npos && end > start)
            {
                std::string content = code.substr(start + 1, end - start - 1);
                
                // Remove quotes if it's a string literal
                if (content.front() == '"' && content.back() == '"')
                {
                    content = content.substr(1, content.length() - 2);
                }
                
                json << "  \"result\": \"" << content << "\",\n";
                json << "  \"type\": \"console_output\",\n";
                json << "  \"status\": \"success\"\n";
            }
            else
            {
                json << "  \"error\": \"Invalid println syntax\",\n";
                json << "  \"type\": \"syntax_error\",\n";
                json << "  \"status\": \"error\"\n";
            }
        }
        // Handle simple arithmetic expressions
        else if (std::regex_match(code, std::regex(R"(^\s*(\d+(?:\.\d+)?)\s*([+\-*/])\s*(\d+(?:\.\d+)?)\s*$)")))
        {
            std::regex arithmeticPattern(R"(^\s*(\d+(?:\.\d+)?)\s*([+\-*/])\s*(\d+(?:\.\d+)?)\s*$)");
            std::smatch match;
            
            if (std::regex_search(code, match, arithmeticPattern))
            {
                double num1 = std::stod(match[1].str());
                char op = match[2].str()[0];
                double num2 = std::stod(match[3].str());
                double result = 0;
                
                switch (op)
                {
                    case '+': result = num1 + num2; break;
                    case '-': result = num1 - num2; break;
                    case '*': result = num1 * num2; break;
                    case '/': 
                        if (num2 != 0) result = num1 / num2;
                        else {
                            json << "  \"error\": \"Division by zero\",\n";
                            json << "  \"type\": \"arithmetic_error\",\n";
                            json << "  \"status\": \"error\"\n";
                            json << "}";
                            return json.str();
                        }
                        break;
                }
                
                json << "  \"result\": " << result << ",\n";
                json << "  \"type\": \"number\",\n";
                json << "  \"status\": \"success\"\n";
            }
        }
        // Handle method calls via JNI
        else if (code.find("(") != std::string::npos && code.find(")") != std::string::npos)
        {
            // Try to parse as ClassName.methodName(args)
            std::regex methodPattern(R"(([A-Za-z_][A-Za-z0-9_]*(?:\.[A-Za-z_][A-Za-z0-9_]*)*?)\.([A-Za-z_][A-Za-z0-9_]*)\s*\((.*?)\))");
            std::smatch methodMatch;
            
            if (std::regex_search(code, methodMatch, methodPattern))
            {
                std::string className = methodMatch[1].str();
                std::string methodName = methodMatch[2].str();
                std::string argsStr = methodMatch[3].str();
                
                // Parse arguments
                std::vector<std::string> args;
                if (!argsStr.empty())
                {
                    std::stringstream ss(argsStr);
                    std::string arg;
                    while (std::getline(ss, arg, ','))
                    {
                        // Trim whitespace
                        arg.erase(0, arg.find_first_not_of(" \t"));
                        arg.erase(arg.find_last_not_of(" \t") + 1);
                        args.push_back(arg);
                    }
                }
                
                std::string result = executeStaticMethod(className, methodName, args);
                json << "  \"result\": " << result << ",\n";
                json << "  \"type\": \"method_call\",\n";
                json << "  \"status\": \"success\"\n";
            }
            else
            {
                json << "  \"error\": \"Could not parse method call\",\n";
                json << "  \"type\": \"parse_error\",\n";
                json << "  \"status\": \"error\"\n";
            }
        }
        else
        {
            json << "  \"error\": \"Unsupported Java expression\",\n";
            json << "  \"type\": \"unsupported\",\n";
            json << "  \"status\": \"error\",\n";
            json << "  \"note\": \"Supported: System.out.println(), arithmetic (+ - * /), static method calls\"\n";
        }
        
        json << "}";
        return json.str();
    }
    catch (const std::exception& e)
    {
        std::ostringstream json;
        json << "{\n";
        json << "  \"error\": \"" << e.what() << "\",\n";
        json << "  \"type\": \"execution_error\",\n";
        json << "  \"status\": \"error\"\n";
        json << "}";
        return json.str();
    }
}

std::string JavaEditor::executeStaticMethod(const std::string& className, const std::string& methodName, const std::vector<std::string>& args)
{
    JNIEnv* env = getJavaEnv();
    if (!env)
    {
        return "\"Java VM not initialized\"";
    }
    
    try
    {
        // Convert dots to slashes for JNI class name
        std::string jniClassName = className;
        std::replace(jniClassName.begin(), jniClassName.end(), '.', '/');
        
        // Find the class
        jclass clazz = env->FindClass(jniClassName.c_str());
        if (!clazz)
        {
            env->ExceptionClear();
            return "\"Class not found: " + className + "\"";
        }
        
        // For now, support only no-argument static methods
        if (!args.empty())
        {
            return "\"Method arguments not yet supported\"";
        }
        
        // Get method ID for static method with no parameters returning Object
        jmethodID methodID = env->GetStaticMethodID(clazz, methodName.c_str(), "()Ljava/lang/Object;");
        if (!methodID)
        {
            env->ExceptionClear();
            // Try with void return type
            methodID = env->GetStaticMethodID(clazz, methodName.c_str(), "()V");
            if (!methodID)
            {
                env->ExceptionClear();
                // Try with string return type
                methodID = env->GetStaticMethodID(clazz, methodName.c_str(), "()Ljava/lang/String;");
                if (!methodID)
                {
                    env->ExceptionClear();
                    return "\"Method not found: " + methodName + "()\"";
                }
            }
        }
        
        // Call the static method
        jobject result = env->CallStaticObjectMethod(clazz, methodID);
        
        if (env->ExceptionCheck())
        {
            env->ExceptionDescribe();
            env->ExceptionClear();
            return "\"Exception occurred during method execution\"";
        }
        
        if (!result)
        {
            return "null";
        }
        
        return convertJavaResultToJson(result, env);
    }
    catch (const std::exception& e)
    {
        return "\"Error: " + std::string(e.what()) + "\"";
    }
}

std::string JavaEditor::convertJavaResultToJson(jobject result, JNIEnv* env)
{
    if (!env || !result)
    {
        return "{\"result\": null}";
    }
    
    // Get the class of the result object
    jclass resultClass = env->GetObjectClass(result);
    
    // Get toString method
    jmethodID toStringMethod = env->GetMethodID(resultClass, "toString", "()Ljava/lang/String;");
    if (!toStringMethod)
    {
        return "{\"error\": \"Could not get toString method\"}";
    }
    
    // Call toString
    jstring resultString = (jstring)env->CallObjectMethod(result, toStringMethod);
    if (!resultString)
    {
        return "{\"result\": null}";
    }
    
    // Convert to C++ string
    const char* stringChars = env->GetStringUTFChars(resultString, nullptr);
    std::string resultStr(stringChars);
    env->ReleaseStringUTFChars(resultString, stringChars);
    
    // Create JSON response
    std::ostringstream json;
    json << "{\n";
    json << "  \"result\": \"" << resultStr << "\",\n";
    json << "  \"type\": \"object\"\n";
    json << "}";
    
    return json.str();
}

void JavaEditor::drawClassInvestigator()
{
    ImVec2 avail = ImGui::GetContentRegionAvail();
    
    // Title
    ImGui::Text("Class Investigator");
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "(Explore available classes and classpaths)");
    ImGui::Separator();
    
    // Left panel - Class list
    ImGui::BeginChild("ClassList", ImVec2(avail.x * 0.4f, 0), true);
    
    ImGui::Text("Available Classes:");
    ImGui::InputText("Filter", &classSearchFilter);
    
    ImGui::Separator();
    
    // Refresh button
    if (ImGui::Button("Refresh Classes"))
    {
        availableClasses = getAvailableClassPaths();
    }
    
    ImGui::SameLine();
    if (ImGui::Button("JVM Properties"))
    {
        std::string result = executeJavaCode("java.lang.System.getProperty(\"java.version\")");
        classInfoOutput = "JVM Information (via JNI):\n";
        classInfoOutput += "Java Version: " + result + "\n";
        
        // Get more system properties via JNI
        std::vector<std::string> props = {
            "java.vm.name", "java.vm.version", "java.vm.vendor",
            "java.runtime.name", "java.runtime.version",
            "os.name", "os.version", "os.arch"
        };
        
        for (const auto& prop : props)
        {
            std::string propResult = executeJavaCode("java.lang.System.getProperty(\"" + prop + "\")");
            classInfoOutput += prop + ": " + propResult + "\n";
        }
    }
    
    ImGui::SameLine();
    if (ImGui::Button("Test JNI"))
    {
        classInfoOutput = "JNI Connection Test:\n";
        classInfoOutput += "Testing basic JNI operations...\n\n";
        
        // Test various JNI operations
        std::vector<std::pair<std::string, std::string>> tests = {
            {"Current Time", "java.lang.System.currentTimeMillis()"},
            {"Math Test", "java.lang.Math.sqrt(16.0)"},
            {"String Test", "java.lang.String.valueOf(42)"},
            {"Memory Info", "java.lang.Runtime.getRuntime().totalMemory()"}
        };
        
        for (const auto& test : tests)
        {
            std::string result = executeJavaCode(test.second);
            classInfoOutput += test.first + ": " + result + "\n";
        }
    }
    
    // Display filtered class list
    for (const auto& className : availableClasses)
    {
        if (classSearchFilter.empty() || 
            className.find(classSearchFilter) != std::string::npos)
        {
            if (ImGui::Selectable(className.c_str(), selectedClassName == className))
            {
                selectedClassName = className;
                classInfoOutput = getClassInfo(className);
            }
        }
    }
    
    ImGui::EndChild();
    
    ImGui::SameLine();
    
    // Right panel - Class details
    ImGui::BeginChild("ClassDetails", ImVec2(0, 0), true);
    
    if (!selectedClassName.empty())
    {
        ImGui::Text("Class: %s", selectedClassName.c_str());
        ImGui::Separator();
        
        // Show class information
        if (!classInfoOutput.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(100, 255, 100, 255)); // Green for info
            ImGui::TextWrapped("%s", classInfoOutput.c_str());
            ImGui::PopStyleColor();
        }
        
        ImGui::Separator();
        
        // Quick actions
        if (ImGui::Button("Get Class Methods"))
        {
            classInfoOutput = getClassInfo(selectedClassName);
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Test Class Access"))
        {
            classInfoOutput = "Testing JNI access to " + selectedClassName + ":\n\n";
            
            // Test if we can find the class via JNI
            std::string jniName = selectedClassName;
            std::replace(jniName.begin(), jniName.end(), '.', '/');
            
            JNIEnv* env = getJavaEnv();
            if (env)
            {
                jclass testClass = env->FindClass(jniName.c_str());
                if (testClass)
                {
                    classInfoOutput += "Class found via FindClass()\n";
                    classInfoOutput += "JNI name: " + jniName + "\n";
                    
                    // Try to get some basic info
                    jclass classClass = env->FindClass("java/lang/Class");
                    if (classClass)
                    {
                        jmethodID isInterfaceMethod = env->GetMethodID(classClass, "isInterface", "()Z");
                        if (isInterfaceMethod)
                        {
                            jboolean isInterface = env->CallBooleanMethod(testClass, isInterfaceMethod);
                            classInfoOutput += "Is Interface: " + std::string(isInterface ? "Yes" : "No") + "\n";
                        }
                        
                        jmethodID getModifiersMethod = env->GetMethodID(classClass, "getModifiers", "()I");
                        if (getModifiersMethod)
                        {
                            jint modifiers = env->CallIntMethod(testClass, getModifiersMethod);
                            classInfoOutput += "Modifiers: " + std::to_string(modifiers) + "\n";
                        }
                    }
                    
                    env->DeleteLocalRef(testClass);
                }
                else
                {
                    env->ExceptionClear();
                    classInfoOutput += "✗ Class not found via JNI\n";
                    classInfoOutput += "This class may not be loaded or accessible\n";
                }
            }
            else
            {
                classInfoOutput += "✗ JNI environment not available\n";
            }
        }
        
        ImGui::SameLine();
        if (ImGui::Button("Discover Package"))
        {
            // Extract package name from class name
            size_t lastDot = selectedClassName.find_last_of('.');
            if (lastDot != std::string::npos)
            {
                std::string packageName = selectedClassName.substr(0, lastDot);
                auto packageClasses = getClassesInPackage(packageName);
                
                classInfoOutput = "Classes in package " + packageName + " (JNI verified):\n\n";
                for (const auto& cls : packageClasses)
                {
                    classInfoOutput += "• " + cls + "\n";
                }
            }
            else
            {
                classInfoOutput = "Cannot determine package for " + selectedClassName;
            }
        }
    }
    else
    {
        ImGui::Text("Select a class to view details");
        
        // Show classpath information
        ImGui::Separator();
        ImGui::Text("Classpath Information:");
        
        if (ImGui::Button("Show Classpath"))
        {
            auto classpaths = getAvailableClassPaths();
            classInfoOutput = "Classpath entries:\n";
            for (const auto& path : classpaths)
            {
                classInfoOutput += "• " + path + "\n";
            }
        }
    }
    
    ImGui::EndChild();
}

std::vector<std::string> JavaEditor::getAvailableClassPaths()
{
    std::vector<std::string> classpaths;
    
    JNIEnv* env = getJavaEnv();
    if (!env)
    {
        classpaths.push_back("Java VM not initialized");
        return classpaths;
    }
    
    try
    {
        // Get system class loader
        jclass systemClass = env->FindClass("java/lang/System");
        if (!systemClass)
        {
            env->ExceptionClear();
            classpaths.push_back("Could not find System class");
            return classpaths;
        }
        
        // Get java.class.path property via JNI
        jmethodID getPropertyMethod = env->GetStaticMethodID(systemClass, "getProperty", "(Ljava/lang/String;)Ljava/lang/String;");
        if (!getPropertyMethod)
        {
            env->ExceptionClear();
            classpaths.push_back("Could not find getProperty method");
            return classpaths;
        }
        
        jstring classpathKey = env->NewStringUTF("java.class.path");
        jstring classpathValue = (jstring)env->CallStaticObjectMethod(systemClass, getPropertyMethod, classpathKey);
        
        if (classpathValue)
        {
            const char* classpathChars = env->GetStringUTFChars(classpathValue, nullptr);
            std::string classpathStr(classpathChars);
            env->ReleaseStringUTFChars(classpathValue, classpathChars);
            
            // Split by path separator (Unix uses :, Windows uses ;)
            char separator = classpathStr.find(';') != std::string::npos ? ';' : ':';
            std::stringstream ss(classpathStr);
            std::string path;
            while (std::getline(ss, path, separator))
            {
                if (!path.empty())
                {
                    classpaths.push_back(path);
                }
            }
        }
        
        // Get loaded classes via JNI reflection
        // First try to get the ClassLoader
        jclass classLoaderClass = env->FindClass("java/lang/ClassLoader");
        if (classLoaderClass)
        {
            jmethodID getSystemClassLoaderMethod = env->GetStaticMethodID(classLoaderClass, "getSystemClassLoader", "()Ljava/lang/ClassLoader;");
            if (getSystemClassLoaderMethod)
            {
                jobject systemClassLoader = env->CallStaticObjectMethod(classLoaderClass, getSystemClassLoaderMethod);
                if (systemClassLoader)
                {
                    // Try to get loaded classes - this is implementation dependent
                    // For now, we'll add some basic classes we know are loaded
                    classpaths.push_back("=== LOADED CLASSES (JNI verified) ===");
                    
                    // Test if these classes are actually loaded by trying to find them
                    std::vector<std::string> testClasses = {
                        "java.lang.Object",
                        "java.lang.String", 
                        "java.lang.System",
                        "java.lang.Math",
                        "java.lang.Thread",
                        "java.lang.Class",
                        "java.util.ArrayList",
                        "java.util.HashMap",
                        "java.io.File",
                        "java.net.URL"
                    };
                    
                    for (const auto& testClass : testClasses)
                    {
                        std::string jniName = testClass;
                        std::replace(jniName.begin(), jniName.end(), '.', '/');
                        
                        jclass foundClass = env->FindClass(jniName.c_str());
                        if (foundClass)
                        {
                            classpaths.push_back("" + testClass);
                            env->DeleteLocalRef(foundClass);
                        }
                        else
                        {
                            env->ExceptionClear();
                        }
                    }
                }
            }
        }
        
        env->ExceptionClear(); // Clear any exceptions
        
    }
    catch (const std::exception& e)
    {
        classpaths.push_back("Error: " + std::string(e.what()));
    }
    
    return classpaths;
}

std::vector<std::string> JavaEditor::getClassesInPackage(const std::string& packageName)
{
    std::vector<std::string> classes;
    
    JNIEnv* env = getJavaEnv();
    if (!env)
    {
        classes.push_back("Java VM not initialized");
        return classes;
    }
    
    try
    {
        // Since getting all classes in a package via JNI is complex and implementation-dependent,
        // we'll use a discovery approach by testing common class patterns
        
        std::vector<std::string> commonClassNames;
        
        if (packageName == "java.lang")
        {
            commonClassNames = {
                "Object", "String", "System", "Math", "Thread", "Class", 
                "Integer", "Double", "Boolean", "Character", "Byte", "Short", "Long", "Float",
                "Exception", "RuntimeException", "Error", "Throwable",
                "StringBuilder", "StringBuffer", "Number"
            };
        }
        else if (packageName == "java.util")
        {
            commonClassNames = {
                "ArrayList", "HashMap", "HashSet", "LinkedList", "Vector", "Hashtable",
                "List", "Map", "Set", "Collection", "Iterator", "Comparator",
                "Date", "Calendar", "Timer", "Random", "Scanner",
                "Properties", "Collections", "Arrays"
            };
        }
        else if (packageName == "java.io")
        {
            commonClassNames = {
                "File", "FileInputStream", "FileOutputStream", "FileReader", "FileWriter",
                "BufferedReader", "BufferedWriter", "PrintWriter", "PrintStream",
                "InputStream", "OutputStream", "Reader", "Writer",
                "IOException", "FileNotFoundException", "Serializable"
            };
        }
        else if (packageName == "java.net")
        {
            commonClassNames = {
                "URL", "URI", "Socket", "ServerSocket", "URLConnection", "HttpURLConnection",
                "InetAddress", "NetworkInterface", "SocketException"
            };
        }
        
        // Test each potential class using JNI FindClass
        for (const auto& className : commonClassNames)
        {
            std::string fullClassName = packageName + "." + className;
            std::string jniClassName = packageName;
            std::replace(jniClassName.begin(), jniClassName.end(), '.', '/');
            jniClassName += "/" + className;
            
            jclass foundClass = env->FindClass(jniClassName.c_str());
            if (foundClass)
            {
                classes.push_back(fullClassName);
                env->DeleteLocalRef(foundClass);
            }
            else
            {
                env->ExceptionClear(); // Clear any ClassNotFoundException
            }
        }
        
        if (classes.empty())
        {
            classes.push_back("No classes found in package " + packageName);
            classes.push_back("Try using the class filter in the main list to search for specific classes");
        }
    }
    catch (const std::exception& e)
    {
        classes.push_back("Error discovering classes: " + std::string(e.what()));
    }
    
    return classes;
}

std::string JavaEditor::getClassInfo(const std::string& className)
{
    JNIEnv* env = getJavaEnv();
    if (!env)
    {
        return "Java VM not initialized";
    }
    
    try
    {
        // Convert dots to slashes for JNI
        std::string jniClassName = className;
        std::replace(jniClassName.begin(), jniClassName.end(), '.', '/');
        
        jclass clazz = env->FindClass(jniClassName.c_str());
        if (!clazz)
        {
            env->ExceptionClear();
            return "Class not found: " + className + "\nVerify the class is loaded in the JVM.";
        }
        
        std::ostringstream info;
        info << "=== CLASS INFORMATION (JNI Reflection) ===\n";
        info << "Class: " << className << "\n";
        info << "JNI Name: " << jniClassName << "\n\n";
        
        // Get Class object to use reflection
        jclass classClass = env->FindClass("java/lang/Class");
        if (classClass)
        {
            // Get class name via reflection
            jmethodID getNameMethod = env->GetMethodID(classClass, "getName", "()Ljava/lang/String;");
            if (getNameMethod)
            {
                jstring nameStr = (jstring)env->CallObjectMethod(clazz, getNameMethod);
                if (nameStr)
                {
                    const char* nameChars = env->GetStringUTFChars(nameStr, nullptr);
                    info << "Canonical Name: " << nameChars << "\n";
                    env->ReleaseStringUTFChars(nameStr, nameChars);
                }
            }
            
            // Get superclass
            jmethodID getSuperclassMethod = env->GetMethodID(classClass, "getSuperclass", "()Ljava/lang/Class;");
            if (getSuperclassMethod)
            {
                jobject superClass = env->CallObjectMethod(clazz, getSuperclassMethod);
                if (superClass)
                {
                    jstring superNameStr = (jstring)env->CallObjectMethod(superClass, getNameMethod);
                    if (superNameStr)
                    {
                        const char* superNameChars = env->GetStringUTFChars(superNameStr, nullptr);
                        info << "Superclass: " << superNameChars << "\n";
                        env->ReleaseStringUTFChars(superNameStr, superNameChars);
                    }
                }
                else
                {
                    info << "Superclass: None (java.lang.Object or primitive)\n";
                }
            }
            
            // Check if it's an interface
            jmethodID isInterfaceMethod = env->GetMethodID(classClass, "isInterface", "()Z");
            if (isInterfaceMethod)
            {
                jboolean isInterface = env->CallBooleanMethod(clazz, isInterfaceMethod);
                info << "Is Interface: " << (isInterface ? "Yes" : "No") << "\n";
            }
            
            // Check modifiers
            jmethodID getModifiersMethod = env->GetMethodID(classClass, "getModifiers", "()I");
            if (getModifiersMethod)
            {
                jint modifiers = env->CallIntMethod(clazz, getModifiersMethod);
                info << "Modifiers: " << modifiers << " (";
                
                // Check common modifiers using java.lang.reflect.Modifier constants
                if (modifiers & 0x0001) info << "public ";
                if (modifiers & 0x0002) info << "private ";
                if (modifiers & 0x0004) info << "protected ";
                if (modifiers & 0x0008) info << "static ";
                if (modifiers & 0x0010) info << "final ";
                if (modifiers & 0x0400) info << "abstract ";
                
                info << ")\n";
            }
            
            // Get methods (simplified - just count them for now)
            jmethodID getMethodsMethod = env->GetMethodID(classClass, "getMethods", "()[Ljava/lang/reflect/Method;");
            if (getMethodsMethod)
            {
                jobjectArray methods = (jobjectArray)env->CallObjectMethod(clazz, getMethodsMethod);
                if (methods)
                {
                    jsize methodCount = env->GetArrayLength(methods);
                    info << "Public Methods: " << methodCount << "\n";
                    
                    // List a few method names if available
                    if (methodCount > 0 && methodCount <= 10)
                    {
                        info << "Method Names:\n";
                        jclass methodClass = env->FindClass("java/lang/reflect/Method");
                        if (methodClass)
                        {
                            jmethodID methodGetNameMethod = env->GetMethodID(methodClass, "getName", "()Ljava/lang/String;");
                            if (methodGetNameMethod)
                            {
                                for (int i = 0; i < methodCount && i < 10; i++)
                                {
                                    jobject method = env->GetObjectArrayElement(methods, i);
                                    if (method)
                                    {
                                        jstring methodName = (jstring)env->CallObjectMethod(method, methodGetNameMethod);
                                        if (methodName)
                                        {
                                            const char* methodNameChars = env->GetStringUTFChars(methodName, nullptr);
                                            info << "  • " << methodNameChars << "()\n";
                                            env->ReleaseStringUTFChars(methodName, methodNameChars);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            // Get fields count
            jmethodID getFieldsMethod = env->GetMethodID(classClass, "getFields", "()[Ljava/lang/reflect/Field;");
            if (getFieldsMethod)
            {
                jobjectArray fields = (jobjectArray)env->CallObjectMethod(clazz, getFieldsMethod);
                if (fields)
                {
                    jsize fieldCount = env->GetArrayLength(fields);
                    info << "Public Fields: " << fieldCount << "\n";
                }
            }
        }
        
        info << "\n=== USAGE ===\n";
        info << "Use in Live Console:\n";
        info << "• Static method: " << className << ".methodName()\n";
        info << "• System properties: java.lang.System.getProperty(\"key\")\n";
        info << "• Math functions: java.lang.Math.abs(-5)\n";
        
        // Clear any exceptions that might have occurred during reflection
        env->ExceptionClear();
        
        return info.str();
    }
    catch (const std::exception& e)
    {
        return "Error getting class info: " + std::string(e.what());
    }
}


static JavaEditor instance;
JavaEditor &JavaEditor::get()
{
    return instance;
}

void JavaEditor::beginTokenize(size_t startLine)
{
    javaInMultiLineComment = false;
    // Pre-scan lines before the visible range for multi-line comment state
    for (size_t lineNum = 0; lineNum < startLine && lineNum < editorState.lines.size(); ++lineNum)
    {
        const std::string& line = editorState.lines[lineNum];
        size_t pos = 0;
        while (pos < line.length())
        {
            if (javaInMultiLineComment)
            {
                size_t endPos = line.find("*/", pos);
                if (endPos != std::string::npos) { javaInMultiLineComment = false; pos = endPos + 2; }
                else break;
            }
            else if (pos + 1 < line.length() && line[pos] == '/' && line[pos + 1] == '/')
            {
                break; // single-line comment
            }
            else if (pos + 1 < line.length() && line[pos] == '/' && line[pos + 1] == '*')
            {
                javaInMultiLineComment = true; pos += 2;
                size_t endPos = line.find("*/", pos);
                if (endPos != std::string::npos) { javaInMultiLineComment = false; pos = endPos + 2; }
                else break;
            }
            else if (line[pos] == '"' || line[pos] == '\'')
            {
                char q = line[pos]; size_t s = pos + 1; bool esc = false;
                while (s < line.length()) { if (!esc && line[s] == q) { s++; break; } esc = (line[s] == '\\' && !esc); s++; }
                pos = s;
            }
            else pos++;
        }
    }
}

std::vector<Editors::SyntaxToken> JavaEditor::tokenizeLine(const std::string& line, size_t lineIndex)
{
    using Editors::SyntaxToken;
    static const std::unordered_set<std::string> javaKeywords = {
        "abstract", "assert", "boolean", "break", "byte", "case", "catch", "char",
        "class", "const", "continue", "default", "do", "double", "else", "enum",
        "extends", "final", "finally", "float", "for", "goto", "if", "implements",
        "import", "instanceof", "int", "interface", "long", "native", "new",
        "package", "private", "protected", "public", "return", "short", "static",
        "strictfp", "super", "switch", "synchronized", "this", "throw", "throws",
        "transient", "try", "void", "volatile", "while", "true", "false", "null"
    };

    std::vector<SyntaxToken> tokens;
    if (line.empty()) return tokens;

    size_t pos = 0;

    while (pos < line.length())
    {
        size_t start = pos;
        ImU32 color = textColor;

        // Handle existing multi-line comment state
        if (javaInMultiLineComment)
        {
            size_t endPos = line.find("*/", pos);
            if (endPos != std::string::npos)
            {
                tokens.push_back({pos, endPos + 2 - pos, commentColor});
                pos = endPos + 2;
                javaInMultiLineComment = false;
            }
            else
            {
                tokens.push_back({pos, line.length() - pos, commentColor});
                pos = line.length();
            }
            continue;
        }
        // Single-line comment
        if (pos + 1 < line.length() && line[pos] == '/' && line[pos + 1] == '/')
        {
            tokens.push_back({pos, line.length() - pos, commentColor});
            break;
        }
        // Multi-line comment start
        if (pos + 1 < line.length() && line[pos] == '/' && line[pos + 1] == '*')
        {
            size_t endPos = line.find("*/", pos + 2);
            if (endPos != std::string::npos)
            {
                tokens.push_back({pos, endPos + 2 - pos, commentColor});
                pos = endPos + 2;
            }
            else
            {
                tokens.push_back({pos, line.length() - pos, commentColor});
                javaInMultiLineComment = true;
                pos = line.length();
            }
            continue;
        }
        // String and character literals
        if (line[pos] == '"' || line[pos] == '\'')
        {
            char quoteChar = line[pos];
            bool escaped = false;
            size_t endPos = pos + 1;
            while (endPos < line.length())
            {
                if (escaped) { escaped = false; }
                else if (line[endPos] == '\\') { escaped = true; }
                else if (line[endPos] == quoteChar) { endPos++; break; }
                endPos++;
            }
            tokens.push_back({pos, endPos - pos, stringColor});
            pos = endPos;
            continue;
        }
        // Numbers
        if (std::isdigit(line[pos]) || (line[pos] == '.' && pos + 1 < line.length() && std::isdigit(line[pos + 1])))
        {
            while (pos < line.length() && (std::isdigit(line[pos]) || line[pos] == '.' ||
                   line[pos] == 'f' || line[pos] == 'F' || line[pos] == 'l' || line[pos] == 'L' ||
                   line[pos] == 'd' || line[pos] == 'D'))
                pos++;
            tokens.push_back({start, pos - start, numberColor});
            continue;
        }
        // Identifiers
        if (std::isalpha(line[pos]) || line[pos] == '_' || line[pos] == '$')
        {
            while (pos < line.length() && (std::isalnum(line[pos]) || line[pos] == '_' || line[pos] == '$'))
                pos++;
            std::string word = line.substr(start, pos - start);

            if (javaKeywords.count(word))
            {
                color = keywordColor;
            }
            else
            {
                // Check if followed by '(' → method call
                size_t next = pos;
                while (next < line.length() && std::isspace(line[next])) next++;
                if (next < line.length() && line[next] == '(')
                    color = methodColor;
                else
                {
                    std::string currentPackage = getCurrentPackageName();
                    const Class* cls = programStructure.findClass(word, currentPackage);
                    if (!cls) cls = programStructure.findClass(word);
                    if (cls) color = classColor;
                    else
                    {
                        std::string currentClass = getCurrentClassName();
                        if (!currentClass.empty())
                        {
                            const Class* currentCls = resolveClass(currentClass);
                            if (currentCls)
                            {
                                for (const auto& m : currentCls->methods)
                                    if (m.name == word) { color = methodColor; break; }
                                if (color == textColor)
                                    for (const auto& v : currentCls->variables)
                                        if (v.name == word) { color = variableColor; break; }
                            }
                        }
                    }
                }
            }
            tokens.push_back({start, pos - start, color});
            continue;
        }
        // Operators / punctuation
        if (line[pos] == '+' || line[pos] == '-' || line[pos] == '*' || line[pos] == '/' ||
            line[pos] == '=' || line[pos] == '<' || line[pos] == '>' || line[pos] == '!' ||
            line[pos] == '&' || line[pos] == '|' || line[pos] == '^' || line[pos] == '%' ||
            line[pos] == '?' || line[pos] == ':' || line[pos] == ';' || line[pos] == ',' ||
            line[pos] == '(' || line[pos] == ')' || line[pos] == '{' || line[pos] == '}' ||
            line[pos] == '[' || line[pos] == ']')
        {
            tokens.push_back({pos, 1, operatorColor});
            pos++;
            continue;
        }
        // Whitespace
        if (std::isspace(line[pos]))
        {
            while (pos < line.length() && std::isspace(line[pos])) pos++;
            tokens.push_back({start, pos - start, textColor});
            continue;
        }
        // Any other character
        tokens.push_back({pos, 1, textColor});
        pos++;
    }

    return tokens;
}

void JavaEditor::onTextChanged()
{
    updateSyntaxErrors();
    updateLiveProgramStructure();
}
