#pragma once
#include <FileBackend.hpp>
#include <string>
#include <mutex>

class LocalFileBackend : public FileBackend
{
public:
    LocalFileBackend() = default;
    ~LocalFileBackend() override = default;

    FileResult readFileSync(const FileUri &uri) override;
    std::future<FileResult> readFileAsync(const FileUri &uri) override;

    FileResult writeFileSync(const FileUri &uri, const std::vector<uint8_t> &data, bool createDirs = true) override;
    std::future<FileResult> writeFileAsync(const FileUri &uri, std::vector<uint8_t> data, bool createDirs = true) override;

    FileResult deleteFile(const FileUri &uri) override;
    FileResult createDirectory(const FileUri &uri) override;

    FileResult stat(const FileUri &uri) override;
    std::vector<std::string> list(const FileUri &uri, bool recursive = false) override;

    WatchHandle watch(const FileUri &uriOrDir, WatchCallback cb) override;
    void unwatch(WatchHandle h) override;

private:
    std::mutex mtx;
};
