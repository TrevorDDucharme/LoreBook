#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

// ImGui markdown rendering using md4c (CommonMark)
#include <md4c.h>

class TextEffectsOverlay; // forward declaration

namespace ImGui
{
    // Main function to render markdown text
    IMGUI_API void MarkdownText(const char* text);
    IMGUI_API void MarkdownText(const char* text, void* context);
    IMGUI_API void MarkdownText(const std::string& text);
    IMGUI_API void MarkdownText(const std::string& text, void* context);

    // Set the active text effects overlay for subsequent MarkdownText() calls
    IMGUI_API void MarkdownTextSetEffectsOverlay(TextEffectsOverlay* overlay, ImVec2 contentOrigin, float scrollY);

    // Convenience formatted versions
    template <typename... Args>
    IMGUI_API void MarkdownTextFormatted(const char* fmt, Args... args) {
        int size = snprintf(nullptr, 0, fmt, args...) + 1;
        if (size <= 0) return;
        std::string buf; buf.resize(size);
        snprintf(buf.data(), size, fmt, args...);
        MarkdownText(buf.c_str());
    }

    template <typename... Args>
    IMGUI_API void MarkdownTextFormatted(const std::string& fmt, Args... args) {
        MarkdownTextFormatted(fmt.c_str(), args...);
    }
}