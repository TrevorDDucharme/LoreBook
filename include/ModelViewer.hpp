#pragma once

#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <imgui.h>

struct ModelViewer {
    using TextureLoader = std::function<std::vector<uint8_t>(const std::string& path)>;

    ModelViewer();
    ~ModelViewer();

    // Load model data from memory (assimp supports reading from memory)
    // name is used to resolve relative textures if provided
    bool loadFromMemory(const std::vector<uint8_t>& data, const std::string& name);
    // Non-blocking: parse model on worker thread, upload on main thread during render
    void loadFromMemoryAsync(const std::vector<uint8_t>& data, const std::string& name);
    bool loadFromFile(const std::string& path);

    // Whether the last load attempt failed
    bool loadFailed() const;

    // Return whether a model load is in progress (parsing or uploading)
    bool isLoading() const;

    // Set a callback to resolve external texture paths to raw bytes
    void setTextureLoader(TextureLoader loader);

    // Return whether a model is loaded and ready to render
    bool isLoaded() const;

    // Render UI and viewport; returns whether the viewer window is open
    bool renderWindow(const char* title, bool* p_open = nullptr);

    // Render directly into a provided area (for embedding)
    void renderToRegion(const ImVec2& size);

    // Process any pending uploads or parse failures that were produced by async parsing
    // This must be called on the main thread each frame (e.g., by the UI loop)
    void processPendingUploads();

    // Clear loaded model
    void clear();

private:
    struct Impl;
    Impl* impl = nullptr;
};