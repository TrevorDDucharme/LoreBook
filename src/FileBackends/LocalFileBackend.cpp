#include "FileBackends/LocalFileBackend.hpp"
#include <fstream>
#include <system_error>

static FileUri normalizeFileUri(const FileUri &uri)
{
    FileUri out = uri;
    if (out.scheme.empty()) out.scheme = "file";
    return out;
}

FileResult LocalFileBackend::readFileSync(const FileUri &uri)
{
    FileResult res;
    FileUri u = normalizeFileUri(uri);
    if (u.scheme != "file")
    {
        res.ok = false;
        res.error = "LocalFileBackend only supports file:// URIs";
        return res;
    }

    std::ifstream ifs(u.path, std::ios::binary);
    if (!ifs)
    {
        res.ok = false;
        res.error = "Failed to open file";
        return res;
    }
    std::vector<uint8_t> buf((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    res.data = std::move(buf);
    res.ok = true;
    try { res.mtime = std::filesystem::last_write_time(u.path); } catch(...) {}
    return res;
}

std::future<FileResult> LocalFileBackend::readFileAsync(const FileUri &uri)
{
    return std::async(std::launch::async, [this, uri]() { return readFileSync(uri); });
}

FileResult LocalFileBackend::writeFileSync(const FileUri &uri, const std::vector<uint8_t> &data, bool createDirs)
{
    FileResult res;
    FileUri u = normalizeFileUri(uri);
    if (u.scheme != "file")
    {
        res.ok = false;
        res.error = "LocalFileBackend only supports file:// URIs";
        return res;
    }

    try
    {
        if (createDirs)
        {
            auto parent = u.path.parent_path();
            if (!parent.empty()) std::filesystem::create_directories(parent);
        }
        std::ofstream ofs(u.path, std::ios::binary);
        if (!ofs) { res.ok = false; res.error = "Failed to open file for write"; return res; }
        ofs.write(reinterpret_cast<const char*>(data.data()), static_cast<std::streamsize>(data.size()));
        res.ok = true;
        res.mtime = std::filesystem::last_write_time(u.path);
    }
    catch (const std::exception &ex)
    {
        res.ok = false;
        res.error = ex.what();
    }
    return res;
}

std::future<FileResult> LocalFileBackend::writeFileAsync(const FileUri &uri, std::vector<uint8_t> data, bool createDirs)
{
    return std::async(std::launch::async, [this, uri, data = std::move(data), createDirs]() mutable { return writeFileSync(uri, data, createDirs); });
}

FileResult LocalFileBackend::deleteFile(const FileUri &uri)
{
    FileResult res;
    FileUri u = normalizeFileUri(uri);
    try
    {
        std::filesystem::remove(u.path);
        res.ok = true;
    }
    catch (const std::exception &ex)
    {
        res.ok = false; res.error = ex.what();
    }
    return res;
}

FileResult LocalFileBackend::createDirectory(const FileUri &uri)
{
    FileResult res;
    FileUri u = normalizeFileUri(uri);
    try { std::filesystem::create_directories(u.path); res.ok = true; }
    catch (const std::exception &ex) { res.ok = false; res.error = ex.what(); }
    return res;
}

FileResult LocalFileBackend::stat(const FileUri &uri)
{
    FileResult res;
    FileUri u = normalizeFileUri(uri);
    try
    {
        res.mtime = std::filesystem::last_write_time(u.path);
        res.ok = true;
    }
    catch (const std::exception &ex)
    {
        res.ok = false; res.error = ex.what();
    }
    return res;
}

std::vector<std::string> LocalFileBackend::list(const FileUri &uri, bool recursive)
{
    std::vector<std::string> out;
    FileUri u = normalizeFileUri(uri);
    try
    {
        if (recursive)
        {
            for (auto &p : std::filesystem::recursive_directory_iterator(u.path))
                out.push_back(p.path().string());
        }
        else
        {
            for (auto &p : std::filesystem::directory_iterator(u.path))
                out.push_back(p.path().string());
        }
    }
    catch (...) {}
    return out;
}

WatchHandle LocalFileBackend::watch(const FileUri &uriOrDir, WatchCallback cb)
{
    (void)uriOrDir; (void)cb; return 0; // polling watch not implemented here
}

void LocalFileBackend::unwatch(WatchHandle h)
{
    (void)h; // no-op
}
