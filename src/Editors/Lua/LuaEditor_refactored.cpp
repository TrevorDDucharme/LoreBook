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

// ============================================================================
// Lua-specific entry points (draw wrappers with tab bar)
// ============================================================================

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
        for (int i = 0; i < static_cast<int>(tabs.size()); ++i)
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

// ============================================================================
// BaseTextEditor overrides
// ============================================================================

void LuaEditor::drawEditorOverlay(const ImVec2 &textOrigin)
{
    // API docs toggle button
    ImGui::SameLine();
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - 120);
    if (ImGui::Button("API Docs", ImVec2(100, 0)))
        showDocViewer = !showDocViewer;

    if (showDocViewer)
        drawClassInvestigator();
}

void LuaEditor::onTextChanged()
{
    updateSyntaxErrors();
    updateLiveProgramStructure();
}

// ============================================================================
// File / Tab helpers
// ============================================================================

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
    openTab(file);
}

void LuaEditor::setWorkingFile(std::filesystem::path file)
{
    openFile(std::move(file));
}

void LuaEditor::forceReloadFromDisk()
{
    Editors::EditorTab *t = getActiveTab();
    if (t)
    {
        loadTabContent(*t);
        syncActiveTabToEditor();
    }
}

void LuaEditor::updateProjectDirectories()
{
    updateFileWatcher();
}

void LuaEditor::loadTabContent(Editors::EditorTab &tab)
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
    tab.editorState.cursorLine = 0;
    tab.editorState.cursorColumn = 0;
    tab.editorState.scrollY = 0.0f;
    tab.editorState.scrollX = 0.0f;
    tab.editorState.hasSelection = false;
    tab.editorState.currentFile = tab.filePath.string();
    tab.editorState.isDirty = false;
}

void LuaEditor::saveActiveTab()
{
    Editors::EditorTab *activeTab = getActiveTab();
    if (!activeTab)
        return;

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

// ============================================================================
// File watcher
// ============================================================================

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

// ============================================================================
// Lua completions and syntax errors
// ============================================================================

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
    if (showCompletions)
    {
        completionOriginLine = editorState.cursorLine;
        completionOriginColumn = editorState.cursorColumn;
    }
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

// ============================================================================
// Live Console
// ============================================================================

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

// ============================================================================
// Per-tab Lua engine management
// ============================================================================

LuaEngine* LuaEditor::getOrCreateTabEngine()
{
    Editors::EditorTab *t = getActiveTab();
    if (!t) return nullptr;

    std::string key = t->filePath.string();
    auto it = tabEngines.find(key);
    if (it != tabEngines.end() && it->second && !t->editorState.isDirty)
        return it->second.get();

    // Gather current editor text
    std::string src;
    for (size_t i = 0; i < t->editorState.lines.size(); ++i)
    {
        src += t->editorState.lines[i];
        if (i + 1 < t->editorState.lines.size()) src += '\n';
    }

    auto eng = std::make_unique<LuaEngine>();
    lua_State *L = eng->L();
    if (L)
    {
        registerLuaImGuiBindings(L);
        Vault *fsVault = nullptr;
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
    if (!eng->loadScript(src))
    {
        // Store it anyway so we don't recreate on every call
        tabEngines[key] = std::move(eng);
        return tabEngines[key].get();
    }

    tabEngines[key] = std::move(eng);
    return tabEngines[key].get();
}

std::string LuaEditor::executeLuaCode(const std::string &code)
{
    Editors::EditorTab *t = getActiveTab();
    if (!t) return std::string("(no active tab)");

    // Gather current editor text
    std::string src;
    for (size_t i = 0; i < t->editorState.lines.size(); ++i) { src += t->editorState.lines[i]; if (i + 1 < t->editorState.lines.size()) src += '\n'; }

    // Get or create per-tab engine
    std::string key = t->filePath.string();
    auto it = tabEngines.find(key);
    LuaEngine *eng = nullptr;
    if (it != tabEngines.end() && it->second && !t->editorState.isDirty)
    {
        eng = it->second.get();
    }
    else
    {
        auto newEng = std::make_unique<LuaEngine>();
        lua_State *L = newEng->L();
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
        if (!newEng->loadScript(src))
        {
            std::string err = newEng->lastError();
            tabEngines[key] = std::move(newEng);
            return std::string("(load error) ") + err;
        }
        tabEngines[key] = std::move(newEng);
        eng = tabEngines[key].get();
    }

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

std::string LuaEditor::runExampleSnippet(const std::string &code)
{
    // If we have an active tab with a live engine, reuse that environment
    Editors::EditorTab *t = getActiveTab();
    std::string key = t ? t->filePath.string() : "";
    auto it = tabEngines.find(key);
    if (t && it != tabEngines.end() && it->second && !t->editorState.isDirty) {
        return executeLuaCode(code);
    }

    // No active tab or no live engine: create a temporary engine and register bindings
    try {
        LuaEngine eng;
        lua_State *L = eng.L();
        if (!L) return std::string("(no lua state)");

        registerLuaImGuiBindings(L);
        registerLuaCanvasBindings(L, ImVec2(0,0), 320, 240);

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

        ImGuiErrorRecoveryState errState;
        ImGui::ErrorRecoveryStoreState(&errState);
        ImGuiIO &io = ImGui::GetIO();
        bool oldAssert = io.ConfigErrorRecoveryEnableAssert;
        bool oldTooltip = io.ConfigErrorRecoveryEnableTooltip;
        io.ConfigErrorRecoveryEnableAssert = false;
        io.ConfigErrorRecoveryEnableTooltip = true;

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

ScriptConfig LuaEditor::detectSnippetConfig(const std::string &code)
{
    ScriptConfig out;
    try {
        LuaEngine eng;
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

bool LuaEditor::startPreview(const std::string &code, ImVec2 origin, int width, int height, ScriptConfig *outConfig)
{
    stopPreview();
    try {
        previewEngine = std::make_unique<LuaEngine>();
        lua_State *L = previewEngine->L();
        if (!L) { stopPreview(); return false; }

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

        ScriptConfig cfg = previewEngine->callConfig();
        if (cfg.type != ScriptConfig::Type::None) {
            previewType = (cfg.type == ScriptConfig::Type::Canvas) ? PreviewType::Canvas : PreviewType::UI;
            if (cfg.width > 0) width = cfg.width;
            if (cfg.height > 0) height = cfg.height;
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
            unsigned int texID = previewEngine->renderCanvasFrame("preview", previewWidth, previewHeight, (float)dt);
            ImVec2 avail = ImGui::GetContentRegionAvail();
            ImGui::Image((ImTextureID)(intptr_t)texID, avail);
        } else if (previewType == PreviewType::UI) {
            previewEngine->callUI();
        } else {
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

std::string LuaEditor::runPreviewSnippet(const std::string &code, ImVec2 origin, int width, int height)
{
    try {
        LuaEngine eng;
        lua_State *L = eng.L();
        if (!L) return std::string("(no lua state)");

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

        ImGuiErrorRecoveryState errState;
        ImGui::ErrorRecoveryStoreState(&errState);
        ImGuiIO &io = ImGui::GetIO();
        bool oldAssert = io.ConfigErrorRecoveryEnableAssert;
        bool oldTooltip = io.ConfigErrorRecoveryEnableTooltip;
        io.ConfigErrorRecoveryEnableAssert = false;
        io.ConfigErrorRecoveryEnableTooltip = true;

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

// ============================================================================
// Lua syntax highlighting
// ============================================================================

void LuaEditor::drawTextContentWithSyntaxHighlighting(ImDrawList *drawList, const ImVec2 &origin, float visibleHeight)
{
    static const std::unordered_set<std::string> luaKeywords = {"and","break","do","else","elseif","end","false","for","function","if","in","local","nil","not","or","repeat","return","then","true","until","while"};
    ImFont *font = ImGui::GetFont(); if (!font) return;
    float baselineOffset = (lineHeight - renderFontSize) * 0.5f;
    float y = origin.y - editorState.scrollY + baselineOffset;
    size_t startLine = static_cast<size_t>(std::max(0.0f, editorState.scrollY / lineHeight));
    size_t endLine = std::min(editorState.lines.size(), startLine + static_cast<size_t>(visibleHeight / lineHeight) + 2);

    bool inMultiComment = false;

    for (size_t ln = startLine; ln < endLine; ++ln)
    {
        float lineY = y + ln * lineHeight; if (lineY > origin.y + visibleHeight) break; if (lineY + lineHeight < origin.y) continue;
        const std::string &line = editorState.lines[ln]; if (line.empty()) { inMultiComment = false; continue; }
        float x = origin.x; size_t pos = 0;

        if (inMultiComment)
        {
            size_t end = line.find("]]", pos);
            if (end == std::string::npos)
            {
                drawList->AddText(font, renderFontSize, ImVec2(x, lineY), commentColor, line.c_str());
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
            if (line[pos] == '-' && pos + 1 < line.length() && line[pos+1] == '-')
            {
                if (pos + 3 < line.length() && line[pos+2] == '[' && line[pos+3] == '[')
                {
                    size_t end = line.find("]]", pos + 4);
                    if (end == std::string::npos)
                    {
                        std::string tok = line.substr(pos);
                        drawList->AddText(font, renderFontSize, ImVec2(x, lineY), commentColor, tok.c_str());
                        inMultiComment = true;
                        break;
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

// ============================================================================
// API Docs control
// ============================================================================

void LuaEditor::openApiDocs() { showDocViewer = true; }
void LuaEditor::closeApiDocs() { showDocViewer = false; }
bool LuaEditor::isApiDocsOpen() const { return showDocViewer; }
void LuaEditor::renderApiDocsIfOpen() { if (showDocViewer) drawClassInvestigator(); }

// ============================================================================
// Program structure stubs
// ============================================================================

void LuaEditor::updateLiveProgramStructure() { /* minimal: update current file in programStructure if needed */ }
void LuaEditor::updateLiveProgramStructureForAllTabs() { /* minimal placeholder */ }
bool LuaEditor::isInClassContext(size_t, size_t) const { return false; }

void LuaEditor::generateContextAwareCompletions(const std::string &prefix, bool isQualifiedAccess, const std::string &objectName)
{
    (void)isQualifiedAccess; (void)objectName;
    static const std::vector<std::string> kws = {"function","local","if","then","else","end","for","in","pairs","ipairs","return","require","while","repeat","until","true","false","nil"};
    for (const auto &k: kws)
        if (k.find(prefix) == 0)
        {
            Editors::CompletionItem it; it.text = k; it.description = "Lua keyword"; it.type = Editors::CompletionItem::KEYWORD; completionItems.push_back(it);
        }

    try {
        LuaEngine eng;
        lua_State *L = eng.L();
        if (L) {
            registerLuaImGuiBindings(L);
            registerLuaCanvasBindings(L, ImVec2(0,0), 300, 200);

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

            if (isQualifiedAccess && !objectName.empty())
            {
                lua_getglobal(L, objectName.c_str());
                if (lua_istable(L, -1))
                {
                    lua_pushnil(L);
                    while (lua_next(L, -2) != 0)
                    {
                        if (lua_type(L, -2) == LUA_TSTRING)
                        {
                            const char *mname = lua_tostring(L, -2);
                            if (mname && mname[0] != '_')
                            {
                                std::string sname(mname);
                                if (sname.rfind(prefix, 0) == 0)
                                {
                                    Editors::CompletionItem it; it.text = sname;
                                    int vtype = lua_type(L, -1);
                                    if (vtype == LUA_TFUNCTION) { it.type = Editors::CompletionItem::METHOD; it.description = "binding method"; }
                                    else { it.type = Editors::CompletionItem::VARIABLE; it.description = "binding field"; }
                                    completionItems.push_back(it);
                                }
                            }
                        }
                        lua_pop(L, 1);
                    }
                }
                lua_pop(L, 1);
            }
            else
            {
                lua_pushglobaltable(L);
                lua_pushnil(L);
                while (lua_next(L, -2) != 0)
                {
                    if (lua_type(L, -2) == LUA_TSTRING)
                    {
                        const char *name = lua_tostring(L, -2);
                        if (name && name[0] != '_')
                        {
                            std::string sname(name);
                            if (sname.rfind(prefix, 0) == 0)
                            {
                                Editors::CompletionItem it;
                                it.text = sname;
                                int vtype = lua_type(L, -1);
                                if (vtype == LUA_TFUNCTION) { it.type = Editors::CompletionItem::METHOD; it.description = "binding function"; }
                                else { it.type = Editors::CompletionItem::VARIABLE; it.description = "binding"; }
                                completionItems.push_back(it);
                            }
                        }
                    }
                    lua_pop(L, 1);
                }
                lua_pop(L, 1);
            }
        }
    }
    catch (...) { /* best-effort: ignore lua completion failures */ }
}

// ============================================================================
// API Docs Viewer (Class Investigator)
// ============================================================================

void LuaEditor::drawClassInvestigator()
{
    static std::string filter;
    static int selected = -1;
    static std::string lastExampleOutput;

    ImGui::SetNextWindowSize(ImVec2(620, 480), ImGuiCond_Appearing);
    ImGui::SetNextWindowDockID(ImGui::GetID("MyDockSpace"), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowViewport(ImGui::GetMainViewport()->ID);
    ImGuiWindowFlags flags = ImGuiWindowFlags_None;
    if (!ImGui::Begin("API Docs", &showDocViewer, flags)) { ImGui::End(); return; }

    ImGui::BeginGroup(); ImGui::Text("Filter:"); ImGui::SameLine(); ImGui::PushItemWidth(260); ImGui::InputText("##ApiFilter", &filter); ImGui::PopItemWidth(); ImGui::SameLine();
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
    static int moduleIndex = 0;
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
        if (moduleIndex != 0 && mod != modList[moduleIndex]) continue;
        groups[mod].push_back((int)i);
    }

    ImGui::BeginChild("DocList", ImVec2(280, -ImGui::GetFrameHeightWithSpacing()));
    for (const auto &kv : groups)
    {
        bool open = ImGui::CollapsingHeader(kv.first.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        if (!open) continue;
        for (int idx : kv.second)
        {
            const auto &e = all[idx];
            std::string label = e.name + " - " + e.signature;
            std::string l = label; std::transform(l.begin(), l.end(), l.begin(), ::tolower);
            bool contains = lfilter.empty() || l.find(lfilter) != std::string::npos;
            if (contains) ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_Text));
            bool sel = (selected == idx);
            if (ImGui::Selectable(label.c_str(), sel)) { selected = idx; lastExampleOutput.clear(); }
            if (contains) ImGui::PopStyleColor();
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    ImGui::BeginChild("DocDetail", ImVec2(0, -ImGui::GetFrameHeightWithSpacing()));
    if (selected >= 0 && selected < (int)all.size())
    {
        const auto &e = all[selected];
        ImGui::TextDisabled("%s", e.name.c_str());
        ImGui::Separator();

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
            static int prevSelected = -2;
            if (selected != prevSelected) {
                if (previewRunning) stopPreview();
                previewExecuteRequested = false;
                previewCode = e.example;
                previewOutput.clear();
                lastExampleOutput.clear();
                prevSelected = selected;
            }
            bool detectionDirty = false;

            ImGui::BeginChild("ExampleEdit", ImVec2(0, 120), true);
            ImGui::PushItemWidth(-1);
            bool edited = ImGui::InputTextMultiline("##PreviewCode", &previewCode, ImVec2(-1, -1));
            ImGui::PopItemWidth();
            ImGui::EndChild();

            if (edited)
            {
                if (previewRunning)
                {
                    startPreview(previewCode, previewOrigin, previewWidth, previewHeight);
                }
                else
                {
                    previewExecuteRequested = true;
                }
                detectionDirty = true;
            }

            ImGui::BeginGroup();
            if (ImGui::Button("Copy Example")) ImGui::SetClipboardText(previewCode.c_str());
            ImGui::SameLine();
            if (ImGui::Button("Insert into Console")) { liveConsoleInput = previewCode; ImGui::SetKeyboardFocusHere(); }
            ImGui::SameLine();

            static int _lastDetectSel = -1;
            static ScriptConfig _cachedCfg;
            bool hasConfig = false;
            if (selected != _lastDetectSel || detectionDirty) { _cachedCfg = detectSnippetConfig(previewCode); _lastDetectSel = selected; }
            if (_cachedCfg.type != ScriptConfig::Type::None) hasConfig = true;

            if (hasConfig)
            {
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
                if (ImGui::Button("Run Example")) { pendingExample = previewCode; runExampleConfirmOpen = true; ImGui::OpenPopup("Run Example Confirmation"); }
            }
            ImGui::EndGroup();

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

                    if (hasConfig)
                    {
                        auto pos = ImGui::GetCursorScreenPos();
                        ImGui::Separator();
                        ImGui::TextDisabled("Preview:");
                        ImGui::BeginChild("PreviewArea", ImVec2(360, 240), true, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NavFlattened);
                        ImVec2 childOrigin = ImGui::GetCursorScreenPos();
                        ImVec2 childSize = ImGui::GetContentRegionAvail();

                        if (previewRunning && previewEngine)
                        {
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
                                            lua_getglobal(pvL, "fs");
                                            bool hasFsGlobal = !lua_isnil(pvL, -1);
                                            lua_pop(pvL, 1);
                                            if (!hasFsGlobal)
                                                registerLuaFSBindings(pvL, vaultPtr.get());
                                        }
                                    }
                                }
                            }
                            previewOrigin = childOrigin;
                            previewWidth = (int)childSize.x;
                            previewHeight = (int)childSize.y;
                            previewTick();
                        }

                        if (previewExecuteRequested && !previewRunning)
                        {
                            previewOutput = runPreviewSnippet(previewCode, childOrigin, (int)childSize.x, (int)childSize.y);
                            previewExecuteRequested = false;
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
                        previewOutput.clear();
                        previewExecuteRequested = false;
                    }

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
