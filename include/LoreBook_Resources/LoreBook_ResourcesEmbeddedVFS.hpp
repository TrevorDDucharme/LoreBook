#pragma once
#include "LoreBook_Resources.hpp"
#include <iostream>
#include <stdlib.h>
#include <string.h>
#include <zstd.h>
#include <physfs.h>
#include <vector>
#include <string>
bool initLoreBook_ResourcesEmbeddedVFS(char* programName);
unsigned char* decompressLoreBook_ResourcesZipInMemory(unsigned char* inputBuffer, unsigned int inputBufferSize);
bool mountLoreBook_ResourcesEmbeddedVFS();
std::vector<std::string> listLoreBook_ResourcesEmbeddedFiles(const char* path, bool fullPath = false);
std::vector<std::string> listLoreBook_ResourcesEmbeddedFilesRelativeTo(const char* path, const char* relativeTo);
std::vector<unsigned char> loadLoreBook_ResourcesEmbeddedFile(const char* path);
std::string loadLoreBook_ResourcesEmbeddedFileAsString(const char* path);
bool existsLoreBook_ResourcesEmbeddedFile(const char* path);
bool isLoreBook_ResourcesEmbeddedFolder(const char* path);
bool isLoreBook_ResourcesEmbeddedFile(const char* path);
bool extractLoreBook_ResourcesTo(const char* embeddedPath, const char* realPath);
