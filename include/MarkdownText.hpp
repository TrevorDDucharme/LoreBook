#pragma once
#include <imgui.h>
#include <imgui_internal.h>
#include <string>

// ImGui markdown rendering
namespace ImGui
{
    // Main function to render markdown text
    IMGUI_API void MarkdownText(const char* text);
    IMGUI_API void MarkdownText(const std::string& text);
    
    // Format string versions
    template <typename... Args>
    IMGUI_API void MarkdownTextFormatted(const char* fmt, Args... args) {
        // Calculate required buffer size
        int size = snprintf(nullptr, 0, fmt, args...) + 1; // +1 for null terminator
        if (size <= 0) {
            return;
        } // Error in formatting

        // Create a buffer to hold the formatted string
        char* buf = new char[size];
        snprintf(buf, size, fmt, args...);

        // Pass to regular MarkdownText
        MarkdownText(buf);

        // Clean up
        delete[] buf;
    }
    
    template <typename... Args>
    IMGUI_API void MarkdownTextFormatted(const std::string& fmt, Args... args) {
        MarkdownTextFormatted(fmt.c_str(), args...);
    }
}