#pragma once
#include <FileBackend.hpp>
#include <memory>

class Vault;

class VaultFileBackend : public FileBackend
{
public:
    explicit VaultFileBackend(std::shared_ptr<Vault> vault);
    // Accept raw Vault* for callers that hold non-owning raw pointers
    explicit VaultFileBackend(Vault* vault);
    ~VaultFileBackend() override;

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
    std::shared_ptr<Vault> m_vault;
public:
    // Expose vault pointer for callers that need access to vault (non-owning)
    std::shared_ptr<Vault> getVault() const { return m_vault; }
};
