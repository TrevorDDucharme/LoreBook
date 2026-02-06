


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
#include <algorithm>
#include <cmath>
#include <chrono>
#include <mutex>
#include <atomic>
#include "GraphView.hpp"
#include <Vault.hpp>
#include "VaultChat.hpp"
#include "ResourceExplorer.hpp"
#include "ScriptEditor.hpp"
#include "Fonts.hpp"
#include <LoreBook_Resources/LoreBook_ResourcesEmbeddedVFS.hpp>
#include "MergeConflictUI.hpp"
#include "MySQLTest.hpp"
#include "VaultSync.hpp"
#include <plog/Log.h>
#include <plog/Init.h>
#include <plog/Appenders/ConsoleAppender.h>
#include <plog/Formatters/TxtFormatter.h>
#include <WorldMaps/WorldMap.hpp>
#include <WorldMaps/Buildings/FloorPlanEditor.hpp>
#include <future>
#include <CharacterEditor/ModelLoader.hpp>
#include <CharacterEditor/CharacterEditorUI.hpp>
#include <CharacterEditor/PartLibrary.hpp>
#include <CharacterEditor/CharacterManager.hpp>

static void glfw_error_callback(int error, const char* description)
{
    fprintf(stderr, "Glfw Error %d: %s\n", error, description);
} 

static void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
}

// Center the next popup on the main viewport and set viewport so it stays attached when viewports are enabled.
static inline void CenterNextPopupOnMainViewport()
{
    ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    if (main_viewport)
    {
        ImVec2 center(main_viewport->WorkPos.x + main_viewport->WorkSize.x * 0.5f,
                       main_viewport->WorkPos.y + main_viewport->WorkSize.y * 0.5f);
        ImGui::SetNextWindowViewport(main_viewport->ID);
        // Use Always so popups get re-centered even if they've already been opened on a previous frame
        ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    }
}

int main(int argc, char** argv)
{
    // Initialize plog to console (verbose). This ensures PLOG* calls produce terminal output.
    static plog::ConsoleAppender<plog::TxtFormatter> consoleAppender(plog::streamStdErr);
    plog::init(plog::verbose, &consoleAppender);
    PLOGI << "plog initialized (verbose -> stderr)";

    try{
        if(!OpenCLContext::get().init()){
            PLOGE << "Failed to initialize OpenCL context!";
            return 1;
        }
    } catch(const std::exception &ex){
        PLOGE << "Failed to initialize OpenCL: " << ex.what();
        return 1;
    }

    // Setup window
    glfwSetErrorCallback(glfw_error_callback);
    if (!glfwInit())
        return 1;

    // GL 3.3 + core profile
    const char* glsl_version = "#version 330";
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

    GLFWwindow* window = glfwCreateWindow(1280, 720, "LoreBook - The Complete Fantasy Creation System", nullptr, nullptr);
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

    if(initLoreBook_ResourcesEmbeddedVFS(argv[0])){
        PLOGI << "LoreBook embedded resources VFS initialized successfully.";
    } else {
        PLOGE << "Failed to initialize LoreBook embedded resources VFS!";
        return 1;
    }

    if(!mountLoreBook_ResourcesEmbeddedVFS()){
        PLOGE << "Failed to mount LoreBook embedded resources VFS!";
        return 1;
    }


    std::vector<std::string> embeddedFiles = listLoreBook_ResourcesEmbeddedFiles("/", true);
    PLOGI << "Embedded resource files:";
    for(const auto& f : embeddedFiles){
        PLOGI << "  " << f;
    }

    // Initialize font system (load embedded fonts and set default)
    // Initialize the icon system
    if (!InitializeIcons("Icons"))
    {
        PLOGE << "Failed to initialize icons system";
        // Continue anyway, non-fatal error
    }
    else
    {
        PLOGI << "Icons system initialized successfully";
    }

    // Initialize the font system
    if (!InitializeFonts())
    {
        PLOGE << "Failed to initialize font system";
        // Continue anyway, non-fatal error
    }
    else
    {
        PLOGI << "Font system initialized successfully";

        // List available font families
        auto fontFamilies = GetAvailableFontFamilies();
        PLOGI << "Available font families:";
        for (const auto &family : fontFamilies)
        {
            PLOGI << "  - " << family;
        }

        // Set a default font family if available
        if (!fontFamilies.empty())
        {
            SetCurrentFontFamily(fontFamilies[0]);
            PLOGI << "Set default font family to: " << fontFamilies[0];
        }
    }
    if (!SetCurrentFontFamily("Roboto")) {
        PLOGW << "Default font 'Roboto' not found; using current family: " << GetCurrentFontFamily();
    } else {
        PLOGI << "Default font set to 'Roboto'";
    }

    // Build ALL fonts with our new system ONCE at startup
    if (!BuildFonts())
    {
        PLOGE << "Failed to build fonts!";
        // Continue anyway, ImGui has fallbacks
    }

    // set font to roboto
    if (!SetCurrentFontFamily("Roboto"))
    {
        PLOGE << "Failed to set current font family to Roboto";
        // Continue anyway, ImGui has fallbacks
    }
    else
    {
        PLOGI << "Current font family set to Roboto";
    }

    // Vault state
    static bool firstDock = true;
    static bool worldMapOpen = true;
    std::unique_ptr<Vault> vault;

    // Graph view (and chat)
    static bool showGraphWindow = true;
    static bool showChatWindow = true; // chat will dock/tab with the graph
    static bool showResourceExplorer = false; // Resource Explorer dockable window
    static bool showScriptEditor = false; // Script Editor dockable window
    static GraphView graphView;
    // Floor Plan Editor
    static FloorPlanEditor floorPlanEditor;
    // Character Editor
    static CharacterEditor::CharacterEditorUI characterEditor;
    static std::unique_ptr<CharacterEditor::PartLibrary> partLibrary;
    static std::unique_ptr<CharacterEditor::CharacterManager> characterManager;
    static sqlite3* lastConnectedVaultDb = nullptr;  // Track which vault we connected managers to
    // Create Vault modal state
    static bool showCreateVaultModal = false;
    static char createVaultDirBuf[1024];
    static char createVaultNameBuf[256] = "example_vault.db";
    static char createVaultError[512] = "";
    // Open Vault modal state
    static bool showOpenVaultModal = false;
    static bool showOpenRemoteVaultModal = false;
    static char openVaultDirBuf[1024];
    static char openVaultNameBuf[256] = "";
    static char openVaultError[512] = "";

    // Open Remote Vault modal state
    static char remoteHostBuf[256] = "127.0.0.1";
    static int remotePort = 33060; // default to X Protocol port for mysqlx (33060)
    static char remoteDBBuf[256] = "";
    static char remoteUserBuf[128] = "";
    static char remotePassBuf[128] = "";
    static bool remoteUseSSL = false;
    static char remoteCAFileBuf[1024] = "";
    static char remoteTestStatusBuf[512] = "";
    static bool remoteTestOk = false;

    // Upload / Sync to Remote modal state (UI-owned)
    static bool showSyncModal = false;
    static char sync_remote_host[256] = "";
    static int sync_remote_port = 3306;
    static char sync_remote_db[128] = "";
    static char sync_remote_user[128] = "";
    static char sync_remote_pass[128] = "";
    static bool sync_createRemote = true;
    static bool sync_dryRun = true; // default to safe dry-run
    static bool syncInProgress = false;
    static char syncStatusBuf[512] = ""; // short status for UI

    // Auth/Login state
    static bool showLoginModal = false;
    static bool showCreateAdminModal = false;
    static char loginUserBuf[128] = "";
    static char loginPassBuf[128] = "";
    static char createAdminUserBuf[128] = "";
    static char createAdminDisplayBuf[128] = "";
    static char createAdminPassBuf[128] = "";
    static char createAdminPassConfirmBuf[128] = "";
    static char authErrorBuf[256] = "";

    // Settings modal state (accessible from menu bar)
    static bool showSettingsModal = false;
    static bool settingsModalInit = false;
    static char settings_displayNameBuf[128] = "";
    static char settings_newPassBuf[128] = "";
    static char settings_newPassConfirmBuf[128] = "";
    // Appearance settings
    static char settings_fontFamilyBuf[128] = "";
    static float settings_fontSize = 16.0f;
    // User Management form fields (admin-only)
    static char userMgmt_newUsernameBuf[128] = "";
    static char userMgmt_newDisplayBuf[128] = "";
    static char userMgmt_newPassBuf[128] = "";
    static char userMgmt_newPassConfirmBuf[128] = "";
    static bool userMgmt_newIsAdmin = false;
    static char userMgmt_errorBuf[256] = "";
    static int64_t userMgmt_resetUserID = -1;
    static char userMgmt_resetPassBuf[128] = "";
    static char userMgmt_resetPassConfirmBuf[128] = "";
    static int64_t userMgmt_deleteUserID = -1;
    static char userMgmt_statusMsg[256] = "";

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

    // Auto-load a test model into the Character Editor if present (for development)
    if (std::filesystem::exists("./Ursine.glb")) {
        characterEditor.setOpen(true);
        if (characterEditor.loadModel("./Ursine.glb")) {
            PLOGI << "Auto-loaded test model into Character Editor";
        }
    }


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
            // Dock both Graph and Chat into the right node so they appear as tabs together
            ImGui::DockBuilderDockWindow("Vault Graph", dock_id_right);
            ImGui::DockBuilderDockWindow("Vault Chat", dock_id_right);
            ImGui::DockBuilderDockWindow("World Map", dock_main_id);
            ImGui::DockBuilderDockWindow("Floor Plan Editor", dock_main_id);
            ImGui::DockBuilderDockWindow("Character Editor", dock_main_id);
            ImGui::DockBuilderDockWindow("Script Editor", dock_main_id);
            ImGui::DockBuilderDockWindow("Resource Explorer", dock_main_id);
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
                if (ImGui::MenuItem("Open Remote Vault")) {
                    // Request remote open modal
                    showOpenRemoteVaultModal = true;
                    remoteTestStatusBuf[0] = '\0';
                    remoteTestOk = false;
                }

                // Upload / Sync to Remote (available when a local sqlite vault is open)
                if(vault && vault->getDBBackendPublic() == nullptr){
                    if (ImGui::MenuItem("Upload / Sync to Remote...")) { showSyncModal = true; }
                }
                if (ImGui::MenuItem("Close Vault", nullptr, false, vault != nullptr)){
                    if(vault) vault.reset();
                    showSettingsModal = false;
                }

                if (ImGui::MenuItem("Exit")) glfwSetWindowShouldClose(window, GLFW_TRUE);
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("View")){
                bool canViewVault = (vault != nullptr && vault->getCurrentUserID() > 0);
                if(ImGui::MenuItem("Vault Graph", nullptr, showGraphWindow, canViewVault)){
                    showGraphWindow = !showGraphWindow;
                }
                if(ImGui::MenuItem("Vault Chat", nullptr, showChatWindow, canViewVault)){
                    showChatWindow = !showChatWindow;
                }
                ImGui::Separator();
                if(ImGui::MenuItem("World Map", nullptr, worldMapOpen)){
                    worldMapOpen = !worldMapOpen;
                }
                if(ImGui::MenuItem("Floor Plan Editor", nullptr, floorPlanEditor.isOpen())){
                    floorPlanEditor.toggleOpen();
                }
                ImGui::Separator();
                if(ImGui::MenuItem("Character Editor", nullptr, characterEditor.isOpen())){
                    characterEditor.toggleOpen();
                }
                if (ImGui::MenuItem("Resource Explorer", nullptr, showResourceExplorer, canViewVault)) {
                    showResourceExplorer = !showResourceExplorer;
                }
                if (ImGui::MenuItem("Script Editor", nullptr, showScriptEditor, canViewVault)) {
                    showScriptEditor = !showScriptEditor;
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Settings")){
                bool canOpenSettings = (vault != nullptr && vault->getCurrentUserID() > 0);
                if(ImGui::MenuItem("Settings...", nullptr, false, canOpenSettings)){
                    showSettingsModal = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        // Ensure Settings popup cannot remain open when there's no vault or no logged-in user
        if(ImGui::IsPopupOpen("Settings") && (!vault || vault->getCurrentUserID() <= 0)){
            ImGui::CloseCurrentPopup();
            showSettingsModal = false;
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
                            if(vault){ if(!vault->hasUsers()) showCreateAdminModal = true; else showLoginModal = true; showSettingsModal = false; }
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
                                if(vault){ if(!vault->hasUsers()) showCreateAdminModal = true; else showLoginModal = true; showSettingsModal = false; }
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
                                if(vault){ if(!vault->hasUsers()) showCreateAdminModal = true; else showLoginModal = true; showSettingsModal = false; }
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

        // Open Remote Vault modal (MySQL)
        if (showOpenRemoteVaultModal) {
            ImGui::OpenPopup("Open Remote Vault");
            showOpenRemoteVaultModal = false;
            remoteTestStatusBuf[0] = '\0';
            remoteTestOk = false;
        }
        if (ImGui::BeginPopupModal("Open Remote Vault", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::Text("Connect to a remote MySQL vault");
            ImGui::Separator();
            ImGui::InputText("Host", remoteHostBuf, sizeof(remoteHostBuf));
            ImGui::SameLine(); ImGui::SetNextItemWidth(80); ImGui::InputInt("Port", &remotePort);
            ImGui::InputText("Database", remoteDBBuf, sizeof(remoteDBBuf));
            ImGui::InputText("Username", remoteUserBuf, sizeof(remoteUserBuf));
            ImGui::InputText("Password", remotePassBuf, sizeof(remotePassBuf), ImGuiInputTextFlags_Password);
            ImGui::Checkbox("Use SSL", &remoteUseSSL);
            if(remoteUseSSL){ ImGui::InputText("CA File", remoteCAFileBuf, sizeof(remoteCAFileBuf)); }
            if(remoteTestStatusBuf[0] != '\0'){
                ImGui::Separator();
                ImGui::TextWrapped("%s", remoteTestStatusBuf);
            }
            ImGui::Separator();
            if(ImGui::Button("Test Connection")){
                std::string err;
                remoteTestOk = TestMySQLConnection(std::string(remoteHostBuf), remotePort, std::string(remoteDBBuf), std::string(remoteUserBuf), std::string(remotePassBuf), remoteUseSSL, std::string(remoteCAFileBuf), err);
                if(remoteTestOk){ strncpy(remoteTestStatusBuf, "Connection OK", sizeof(remoteTestStatusBuf)); }
                else {
                    // If the server reports SSL/TLS messages, suggest enabling SSL but do NOT toggle the checkbox automatically
                    std::string low = err;
                    std::transform(low.begin(), low.end(), low.begin(), ::tolower);
                    if((low.find("ssl") != std::string::npos || low.find("tls") != std::string::npos)){
                        std::string msg = std::string(err) + " -- Tip: If your server requires SSL/TLS, enable 'Use SSL' and provide a CA file if necessary, then press Test Connection again.";
                        strncpy(remoteTestStatusBuf, msg.c_str(), sizeof(remoteTestStatusBuf));
                    } else {
                        strncpy(remoteTestStatusBuf, err.c_str(), sizeof(remoteTestStatusBuf));
                    }
                }
            }
            ImGui::SameLine();
            if(ImGui::Button("Open")){
                if(!remoteTestOk){ strncpy(remoteTestStatusBuf, "Please test the connection before opening", sizeof(remoteTestStatusBuf)); }
                else {
                    // Attempt to open a remote MySQL-backed Vault using the provided connection info
                    LoreBook::DBConnectionInfo ci;
                    ci.backend = LoreBook::DBConnectionInfo::Backend::MySQL;
                    ci.mysql_host = std::string(remoteHostBuf);
                    ci.mysql_port = remotePort;
                    ci.mysql_db = std::string(remoteDBBuf);
                    ci.mysql_user = std::string(remoteUserBuf);
                    ci.mysql_password = std::string(remotePassBuf);
                    ci.mysql_use_ssl = remoteUseSSL;
                    ci.mysql_ca_file = std::string(remoteCAFileBuf);
                    VaultConfig cfg; cfg.connInfo = ci; cfg.createIfMissing = true;
                    std::string err;
                    auto v = Vault::Open(cfg, &err);
                    if(!v){ strncpy(remoteTestStatusBuf, err.c_str(), sizeof(remoteTestStatusBuf)); }
                    else {
                        vault = std::move(v);
                        if(vault){ if(!vault->hasUsers()) showCreateAdminModal = true; else showLoginModal = true; showSettingsModal = false; }
                        ImGui::CloseCurrentPopup();
                        showOpenRemoteVaultModal = false;
                    }
                }
            }
            ImGui::SameLine(); if(ImGui::Button("Cancel")){ ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }

        // Upload / Sync to Remote modal
        if(vault && showSyncModal){ ImGui::OpenPopup("Upload / Sync to Remote"); showSyncModal = false; }
        if (ImGui::BeginPopupModal("Upload / Sync to Remote", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::Text("Upload or synchronize local vault to a remote MySQL vault");
            ImGui::Separator();
            ImGui::InputText("Host", sync_remote_host, sizeof(sync_remote_host)); ImGui::SameLine(); ImGui::SetNextItemWidth(80); ImGui::InputInt("Port", &sync_remote_port);
            ImGui::InputText("Database", sync_remote_db, sizeof(sync_remote_db));
            ImGui::InputText("Username", sync_remote_user, sizeof(sync_remote_user));
            ImGui::InputText("Password", sync_remote_pass, sizeof(sync_remote_pass), ImGuiInputTextFlags_Password);
            ImGui::Checkbox("Create remote DB if missing", &sync_createRemote);
            ImGui::Checkbox("Dry run (no writes)", &sync_dryRun);
            ImGui::Separator();
            if(!syncInProgress){
                if(ImGui::Button("Test Connection")){
                    LoreBook::DBConnectionInfo ci;
                    ci.backend = LoreBook::DBConnectionInfo::Backend::MySQL;
                    ci.mysql_host = std::string(sync_remote_host);
                    ci.mysql_port = sync_remote_port;
                    ci.mysql_db = std::string(sync_remote_db);
                    ci.mysql_user = std::string(sync_remote_user);
                    ci.mysql_password = std::string(sync_remote_pass);
                    std::string err;
                    bool ok = TestMySQLConnection(ci.mysql_host, ci.mysql_port, ci.mysql_db, ci.mysql_user, ci.mysql_password, false, std::string(), err);
                    if(ok) strncpy(syncStatusBuf, "Connection OK", sizeof(syncStatusBuf)); else strncpy(syncStatusBuf, (std::string("Connection failed: ") + err).c_str(), sizeof(syncStatusBuf));
                }
                ImGui::SameLine();
                if(ImGui::Button("Start Upload")){
                    // Prepare connection info
                    LoreBook::DBConnectionInfo ci;
                    ci.backend = LoreBook::DBConnectionInfo::Backend::MySQL;
                    ci.mysql_host = std::string(sync_remote_host);
                    ci.mysql_port = sync_remote_port;
                    ci.mysql_db = std::string(sync_remote_db);
                    ci.mysql_user = std::string(sync_remote_user);
                    ci.mysql_password = std::string(sync_remote_pass);
                    ci.mysql_use_ssl = false; // expose later if needed
                    // start worker
                    syncInProgress = true;
                    strncpy(syncStatusBuf, "Starting upload...", sizeof(syncStatusBuf));
                    int64_t uploader = vault->getCurrentUserID();
                    // Launch background worker
                    LoreBook::VaultSync::startUpload(vault.get(), ci, sync_dryRun, uploader, [&syncInProgress, &syncStatusBuf](int pct, const std::string &msg){ strncpy(syncStatusBuf, msg.c_str(), sizeof(syncStatusBuf)); if(pct >= 100 || pct < 0) syncInProgress = false; });
                }
                ImGui::SameLine(); if(ImGui::Button("Cancel")){ ImGui::CloseCurrentPopup(); }
            } else {
                ImGui::Text("Upload in progress...");
                ImGui::TextWrapped("%s", syncStatusBuf);
                if(ImGui::Button("Cancel (not implemented)")) { strncpy(syncStatusBuf, "Cancellation requested (not implemented)", sizeof(syncStatusBuf)); }
            }

            ImGui::Separator();
            if(syncStatusBuf[0] != '\0') ImGui::TextWrapped("Status: %s", syncStatusBuf);
            if(ImGui::Button("Close")){ ImGui::CloseCurrentPopup(); }
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
        CenterNextPopupOnMainViewport();
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
                        // Single-click accepts and closes, to match file selection behavior
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
                    if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && ImGui::IsItemHovered()){
                        // Fallback: double-click also accepts the directory
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
        CenterNextPopupOnMainViewport();
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
                                    if(vault){ if(!vault->hasUsers()) showCreateAdminModal = true; else showLoginModal = true; showSettingsModal = false; }
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
                                    if(vault){ if(!vault->hasUsers()) showCreateAdminModal = true; else showLoginModal = true; showSettingsModal = false; }
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

        // Auth modals: Create Admin (required when no users) and Login (block until authenticated)
        if (showCreateAdminModal && vault) { ImGui::OpenPopup("Create Admin"); showCreateAdminModal = false; }
        if (ImGui::BeginPopupModal("Create Admin", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::Text("Create the vault administrator (required)");
            ImGui::Separator();
            ImGui::InputText("Username", createAdminUserBuf, sizeof(createAdminUserBuf));
            ImGui::InputText("Display Name", createAdminDisplayBuf, sizeof(createAdminDisplayBuf));
            ImGui::InputText("Password", createAdminPassBuf, sizeof(createAdminPassBuf), ImGuiInputTextFlags_Password);
            ImGui::InputText("Confirm Password", createAdminPassConfirmBuf, sizeof(createAdminPassConfirmBuf), ImGuiInputTextFlags_Password);
            if(authErrorBuf[0] != '\0') ImGui::TextColored(ImVec4(1,0.4f,0.4f,1.0f), "%s", authErrorBuf);
            if(ImGui::Button("Create Admin")){
                if(!vault){ strncpy(authErrorBuf, "No vault", sizeof(authErrorBuf)); }
                else {
                    std::string u(createAdminUserBuf); std::string d(createAdminDisplayBuf); std::string p(createAdminPassBuf); std::string q(createAdminPassConfirmBuf);
                    if(u.empty() || p.empty()) strncpy(authErrorBuf, "Username and password required", sizeof(authErrorBuf));
                    else if(p != q) strncpy(authErrorBuf, "Passwords do not match", sizeof(authErrorBuf));
                    else {
                        int64_t uid = vault->createUser(u, d, p, true);
                        if(uid <= 0) strncpy(authErrorBuf, "Failed to create user (username may exist)", sizeof(authErrorBuf));
                        else { vault->setCurrentUser(uid); ImGui::CloseCurrentPopup(); authErrorBuf[0] = '\0'; }
                    }
                }
            }
            ImGui::EndPopup();
        }

        if (showLoginModal && vault) { ImGui::OpenPopup("Login"); showLoginModal = false; authErrorBuf[0] = '\0'; }
        CenterNextPopupOnMainViewport();
        if (ImGui::BeginPopupModal("Login", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::Text("Please login to open the vault");
            ImGui::Separator();
            bool usernameEnterPressed = ImGui::InputText("Username", loginUserBuf, sizeof(loginUserBuf), ImGuiInputTextFlags_EnterReturnsTrue);
            if(usernameEnterPressed){
                ImGui::SetKeyboardFocusHere(); // Move focus to next field (password)
            }
            bool passwordEnterPressed = ImGui::InputText("Password", loginPassBuf, sizeof(loginPassBuf), ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
            if(authErrorBuf[0] != '\0') ImGui::TextColored(ImVec4(1,0.4f,0.4f,1.0f), "%s", authErrorBuf);
            if(ImGui::Button("Login") || passwordEnterPressed){
                if(!vault){ strncpy(authErrorBuf, "No vault", sizeof(authErrorBuf)); }
                else {
                    std::string u(loginUserBuf); std::string p(loginPassBuf);
                    int64_t uid = vault->authenticateUser(u, p);
                    if(uid <= 0) strncpy(authErrorBuf, "Invalid username or password", sizeof(authErrorBuf));
                    else { vault->setCurrentUser(uid); ImGui::CloseCurrentPopup(); authErrorBuf[0] = '\0'; }
                }
            }
            ImGui::SameLine();
            if(ImGui::Button("Cancel")){
                // Cancel opening the vault
                vault.reset();
                ImGui::CloseCurrentPopup();
                authErrorBuf[0] = '\0';
            }
            ImGui::EndPopup();
        }

        // Settings modal
        if(showSettingsModal && vault){
            ImGui::OpenPopup("Settings"); showSettingsModal = false; settingsModalInit = true;
            // initialize buffers
            int64_t cu = vault->getCurrentUserID();
            if(cu > 0){ strncpy(settings_displayNameBuf, vault->getCurrentUserDisplayName().c_str(), sizeof(settings_displayNameBuf)); settings_displayNameBuf[sizeof(settings_displayNameBuf)-1] = '\0'; }
            settings_newPassBuf[0] = settings_newPassConfirmBuf[0] = '\0';
            userMgmt_errorBuf[0] = userMgmt_statusMsg[0] = '\0';
            userMgmt_newUsernameBuf[0] = userMgmt_newDisplayBuf[0] = userMgmt_newPassBuf[0] = userMgmt_newPassConfirmBuf[0] = '\0';
            userMgmt_newIsAdmin = false; userMgmt_resetUserID = -1; userMgmt_deleteUserID = -1;
            // initialize appearance settings
            if (settings_fontFamilyBuf[0] == '\0') {
                std::string curFamily = GetCurrentFontFamily();
                if (!curFamily.empty()) strncpy(settings_fontFamilyBuf, curFamily.c_str(), sizeof(settings_fontFamilyBuf));
                settings_fontSize = GetDefaultFontSize();
            }
        }
        if(settingsModalInit){
            ImGui::SetNextWindowSize(ImVec2(700,520), ImGuiCond_FirstUseEver);
            settingsModalInit = false;
        }
        if(ImGui::BeginPopupModal("Settings", nullptr, 0)){
            if(ImGui::BeginTabBar("SettingsTabs")){
                if(ImGui::BeginTabItem("User Settings")){
                    int64_t cu = vault->getCurrentUserID();
                    if(cu <= 0){ ImGui::Text("Not logged in"); }
                    else {
                        ImGui::Text("Username: %s", vault->listUsers().size() == 0 ? "" : "");
                        ImGui::TextDisabled("(Username cannot be changed)");
                        ImGui::InputText("Display Name", settings_displayNameBuf, sizeof(settings_displayNameBuf));
                        if(ImGui::Button("Save")){
                            if(vault->updateUserDisplayName(cu, std::string(settings_displayNameBuf))){ strncpy(userMgmt_statusMsg, "Display name saved", sizeof(userMgmt_statusMsg)); }
                            else { strncpy(userMgmt_statusMsg, "Failed to save display name", sizeof(userMgmt_statusMsg)); }
                        }
                        ImGui::Separator();
                        ImGui::InputText("New Password", settings_newPassBuf, sizeof(settings_newPassBuf), ImGuiInputTextFlags_Password);
                        ImGui::InputText("Confirm Password", settings_newPassConfirmBuf, sizeof(settings_newPassConfirmBuf), ImGuiInputTextFlags_Password);
                        if(ImGui::Button("Change Password")){
                            std::string a(settings_newPassBuf); std::string b(settings_newPassConfirmBuf);
                            if(a.empty()) strncpy(userMgmt_errorBuf, "Password cannot be empty", sizeof(userMgmt_errorBuf));
                            else if(a != b) strncpy(userMgmt_errorBuf, "Passwords do not match", sizeof(userMgmt_errorBuf));
                            else {
                                if(vault->changeUserPassword(cu, a)){ strncpy(userMgmt_statusMsg, "Password changed", sizeof(userMgmt_statusMsg)); settings_newPassBuf[0]=settings_newPassConfirmBuf[0]='\0'; userMgmt_errorBuf[0] = '\0'; }
                                else strncpy(userMgmt_errorBuf, "Failed to change password", sizeof(userMgmt_errorBuf));
                            }
                        }
                        if(userMgmt_errorBuf[0] != '\0') ImGui::TextColored(ImVec4(1,0.4f,0.4f,1.0f), "%s", userMgmt_errorBuf);
                    }
                    ImGui::EndTabItem();
                }

                // Appearance tab
                if(ImGui::BeginTabItem("Appearance")){
                    ImGui::Text("Font Family");
                    auto families = GetAvailableFontFamilies();
                    if (ImGui::BeginCombo("Font Family", settings_fontFamilyBuf[0] ? settings_fontFamilyBuf : "Select...")){
                        for (const auto &f : families){
                            bool sel = (strcmp(settings_fontFamilyBuf, f.c_str()) == 0);
                            if (ImGui::Selectable(f.c_str(), sel)){
                                strncpy(settings_fontFamilyBuf, f.c_str(), sizeof(settings_fontFamilyBuf));
                            }
                            if (sel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                    ImGui::SliderFloat("Font Size", &settings_fontSize, 10.0f, 36.0f, "%.0f");
                    ImGui::SameLine();
                    if (ImGui::Button("Apply")){
                        if (!SetCurrentFontFamily(std::string(settings_fontFamilyBuf))){
                            PLOGW << "Failed to set font family: " << settings_fontFamilyBuf;
                        }
                        SetFontSizeImmediate(settings_fontSize);
                    }
                    ImGui::EndTabItem();
                }

                // User Management tab (admin only)
                if(vault->isUserAdmin(vault->getCurrentUserID()) && ImGui::BeginTabItem("User Management")){
                    // Admin Merge Conflicts menu
                    static bool showMergeConflicts = false;
                    if(ImGui::Button("Merge Conflicts")) showMergeConflicts = true;
                    if(showMergeConflicts){ LoreBook::RenderMergeConflictModal(vault.get(), &showMergeConflicts); }
                    ImGui::Text("Create new user");
                    ImGui::InputText("Username", userMgmt_newUsernameBuf, sizeof(userMgmt_newUsernameBuf));
                    ImGui::InputText("Display Name", userMgmt_newDisplayBuf, sizeof(userMgmt_newDisplayBuf));
                    ImGui::InputText("Password", userMgmt_newPassBuf, sizeof(userMgmt_newPassBuf), ImGuiInputTextFlags_Password);
                    ImGui::InputText("Confirm Password", userMgmt_newPassConfirmBuf, sizeof(userMgmt_newPassConfirmBuf), ImGuiInputTextFlags_Password);
                    ImGui::Checkbox("Admin", &userMgmt_newIsAdmin);
                    if(ImGui::Button("Create User")){
                        std::string u(userMgmt_newUsernameBuf); std::string d(userMgmt_newDisplayBuf); std::string p(userMgmt_newPassBuf); std::string q(userMgmt_newPassConfirmBuf);
                        if(u.empty() || p.empty()) strncpy(userMgmt_errorBuf, "Username and password required", sizeof(userMgmt_errorBuf));
                        else if(p != q) strncpy(userMgmt_errorBuf, "Passwords do not match", sizeof(userMgmt_errorBuf));
                        else {
                            int64_t uid = vault->createUser(u,d,p,userMgmt_newIsAdmin);
                            if(uid <= 0) strncpy(userMgmt_errorBuf, "Failed to create user (username may exist)", sizeof(userMgmt_errorBuf));
                            else { strncpy(userMgmt_statusMsg, "User created", sizeof(userMgmt_statusMsg)); userMgmt_newUsernameBuf[0]=userMgmt_newDisplayBuf[0]=userMgmt_newPassBuf[0]=userMgmt_newPassConfirmBuf[0]='\0'; userMgmt_newIsAdmin=false; userMgmt_errorBuf[0]='\0'; }
                        }
                    }
                    if(userMgmt_errorBuf[0] != '\0') ImGui::TextColored(ImVec4(1,0.4f,0.4f,1.0f), "%s", userMgmt_errorBuf);
                    ImGui::Separator();
                    ImGui::Text("Existing users:");
                    ImGui::BeginChild("UserList", ImVec2(0,200), true);
                    auto users = vault->listUsers();
                    if(ImGui::BeginTable("UserTable", 4, ImGuiTableFlags_Resizable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Sortable)){
                        ImGui::TableSetupColumn("Username", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_WidthFixed, 120.0f);
                        ImGui::TableSetupColumn("Display", ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("Admin", ImGuiTableColumnFlags_WidthFixed, 80.0f);
                        ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 200.0f);
                        ImGui::TableHeadersRow();

                        // Apply sorting if requested
                        ImGuiTableSortSpecs* sorts_specs = ImGui::TableGetSortSpecs();
                        if(sorts_specs && sorts_specs->SpecsDirty){
                            if(!users.empty()){
                                auto spec = sorts_specs->Specs[0];
                                int column = spec.ColumnIndex;
                                bool asc = (spec.SortDirection == ImGuiSortDirection_Ascending);
                                std::stable_sort(users.begin(), users.end(), [column, asc](const auto &a, const auto &b){
                                    switch(column){
                                        case 0: return asc ? (a.username < b.username) : (a.username > b.username);
                                        case 1: return asc ? (a.displayName < b.displayName) : (a.displayName > b.displayName);
                                        case 2: return asc ? (a.isAdmin < b.isAdmin) : (a.isAdmin > b.isAdmin);
                                        default: return asc ? (a.username < b.username) : (a.username > b.username);
                                    }
                                });
                            }
                            sorts_specs->SpecsDirty = false;
                        }

                        for(auto &u : users){
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(u.username.c_str());

                            ImGui::TableSetColumnIndex(1);
                            ImGui::PushID((int)u.id);
                            char dBuf[128]; strncpy(dBuf, u.displayName.c_str(), sizeof(dBuf)); dBuf[sizeof(dBuf)-1]='\0';
                            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
                            if(ImGui::InputText("display", dBuf, sizeof(dBuf))){ vault->updateUserDisplayName(u.id, std::string(dBuf)); strncpy(userMgmt_statusMsg, "Display updated", sizeof(userMgmt_statusMsg)); }

                            ImGui::TableSetColumnIndex(2);
                            bool isAdmin = u.isAdmin;
                            if(ImGui::Checkbox("admin", &isAdmin)){
                                if(!vault->setUserAdminFlag(u.id, isAdmin)) strncpy(userMgmt_errorBuf, "Failed to set admin flag (cannot remove last admin or change your own admin)", sizeof(userMgmt_errorBuf));
                                else strncpy(userMgmt_statusMsg, "Admin flag updated", sizeof(userMgmt_statusMsg));
                            }

                            ImGui::TableSetColumnIndex(3);
                            if(ImGui::Button("Reset Password")){
                                userMgmt_resetUserID = u.id; userMgmt_resetPassBuf[0]=userMgmt_resetPassConfirmBuf[0]='\0'; ImGui::OpenPopup("Reset Password");
                            }
                            ImGui::SameLine();
                            bool canDelete = (vault->getCurrentUserID() != u.id);
                            if(ImGui::Button("Delete")){
                                if(!canDelete) strncpy(userMgmt_errorBuf, "Cannot delete the currently logged-in user", sizeof(userMgmt_errorBuf));
                                else { userMgmt_deleteUserID = u.id; ImGui::OpenPopup("Delete User"); }
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndChild();
                    ImGui::EndTabItem();
                }
                ImGui::EndTabBar();
            }

            if(userMgmt_statusMsg[0] != '\0') ImGui::TextColored(ImVec4(0.4f,1,0.4f,1.0f), "%s", userMgmt_statusMsg);
            ImGui::Separator();

            // Reset Password modal (admin triggers)
            if(ImGui::BeginPopupModal("Reset Password", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
                ImGui::Text("Set a new password for this user");
                ImGui::Separator();
                ImGui::InputText("New Password", userMgmt_resetPassBuf, sizeof(userMgmt_resetPassBuf), ImGuiInputTextFlags_Password);
                ImGui::InputText("Confirm", userMgmt_resetPassConfirmBuf, sizeof(userMgmt_resetPassConfirmBuf), ImGuiInputTextFlags_Password);
                if(ImGui::Button("Set")){
                    std::string a(userMgmt_resetPassBuf); std::string b(userMgmt_resetPassConfirmBuf);
                    if(a.empty()) strncpy(userMgmt_errorBuf, "Password cannot be empty", sizeof(userMgmt_errorBuf));
                    else if(a != b) strncpy(userMgmt_errorBuf, "Passwords do not match", sizeof(userMgmt_errorBuf));
                    else {
                        if(userMgmt_resetUserID > 0 && vault->changeUserPassword(userMgmt_resetUserID, a)){
                            strncpy(userMgmt_statusMsg, "Password reset", sizeof(userMgmt_statusMsg)); userMgmt_resetUserID = -1; userMgmt_resetPassBuf[0]=userMgmt_resetPassConfirmBuf[0]='\0'; userMgmt_errorBuf[0] = '\0'; ImGui::CloseCurrentPopup();
                        } else strncpy(userMgmt_errorBuf, "Failed to reset password", sizeof(userMgmt_errorBuf));
                    }
                }
                ImGui::SameLine(); if(ImGui::Button("Cancel")){ userMgmt_resetUserID = -1; ImGui::CloseCurrentPopup(); }
                if(userMgmt_errorBuf[0] != '\0') ImGui::TextColored(ImVec4(1,0.4f,0.4f,1.0f), "%s", userMgmt_errorBuf);
                ImGui::EndPopup();
            }

            // Delete User confirmation
            if(ImGui::BeginPopupModal("Delete User", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
                ImGui::Text("Delete this user? This will remove the user and their permissions."); ImGui::Separator();
                if(ImGui::Button("Delete")){
                    if(userMgmt_deleteUserID > 0){ if(vault->deleteUser(userMgmt_deleteUserID)){ strncpy(userMgmt_statusMsg, "User deleted", sizeof(userMgmt_statusMsg)); } else { strncpy(userMgmt_errorBuf, "Failed to delete user (cannot delete last admin or yourself)", sizeof(userMgmt_errorBuf)); } }
                    userMgmt_deleteUserID = -1; ImGui::CloseCurrentPopup();
                }
                ImGui::SameLine(); if(ImGui::Button("Cancel")){ userMgmt_deleteUserID = -1; ImGui::CloseCurrentPopup(); }
                if(userMgmt_errorBuf[0] != '\0') ImGui::TextColored(ImVec4(1,0.4f,0.4f,1.0f), "%s", userMgmt_errorBuf);
                ImGui::EndPopup();
            }

            if(ImGui::Button("Close")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::End(); // DockSpaceHost

        // Render Vault windows only when a user is logged in
        if(vault){
            if(vault->getCurrentUserID() > 0){
                vault->drawVaultTree();
                vault->drawVaultContent();
                graphView.setVault(vault.get());
            } else {
                // Do not render any vault UI while logged out; disconnect GraphView to avoid leaks
                graphView.setVault(nullptr);
            }
        } else {
            graphView.setVault(nullptr);
        }

        // Graph view and Chat (dockable)
        if(showGraphWindow && vault && vault->getCurrentUserID() > 0){
            // pass dt as ImGui frame delta
            graphView.updateAndDraw(ImGui::GetIO().DeltaTime);
        }

        // Render Vault Chat as its own window (docked with the graph by default)
        if(showChatWindow && vault && vault->getCurrentUserID() > 0){
            ImGui::Begin("Vault Chat");
            RenderVaultChat(vault.get());
            ImGui::End();
        }

        // If there's a pending request (from a preview attempt), open the Resource Explorer
        if (vault && HasPendingOpenResourceExplorer()) {
            showResourceExplorer = true;
        }
        // If there's a pending request to open a script, show the Script Editor
        if (vault && HasPendingOpenScriptEditor()) {
            showScriptEditor = true;
        }

        // Resource Explorer window
        if (showResourceExplorer && vault && vault->getCurrentUserID() > 0) {
            RenderResourceExplorer(vault.get(), &showResourceExplorer);
        }
        // Script Editor window
        if (showScriptEditor && vault && vault->getCurrentUserID() > 0) {
            RenderScriptEditor(vault.get(), &showScriptEditor);
        }

        if(worldMapOpen){
            worldMap(worldMapOpen);
        }

        // Floor Plan Editor
        floorPlanEditor.render();

        // Character Editor - connect managers when vault is available and logged in
        if (vault && vault->isOpen() && vault->getCurrentUserID() > 0) {
            sqlite3* currentDb = vault->getDBPublic();
            if (currentDb && currentDb != lastConnectedVaultDb) {
                // Vault changed or newly connected, initialize managers
                partLibrary = std::make_unique<CharacterEditor::PartLibrary>();
                characterManager = std::make_unique<CharacterEditor::CharacterManager>();
                if (partLibrary->initialize(currentDb)) {
                    characterEditor.setPartLibrary(partLibrary.get());
                    PLOGI << "Connected PartLibrary to CharacterEditorUI";
                }
                if (characterManager->initialize(currentDb, partLibrary.get())) {
                    characterEditor.setCharacterManager(characterManager.get());
                    PLOGI << "Connected CharacterManager to CharacterEditorUI";
                }
                lastConnectedVaultDb = currentDb;
            }
        } else if (!vault || !vault->isOpen()) {
            // Vault closed, disconnect managers
            if (lastConnectedVaultDb) {
                characterEditor.setPartLibrary(nullptr);
                characterEditor.setCharacterManager(nullptr);
                partLibrary.reset();
                characterManager.reset();
                lastConnectedVaultDb = nullptr;
            }
        }
        characterEditor.render();

        // Rendering
        ImGui::Render();
        // Process pending font changes/rebuilds (apply delayed font-size changes or rebuilds)
        ProcessPendingFontRebuild();
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

        FrameMark;
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