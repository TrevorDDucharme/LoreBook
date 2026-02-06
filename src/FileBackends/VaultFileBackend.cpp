#include "FileBackends/VaultFileBackend.hpp"
#include <Vault.hpp>

VaultFileBackend::VaultFileBackend(std::shared_ptr<Vault> vault) : m_vault(std::move(vault)) {}
// Non-owning constructor from raw Vault* (uses no-op deleter)
VaultFileBackend::VaultFileBackend(Vault* vault) : m_vault(std::shared_ptr<Vault>(vault, [](Vault*){})) {}
VaultFileBackend::~VaultFileBackend() = default;

FileResult VaultFileBackend::readFileSync(const FileUri &uri)
{
    FileResult res;
    if (!m_vault)
    {
        res.ok = false; res.error = "No vault instance"; return res;
    }
    FileUri u = uri;
    if (u.scheme.empty()) u.scheme = "vault";
    if (u.scheme != "vault")
    {
        res.ok = false; res.error = "VaultFileBackend only supports vault:// URIs"; return res;
    }
    std::string name = u.path.string();
    // normalize leading '/'
    if (!name.empty() && name.front() == '/') name.erase(name.begin());
    // If the URI was provided as vault://Scripts/<name> then FileUri::parse() leaves
    // the path as "Scripts/<name>". Vault APIs expect just the script name, so
    // strip a leading "Scripts/" segment if present.
    const std::string scriptsPrefix = "Scripts/";
    if (name.rfind(scriptsPrefix, 0) == 0) name = name.substr(scriptsPrefix.size());
    try {
        std::string code = m_vault->getScript(name);
        res.data.assign(code.begin(), code.end());
        res.ok = true;
    } catch (...) {
        res.ok = false; res.error = "Vault getScript failed";
    }
    return res;
}

std::future<FileResult> VaultFileBackend::readFileAsync(const FileUri &uri)
{
    return std::async(std::launch::async, [this, uri]() { return readFileSync(uri); });
}

FileResult VaultFileBackend::writeFileSync(const FileUri &uri, const std::vector<uint8_t> &data, bool createDirs)
{
    FileResult res;
    if (!m_vault) { res.ok = false; res.error = "No vault instance"; return res; }
    FileUri u = uri;
    if (u.scheme.empty()) u.scheme = "vault";
    if (u.scheme != "vault") { res.ok = false; res.error = "VaultFileBackend only supports vault:// URIs"; return res; }
    std::string name = u.path.string(); if (!name.empty() && name.front() == '/') name.erase(name.begin());
    // Normalize "Scripts/<name>" -> "<name>" to match Vault::getScript/updateScript expectations
    const std::string scriptsPrefix = "Scripts/";
    if (name.rfind(scriptsPrefix, 0) == 0) name = name.substr(scriptsPrefix.size());
    std::string code(data.begin(), data.end());
    bool ok = m_vault->updateScript(name, code);
    if (ok) { res.ok = true; }
    else { res.ok = false; res.error = "Vault updateScript failed"; }
    return res;
}

std::future<FileResult> VaultFileBackend::writeFileAsync(const FileUri &uri, std::vector<uint8_t> data, bool createDirs)
{
    return std::async(std::launch::async, [this, uri, data = std::move(data), createDirs]() mutable { return writeFileSync(uri, data, createDirs); });
}

FileResult VaultFileBackend::deleteFile(const FileUri &uri) { FileResult r; r.ok = false; r.error = "not implemented"; (void)uri; return r; }
FileResult VaultFileBackend::createDirectory(const FileUri &uri) { FileResult r; r.ok = false; r.error = "not implemented"; (void)uri; return r; }
FileResult VaultFileBackend::stat(const FileUri &uri)
{
    FileResult r;
    if (!m_vault) { r.ok = false; r.error = "No vault"; return r; }
    // best-effort: scripts don't expose mtime; return ok=true
    r.ok = true;
    return r;
}

std::vector<std::string> VaultFileBackend::list(const FileUri &uri, bool recursive)
{
    (void)uri; (void)recursive;
    if (!m_vault) return {};
    return m_vault->listScripts();
}

WatchHandle VaultFileBackend::watch(const FileUri &uriOrDir, WatchCallback cb) { (void)uriOrDir; (void)cb; return 0; }
void VaultFileBackend::unwatch(WatchHandle h) { (void)h; }
