#pragma once
#include <string>

class SyntaxError{
    size_t lineNumber;
    size_t columnNumber;
    std::string message;
    bool isWarning = false; // Default to false, can be set later
    bool isFatal = false; // Default to false, can be set later
    bool isError = true; // Default to true, can be set later
public:
    SyntaxError(size_t line, size_t column, const std::string& msg)
        : lineNumber(line), columnNumber(column), message(msg) {}

    size_t getLine() const { return lineNumber; }
    size_t getColumn() const { return columnNumber; }
    const std::string& getMessage() const { return message; }
    void setWarning(bool warning) { isWarning = warning; }
    bool getIsWarning() const { return isWarning; }
    void setFatal(bool fatal) { isFatal = fatal; }
    bool isFatalError() const { return isFatal; }
    void setError(bool error) { isError = error; }
    bool getIsError() const { return isError; }

    // Convert to a human-readable string
    std::string toString() const {
        return "SyntaxError at line " + std::to_string(lineNumber) +
               ", column " + std::to_string(columnNumber) + ": " + message;
    }
};