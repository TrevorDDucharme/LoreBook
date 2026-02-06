#pragma once
#include <iostream>
#include <streambuf>
#include <string>
#include <vector>
#include <mutex>
#include <sstream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <thread>
#include <chrono>

class ErrorStream : public std::streambuf
{
private:
    std::streambuf *originalBuffer = nullptr; // Store original buffer
    std::vector<unsigned char> buffer_; // Internal buffer for writing data
    mutable std::mutex mutex_; // Thread safety
    bool isRedirected = false; // Track redirection state
    
    // File descriptor redirection variables
    int originalFd = -1; // Original stderr file descriptor
    int pipeFd[2] = {-1, -1}; // Pipe for capturing stderr
    bool fdRedirected = false; // Track if file descriptor is redirected
    
    ErrorStream() = default; // Private constructor for singleton pattern

    // Disable copy constructor and assignment operator
    ErrorStream(const ErrorStream&) = delete;
    ErrorStream& operator=(const ErrorStream&) = delete;

public:
    // Override overflow to handle character output
    int_type overflow(int_type c) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (c != traits_type::eof()) {
            buffer_.push_back(static_cast<unsigned char>(c));
            
            // Also output to original buffer if we have one
            if (originalBuffer) {
                originalBuffer->sputc(c);
            }
        }
        return c;
    }

    // Override sync to flush the buffer
    int sync() override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Flush the original buffer if it exists
        if (originalBuffer) {
            return originalBuffer->pubsync();
        }
        return 0;
    }

    // Override xsputn for efficient multi-character output
    std::streamsize xsputn(const char_type* s, std::streamsize count) override {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Add to our internal buffer
        for (std::streamsize i = 0; i < count; ++i) {
            buffer_.push_back(static_cast<unsigned char>(s[i]));
        }
        
        // Also output to original buffer if we have one
        if (originalBuffer) {
            return originalBuffer->sputn(s, count);
        }
        
        return count;
    }

    // Redirect stderr to this stream buffer (both C++ streams and file descriptor)
    bool redirect() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // if (!isRedirected) {
        //     // Redirect C++ stream
        //     originalBuffer = std::cerr.rdbuf(this);
        //     isRedirected = true;
            
        //     // Redirect file descriptor for printf/fprintf
        //     if (!fdRedirected) {
        //         // Create a pipe
        //         if (pipe(pipeFd) == 0) {
        //             // Save original stderr
        //             originalFd = dup(STDERR_FILENO);
                    
        //             // Redirect stderr to write end of pipe
        //             if (dup2(pipeFd[1], STDERR_FILENO) != -1) {
        //                 fdRedirected = true;
                        
        //                 // Make the read end non-blocking
        //                 int flags = fcntl(pipeFd[0], F_GETFL);
        //                 fcntl(pipeFd[0], F_SETFL, flags | O_NONBLOCK);
                        
        //                 // Start a thread to read from the pipe
        //                 std::thread([this]() {
        //                     char buffer[4096];
        //                     while (fdRedirected) {
        //                         ssize_t bytesRead = read(pipeFd[0], buffer, sizeof(buffer) - 1);
        //                         if (bytesRead > 0) {
        //                             std::lock_guard<std::mutex> lock(mutex_);
        //                             for (ssize_t i = 0; i < bytesRead; ++i) {
        //                                 buffer_.push_back(static_cast<unsigned char>(buffer[i]));
        //                             }
                                    
        //                             // Also write to original stderr if available
        //                             if (originalFd != -1) {
        //                                 write(originalFd, buffer, bytesRead);
        //                             }
        //                         } else if (bytesRead == 0) {
        //                             break; // EOF
        //                         } else {
        //                             // EAGAIN/EWOULDBLOCK for non-blocking read
        //                             std::this_thread::sleep_for(std::chrono::milliseconds(1));
        //                         }
        //                     }
        //                 }).detach();
        //             }
        //         }
        //     }
            
        //     return originalBuffer != nullptr;
        // }
        return false; // Already redirected
    }

    // Restore original stderr (both C++ stream and file descriptor)
    void restore() {
        std::lock_guard<std::mutex> lock(mutex_);
        
        // Restore C++ stream
        if (isRedirected && originalBuffer) {
            std::cerr.rdbuf(originalBuffer);
            isRedirected = false;
        }
        
        // Restore file descriptor
        if (fdRedirected) {
            fdRedirected = false; // Signal the thread to stop
            
            // Restore original stderr
            if (originalFd != -1) {
                dup2(originalFd, STDERR_FILENO);
                close(originalFd);
                originalFd = -1;
            }
            
            // Close pipe
            if (pipeFd[0] != -1) {
                close(pipeFd[0]);
                pipeFd[0] = -1;
            }
            if (pipeFd[1] != -1) {
                close(pipeFd[1]);
                pipeFd[1] = -1;
            }
        }
    }

    // Get the captured buffer as a string
    std::string getBuffer() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::string(buffer_.begin(), buffer_.end());
    }

    // Get the captured buffer as raw bytes
    const std::vector<unsigned char>& getRawBuffer() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_;
    }

    // Clear the internal buffer
    void clearBuffer() {
        std::lock_guard<std::mutex> lock(mutex_);
        buffer_.clear();
    }

    // Get buffer size
    size_t getBufferSize() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return buffer_.size();
    }

    // Check if currently redirected
    bool isCurrentlyRedirected() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return isRedirected;
    }

    // Get the last N characters from buffer
    std::string getLastChars(size_t n) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (buffer_.empty() || n == 0) {
            return "";
        }
        
        size_t start = buffer_.size() > n ? buffer_.size() - n : 0;
        return std::string(buffer_.begin() + start, buffer_.end());
    }

    // Get buffer content from a specific position
    std::string getBufferFrom(size_t position) const {
        std::lock_guard<std::mutex> lock(mutex_);
        
        if (position >= buffer_.size()) {
            return "";
        }
        
        return std::string(buffer_.begin() + position, buffer_.end());
    }

    // Stream insertion operators for convenient output
    template<typename T>
    ErrorStream& operator<<(const T& value) {
        std::ostringstream oss;
        oss << value;
        std::string str = oss.str();
        xsputn(str.c_str(), str.length());
        return *this;
    }

    // Specialized operator for strings
    ErrorStream& operator<<(const std::string& str) {
        xsputn(str.c_str(), str.length());
        return *this;
    }

    // Specialized operator for C-strings
    ErrorStream& operator<<(const char* str) {
        if (str) {
            xsputn(str, std::strlen(str));
        }
        return *this;
    }

    // Specialized operator for characters
    ErrorStream& operator<<(char c) {
        overflow(c);
        return *this;
    }

    // Stream manipulators support
    ErrorStream& operator<<(std::ostream& (*manipulator)(std::ostream&)) {
        if (manipulator == static_cast<std::ostream& (*)(std::ostream&)>(std::endl)) {
            overflow('\n');
            sync();
        } else if (manipulator == static_cast<std::ostream& (*)(std::ostream&)>(std::flush)) {
            sync();
        }
        return *this;
    }

    // Conversion operator to string
    operator std::string() const {
        return getBuffer();
    }

    // Boolean conversion operator (true if buffer has content)
    explicit operator bool() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return !buffer_.empty();
    }

    // Subscript operator for accessing buffer characters
    unsigned char operator[](size_t index) const {
        std::lock_guard<std::mutex> lock(mutex_);
        if (index < buffer_.size()) {
            return buffer_[index];
        }
        return 0; // Return null character for out-of-bounds access
    }

    // Addition operator to append content
    ErrorStream& operator+=(const std::string& str) {
        xsputn(str.c_str(), str.length());
        return *this;
    }

    ErrorStream& operator+=(const char* str) {
        if (str) {
            xsputn(str, std::strlen(str));
        }
        return *this;
    }

    ErrorStream& operator+=(char c) {
        overflow(c);
        return *this;
    }

    // Singleton instance getter
    static ErrorStream& getInstance() {
        static ErrorStream instance;
        return instance;
    }

    // Destructor to ensure proper cleanup
    ~ErrorStream() {
        restore();
    }
};