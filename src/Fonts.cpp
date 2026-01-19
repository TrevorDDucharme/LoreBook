#include <Fonts.hpp>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>
#define STB_IMAGE_RESIZE_IMPLEMENTATION
#define STB_IMAGE_RESIZE2_IMPLEMENTATION
#include <stb_image_resize2.h>

#include <plog/Log.h>

// Initialize static instance
FontManager* FontManager::instance = nullptr;

std::string FontManager::getFontDirectory() const {
    return "Fonts";
}

bool FontManager::initialize() {
    // Initialize font scale
    fontScale = defaultFontSize / BASE_FONT_SIZE;
    
    // Check if fonts directory exists
    std::string fontDir = getFontDirectory();
    if (!std::filesystem::exists(fontDir)) {
        PLOGW << "Fonts directory does not exist: " << fontDir;
        // Create it
        if (!std::filesystem::create_directory(fontDir)) {
            PLOGE << "Failed to create fonts directory: " << fontDir;
            return false;
        }
        PLOGI << "Fonts directory created: " << fontDir;
        
		extractLoreBook_ResourcesTo("Fonts", fontDir.c_str());
        PLOGI << "Fonts unpacked from embedded resources";
    }
    
    // Scan the fonts directory for font families
    for (const auto &entry : std::filesystem::directory_iterator(fontDir)) {
        if (entry.is_directory()) {
            // Found a potential font family folder
            std::string familyName = entry.path().filename().string();
            PLOGI << "Found font family directory: " << familyName;
            
            // Scan this font family directory
            scanFontFamily(entry.path().string());
        }
    }
    
    // If no font families were found, check if we have .ttf files directly in the Fonts directory
    // This is for backward compatibility with the old approach
    if (fontFamilies.empty()) {
        PLOGW << "No font families found in directories, checking for TTF files in root";
        bool hasFonts = false;
        for (const auto &entry : std::filesystem::directory_iterator(fontDir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ttf") {
                hasFonts = true;
                
                // Create a default "Roboto" family (or whatever the first font we find is)
                std::string fileName = entry.path().filename().string();
                std::string familyName = fileName;
                
                // Try to extract family name from filename (remove style indicators)
                size_t dashPos = fileName.find('-');
                if (dashPos != std::string::npos) {
                    familyName = fileName.substr(0, dashPos);
                } else {
                    // Remove extension
                    size_t dotPos = fileName.find_last_of('.');
                    if (dotPos != std::string::npos) {
                        familyName = fileName.substr(0, dotPos);
                    }
                }
                
                // If we don't have this family yet, create it
                if (fontFamilies.find(familyName) == fontFamilies.end()) {
                    fontFamilies.emplace(familyName, FontFamily(familyName));
                }
                
                // Detect style from filename
                FontStyle style = detectFontStyleFromFilename(fileName);
                
                // Add this font file to the family with BASE_FONT_SIZE (we'll scale it at runtime)
                fontFamilies[familyName].addStyle(entry.path().string(), style, BASE_FONT_SIZE);
                
                PLOGI << "Added font " << fileName << " to family " << familyName 
                                << " with style " << static_cast<int>(style);
            }
        }
        
        if (!hasFonts) {
            PLOGE << "No font files found in the fonts directory";
            return false;
        }
    }
    
    // Set default font family if none is set
    if (currentFontFamily.empty() && !fontFamilies.empty()) {
        currentFontFamily = fontFamilies.begin()->first;
        PLOGI << "Setting default font family to: " << currentFontFamily;
    }
    
    return true;
}

void FontManager::scanFontFamily(const std::string& familyDir) {
    std::string familyName = std::filesystem::path(familyDir).filename().string();
    
    // Create a font family entry
    FontFamily family(familyName);
    
    // Track if we found any font files
    bool foundFonts = false;
    
    // Function to process TTF files in a directory
    auto processTTFFiles = [&](const std::filesystem::path& dir) {
        for (const auto& entry : std::filesystem::directory_iterator(dir)) {
            if (entry.is_regular_file() && entry.path().extension() == ".ttf") {
                std::string fileName = entry.path().filename().string();
                FontStyle style = detectFontStyleFromFilename(fileName);
                
                family.addStyle(entry.path().string(), style, BASE_FONT_SIZE);
                foundFonts = true;
                
                PLOGI << "Added font " << fileName 
                              << " to family " << familyName 
                              << " with style " << static_cast<int>(style);
            }
        }
    };
    
    // First check the main family directory
    processTTFFiles(familyDir);
    
    // Then check the "static" subdirectory if it exists
    std::filesystem::path staticDir = std::filesystem::path(familyDir) / "static";
    if (std::filesystem::exists(staticDir) && std::filesystem::is_directory(staticDir)) {
        processTTFFiles(staticDir);
    }
    
    // Add the family to our collection if we found fonts
    if (foundFonts) {
        fontFamilies[familyName] = family;
        PLOGI << "Added font family: " << familyName << " with " 
                        << family.styles.size() << " styles";
    } else {
        PLOGW << "No font files found in family directory: " << familyDir;
    }
}

FontStyle FontManager::detectFontStyleFromFilename(const std::string& filename) {
    // Convert to lowercase for case-insensitive matching
    std::string lower = filename;
    std::transform(lower.begin(), lower.end(), lower.begin(), 
                   [](unsigned char c) { return std::tolower(c); });
    
    FontStyle style = FontStyle::Regular;
    
    // Check for style indicators in the filename
    if (lower.find("bold") != std::string::npos && lower.find("italic") != std::string::npos) {
        style = FontStyle::Bold | FontStyle::Italic;
    } else if (lower.find("bold") != std::string::npos) {
        style = FontStyle::Bold;
    } else if (lower.find("italic") != std::string::npos) {
        style = FontStyle::Italic;
    } else if (lower.find("light") != std::string::npos) {
        style = FontStyle::Light;
    } else if (lower.find("medium") != std::string::npos) {
        style = FontStyle::Medium;
    } else if (lower.find("black") != std::string::npos) {
        style = FontStyle::Black;
    } else if (lower.find("thin") != std::string::npos) {
        style = FontStyle::Thin;
    } else if (lower.find("regular") != std::string::npos || lower.find("normal") != std::string::npos) {
        style = FontStyle::Regular;
    }
    
    return style;
}

bool FontManager::loadFontFamily(const std::string& familyName, float fontSize) {
    auto it = fontFamilies.find(familyName);
    if (it == fontFamilies.end()) {
        PLOGE << "Font family not found: " << familyName;
        return false;
    }
    
    // Set font size to BASE_FONT_SIZE to load at maximum quality
    for (auto& fontData : it->second.styles) {
        fontData.size = BASE_FONT_SIZE;
    }
    
    // Update the fontSize parameter via the scale factor
    if (fontSize > 0) {
        setDefaultFontSize(fontSize);
    }
    
    return true;
}

bool FontManager::setCurrentFontFamily(const std::string& familyName) {
    auto it = fontFamilies.find(familyName);
    if (it == fontFamilies.end()) {
        PLOGE << "Font family not found: " << familyName;
        return false;
    }
    
    // Update current font family name
    currentFontFamily = familyName;
    PLOGI << "Set current font family to: " << familyName;
    
    // Also update ImGui's default font to the regular style of this family
    int regularStyleVal = static_cast<int>(FontStyle::Regular);
    auto fontIt = it->second.loadedFonts.find(regularStyleVal);
    
    if (fontIt != it->second.loadedFonts.end()) {
        // Found the regular font for this family, set it as default
        ImGui::GetIO().FontDefault = fontIt->second;
        PLOGI << "Updated default font to " << familyName << " Regular";
    } else {
        // If no regular style, try to use the first available font in this family
        if (!it->second.loadedFonts.empty()) {
            ImGui::GetIO().FontDefault = it->second.loadedFonts.begin()->second;
            PLOGW << "No Regular style found for " << familyName 
                            << ", using alternative style";
        } else {
            PLOGE << "No fonts loaded for family: " << familyName;
            return false;
        }
    }
    
    return true;
}

std::string FontManager::getCurrentFontFamily() const {
    return currentFontFamily;
}

ImFont* FontManager::getFont(FontStyle style) {
    return getFontByFamily(currentFontFamily, style);
}

ImFont* FontManager::getFontByFamily(const std::string& familyName, FontStyle style) {
    auto familyIt = fontFamilies.find(familyName);
    if (familyIt == fontFamilies.end()) {
        PLOGW << "Font family not found: " << familyName;
        return ImGui::GetFont(); // Return default
    }
    
    int styleVal = static_cast<int>(style);
    auto fontIt = familyIt->second.loadedFonts.find(styleVal);
    
    if (fontIt != familyIt->second.loadedFonts.end()) {
        return fontIt->second;
    }
    
    // If exact style not found, try to find a fallback
    if (style != FontStyle::Regular) {
        // First try Regular
        fontIt = familyIt->second.loadedFonts.find(static_cast<int>(FontStyle::Regular));
        if (fontIt != familyIt->second.loadedFonts.end()) {
            return fontIt->second;
        }
        
        // If no regular, return the first available font in this family
        if (!familyIt->second.loadedFonts.empty()) {
            return familyIt->second.loadedFonts.begin()->second;
        }
    }
    
    // Last resort, return ImGui default font
    PLOGW << "No suitable font found for family: " << familyName << " style: " << styleVal;
    return ImGui::GetFont();
}

std::vector<std::string> FontManager::getAvailableFontFamilies() const {
    std::vector<std::string> families;
    for (const auto& family : fontFamilies) {
        families.push_back(family.first);
    }
    return families;
}

void FontManager::setDefaultFontSize(float size) {
    if (defaultFontSize == size) {
        return; // No change, nothing to do
    }
    
    if(size < 20.0f) {
        PLOGW << "Font size too small, minimum should be 20.0f for visibility";
    }

    // Instead of immediately applying the change, just record it
    pendingFontSize = size;
    fontSizeChanged = true;
    lastChangedTime = glfwGetTime(); // Get current time
    
    PLOGV << "Font size change pending: " << size;
}

void FontManager::applyFontScale() {
    // Update the scale factor for all fonts
    ImGuiIO& io = ImGui::GetIO();
    
    // Set the scale for all fonts based on default font size
    float scale = defaultFontSize / BASE_FONT_SIZE;
    fontScale = scale;
    
    // Apply the scale to all loaded fonts
    for (int i = 0; i < io.Fonts->Fonts.Size; i++) {
        ImFont* font = io.Fonts->Fonts[i];
        font->Scale = scale;
    }
    
    PLOGV << "Applied font scale: " << scale;
}

bool FontManager::applyPendingFontSizeChange() {
    if (!fontSizeChanged) {
        return false;
    }
    
    // Check if enough time has passed since the last change
    double currentTime = glfwGetTime();
    if (currentTime - lastChangedTime < REBUILD_DELAY) {
        return false; // Not enough time has passed, wait more
    }
    
    // Apply the pending font size change
    defaultFontSize = pendingFontSize;
    PLOGV << "Applying font size change to: " << defaultFontSize;
    
    // Apply scaling instead of requesting a full rebuild
    applyFontScale();
    
    // Reset the change flag
    fontSizeChanged = false;
    
    return true;
}

// Set font size immediately (without debouncing)
void FontManager::setFontSizeImmediate(float size) {
    if (defaultFontSize == size) {
        return; // No change, nothing to do
    }
    
    defaultFontSize = size;
    fontScale = defaultFontSize / BASE_FONT_SIZE;
    
    // Apply the scale to all loaded fonts immediately
    applyFontScale();
    
    PLOGI << "Applied immediate font size change to: " << size 
                  << " (scale factor: " << fontScale << ")";
}

// Process any pending font rebuilds (call after Render)
bool FontManager::processPendingRebuild() {
    // First check if we need to apply any pending font size changes
    bool sizeChanged = applyPendingFontSizeChange();
    
    // If the size changed, apply the scale factor to all fonts
    if (sizeChanged) {
        applyFontScale();
    }
    
    // Only rebuild if explicitly requested and it's not just a font size change
    if (rebuildPending) {
        PLOGI << "Processing pending font rebuild";
        bool result = buildFonts();
        rebuildPending = false;
        return result;
    }
    
    return sizeChanged;
}

bool FontManager::buildFonts() {
    ImGuiIO& io = ImGui::GetIO();
    
    // Clear existing fonts
    io.Fonts->Clear();
    
    // Use ImFontGlyphRangesBuilder to include all necessary ranges
    ImFontGlyphRangesBuilder builder;
    builder.AddRanges(io.Fonts->GetGlyphRangesDefault());
    builder.AddRanges(io.Fonts->GetGlyphRangesCyrillic());
    
    // Add all our custom icons to ensure consistent codepoints
    AddIconsToGlyphBuilder(builder);
    
    // Add other special characters
    builder.AddChar(0x1F6E0); // 🛠 - Tools for Miscellaneous
    
    // Build the custom ranges
    ImVector<ImWchar> customRanges;
    builder.BuildRanges(&customRanges);
    
    // Set FreeType as the font builder with appropriate flags
    unsigned int flags = ImGuiFreeTypeBuilderFlags_LightHinting | 
                         ImGuiFreeTypeBuilderFlags_ForceAutoHint;
    io.Fonts->FontBuilderIO = ImGuiFreeType::GetBuilderForFreeType();
    io.Fonts->FontBuilderFlags = flags;
    
    // Get icon data (for later use)
    std::vector<IconData> icons = GetIconsData();
    
    // Store references to all regular style fonts for icon registration
    std::vector<std::pair<std::string, ImFont*>> regularFonts;
    
    // Calculate proper font scale factor based on requested size
    float scale = defaultFontSize / BASE_FONT_SIZE;
    fontScale = scale;
    
    // Load ALL font families, not just the current one
    for (auto& familyPair : fontFamilies) {
        FontFamily& family = familyPair.second;
        
        // For each style in this family
        for (auto& fontData : family.styles) {
            // All fonts loaded at BASE_FONT_SIZE for maximum quality
            fontData.size = BASE_FONT_SIZE;
            
            // Setup font config
            ImFontConfig config;
            config.MergeMode = false;
            config.FontBuilderFlags = flags;
            snprintf(config.Name, IM_ARRAYSIZE(config.Name), "%s %d", 
                     family.name.c_str(), static_cast<int>(fontData.style));
            
            // Load the font
            ImFont* font = io.Fonts->AddFontFromFileTTF(
                fontData.path.c_str(), 
                fontData.size, 
                &config, 
                customRanges.Data
            );
            
            if (font) {
                // Set the scale factor immediately
                font->Scale = scale;
                
                // Store in the loaded fonts map
                int styleVal = static_cast<int>(fontData.style);
                family.loadedFonts[styleVal] = font;
                
                // If this is the regular style, store it for icon registration
                if (fontData.style == FontStyle::Regular) {
                    regularFonts.push_back(std::make_pair(family.name, font));
                }
                
                // If this is the current family and regular style, set as default
                if (family.name == currentFontFamily && 
                    fontData.style == FontStyle::Regular) {
                    io.FontDefault = font;
                }
                
                PLOGI << "Loaded font " << family.name 
                              << " style " << styleVal 
                              << " from " << fontData.path
                              << " with scale " << scale;
            } else {
                PLOGE << "Failed to load font from: " << fontData.path;
            }
        }
    }
    
    // If no current font family was set successfully, use the first available
    if (currentFontFamily.empty() && !fontFamilies.empty() && !io.Fonts->Fonts.empty()) {
        currentFontFamily = fontFamilies.begin()->first;
        io.FontDefault = io.Fonts->Fonts[0];
        PLOGW << "Current font family not set, using first available font";
    }
    
    // Now register icons with ALL regular style fonts and store all rect IDs
    struct IconRectInfo {
        int rect_id;
        std::string fontFamily;
    };
    std::unordered_map<std::string, std::vector<IconRectInfo>> iconRects; // filename -> vector of rects
    
    for (auto& fontPair : regularFonts) {
        std::string familyName = fontPair.first;
        ImFont* font = fontPair.second;
        float fontSize = GetDefaultFontSize();
        
        // Get font size from the font data
        auto it = fontFamilies.find(familyName);
        if (it != fontFamilies.end()) {
            for (auto& fontData : it->second.styles) {
                if (fontData.style == FontStyle::Regular) {
                    fontSize = fontData.size;
                    break;
                }
            }
        }
        
            // Add the icons to this font
            float iconSize = BASE_FONT_SIZE * 1.1f; // Base size for icons
            float offsetY = (BASE_FONT_SIZE - iconSize) * 0.5f;
            
            for (auto& icon : icons) {
                int rect_id = io.Fonts->AddCustomRectFontGlyph(
                    font,
                    icon.codepoint,
                    iconSize,
                    iconSize,
                    BASE_FONT_SIZE,
                    ImVec2(0.0f, offsetY)
                );
                
                // Store the rect_id for the first font for backwards compatibility
                if (familyName == regularFonts[0].first) {
                    icon.rect_id = rect_id;
                }
                
                // Store all rect IDs for processing later
                iconRects[icon.filename].push_back({rect_id, familyName});
                
                PLOGI << "Added custom rect for " << icon.filename 
                              << " with ID: " << rect_id 
                              << " to font family: " << familyName;
            }
    }
    
    // Build the font atlas
    PLOGI << "Building font atlas...";
    if (!io.Fonts->Build()) {
        PLOGE << "Failed to build font atlas!";
        return false;
    }
    PLOGI << "Font atlas built successfully.";
    
    // Process icons similar to the original BuildFonts function
    auto LoadPNGToImageData = [](const char* filename, int& width, int& height) -> unsigned char* {
        FILE* file = fopen(filename, "rb");
        if (!file) {
            PLOGE << "Icon file not found: " << filename;
            return nullptr;
        }
        fclose(file);
        
        unsigned char* image_data = nullptr;
        int channels;
        image_data = stbi_load(filename, &width, &height, &channels, 4);
        if (!image_data) {
            PLOGE << "Failed to load image: " << filename << " - " << stbi_failure_reason();
            return nullptr;
        }
        PLOGI << "Successfully loaded image " << filename 
                      << ": " << width << "x" << height 
                      << " with " << channels << " channels";
        return image_data;
    };
    
    // After building, load and copy icon data to atlas for ALL fonts
    for (auto& icon : icons) {
        // Load icon data once
        icon.data = LoadPNGToImageData(icon.filename.c_str(), icon.width, icon.height);
        
        if (icon.data) {
            // Get texture data info
            unsigned char* tex_pixels = nullptr;
            int tex_width, tex_height;
            io.Fonts->GetTexDataAsRGBA32(&tex_pixels, &tex_width, &tex_height);
            
            // Process all rects for this icon across all fonts
            auto rectInfos = iconRects[icon.filename];
            for (const auto& rectInfo : rectInfos) {
                ImFontAtlasCustomRect* rect = io.Fonts->GetCustomRectByIndex(rectInfo.rect_id);
                if (rect && rect->IsPacked()) {
                    PLOGI << "Copying icon data for " << icon.filename 
                                  << " to atlas rect: x=" << rect->X 
                                  << " y=" << rect->Y 
                                  << " for font family: " << rectInfo.fontFamily;
                    
                    // Resize the icon to the exact dimensions needed
                    unsigned char* resized_data = new unsigned char[rect->Width * rect->Height * 4];
                    
                    if (stbir_resize(
                        icon.data, icon.width, icon.height, icon.width * 4,
                        resized_data, rect->Width, rect->Height, rect->Width * 4,
                        stbir_pixel_layout::STBIR_RGBA,
                        stbir_datatype::STBIR_TYPE_UINT8,
                        stbir_edge::STBIR_EDGE_CLAMP,
                        stbir_filter::STBIR_FILTER_BOX)) 
                    {
                        // Copy the resized icon into the atlas
                        for (int y = 0; y < rect->Height; y++) {
                            for (int x = 0; x < rect->Width; x++) {
                                int atlas_offset = (rect->Y + y) * tex_width + (rect->X + x);
                                int icon_offset = y * rect->Width + x;
                                
                                // Ensure we're within bounds
                                if (atlas_offset >= 0 && atlas_offset < tex_width * tex_height) {
                                    for (int i = 0; i < 4; i++) { // RGBA
                                        tex_pixels[atlas_offset * 4 + i] = resized_data[icon_offset * 4 + i];
                                    }
                                }
                            }
                        }
                    } else {
                        PLOGE << "Failed to resize icon: " << icon.filename;
                    }
                    
                    delete[] resized_data;
                } else {
                    PLOGE << "Custom rect not packed for icon: " << icon.filename 
                                  << " (rect_id: " << rectInfo.rect_id 
                                  << ") for font family: " << rectInfo.fontFamily;
                }
            }
            
            // Free image data after processing all rects
            stbi_image_free(icon.data);
            icon.data = nullptr;
        }
    }
    
    // Recreate the font texture
    ImGui_ImplOpenGL3_DestroyFontsTexture();
    ImGui_ImplOpenGL3_CreateFontsTexture();
    
    return true;
}

void FontManager::cleanup() {
    // Clear font family data
    fontFamilies.clear();
    currentFontFamily.clear();
    
    // ImGui will handle the actual font texture cleanup
}

// Global function implementations for easier access
bool InitializeFonts() {
    return FontManager::getInstance()->initialize();
}

bool LoadFontFamily(const std::string& familyName, float fontSize) {
    return FontManager::getInstance()->loadFontFamily(familyName, fontSize);
}

bool SetCurrentFontFamily(const std::string& familyName) {
    return FontManager::getInstance()->setCurrentFontFamily(familyName);
}

std::string GetCurrentFontFamily() {
    return FontManager::getInstance()->getCurrentFontFamily();
}

ImFont* GetFont(FontStyle style) {
    return FontManager::getInstance()->getFont(style);
}

ImFont* GetFontByFamily(const std::string& familyName, FontStyle style) {
    return FontManager::getInstance()->getFontByFamily(familyName, style);
}

std::vector<std::string> GetAvailableFontFamilies() {
    return FontManager::getInstance()->getAvailableFontFamilies();
}

void SetDefaultFontSize(float size) {
    FontManager::getInstance()->setDefaultFontSize(size);
}

float GetDefaultFontSize() {
    return FontManager::getInstance()->getDisplayFontSize();
}

float GetFontScale() {
    return FontManager::getInstance()->getFontScale();
}

bool BuildFonts() {
    return FontManager::getInstance()->buildFonts();
}

void CleanupFonts() {
    FontManager::getInstance()->cleanup();
}

void RequestFontRebuild() {
    FontManager::getInstance()->requestRebuild();
}

bool ProcessPendingFontRebuild() {
    return FontManager::getInstance()->processPendingRebuild();
}

// Make sure you call this after any font size change to apply scaling
void ApplyFontScale() {
    FontManager::getInstance()->applyFontScale();
}

// Set font size immediately (without debouncing)
void SetFontSizeImmediate(float size) {
    FontManager::getInstance()->setFontSizeImmediate(size);
}

float GetBaseFontSize() {
    return FontManager::getInstance()->getBaseFontSize();
}