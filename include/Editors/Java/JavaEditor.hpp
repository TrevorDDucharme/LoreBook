#pragma once
#define IMGUI_DEFINE_MATH_OPERATORS
#include <imgui.h>
#include <imgui_stdlib.h>
#include <filesystem>
#include <vector>
#include <thread>
#include <string>
#include <Editors/Common/BaseTextEditor.hpp>
#include <Editors/Java/ProgramStructure/ProgramStructure.hpp>
#include <Util/JVM.hpp>

class JavaEditor : public Editors::BaseTextEditor
{
    std::filesystem::path src;
    std::vector<std::filesystem::path> classPaths;
    ProgramStructure programStructure;
    std::thread fileWatcherThread;

    // Live console state
    std::string liveConsoleInput;
    std::string liveConsoleOutput;
    std::vector<std::string> liveConsoleHistory;
    int historyIndex = -1;

    // Class investigator state
    std::string classSearchFilter;
    std::string selectedClassName;
    std::string classInfoOutput;
    std::vector<std::string> availableClasses;
    std::vector<std::string> availablePackages;

    // Private helper methods
    void updateFileWatcher();
    void updateLiveProgramStructure();
    void updateLiveProgramStructureForAllTabs();
    bool isInClassContext(size_t line, size_t column) const;
    std::string getCurrentClassName() const;
    std::string getCurrentPackageName() const;
    void parseCurrentFileForContext();
    std::vector<std::string> getCurrentImports() const;
    const Class* resolveClass(const std::string &className) const;

    // Live console methods
    void drawLiveConsole();
    std::string executeJavaCode(const std::string& code);
    std::string convertJavaResultToJson(jobject result, JNIEnv* env);

    // Class investigation methods
    void drawClassInvestigator();
    std::vector<std::string> getAvailableClassPaths();
    std::vector<std::string> getClassesInPackage(const std::string& packageName);
    std::string getClassInfo(const std::string& className);
    std::string executeStaticMethod(const std::string& className, const std::string& methodName, const std::vector<std::string>& args);

protected:
    // BaseTextEditor overrides
    void drawTextContentWithSyntaxHighlighting(ImDrawList* drawList, const ImVec2& origin, float visibleHeight) override;
    void updateCompletions() override;
    void updateSyntaxErrors() override;
    void generateContextAwareCompletions(const std::string& prefix, bool isQualifiedAccess, const std::string& objectName) override;
    void onTextChanged() override;
    std::string getNewFileName() const override { return "Untitled.java"; }
    std::string getTabBarId() const override { return "JavaEditorTabs"; }

public:
    JavaEditor();
    ~JavaEditor();

    ImVec2 draw();
    ImVec2 drawConsole();

    void setSrc(std::filesystem::path source);
    void setClassPath(std::vector<std::filesystem::path> path);

    void openFile(std::filesystem::path file);
    void setWorkingFile(std::filesystem::path file);
    void updateVaultDirectories();

    const ProgramStructure& getProgramStructure() const { return programStructure; }

    static JavaEditor& get();
};