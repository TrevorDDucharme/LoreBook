#include <MarkdownText.hpp>
#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <regex>
#include <stack>
#include <Fonts.hpp>

namespace ImGui
{
    // Structure to hold line information and type
    struct MarkdownLine {
        enum Type {
            PARAGRAPH,
            HEADER,
            BULLET_LIST,
            NUMBERED_LIST,
            CODE_BLOCK,
            BLOCKQUOTE,
            HORIZONTAL_RULE,
            EMPTY
        } type;
        int level; // Header level or list indent level
        std::string content;
        bool hasCheckbox = false;
        bool isChecked = false;
    };
    
    // Structure to hold inline text style
    struct MarkdownStyle {
        bool bold = false;
        bool italic = false;
        bool code = false;
        bool link = false;
        ImVec4 color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // Default white
        std::string linkText;
        std::string linkUrl;
    };
    
    // Structure to hold parsed text segments with style
    struct MarkdownTextSegment {
        std::string text;
        MarkdownStyle style;
    };
    
    // Markdown parser class
    struct MarkdownParser {
        const char* text;
        size_t textLen;
        std::vector<MarkdownLine> lines;
        std::vector<std::string> errorMessages;
        bool inCodeBlock = false;
        std::string codeBlockLanguage;
        
        MarkdownParser(const char* txt) : text(txt), inCodeBlock(false) {
            textLen = strlen(text);
            parseLines();
            processLines();
        }
        
        // Split text into lines and perform initial parsing
        void parseLines() {
            std::istringstream stream(text);
            std::string line;
            std::vector<std::string> rawLines;
            
            // First, split into raw lines
            while (std::getline(stream, line)) {
                // Handle line endings
                if (!line.empty() && line.back() == '\r') {
                    line.pop_back();
                }
                rawLines.push_back(line);
            }
            
            // Process lines for Markdown elements
            for (size_t i = 0; i < rawLines.size(); i++) {
                processLine(rawLines, i);
            }
        }
        
        // Process a single line
        void processLine(const std::vector<std::string>& rawLines, size_t lineIndex) {
            const std::string& line = rawLines[lineIndex];
            MarkdownLine mdLine;
            
            // Skip processing if we're in a code block
            if (inCodeBlock) {
                if (line.find("```") == 0) {
                    // End of code block
                    inCodeBlock = false;
                    mdLine.type = MarkdownLine::EMPTY;
                } else {
                    mdLine.type = MarkdownLine::CODE_BLOCK;
                    mdLine.content = line;
                }
                lines.push_back(mdLine);
                return;
            }
            
            // Check for code block start
            if (line.find("```") == 0) {
                inCodeBlock = true;
                mdLine.type = MarkdownLine::CODE_BLOCK;
                // Extract language if specified
                if (line.length() > 3) {
                    codeBlockLanguage = line.substr(3);
                }
                mdLine.content = codeBlockLanguage; // Store language in the content
                lines.push_back(mdLine);
                return;
            }
            
            // Empty line
            if (line.empty()) {
                mdLine.type = MarkdownLine::EMPTY;
                lines.push_back(mdLine);
                return;
            }
            
            // Headers (# Header)
            if (line[0] == '#') {
                size_t level = 0;
                size_t pos = 0;
                
                // Count # symbols for header level
                while (pos < line.length() && line[pos] == '#') {
                    level++;
                    pos++;
                }
                
                // Valid header has a space after the # symbols
                if (pos < line.length() && line[pos] == ' ') {
                    mdLine.type = MarkdownLine::HEADER;
                    mdLine.level = level;
                    mdLine.content = line.substr(pos + 1); // Skip the space too
                    lines.push_back(mdLine);
                    return;
                }
            }
            
            // Horizontal rule (---, ***, ___)
            if (line == "---" || line == "***" || line == "___" || 
                std::regex_match(line, std::regex("^-{3,}$")) || 
                std::regex_match(line, std::regex("^\\*{3,}$")) || 
                std::regex_match(line, std::regex("^_{3,}$"))) {
                mdLine.type = MarkdownLine::HORIZONTAL_RULE;
                lines.push_back(mdLine);
                return;
            }
            
            // Blockquote (> text)
            if (line[0] == '>') {
                mdLine.type = MarkdownLine::BLOCKQUOTE;
                size_t pos = 1;
                // Skip space after > if present
                if (pos < line.length() && line[pos] == ' ') {
                    pos++;
                }
                mdLine.content = line.substr(pos);
                lines.push_back(mdLine);
                return;
            }
            
            // Bullet list (-, *, +)
            if ((line[0] == '-' || line[0] == '*' || line[0] == '+') && 
                line.length() > 1 && line[1] == ' ') {
                mdLine.type = MarkdownLine::BULLET_LIST;
                
                // Check for checkbox [x] or [ ]
                size_t pos = 2; // Skip the bullet and space
                if (line.length() > 5 && line.substr(pos, 3) == "[ ]") {
                    mdLine.hasCheckbox = true;
                    mdLine.isChecked = false;
                    pos += 3;
                    if (pos < line.length() && line[pos] == ' ') pos++;
                } else if (line.length() > 5 && (line.substr(pos, 3) == "[x]" || line.substr(pos, 3) == "[X]")) {
                    mdLine.hasCheckbox = true;
                    mdLine.isChecked = true;
                    pos += 3;
                    if (pos < line.length() && line[pos] == ' ') pos++;
                }
                
                mdLine.content = line.substr(pos);
                mdLine.level = countIndent(line);
                lines.push_back(mdLine);
                return;
            }
            
            // Numbered list (1. text)
            std::regex numListRegex("^\\d+\\. ");
            std::smatch match;
            if (std::regex_search(line, match, numListRegex)) {
                mdLine.type = MarkdownLine::NUMBERED_LIST;
                size_t pos = match.str().length();
                mdLine.content = line.substr(pos);
                mdLine.level = countIndent(line);
                lines.push_back(mdLine);
                return;
            }
            
            // Default: paragraph text
            mdLine.type = MarkdownLine::PARAGRAPH;
            mdLine.content = line;
            lines.push_back(mdLine);
        }
        
        // Count indentation level
        int countIndent(const std::string& line) {
            int count = 0;
            size_t pos = 0;
            while (pos < line.length() && (line[pos] == ' ' || line[pos] == '\t')) {
                if (line[pos] == '\t') {
                    count += 4; // Count tab as 4 spaces
                } else {
                    count++;
                }
                pos++;
            }
            return count / 2; // Divide by 2 to get logical level
        }
        
        // Process lines to handle multi-line elements
        void processLines() {
            // Combine adjacent paragraph lines without empty line between them
            for (size_t i = 0; i < lines.size(); i++) {
                if (i > 0 && lines[i].type == MarkdownLine::PARAGRAPH && 
                    lines[i-1].type == MarkdownLine::PARAGRAPH) {
                    lines[i-1].content += " " + lines[i].content;
                    lines[i].type = MarkdownLine::EMPTY;
                }
            }
            
            // Remove empty lines that were marked for removal
            lines.erase(
                std::remove_if(lines.begin(), lines.end(), 
                               [](const MarkdownLine& line) { 
                                   return line.type == MarkdownLine::EMPTY && line.content.empty(); 
                               }),
                lines.end());
        }
        
        // Parse inline styles and return segments with appropriate styling
        std::vector<MarkdownTextSegment> parseInlineStyles(const std::string& text) {
            std::vector<MarkdownTextSegment> segments;
            
            // If text is empty, return empty segments
            if (text.empty()) {
                return segments;
            }
            
            // Create a parser state to track nested styles
            std::stack<size_t> boldStartStack;
            std::stack<size_t> italicStartStack;
            std::stack<size_t> codeStartStack;
            
            // Start with a default segment
            MarkdownTextSegment currentSegment;
            currentSegment.text = "";
            
            // Process character by character to handle nested styles correctly
            for (size_t i = 0; i < text.length(); i++) {
                // Check for bold (**text** or __text__)
                if ((i + 1 < text.length() && text[i] == '*' && text[i+1] == '*') ||
                    (i + 1 < text.length() && text[i] == '_' && text[i+1] == '_')) {
                    // End of bold
                    if (!boldStartStack.empty()) {
                        size_t start = boldStartStack.top();
                        boldStartStack.pop();
                        
                        // Add current segment if not empty
                        if (!currentSegment.text.empty()) {
                            segments.push_back(currentSegment);
                        }
                        
                        // Start new segment with bold style
                        currentSegment = MarkdownTextSegment();
                        currentSegment.style.bold = !currentSegment.style.bold;
                        i++; // Skip second * or _
                        continue;
                    } 
                    // Start of bold
                    else {
                        // Add current segment if not empty
                        if (!currentSegment.text.empty()) {
                            segments.push_back(currentSegment);
                        }
                        
                        // Start new segment with bold style
                        currentSegment = MarkdownTextSegment();
                        currentSegment.style.bold = true;
                        boldStartStack.push(i);
                        i++; // Skip second * or _
                        continue;
                    }
                }
                
                // Check for italic (*text* or _text_)
                if ((text[i] == '*' || text[i] == '_') && 
                    (i == 0 || text[i-1] != '\\') && // Not escaped
                    !(i + 1 < text.length() && text[i+1] == text[i])) { // Not part of bold
                    // End of italic
                    if (!italicStartStack.empty() && text[italicStartStack.top()] == text[i]) {
                        italicStartStack.pop();
                        
                        // Add current segment if not empty
                        if (!currentSegment.text.empty()) {
                            segments.push_back(currentSegment);
                        }
                        
                        // Start new segment with italic style toggled
                        currentSegment = MarkdownTextSegment();
                        currentSegment.style.italic = !currentSegment.style.italic;
                        continue;
                    } 
                    // Start of italic
                    else if (italicStartStack.empty() || text[italicStartStack.top()] != text[i]) {
                        // Add current segment if not empty
                        if (!currentSegment.text.empty()) {
                            segments.push_back(currentSegment);
                        }
                        
                        // Start new segment with italic style
                        currentSegment = MarkdownTextSegment();
                        currentSegment.style.italic = true;
                        italicStartStack.push(i);
                        continue;
                    }
                }
                
                // Check for inline code (`code`)
                if (text[i] == '`' && (i == 0 || text[i-1] != '\\')) { // Not escaped
                    // End of code
                    if (!codeStartStack.empty()) {
                        codeStartStack.pop();
                        
                        // Add current segment if not empty
                        if (!currentSegment.text.empty()) {
                            segments.push_back(currentSegment);
                        }
                        
                        // Start new segment with code style toggled
                        currentSegment = MarkdownTextSegment();
                        currentSegment.style.code = !currentSegment.style.code;
                        continue;
                    } 
                    // Start of code
                    else {
                        // Add current segment if not empty
                        if (!currentSegment.text.empty()) {
                            segments.push_back(currentSegment);
                        }
                        
                        // Start new segment with code style
                        currentSegment = MarkdownTextSegment();
                        currentSegment.style.code = true;
                        codeStartStack.push(i);
                        continue;
                    }
                }
                
                // Check for links [text](url)
                if (text[i] == '[' && (i == 0 || text[i-1] != '\\')) {
                    size_t closeBracket = text.find(']', i);
                    if (closeBracket != std::string::npos && 
                        closeBracket + 1 < text.length() && 
                        text[closeBracket + 1] == '(') {
                        
                        size_t closeParenthesis = text.find(')', closeBracket + 2);
                        if (closeParenthesis != std::string::npos) {
                            // Add current segment if not empty
                            if (!currentSegment.text.empty()) {
                                segments.push_back(currentSegment);
                            }
                            
                            // Create link segment
                            MarkdownTextSegment linkSegment;
                            linkSegment.style.link = true;
                            linkSegment.style.color = ImVec4(0.26f, 0.53f, 0.96f, 1.0f); // Link blue color
                            linkSegment.text = text.substr(i + 1, closeBracket - i - 1); // Link text
                            linkSegment.style.linkText = linkSegment.text;
                            linkSegment.style.linkUrl = text.substr(closeBracket + 2, closeParenthesis - closeBracket - 2); // URL
                            
                            segments.push_back(linkSegment);
                            
                            // Reset current segment
                            currentSegment = MarkdownTextSegment();
                            
                            // Skip to end of link
                            i = closeParenthesis;
                            continue;
                        }
                    }
                }
                
                // Add regular character to current segment
                currentSegment.text += text[i];
            }
            
            // Add final segment if not empty
            if (!currentSegment.text.empty()) {
                segments.push_back(currentSegment);
            }
            
            return segments;
        }
        
        // Render text with the specified style
        void renderStyledText(const std::string& text, const MarkdownStyle& style) {
            ImGuiWindow* window = GetCurrentWindow();
            ImDrawList* drawList = window->DrawList;
            
            // Save the current style
            ImFont* originalFont = GetFont();
            ImU32 originalColor = GetColorU32(ImGuiCol_Text);
            
            // Apply font based on style
            if (style.code) {
                ImFont* monoFont = GetFontByFamily("Monospace", FontStyle::Regular);
                if (monoFont != nullptr) {
                    PushFont(monoFont);
                }
            } else if (style.bold && style.italic) {
                ImFont* boldItalicFont = GetFont(FontStyle::Bold | FontStyle::Italic);
                if (boldItalicFont != nullptr) {
                    PushFont(boldItalicFont);
                }
            } else if (style.bold) {
                ImFont* boldFont = GetFont(FontStyle::Bold);
                if (boldFont != nullptr) {
                    PushFont(boldFont);
                }
            } else if (style.italic) {
                ImFont* italicFont = GetFont(FontStyle::Italic);
                if (italicFont != nullptr) {
                    PushFont(italicFont);
                }
            }
            
            // Apply color
            if (style.color.w > 0.0f) {
                PushStyleColor(ImGuiCol_Text, style.color);
            }
            
            // Calculate text size and position
            ImVec2 textSize = CalcTextSize(text.c_str());
            ImVec2 pos = GetCursorScreenPos();
            
            // Render the text
            if (style.link) {
                // Render link with underline
                ImU32 linkColor = GetColorU32(style.color);
                TextUnformatted(text.c_str());
                
                // Draw underline
                ImVec2 lineStart = ImVec2(pos.x, pos.y + textSize.y);
                ImVec2 lineEnd = ImVec2(pos.x + textSize.x, pos.y + textSize.y);
                drawList->AddLine(lineStart, lineEnd, linkColor, 1.0f);
                
                // Handle hover and click
                if (IsItemHovered()) {
                    SetTooltip("%s", style.linkUrl.c_str());
                    if (IsMouseClicked(0)) {
                        // Open URL functionality would go here
                        // This is just a placeholder, actual opening depends on system integration
                        // OpenURL(style.linkUrl.c_str());
                    }
                }
            } else {
                TextUnformatted(text.c_str());
            }
            
            // Restore style
            if (style.color.w > 0.0f) {
                PopStyleColor();
            }
            
            if (style.code || style.bold || style.italic) {
                PopFont();
            }
            
            // Adjust cursor position
            SameLine(0.0f, 0.0f);
        }
        
        // Render text segments with their respective styles
        void renderTextSegments(const std::vector<MarkdownTextSegment>& segments) {
            for (const auto& segment : segments) {
                renderStyledText(segment.text, segment.style);
            }
            NewLine();
        }
        
        // Render the parsed Markdown
        void render() {
            ImGuiContext& g = *GImGui;
            const float windowWidth = GetContentRegionAvail().x;
            const float textBaseWidth = CalcTextSize("A").x;
            bool inCodeBlock = false;
            
            for (size_t i = 0; i < lines.size(); i++) {
                const auto& line = lines[i];
                
                switch (line.type) {
                    case MarkdownLine::HEADER: {
                        // Apply header styling based on level
                        float fontScale = 1.0f + (6 - line.level) * 0.2f; // H1 largest, H6 smallest
                        float fontSize = GetFontSize();
                        
                        // Use bold font for headers and scale it
                        ImFont* headerFont = GetFont(FontStyle::Bold);
                        PushFont(headerFont);
                        SetWindowFontScale(fontScale);
                        
                        // Parse and render header content with inline styles
                        auto segments = parseInlineStyles(line.content);
                        renderTextSegments(segments);
                        
                        // Restore original font settings
                        SetWindowFontScale(1.0f);
                        PopFont();
                        
                        // Add space after header
                        Dummy(ImVec2(0, 5));
                        break;
                    }
                    
                    case MarkdownLine::BULLET_LIST: {
                        // Indent based on list level
                        float indentAmount = line.level * 20.0f;
                        Indent(indentAmount);
                        
                        // Render bullet
                        AlignTextToFramePadding();
                        TextUnformatted(IconTag("point").c_str()); // Bullet character
                        SameLine();
                        
                        // Render checkbox if present
                        if (line.hasCheckbox) {
                            bool checked = line.isChecked;
                            PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
                            Checkbox("##checkbox", &checked);
                            PopStyleVar();
                            SameLine();
                        }
                        
                        // Parse and render list item content with inline styles
                        auto segments = parseInlineStyles(line.content);
                        renderTextSegments(segments);
                        
                        // Restore indentation
                        Unindent(indentAmount);
                        break;
                    }
                    
                    case MarkdownLine::NUMBERED_LIST: {
                        // Indent based on list level
                        float indentAmount = line.level * 20.0f;
                        Indent(indentAmount);
                        
                        // Calculate item number
                        int itemNumber = 1;
                        for (int j = i - 1; j >= 0; j--) {
                            if (lines[j].type != MarkdownLine::NUMBERED_LIST) {
                                break;
                            }
                            itemNumber++;
                        }
                        
                        // Render number
                        AlignTextToFramePadding();
                        Text("%d.", itemNumber);
                        SameLine();
                        
                        // Parse and render list item content with inline styles
                        auto segments = parseInlineStyles(line.content);
                        renderTextSegments(segments);
                        
                        // Restore indentation
                        Unindent(indentAmount);
                        break;
                    }
                    
                    case MarkdownLine::CODE_BLOCK: {
                        if (!inCodeBlock) {
                            // Begin code block
                            inCodeBlock = true;
                            Spacing();
                            codeBlockLanguage = line.content;
                        } else if (line.content.empty() && codeBlockLanguage.empty()) {
                            // End of code block
                            inCodeBlock = false;
                            Spacing();
                        } else {
                            // Code content - style for code block
                            PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.2f, 0.2f, 0.2f, 0.3f));
                            PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);
                            BeginChild(("CodeBlock" + std::to_string(i)).c_str(), 
                                      ImVec2(windowWidth - 16, 0), true);
                            
                            // Use monospace font for code
                            ImFont* monoFont = GetFontByFamily("Monospace", FontStyle::Regular);
                            if (monoFont != nullptr) {
                                PushFont(monoFont);
                            }
                            
                            // Render code with syntax highlighting color
                            PushStyleColor(ImGuiCol_Text, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
                            TextUnformatted(line.content.c_str());
                            PopStyleColor();
                            
                            if (monoFont != nullptr) {
                                PopFont();
                            }
                            
                            EndChild();
                            PopStyleVar();
                            PopStyleColor();
                            Spacing();
                        }
                        break;
                    }
                    
                    case MarkdownLine::BLOCKQUOTE: {
                        // Style for blockquote
                        PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.3f, 0.3f, 0.3f, 0.1f));
                        PushStyleVar(ImGuiStyleVar_ChildRounding, 3.0f);
                        BeginChild(("Blockquote" + std::to_string(i)).c_str(), 
                                  ImVec2(windowWidth - 16, 0), true);
                        
                        // Use slightly muted text color
                        PushStyleColor(ImGuiCol_Text, ImVec4(0.8f, 0.8f, 0.8f, 1.0f));
                        
                        // Parse and render blockquote content with inline styles
                        auto segments = parseInlineStyles(line.content);
                        renderTextSegments(segments);
                        
                        PopStyleColor();
                        
                        EndChild();
                        PopStyleVar();
                        PopStyleColor();
                        Spacing();
                        break;
                    }
                    
                    case MarkdownLine::HORIZONTAL_RULE: {
                        Spacing();
                        Separator();
                        Spacing();
                        break;
                    }
                    
                    case MarkdownLine::PARAGRAPH: {
                        // Parse and render paragraph content with inline styles
                        auto segments = parseInlineStyles(line.content);
                        
                        // Calculate text wrapping
                        float wrapWidth = GetContentRegionAvail().x;
                        float currentLineWidth = 0.0f;
                        
                        // Render text segments with wrapping
                        PushTextWrapPos(wrapWidth);
                        renderTextSegments(segments);
                        PopTextWrapPos();
                        
                        Spacing();
                        break;
                    }
                    
                    case MarkdownLine::EMPTY: {
                        Spacing();
                        break;
                    }
                }
            }
            
            // Report any errors
            if (!errorMessages.empty()) {
                for (const auto& error : errorMessages) {
                    TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "Error: %s", error.c_str());
                }
            }
        }
    };
    
    // Main entry point function for markdown rendering
    void MarkdownText(const char* text) {
        if (!text || !text[0])
            return;
            
        ImGuiWindow* window = GetCurrentWindow();
        if (window->SkipItems)
            return;
            
        MarkdownParser parser(text);
        parser.render();
    }
    
    void MarkdownText(const std::string& text) {
        MarkdownText(text.c_str());
    }
}