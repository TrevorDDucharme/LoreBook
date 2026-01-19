#pragma once

#include <imgui.h>
#include <vector>
#include <string>
#include <unordered_map>
#include <filesystem>
#include <GL/glew.h>
#include <LoreBook_Resources/LoreBook_ResourcesEmbeddedVFS.hpp>

#define FORCE_ICON_UNPACK true // Set to true to force unpacking icons from embedded resources

// Icon structure for storing icon data
struct IconData {
    ImWchar codepoint;
    std::string filename;
    unsigned char* data;
    int width, height;
    int rect_id;

    // Constructor for empty initialization
    IconData() : codepoint(0), filename(""), data(nullptr), width(0), height(0), rect_id(-1) {}
    
    // Constructor with initialization
    IconData(ImWchar cp, const std::string& fn) : codepoint(cp), filename(fn), data(nullptr), width(0), height(0), rect_id(-1) {}
};

// Initialize the icon system by scanning icon directory and assigning codepoints
bool InitializeIcons(const std::string& iconDirectoryPath);

// Get all icons data as a vector
std::vector<IconData> GetIconsData();

// Add all icon codepoints to a glyph builder
void AddIconsToGlyphBuilder(ImFontGlyphRangesBuilder& builder);

// Get unicode codepoint for an icon by its filename (without path or extension)
// E.g., For "icons/gear.png", use GetIconCodepoint("gear")
ImWchar GetIconCodepoint(const std::string& iconName);

// Get full list of available icon names (filename without path/extension)
std::vector<std::string> GetAvailableIconNames();

// Get the full path for an icon by its name
std::string GetIconFilePath(const std::string& iconName);

// Helper function to easily insert an icon in ImGui text
// Usage: IconTag("gear") will insert the gear icon
inline std::string IconTag(const std::string& iconName) {
    static char buffer[8] = {0}; // UTF-8 can be up to 4 bytes per char + null terminator
    ImWchar codepoint = GetIconCodepoint(iconName);
    if (codepoint) {
        char* p = buffer;
        // Convert Unicode codepoint to UTF-8
        if (codepoint < 0x80) {
            *p++ = (char)codepoint;
        }
        else if (codepoint < 0x800) {
            *p++ = 0xC0 | (codepoint >> 6);
            *p++ = 0x80 | (codepoint & 0x3F);
        }
        else if (codepoint < 0x10000) {
            *p++ = 0xE0 | (codepoint >> 12);
            *p++ = 0x80 | ((codepoint >> 6) & 0x3F);
            *p++ = 0x80 | (codepoint & 0x3F);
        }
        else {
            *p++ = 0xF0 | (codepoint >> 18);
            *p++ = 0x80 | ((codepoint >> 12) & 0x3F);
            *p++ = 0x80 | ((codepoint >> 6) & 0x3F);
            *p++ = 0x80 | (codepoint & 0x3F);
        }
        *p = 0;
        return buffer;
    }
    return "?"; // Fallback character if icon not found
}

// Structure to store loaded icon textures
struct IconTexture {
    GLuint textureID;
    int width;
    int height;
    bool loaded;
    
    IconTexture() : textureID(0), width(0), height(0), loaded(false) {}
};

// Load an icon texture from file and return its OpenGL texture ID
// The texture is cached for subsequent calls
IconTexture LoadIconTexture(const std::string& iconName);

// Get the dimensions of an icon
bool GetIconDimensions(const std::string& iconName, int& width, int& height);

// Draw an icon as an ImGui::Image using the original image file (not the font atlas)
// Usage: DrawIcon("gear", ImVec2(32, 32)) will draw the gear icon at 32x32 size
// Returns true if icon was drawn successfully, false otherwise
bool DrawIcon(const std::string& iconName, const ImVec2& size, const ImVec4& tint_col = ImVec4(1,1,1,1), 
             const ImVec4& border_col = ImVec4(0,0,0,0));

// Draw an icon as an ImGui::ImageButton using the original image file
// Returns true if button was clicked and icon was drawn successfully, false otherwise
bool DrawIconButton(const std::string& iconName, const ImVec2& size, const ImVec4& tint_col = ImVec4(1,1,1,1),
                   const ImVec4& bg_col = ImVec4(0,0,0,0), const ImVec4& border_col = ImVec4(0,0,0,0));

// Blit an icon to the current draw list at given position using the original image file
// More flexible than DrawIcon as it allows precise positioning in screen coordinates
bool BlitIcon(const std::string& iconName, const ImVec2& pos, const ImVec2& size, 
             const ImVec4& tint_col = ImVec4(1,1,1,1), float rounding = 0.0f, 
             ImDrawFlags flags = 0);


