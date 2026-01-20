#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

// ImGui markdown rendering using md4c (CommonMark)
#include <md4c.h>

namespace ImGui
{
    // Main function to render markdown text
    IMGUI_API void MarkdownText(const char* text);
    IMGUI_API void MarkdownText(const std::string& text);

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