// FileBackend.hpp - pluggable file backend interface
#pragma once
#include <string>
#include <vector>
#include <filesystem>
#include <cstdint>
#include <future>
#include <functional>

struct FileUri
{
    std::string scheme; // e.g., "file", "vault"
    std::filesystem::path path; // normalized path or external path

    static FileUri parse(const std::string &uri);
    static FileUri fromPath(const std::filesystem::path &p);
    std::string toString() const;
};

// Simple inline implementations
inline FileUri FileUri::parse(const std::string &uri)
{
    FileUri out;
    // very small parser: if starts with "scheme://" split, else treat as file path
    auto pos = uri.find("://");
    if (pos != std::string::npos)
    {
        out.scheme = uri.substr(0, pos);
        out.path = uri.substr(pos + 3);
    }
    else
    {
        out.scheme = "file";
        out.path = uri;
    }
    return out;
}

inline FileUri FileUri::fromPath(const std::filesystem::path &p)
{
    // If the provided path string already contains a scheme (e.g. "vault://..."), parse it
    std::string s = p.string();
    auto pos = s.find("://");
    if (pos != std::string::npos)
    {
        return parse(s);
    }
    FileUri out;
    out.scheme = "file";
    out.path = p;
    return out;
}

inline std::string FileUri::toString() const
{
    if (scheme.empty() || scheme == "file")
        return path.string();
    return scheme + std::string("://") + path.string();
}

enum class FileEventType { Created, Modified, Deleted, Renamed };

struct FileEvent
{
    FileEventType type;
    FileUri uri;
    FileUri oldUri; // used for Renamed
};

struct FileResult
{
    bool ok = false;
    std::string error;
    std::vector<uint8_t> data;
    std::filesystem::file_time_type mtime;

    std::string text() const { return std::string(data.begin(), data.end()); }
};

using WatchHandle = uint64_t;
using WatchCallback = std::function<void(const FileEvent &)>;

class FileBackend
{
public:
    virtual ~FileBackend() = default;

    virtual FileResult readFileSync(const FileUri &uri) = 0;
    virtual std::future<FileResult> readFileAsync(const FileUri &uri) = 0;

    virtual FileResult writeFileSync(const FileUri &uri, const std::vector<uint8_t> &data, bool createDirs = true) = 0;
    virtual std::future<FileResult> writeFileAsync(const FileUri &uri, std::vector<uint8_t> data, bool createDirs = true) = 0;

    virtual FileResult deleteFile(const FileUri &uri) = 0;
    virtual FileResult createDirectory(const FileUri &uri) = 0;

    virtual FileResult stat(const FileUri &uri) = 0;
    virtual std::vector<std::string> list(const FileUri &uri, bool recursive = false) = 0;

    virtual WatchHandle watch(const FileUri &uriOrDir, WatchCallback cb) = 0;
    virtual void unwatch(WatchHandle h) = 0;
};
