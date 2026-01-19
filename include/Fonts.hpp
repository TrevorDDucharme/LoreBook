#pragma once

#include <imgui.h>
#include <filesystem>
#include <string>
#include <vector>
#include <unordered_map>
#include <memory>
#include <LoreBook_Resources/LoreBook_ResourcesEmbeddedVFS.hpp>
#include <fstream>
//define for imwchar32
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
// Enable FreeType integration
#include <imgui_freetype.h>
#include <Icons.hpp>
#include <GLFW/glfw3.h>

// Font style flags
enum class FontStyle {
    Regular = 0,
    Bold = 1 << 0,
    Italic = 1 << 1,
    Light = 1 << 2,
    Medium = 1 << 3,
    Black = 1 << 4,
    Thin = 1 << 5,
    // Bold Italic would be Bold | Italic (value 3)
};

// Allow combining font styles with bitwise OR
inline FontStyle operator|(FontStyle a, FontStyle b) {
    return static_cast<FontStyle>(static_cast<int>(a) | static_cast<int>(b));
}

inline bool HasStyle(FontStyle base, FontStyle style) {
    return (static_cast<int>(base) & static_cast<int>(style)) == static_cast<int>(style);
}

// Structure to hold data for a single font style
struct FontData {
    std::string path;          // Full path to the font file
    FontStyle style;           // Style of this font
    ImFont* imFont = nullptr;  // ImGui font pointer (after loading)
    float size = 16.0f;        // Font size
    
    FontData(const std::string& p, FontStyle s, float sz = 16.0f)
        : path(p), style(s), size(sz) {}
};

// Structure to hold data for a font family (collection of styles)
struct FontFamily {
    std::string name;                               // Font family name (e.g., "Roboto")
    std::vector<FontData> styles;                   // Available styles for this font
    std::unordered_map<int, ImFont*> loadedFonts;   // Loaded fonts by style int value
    
    // Add a default constructor
    FontFamily() : name("") {}
    
    FontFamily(const std::string& n) : name(n) {}
    
    // Add a new font style
    void addStyle(const std::string& path, FontStyle style, float size = 16.0f) {
        styles.emplace_back(path, style, size);
    }
    
    // Get ImFont for a specific style
    ImFont* getFont(FontStyle style = FontStyle::Regular) {
        int styleVal = static_cast<int>(style);
        auto it = loadedFonts.find(styleVal);
        return (it != loadedFonts.end()) ? it->second : nullptr;
    }
};

// Font manager class to handle font loading and selection
class FontManager {
private:
    static FontManager* instance;
    
    std::unordered_map<std::string, FontFamily> fontFamilies;
    std::string currentFontFamily;
    float defaultFontSize = 16.0f;
    bool rebuildPending = false;  // Flag for deferred rebuilding
    float pendingFontSize = 0.0f; // Store pending font size changes
    bool fontSizeChanged = false; // Track if the font size was changed
    double lastChangedTime = 0.0; // Time of last font size change
    const double REBUILD_DELAY = 0.1; // Delay in seconds before rebuilding
    
    // Font scaling parameters
    const float BASE_FONT_SIZE = 32.0f; // The size at which fonts will be loaded
    float fontScale = 0.5f;            // Current scale factor (defaultFontSize / BASE_FONT_SIZE)
    
    // Private constructor for singleton
    FontManager() {}
    
public:
    static FontManager* getInstance() {
        if (!instance) {
            instance = new FontManager();
        }
        return instance;
    }

    float getBaseFontSize() const {
        return BASE_FONT_SIZE;
    }
    
    // Scan fonts directory and initialize the font system
    bool initialize();
    
    // Load a font family with all its styles
    bool loadFontFamily(const std::string& familyName, float fontSize = 16.0f);
    
    // Set the current font family (without rebuilding)
    bool setCurrentFontFamily(const std::string& familyName);
    
    // Get the current font family name
    std::string getCurrentFontFamily() const;
    
    // Check if a rebuild is pending
    bool isRebuildPending() const { return rebuildPending; }
    
    // Request a rebuild (will be processed after rendering)
    void requestRebuild() { rebuildPending = true; }
    
    // Clear rebuild flag after processing
    void clearRebuildFlag() { rebuildPending = false; }
    
    // Build all fonts with FreeType and icon glyphs
    bool buildFonts();
    
    // Process any pending font rebuilds (call after Render)
    bool processPendingRebuild();
    
    // Get ImFont for a specific style in the current font family
    ImFont* getFont(FontStyle style = FontStyle::Regular);
    
    // Get ImFont by family name and style
    ImFont* getFontByFamily(const std::string& familyName, FontStyle style = FontStyle::Regular);
    
    // Get all available font family names
    std::vector<std::string> getAvailableFontFamilies() const;
    
    // Set default font size (now without rebuilding)
    void setDefaultFontSize(float size);
    
    // Apply font scale to all loaded fonts
    void applyFontScale();
    
    // Apply pending font size changes 
    bool applyPendingFontSizeChange();
    
    // Get default font size
    float getDefaultFontSize() const;
    
    // Get the size that's currently displayed (either pending or current)
    float getDisplayFontSize() const {
        return fontSizeChanged ? pendingFontSize : defaultFontSize;
    }
    
    // Get the current font scale factor
    float getFontScale() const {
        return fontScale;
    }
    
    // Check if a font size change is pending
    bool isFontSizeChangePending() const {
        return fontSizeChanged;
    }
    
    // Cleanup font resources
    void cleanup();
    
    // Helper to get font path
    std::string getFontDirectory() const;
    
    // Helper function to detect font style from filename
    static FontStyle detectFontStyleFromFilename(const std::string& filename);
    
    // Scan a font family directory for font files
    void scanFontFamily(const std::string& familyDir);
    
    // Set font size immediately (without debouncing)
    void setFontSizeImmediate(float size);
};

// Global functions for easier access
bool InitializeFonts();
bool LoadFontFamily(const std::string& familyName, float fontSize = 16.0f);
bool SetCurrentFontFamily(const std::string& familyName);
std::string GetCurrentFontFamily();
ImFont* GetFont(FontStyle style = FontStyle::Regular);
ImFont* GetFontByFamily(const std::string& familyName, FontStyle style = FontStyle::Regular);
std::vector<std::string> GetAvailableFontFamilies();
void SetDefaultFontSize(float size);
float GetDefaultFontSize();
float GetFontScale();
bool BuildFonts();
void CleanupFonts();

// Global function to request a font rebuild (to be processed after rendering)
void RequestFontRebuild();

// Process any pending font rebuilds (call after rendering)
bool ProcessPendingFontRebuild();

// Set font size immediately (without debouncing)
void SetFontSizeImmediate(float size);

// Make sure you call this after any font size change to apply scaling
void ApplyFontScale();

// Global getter for BASE_FONT_SIZE
float GetBaseFontSize();