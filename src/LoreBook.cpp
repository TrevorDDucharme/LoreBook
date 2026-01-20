


#include <iostream>
#include <stdio.h>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#define IMGUI_IMPL_OPENGL_LOADER_GLEW
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <memory>
#include <filesystem>
#include "GraphView.hpp"
#include <Vault.hpp>

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
}

static void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

int main(int argc, char** argv)
{
    // Setup window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // GL 3.3 + core profile
    const char* glsl_version = "#version 330";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "LoreBook - ImGui Docking Demo", nullptr, nullptr);
    if (window == nullptr)
        return 1;
    glfwMakeContextCurrent(window);
    glfwSwapInterval(1); // vsync
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);

    // Initialize GLEW
    if (glewInit() != GLEW_OK) {
        std::cerr << "Failed to initialize GLEW\n";
        return 1;
    }

    // Setup Dear ImGui context
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;     // Enable Keyboard Controls
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;         // Enable Docking
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;       // Enable Multi-Viewport / Platform Windows

    // Setup Dear ImGui style
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
    {
        style.WindowRounding = 0.0f;
        style.Colors[ImGuiCol_WindowBg].w = 1.0f;
    }

    // Setup Platform/Renderer backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init(glsl_version);

    // Vault state
    static bool firstDock = true;
    std::unique_ptr<Vault> vault;

    // Graph view
    static bool showGraphWindow = true;
    static GraphView graphView;
    // Create Vault modal state
    static bool showCreateVaultModal = false;
    static char createVaultDirBuf[1024];
    static char createVaultNameBuf[256] = "example_vault.db";
    static char createVaultError[512] = "";
    // Open Vault modal state
    static bool showOpenVaultModal = false;
    static char openVaultDirBuf[1024];
    static char openVaultNameBuf[256] = "";
    static char openVaultError[512] = "";

    // File browser state
    enum class BrowserMode { None, BrowseForCreateDir, BrowseForOpenDir, BrowseForOpenFile };
    static bool showBrowseModal = false;
    static BrowserMode browserMode = BrowserMode::None;
    static std::filesystem::path browserPath;
    static std::string browserSelectedFile;
    // When a file is opened from the file picker successfully, request the parent Open modal to close
    static bool requestCloseOpenVaultModal = false;
    // When set by the file selector, request automatic opening of the selected file (legacy fallback)
    static bool openVaultAutoOpenRequested = false;

    // initialize directory buffers with current path once
    strncpy(createVaultDirBuf, std::filesystem::current_path().string().c_str(), sizeof(createVaultDirBuf));
    createVaultDirBuf[sizeof(createVaultDirBuf)-1] = '\0';
    strncpy(openVaultDirBuf, std::filesystem::current_path().string().c_str(), sizeof(openVaultDirBuf));
    openVaultDirBuf[sizeof(openVaultDirBuf)-1] = '\0';

    // Main loop
    while (!glfwWindowShouldClose(window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        // Fullscreen DockSpace on the main viewport
        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);
        ImGuiWindowFlags host_window_flags = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_MenuBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::Begin("DockSpaceHost", nullptr, host_window_flags);
        ImGui::PopStyleVar(2);

        // DockSpace
        ImGuiID dockspace_id = ImGui::GetID("MyDockSpace");
        ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f));

        // Initial layout: Vault Tree on the left, Vault Content in the main center
        if(firstDock){
            ImGui::DockBuilderRemoveNode(dockspace_id); // clear any existing layout
            ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspace_id, viewport->WorkSize);
            ImGuiID dock_main_id = dockspace_id;
            // Split: left for Vault Tree, right for Vault Graph, center for Vault Content
            ImGuiID dock_id_left = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Left, 0.25f, nullptr, &dock_main_id);
            ImGuiID dock_id_right = ImGui::DockBuilderSplitNode(dock_main_id, ImGuiDir_Right, 0.25f, nullptr, &dock_main_id);
            ImGui::DockBuilderDockWindow("Vault Tree", dock_id_left);
            ImGui::DockBuilderDockWindow("Vault Content", dock_main_id);
            ImGui::DockBuilderDockWindow("Vault Graph", dock_id_right);
            ImGui::DockBuilderFinish(dockspace_id);
            firstDock = false;
        }

        // Optional: menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Vault")) {
                    // Request modal to open on next frame to avoid menu parenting issues
                    showCreateVaultModal = true;
                    createVaultError[0] = '\0';
                }
                if (ImGui::MenuItem("Open Vault")) {
                    // Request open modal on next frame
                    showOpenVaultModal = true;
                    openVaultError[0] = '\0';
                }
                if (ImGui::MenuItem("Close Vault", nullptr, false, vault != nullptr)){
                    if(vault) vault.reset();
                }

                if (ImGui::MenuItem("Exit")) glfwSetWindowShouldClose(window, GLFW_TRUE);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View")){
                if(ImGui::MenuItem("Vault Graph", nullptr, showGraphWindow)){
                    showGraphWindow = !showGraphWindow;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // If requested, open the popup now (outside of the menu) so it isn't parented/blocked by the menu
        if (showCreateVaultModal) {
            ImGui::OpenPopup("Create Vault");
            showCreateVaultModal = false;
        }

        // Create Vault modal (moved out of menu to avoid menu/pop-up parenting issues)
        if (ImGui::BeginPopupModal("Create Vault", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::Text("Choose directory and filename for the new vault");
            ImGui::Separator();
            ImGui::InputText("Directory", createVaultDirBuf, sizeof(createVaultDirBuf));
            ImGui::SameLine();
            if(ImGui::Button("Use CWD")){
                strncpy(createVaultDirBuf, std::filesystem::current_path().string().c_str(), sizeof(createVaultDirBuf));
                createVaultDirBuf[sizeof(createVaultDirBuf)-1] = '\0';
            }
            ImGui::SameLine();
            if(ImGui::Button("Browse...")){
                showBrowseModal = true;
                browserMode = BrowserMode::BrowseForCreateDir;
                browserPath = std::filesystem::path(createVaultDirBuf);
            }
            ImGui::InputText("Filename", createVaultNameBuf, sizeof(createVaultNameBuf));

            if(createVaultError[0] != '\0'){
                ImGui::TextColored(ImVec4(1,0.4f,0.4f,1.0f), "%s", createVaultError);
            }

            if(ImGui::Button("Create")){
                // Validate inputs
                std::string dirStr(createVaultDirBuf);
                std::string nameStr(createVaultNameBuf);
                if(nameStr.empty()){
                    strncpy(createVaultError, "Filename cannot be empty", sizeof(createVaultError));
                } else {
                    try{
                        std::filesystem::path dir(dirStr);
                        if(!std::filesystem::exists(dir)) std::filesystem::create_directories(dir);
                        auto v = Vault::createExampleStructure(dir, nameStr);
                        if(!v.isOpen()){
                            strncpy(createVaultError, "Failed to open database file.", sizeof(createVaultError));
                        } else {
                            vault = std::make_unique<Vault>(std::move(v));
                            ImGui::CloseCurrentPopup();
                            showCreateVaultModal = false;
                        }
                    } catch(const std::exception &ex){
                        strncpy(createVaultError, ex.what(), sizeof(createVaultError));
                    }
                }
            }
            ImGui::SameLine();
            if(ImGui::Button("Cancel")){
                ImGui::CloseCurrentPopup();
                showCreateVaultModal = false;
            }

            ImGui::EndPopup();
        }

        // If requested, open the Open Vault popup now (outside of the menu) so it isn't parented/blocked by the menu
        if (showOpenVaultModal) {
            ImGui::OpenPopup("Open Vault");
            showOpenVaultModal = false;
        }

        // Open Vault modal
        if (ImGui::BeginPopupModal("Open Vault", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::Text("Choose directory and filename for the vault to open");
            ImGui::Separator();
            ImGui::InputText("Directory", openVaultDirBuf, sizeof(openVaultDirBuf));
            ImGui::SameLine();
            if(ImGui::Button("Use CWD")){
                strncpy(openVaultDirBuf, std::filesystem::current_path().string().c_str(), sizeof(openVaultDirBuf));
                openVaultDirBuf[sizeof(openVaultDirBuf)-1] = '\0';
            }
            ImGui::SameLine();
            if(ImGui::Button("Browse...")){
                showBrowseModal = true;
                browserMode = BrowserMode::BrowseForOpenDir;
                browserPath = std::filesystem::path(openVaultDirBuf);
            }
            ImGui::InputText("Filename", openVaultNameBuf, sizeof(openVaultNameBuf));
            ImGui::SameLine();
            if(ImGui::Button("Browse File...")){
                showBrowseModal = true;
                browserMode = BrowserMode::BrowseForOpenFile;
                browserPath = std::filesystem::path(openVaultDirBuf);
                browserSelectedFile.clear();
            }

            // If a file was selected in the file browser, handle it like the Open button was pressed
            if(openVaultAutoOpenRequested){
                // legacy fallback: process immediately and clear the flag so it doesn't trigger later
                openVaultAutoOpenRequested = false;
                std::string dirStr(openVaultDirBuf);
                std::string nameStr(openVaultNameBuf);
                if(nameStr.empty()){
                    strncpy(openVaultError, "Filename cannot be empty", sizeof(openVaultError));
                } else {
                    try{
                        std::filesystem::path full;
                        std::filesystem::path namePath(nameStr);
                        std::filesystem::path dirPath(dirStr);
                        if(namePath.is_absolute()){
                            full = namePath;
                        } else {
                            full = dirPath / namePath;
                        }
                        if(!std::filesystem::exists(full) || !std::filesystem::is_regular_file(full)){
                            strncpy(openVaultError, "File does not exist", sizeof(openVaultError));
                        } else {
                            // open using directory + filename
                            std::filesystem::path parent = full.parent_path();
                            std::string fname = full.filename().string();
                            Vault v(parent, fname);
                            if(!v.isOpen()){
                                strncpy(openVaultError, "Failed to open database file.", sizeof(openVaultError));
                            } else {
                                vault = std::make_unique<Vault>(std::move(v));
                                ImGui::CloseCurrentPopup();
                                showOpenVaultModal = false;
                            }
                        }
                    } catch(const std::exception &ex){
                        strncpy(openVaultError, ex.what(), sizeof(openVaultError));
                    }
                }
            }

            // If requested (e.g., the file selector opened and succeeded), close this Open modal now
            if(requestCloseOpenVaultModal){
                requestCloseOpenVaultModal = false;
                ImGui::CloseCurrentPopup();
                showOpenVaultModal = false;
            }

            if(openVaultError[0] != '\0'){
                ImGui::TextColored(ImVec4(1,0.4f,0.4f,1.0f), "%s", openVaultError);
            }

            if(ImGui::Button("Open")){
                std::string dirStr(openVaultDirBuf);
                std::string nameStr(openVaultNameBuf);
                if(nameStr.empty()){
                    strncpy(openVaultError, "Filename cannot be empty", sizeof(openVaultError));
                } else {
                    try{
                        std::filesystem::path full;
                        std::filesystem::path namePath(nameStr);
                        std::filesystem::path dirPath(dirStr);
                        if(namePath.is_absolute()){
                            full = namePath;
                        } else {
                            full = dirPath / namePath;
                        }
                        if(!std::filesystem::exists(full) || !std::filesystem::is_regular_file(full)){
                            strncpy(openVaultError, "File does not exist", sizeof(openVaultError));
                        } else {
                            // open using directory + filename
                            std::filesystem::path parent = full.parent_path();
                            std::string fname = full.filename().string();
                            Vault v(parent, fname);
                            if(!v.isOpen()){
                                strncpy(openVaultError, "Failed to open database file.", sizeof(openVaultError));
                            } else {
                                vault = std::make_unique<Vault>(std::move(v));
                                ImGui::CloseCurrentPopup();
                                showOpenVaultModal = false;
                            }
                        }
                    } catch(const std::exception &ex){
                        strncpy(openVaultError, ex.what(), sizeof(openVaultError));
                    }
                }
            }
            ImGui::SameLine();
            if(ImGui::Button("Cancel")){
                ImGui::CloseCurrentPopup();
                showOpenVaultModal = false;
            }

            ImGui::EndPopup();
        }

        // Open the appropriate browse popup if requested
        if(showBrowseModal){
            if(browserMode == BrowserMode::BrowseForOpenFile)
                ImGui::OpenPopup("Select Vault File");
            else
                ImGui::OpenPopup("Browse Directory");
            showBrowseModal = false;
        }

        // Browse Directory modal (select a directory)
        if(ImGui::BeginPopupModal("Browse Directory", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::Text("Select directory:");
            ImGui::Separator();
            ImGui::TextWrapped("%s", browserPath.string().c_str());
            ImGui::SameLine();
            if(ImGui::Button("Up")){
                if(browserPath.has_parent_path()) browserPath = browserPath.parent_path();
            }
            ImGui::Separator();
            // List directories
            try{
                std::vector<std::filesystem::directory_entry> dirs;
                for(auto &e : std::filesystem::directory_iterator(browserPath)){
                    if(e.is_directory()) dirs.push_back(e);
                }
                std::sort(dirs.begin(), dirs.end(), [](const auto &a, const auto &b){ return a.path().filename().string() < b.path().filename().string(); });
                for(auto &d : dirs){
                    std::string label = d.path().filename().string();
                    if(ImGui::Selectable(label.c_str())){
                        browserPath = d.path();
                    }
                    if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ImGui::IsItemHovered()){
                        // Accept this directory for the mode that requested it
                        if(browserMode == BrowserMode::BrowseForCreateDir){
                            strncpy(createVaultDirBuf, browserPath.string().c_str(), sizeof(createVaultDirBuf));
                            createVaultDirBuf[sizeof(createVaultDirBuf)-1] = '\0';
                            ImGui::CloseCurrentPopup();
                        } else if(browserMode == BrowserMode::BrowseForOpenDir){
                            strncpy(openVaultDirBuf, browserPath.string().c_str(), sizeof(openVaultDirBuf));
                            openVaultDirBuf[sizeof(openVaultDirBuf)-1] = '\0';
                            ImGui::CloseCurrentPopup();
                        }
                    }
                }
            } catch(...){}

            if(ImGui::Button("Select Here")){
                if(browserMode == BrowserMode::BrowseForCreateDir){
                    strncpy(createVaultDirBuf, browserPath.string().c_str(), sizeof(createVaultDirBuf));
                    createVaultDirBuf[sizeof(createVaultDirBuf)-1] = '\0';
                } else if(browserMode == BrowserMode::BrowseForOpenDir){
                    strncpy(openVaultDirBuf, browserPath.string().c_str(), sizeof(openVaultDirBuf));
                    openVaultDirBuf[sizeof(openVaultDirBuf)-1] = '\0';
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        // Select Vault File modal (choose a file in the directory)
        if(ImGui::BeginPopupModal("Select Vault File", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::Text("Select file:");
            ImGui::Separator();
            ImGui::TextWrapped("%s", browserPath.string().c_str());
            ImGui::SameLine();
            if(ImGui::Button("Up")){
                if(browserPath.has_parent_path()) browserPath = browserPath.parent_path();
            }
            ImGui::Separator();
            try{
                std::vector<std::filesystem::directory_entry> dirs, files;
                for(auto &e : std::filesystem::directory_iterator(browserPath)){
                    if(e.is_directory()) dirs.push_back(e);
                    else if(e.is_regular_file()) files.push_back(e);
                }
                std::sort(dirs.begin(), dirs.end(), [](const auto &a, const auto &b){ return a.path().filename().string() < b.path().filename().string(); });
                std::sort(files.begin(), files.end(), [](const auto &a, const auto &b){ return a.path().filename().string() < b.path().filename().string(); });
                for(auto &d : dirs){
                    std::string label = std::string("[DIR] ") + d.path().filename().string();
                    if(ImGui::Selectable(label.c_str())){
                        browserPath = d.path();
                    }
                }
                for(auto &f : files){
                    std::string fname = f.path().filename().string();
                    bool sel = (browserSelectedFile == fname);
                    if(ImGui::Selectable(fname.c_str(), sel)){
                        // Single-click accepts the file immediately and attempt to open it
                        browserSelectedFile = fname;
                        strncpy(openVaultDirBuf, browserPath.string().c_str(), sizeof(openVaultDirBuf));
                        openVaultDirBuf[sizeof(openVaultDirBuf)-1] = '\0';
                        strncpy(openVaultNameBuf, browserSelectedFile.c_str(), sizeof(openVaultNameBuf));
                        openVaultNameBuf[sizeof(openVaultNameBuf)-1] = '\0';

                        // Attempt to open immediately so the action completes where the user clicked
                        try{
                            std::filesystem::path full = browserPath / fname;
                            if(!std::filesystem::exists(full) || !std::filesystem::is_regular_file(full)){
                                strncpy(openVaultError, "File does not exist or is not a regular file", sizeof(openVaultError));
                                // Close file picker and re-open Open dialog so user sees error
                                ImGui::CloseCurrentPopup();
                                showOpenVaultModal = true;
                            } else {
                                std::filesystem::path parent = full.parent_path();
                                std::string fonly = full.filename().string();
                                Vault v(parent, fonly);
                                if(!v.isOpen()){
                                    strncpy(openVaultError, "Failed to open database file.", sizeof(openVaultError));
                                    ImGui::CloseCurrentPopup();
                                    showOpenVaultModal = true;
                                } else {
                                    vault = std::make_unique<Vault>(std::move(v));
                                    openVaultError[0] = '\0';
                                    // Close the file picker now and request the parent Open modal to close too
                                    ImGui::CloseCurrentPopup();
                                    showOpenVaultModal = false;
                                    requestCloseOpenVaultModal = true;
                                }
                            }
                        } catch(const std::exception &ex){
                            strncpy(openVaultError, ex.what(), sizeof(openVaultError));
                            ImGui::CloseCurrentPopup();
                            showOpenVaultModal = true;
                        }
                    }
                    // Also accept on double-click for convenience
                    if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ImGui::IsItemHovered()){
                        // Duplicate single-click behavior
                        browserSelectedFile = fname;
                        strncpy(openVaultDirBuf, browserPath.string().c_str(), sizeof(openVaultDirBuf));
                        openVaultDirBuf[sizeof(openVaultDirBuf)-1] = '\0';
                        strncpy(openVaultNameBuf, browserSelectedFile.c_str(), sizeof(openVaultNameBuf));
                        openVaultNameBuf[sizeof(openVaultNameBuf)-1] = '\0';

                        try{
                            std::filesystem::path full = browserPath / fname;
                            if(!std::filesystem::exists(full) || !std::filesystem::is_regular_file(full)){
                                strncpy(openVaultError, "File does not exist or is not a regular file", sizeof(openVaultError));
                                ImGui::CloseCurrentPopup();
                                showOpenVaultModal = true;
                            } else {
                                std::filesystem::path parent = full.parent_path();
                                std::string fonly = full.filename().string();
                                Vault v(parent, fonly);
                                if(!v.isOpen()){
                                    strncpy(openVaultError, "Failed to open database file.", sizeof(openVaultError));
                                    ImGui::CloseCurrentPopup();
                                    showOpenVaultModal = true;
                                } else {
                                    vault = std::make_unique<Vault>(std::move(v));
                                    openVaultError[0] = '\0';
                                    ImGui::CloseCurrentPopup();
                                    showOpenVaultModal = false;
                                    requestCloseOpenVaultModal = true;
                                }
                            }
                        } catch(const std::exception &ex){
                            strncpy(openVaultError, ex.what(), sizeof(openVaultError));
                            ImGui::CloseCurrentPopup();
                            showOpenVaultModal = true;
                        }
                    }
                }
            } catch(...){}

            if(ImGui::Button("Open") && !browserSelectedFile.empty()){
                strncpy(openVaultDirBuf, browserPath.string().c_str(), sizeof(openVaultDirBuf));
                openVaultDirBuf[sizeof(openVaultDirBuf)-1] = '\0';
                strncpy(openVaultNameBuf, browserSelectedFile.c_str(), sizeof(openVaultNameBuf));
                openVaultNameBuf[sizeof(openVaultNameBuf)-1] = '\0';
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::End(); // DockSpaceHost

        // Render Vault windows if available
        if(vault){
            vault->drawVaultTree();
            vault->drawVaultContent();
        }

        // Graph view (dockable)
        graphView.setVault(vault.get());
        if(showGraphWindow){
            // pass dt as ImGui frame delta
            graphView.updateAndDraw(ImGui::GetIO().DeltaTime);
        }

        // Rendering
        ImGui::Render();
        int display_w, display_h;
        glfwGetFramebufferSize(window, &display_w, &display_h);
        glViewport(0, 0, display_w, display_h);
        glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        // Update and Render additional Platform Windows (when multi-viewports enabled)
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        {
            GLFWwindow* backup_current_context = glfwGetCurrentContext();
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
            glfwMakeContextCurrent(backup_current_context);
        }

        glfwSwapBuffers(window);
    }

    // Cleanup
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();

    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}