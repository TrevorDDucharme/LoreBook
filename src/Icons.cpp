#include <Icons.hpp>
#include <imgui.h>
#include <filesystem>
#include <algorithm>
#include <plog/Log.h>
#include <set>
#include <string>
#include <unordered_map>
#include <stb_image.h>

// Internal structures and static storage
namespace {
    // Map to store all loaded icons (by filename without path/extension)
    std::unordered_map<std::string, IconData> s_icons;
    
    // Map to cache loaded textures
    std::unordered_map<std::string, IconTexture> s_iconTextures;
    
    // Base directory for icons
    std::string s_iconDirectoryPath;
    
    // Starting codepoint for assigned icons (private use area in Unicode)
    const ImWchar ICON_START_CODEPOINT = 0xE000; // Start of Unicode Private Use Area
    
    // Set of used codepoints to avoid duplicates
    std::set<ImWchar> s_usedCodepoints;
    
    // Flag to track if icons have been initialized
    bool s_initialized = false;
    
    // Extract icon name from filepath (remove path and extension)
    std::string ExtractIconName(const std::string& filepath) {
        std::filesystem::path path(filepath);
        return path.stem().string();
    }
    
    // Find an unused codepoint for a new icon
    ImWchar GetNextAvailableCodepoint() {
        ImWchar candidate = ICON_START_CODEPOINT;
        while (s_usedCodepoints.find(candidate) != s_usedCodepoints.end()) {
            candidate++;
        }
        s_usedCodepoints.insert(candidate);
        return candidate;
    }
}

// Initialize the icon system
bool InitializeIcons(const std::string& iconDirectoryPath) {
    // Don't initialize more than once
    if (s_initialized) {
        return true;
    }
    
    // Store the base directory path for later use
    s_iconDirectoryPath = iconDirectoryPath;
    
    // Now scan the directory for PNG files
    try {
        if (!std::filesystem::exists(iconDirectoryPath)) {
            PLOGW << "Icon directory not found: " << iconDirectoryPath;
            std::filesystem::create_directories(iconDirectoryPath);
            PLOGI << "Created icon directory: " << iconDirectoryPath;
        }
        //unpack from embedded resources if needed
        if (!existsLoreBook_ResourcesEmbeddedFile("/Icons")) {
            PLOGE << "No embedded icons found, please add icons to the directory.";
            return false;
        }
        PLOGI << "Automatically extracting icons from embedded resources...";
        
        // Get a list of all embedded icons
        std::vector<std::string> embeddedIcons = listLoreBook_ResourcesEmbeddedFiles("/Icons");
        
        // Extract each icon to the filesystem
        for (const auto& iconName : embeddedIcons) {
            // Skip directories and non-PNG files
            if (iconName.find(".png") == std::string::npos) {
            continue;
            }
            
            // Extract the icon name from the path
            std::string extractPath = iconDirectoryPath + "/" + iconName;
            
            // Skip extraction if file already exists
            if (std::filesystem::exists(extractPath)&&!FORCE_ICON_UNPACK) {
            PLOGI << "Icon already exists, skipping extraction: " << extractPath;
            continue;
            }
            
            // Create the full path for extraction
            std::string iconPath = "/Icons/" + iconName;

            PLOGI << "Extracting " << iconPath << " to " << extractPath;
            
            // Load the embedded icon data
            std::vector<unsigned char> embeddedData = loadLoreBook_ResourcesEmbeddedFile(iconPath.c_str());
            
            // Write the file to disk
            FILE* file = fopen(extractPath.c_str(), "wb");
            if (file) {
            fwrite(embeddedData.data(), 1, embeddedData.size(), file);
            fclose(file);
            PLOGI << "Successfully extracted icon: " << iconName;
            } else {
            PLOGE << "Failed to write icon to filesystem: " << extractPath;
            }
        }
        
        int totalIconsLoaded = 0;
        
        for (const auto& entry : std::filesystem::directory_iterator(iconDirectoryPath)) {
            if (entry.path().extension() == ".png") {
                std::string fullPath = entry.path().string();
                std::string iconName = ExtractIconName(fullPath);
                
                // Assign a unique Unicode codepoint from the Private Use Area
                ImWchar codepoint = GetNextAvailableCodepoint();
                
                // Store the icon data
                s_icons[iconName] = IconData(codepoint, fullPath);
                totalIconsLoaded++;
                
                PLOGI << "Loaded icon: " << iconName << " with codepoint U+" << std::hex << codepoint;
            }
        }
        
        PLOGI << "Automatically loaded " << totalIconsLoaded << " icons";
        s_initialized = true;
        return true;
    }
    catch (const std::exception& ex) {
        PLOGE << "Error loading icons: " << ex.what();
        return false;
    }
}

// Get all icons data as a vector
std::vector<IconData> GetIconsData() {
    std::vector<IconData> icons;
    icons.reserve(s_icons.size());
    
    for (const auto& pair : s_icons) {
        icons.push_back(pair.second);
    }
    
    return icons;
}

// Add all icon codepoints to a glyph builder
void AddIconsToGlyphBuilder(ImFontGlyphRangesBuilder& builder) {
    for (const auto& pair : s_icons) {
        builder.AddChar(pair.second.codepoint);
    }
}

// Get unicode codepoint for an icon by its filename (without path or extension)
ImWchar GetIconCodepoint(const std::string& iconName) {
    auto it = s_icons.find(iconName);
    if (it != s_icons.end()) {
        return it->second.codepoint;
    }
    
    return 0; // Not found
}

// Get full list of available icon names
std::vector<std::string> GetAvailableIconNames() {
    std::vector<std::string> names;
    names.reserve(s_icons.size());
    
    for (const auto& pair : s_icons) {
        names.push_back(pair.first);
    }
    
    return names;
}

// Get the full path for an icon by its name
std::string GetIconFilePath(const std::string& iconName) {
    auto it = s_icons.find(iconName);
    if (it != s_icons.end()) {
        return it->second.filename;
    }
    return ""; // Not found
}

// Get the dimensions of an icon
bool GetIconDimensions(const std::string& iconName, int& width, int& height) {
    IconTexture texture = LoadIconTexture(iconName);
    if (!texture.loaded) {
        return false;
    }
    
    width = texture.width;
    height = texture.height;
    return true;
}

// Load an icon texture from file and return its OpenGL texture ID
IconTexture LoadIconTexture(const std::string& iconName) {
    // Check if we already have this texture loaded
    auto it = s_iconTextures.find(iconName);
    if (it != s_iconTextures.end()) {
        return it->second;
    }
    
    // If not, create a new texture
    IconTexture texture;
    std::string filepath = GetIconFilePath(iconName);
    
    // Flag to track if we're using embedded resources
    bool usingEmbeddedResource = false;
    std::vector<unsigned char> embeddedData;
    
    if (filepath.empty()) {
        PLOGE << "Icon not found in filesystem: " << iconName << ". Checking embedded resources...";
        
        // Try to find it in embedded resources
        std::string embeddedPath = "/Icons/" + iconName + ".png";
        if (existsLoreBook_ResourcesEmbeddedFile(embeddedPath.c_str())) {
            PLOGI << "Found icon in embedded resources: " << embeddedPath;
            embeddedData = loadLoreBook_ResourcesEmbeddedFile(embeddedPath.c_str());
            usingEmbeddedResource = true;
            
            // Also extract the file to the filesystem for future use
            std::string extractPath = s_iconDirectoryPath + "/" + iconName + ".png";
            PLOGI << "Extracting embedded icon to: " << extractPath;
            
            // Create directory if it doesn't exist
            std::filesystem::path dir = std::filesystem::path(s_iconDirectoryPath);
            if (!std::filesystem::exists(dir)) {
                std::filesystem::create_directories(dir);
            }
            
            // Write the file
            FILE* file = fopen(extractPath.c_str(), "wb");
            if (file) {
                fwrite(embeddedData.data(), 1, embeddedData.size(), file);
                fclose(file);
                
                // Update the filepath to use in the future
                filepath = extractPath;
            }
        } else {
            PLOGE << "Icon not found in embedded resources either: " << iconName;
            return texture;
        }
    }
    
    // Load the image data
    int channels;
    unsigned char* image_data = nullptr;
    
    if (usingEmbeddedResource) {
        // Use stbi_load_from_memory for embedded resource data
        image_data = stbi_load_from_memory(
            embeddedData.data(), 
            static_cast<int>(embeddedData.size()),
            &texture.width, &texture.height, &channels, 4
        );
    } else {
        // Use regular stbi_load for filesystem
        image_data = stbi_load(filepath.c_str(), &texture.width, &texture.height, &channels, 4);
    }


    
    if (!image_data) {
        PLOGE << "Failed to load image: " << filepath << " - " << stbi_failure_reason();
        return texture;
    }
    
    // Create an OpenGL texture
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    
    // Setup filtering parameters
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    
    // Upload pixels into texture
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture.width, texture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    
    // Free the image data
    stbi_image_free(image_data);
    
    // Store the texture ID
    texture.textureID = textureID;
    texture.loaded = true;
    
    // Cache the texture for future use
    s_iconTextures[iconName] = texture;
    
    PLOGI << "Loaded texture for icon: " << iconName << " (" << texture.width << "x" << texture.height << ")";
    return texture;
}

// Helper: dynamic texture cache and loader for in-memory images
static std::unordered_map<std::string, IconTexture> s_dynamicTextures;

// Load a generic image from raw bytes into GL texture and cache it by key
IconTexture LoadTextureFromMemory(const std::string& key, const std::vector<uint8_t>& data) {
    // Check cache
    auto it = s_dynamicTextures.find(key);
    if (it != s_dynamicTextures.end()) return it->second;

    IconTexture texture;
    int channels;
    int w = 0, h = 0;
    unsigned char* image_data = stbi_load_from_memory(data.data(), static_cast<int>(data.size()), &w, &h, &channels, 4);
    if (!image_data) return texture;

    texture.width = w; texture.height = h;
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_2D, textureID);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, texture.width, texture.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, image_data);
    stbi_image_free(image_data);
    texture.textureID = textureID; texture.loaded = true;
    s_dynamicTextures[key] = texture;
    return texture;
}

bool HasDynamicTexture(const std::string& key){
    return s_dynamicTextures.find(key) != s_dynamicTextures.end();
}

IconTexture GetDynamicTexture(const std::string& key){
    auto it = s_dynamicTextures.find(key);
    if(it != s_dynamicTextures.end()) return it->second;
    return IconTexture();
}

// Draw an icon as an ImGui::Image using the original image file
bool DrawIcon(const std::string& iconName, const ImVec2& size, const ImVec4& tint_col, const ImVec4& border_col) {
    IconTexture texture = LoadIconTexture(iconName);
    if (!texture.loaded) {
        return false;
    }
    
    ImGui::Image((ImTextureID)(intptr_t)texture.textureID, size, ImVec2(0, 0), ImVec2(1, 1), tint_col, border_col);
    return true;
}

// Draw an icon as an ImGui::ImageButton using the original image file
bool DrawIconButton(const std::string& iconName, const ImVec2& size, const ImVec4& tint_col, 
                   const ImVec4& bg_col, const ImVec4& border_col) {
    IconTexture texture = LoadIconTexture(iconName);
    if (!texture.loaded) {
        return false;
    }
    
    return ImGui::ImageButton(
        ("##" + iconName).c_str(), 
        (ImTextureID)(intptr_t)texture.textureID, 
        size, 
        ImVec2(0, 0), ImVec2(1, 1), 
        bg_col, tint_col
    );
}

// Blit an icon to the current draw list at given position
bool BlitIcon(const std::string& iconName, const ImVec2& pos, const ImVec2& size, 
             const ImVec4& tint_col, float rounding, ImDrawFlags flags) {
    IconTexture texture = LoadIconTexture(iconName);
    if (!texture.loaded) {
        return false;
    }
    
    ImGui::GetWindowDrawList()->AddImage(
        (ImTextureID)(intptr_t)texture.textureID,
        pos,
        ImVec2(pos.x + size.x, pos.y + size.y),
        ImVec2(0, 0), ImVec2(1, 1),
        ImGui::ColorConvertFloat4ToU32(tint_col)
    );
    
    return true;
}