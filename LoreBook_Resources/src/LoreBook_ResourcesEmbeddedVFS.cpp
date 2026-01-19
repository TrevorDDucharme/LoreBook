#include "LoreBook_ResourcesEmbeddedVFS.hpp"
static unsigned char* inMemFilesystem = nullptr;
static unsigned int inMemFilesystemSize = 0;

unsigned char* decompressLoreBook_ResourcesZipInMemory(unsigned char* inputBuffer, unsigned int inputBufferSize, unsigned int &outputSize)
{
	size_t const cBuffOutSize = ZSTD_getFrameContentSize(inputBuffer, inputBufferSize);
	unsigned char* zipBuffer = (unsigned char*)malloc(cBuffOutSize);
	size_t const cSize = ZSTD_decompress(zipBuffer, cBuffOutSize, inputBuffer, inputBufferSize);
	zipBuffer = (unsigned char*)realloc(zipBuffer, cSize);
	outputSize = cSize;
	return zipBuffer;
}

bool initLoreBook_ResourcesEmbeddedVFS(char* programName)
{
	static bool isInitialized = false;
	if (isInitialized)
	{
	    return true;
	}
	if (PHYSFS_init(programName) == 0)
	{
	    std::cerr << "Failed to initialize PhysFS: " << PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()) << std::endl;
	    isInitialized = false;
	}
	else
	{
	    isInitialized = true;
	}
	return isInitialized;
}

bool mountLoreBook_ResourcesEmbeddedVFS()
{
	unsigned int outputSize = 0;
	inMemFilesystem = decompressLoreBook_ResourcesZipInMemory(getLoreBook_ResourcesMemoryFile(), getLoreBook_ResourcesMemoryFileSize(), outputSize);
	inMemFilesystemSize = outputSize;
	if (PHYSFS_mountMemory(inMemFilesystem, inMemFilesystemSize, nullptr, "/", "/", 0) == 0)
	{
		std::cerr << "Failed to mount memory filesystem: " << PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()) << std::endl;
		return false;
	}
	return true;
}

std::vector<std::string> listLoreBook_ResourcesEmbeddedFiles(const char* path, bool fullPath)
{
	std::vector<std::string> files;
	char **rc = PHYSFS_enumerateFiles(path);
	char **i;
	for (i = rc; *i != nullptr; i++)
	{
		if (fullPath) {
			std::string full = std::string(path);
			if (full.back() != '/' && full.back() != '\0') full += '/';
			full += *i;
			files.push_back(full);
		} else {
			files.push_back(*i);
		}
	}
	PHYSFS_freeList(rc);
	return files;
}

std::vector<std::string> listLoreBook_ResourcesEmbeddedFilesRelativeTo(const char* path, const char* relativeTo)
{
	std::vector<std::string> files;
	char **rc = PHYSFS_enumerateFiles(path);
	char **i;
	std::string relTo = std::string(relativeTo);
	if (!relTo.empty() && relTo.back() != '/' && relTo.back() != '\0') relTo += '/';
	for (i = rc; *i != nullptr; i++)
	{
		std::string full = std::string(path);
		if (full.back() != '/' && full.back() != '\0') full += '/';
		full += *i;
		if (full.find(relTo) == 0) {
			files.push_back(full.substr(relTo.length()));
		} else {
			files.push_back(full);
		}
	}
	PHYSFS_freeList(rc);
	return files;
}

std::vector<unsigned char> loadLoreBook_ResourcesEmbeddedFile(const char* path)
{
	PHYSFS_file* file = PHYSFS_openRead(path);
	if (file == nullptr)
	{
		throw std::runtime_error("Failed to open file: " + std::string(path) + ": " + PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
	}
	PHYSFS_sint64 fileSize = PHYSFS_fileLength(file);
	std::vector<unsigned char> buffer(fileSize);
	PHYSFS_read(file, buffer.data(), 1, fileSize);
	PHYSFS_close(file);
	return buffer;
}

std::string loadLoreBook_ResourcesEmbeddedFileAsString(const char* path)
{
	PHYSFS_file* file = PHYSFS_openRead(path);
	if (file == nullptr)
	{
		throw std::runtime_error("Failed to open file: " + std::string(path) + ": " + PHYSFS_getErrorByCode(PHYSFS_getLastErrorCode()));
	}
	PHYSFS_sint64 fileSize = PHYSFS_fileLength(file);
	std::vector<char> buffer(fileSize);
	PHYSFS_read(file, buffer.data(), 1, fileSize);
	PHYSFS_close(file);
	return std::string(buffer.begin(), buffer.end());
}

bool existsLoreBook_ResourcesEmbeddedFile(const char* path)
{
	return PHYSFS_exists(path) != 0;
}

bool isLoreBook_ResourcesEmbeddedFolder(const char* path)
{
	return PHYSFS_isDirectory(path) != 0;
}

bool isLoreBook_ResourcesEmbeddedFile(const char* path)
{
	return PHYSFS_isDirectory(path) == 0 && PHYSFS_exists(path) != 0;
}

#include <sys/stat.h>
#include <sys/types.h>
#include <fstream>
#include <vector>
#include <string>
#include <cstring>
#include <cerrno>
#include <filesystem>
bool extractLoreBook_ResourcesTo(const char* embeddedPath, const char* realPath) {
    if (PHYSFS_isDirectory(embeddedPath)) {
        std::filesystem::create_directories(realPath);
        char **rc = PHYSFS_enumerateFiles(embeddedPath);
        char **i;
        for (i = rc; *i != nullptr; i++) {
            std::string subEmbedded = std::string(embeddedPath);
            if (!subEmbedded.empty() && subEmbedded.back() != '/') subEmbedded += '/';
            subEmbedded += *i;
            std::string subReal = std::string(realPath);
            if (!subReal.empty() && subReal.back() != '/') subReal += '/';
            subReal += *i;
            if (!extractLoreBook_ResourcesTo(subEmbedded.c_str(), subReal.c_str())) {
                PHYSFS_freeList(rc);
                return false;
            }
        }
        PHYSFS_freeList(rc);
        return true;
    } else if (PHYSFS_exists(embeddedPath)) {
        PHYSFS_file* file = PHYSFS_openRead(embeddedPath);
        if (!file) return false;
        PHYSFS_sint64 fileSize = PHYSFS_fileLength(file);
        std::vector<unsigned char> buffer(fileSize);
        PHYSFS_read(file, buffer.data(), 1, fileSize);
        PHYSFS_close(file);
        std::ofstream out(realPath, std::ios::binary);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
        out.close();
        return true;
    }
    return false;
}
