#include <Editors/Java/ProgramStructure/ProgramStructure.hpp>
#include <zstd.h>
#include <fstream>
#include <archive.h>
#include <archive_entry.h>
#include <vector>
#include <memory>
#include <sstream>
#include <regex>
#include <algorithm>

ProgramStructure::~ProgramStructure() {
    stopWatching();
}

bool ProgramStructure::parseJavaFile(const std::filesystem::path &filePath, const std::filesystem::path &srcRoot)
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    PLOGD << "Parsing Java file: " << filePath.string();
    
    std::ifstream file(filePath);
    if (!file.is_open()) {
        PLOGE << "Failed to open Java file: " << filePath.string();
        return false;
    }
    
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    
    // Determine package name from filesystem path
    std::string packageName = getPackageNameFromPath(filePath, srcRoot);
    
    // Find or create the package
    Package* package = findOrCreatePackage(packageName);
    if (!package) {
        PLOGE << "Failed to find or create package: " << packageName;
        return false;
    }
    
    // Extract class name from filename
    std::string className = filePath.stem().string();
    
    // Remove existing class definition if it exists
    removeClassFromPackage(packageName, className);
    
    // Create new class
    Class newClass;
    newClass.name = className;
    
    // Basic Java parsing (this is a simplified parser)
    // Parse class declaration
    std::regex classRegex(R"((?:public\s+)?(?:abstract\s+)?(?:final\s+)?class\s+(\w+))");
    std::smatch classMatch;
    if (std::regex_search(content, classMatch, classRegex)) {
        newClass.name = classMatch[1].str();
    }
    
    // Parse methods
    std::regex methodRegex(R"((?:(public|private|protected)\s+)?(?:(static)\s+)?(?:(final)\s+)?(?:(abstract)\s+)?(?:(synchronized)\s+)?(?:(native)\s+)?(?:(strictfp)\s+)?(\w+)\s+(\w+)\s*\([^)]*\))");
    std::sregex_iterator methodIter(content.begin(), content.end(), methodRegex);
    std::sregex_iterator methodEnd;
    
    for (; methodIter != methodEnd; ++methodIter) {
        const std::smatch& match = *methodIter;
        Method method;
        
        method.returnType = match[8].str();
        method.name = match[9].str();
        
        // Parse access modifiers
        std::string visibility = match[1].str();
        method.isPublic = (visibility == "public");
        method.isPrivate = (visibility == "private");
        method.isProtected = (visibility == "protected");
        
        method.isStatic = !match[2].str().empty();
        method.isFinal = !match[3].str().empty();
        method.isAbstract = !match[4].str().empty();
        method.isSynchronized = !match[5].str().empty();
        method.isNative = !match[6].str().empty();
        method.isStrictfp = !match[7].str().empty();
        
        newClass.methods.push_back(method);
    }
    
    // Parse fields/variables
    std::regex fieldRegex(R"((?:(public|private|protected)\s+)?(?:(static)\s+)?(?:(final)\s+)?(?:(transient)\s+)?(?:(volatile)\s+)?(\w+)\s+(\w+)(?:\s*=\s*([^;]+))?\s*;)");
    std::sregex_iterator fieldIter(content.begin(), content.end(), fieldRegex);
    std::sregex_iterator fieldEnd;
    
    for (; fieldIter != fieldEnd; ++fieldIter) {
        const std::smatch& match = *fieldIter;
        Variable variable;
        
        variable.type = match[6].str();
        variable.name = match[7].str();
        variable.value = match[8].str();
        
        // Parse access modifiers
        std::string visibility = match[1].str();
        variable.isPublic = (visibility == "public");
        variable.isPrivate = (visibility == "private");
        variable.isProtected = (visibility == "protected");
        
        variable.isStatic = !match[2].str().empty();
        variable.isFinal = !match[3].str().empty();
        variable.isTransient = !match[4].str().empty();
        variable.isVolatile = !match[5].str().empty();
        
        newClass.variables.push_back(variable);
    }
    
    // Add the class to the package
    package->classes.push_back(newClass);
    
    PLOGD << "Successfully parsed Java file: " << filePath.string() << " (class: " << newClass.name << ", package: " << packageName << ")";
    return true;
}

bool ProgramStructure::parseClassFile(const std::filesystem::path &filePath, const std::filesystem::path &classRoot)
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    PLOGD << "Parsing Class file: " << filePath.string();
    
    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        PLOGE << "Failed to open class file: " << filePath.string();
        return false;
    }
    
    // Read the entire file into memory
    file.seekg(0, std::ios::end);
    std::streamsize size = file.tellg();
    file.seekg(0, std::ios::beg);
    
    std::vector<uint8_t> buffer(size);
    if (!file.read(reinterpret_cast<char*>(buffer.data()), size)) {
        PLOGE << "Failed to read class file: " << filePath.string();
        return false;
    }
    file.close();
    
    try {
        return parseClassFileData(buffer, filePath, classRoot);
    } catch (const std::exception& e) {
        PLOGE << "Error parsing class file " << filePath.string() << ": " << e.what();
        return false;
    }
}

bool ProgramStructure::parseClassFileData(const std::vector<uint8_t>& data, const std::filesystem::path &filePath, const std::filesystem::path &classRoot)
{
    if (data.size() < 10) {
        PLOGE << "Class file too small: " << filePath.string();
        return false;
    }
    
    size_t offset = 0;
    
    // Read magic number (0xCAFEBABE)
    uint32_t magic = readU4(data, offset);
    if (magic != 0xCAFEBABE) {
        PLOGE << "Invalid class file magic number: " << std::hex << magic;
        return false;
    }
    
    // Read version numbers
    uint16_t minorVersion = readU2(data, offset);
    uint16_t majorVersion = readU2(data, offset);
    
    PLOGD << "Class file version: " << majorVersion << "." << minorVersion;
    
    // Read constant pool
    uint16_t constantPoolCount = readU2(data, offset);
    std::vector<ConstantPoolEntry> constantPool(constantPoolCount);
    
    // Parse constant pool (index 0 is not used)
    for (uint16_t i = 1; i < constantPoolCount; ++i) {
        if (!parseConstantPoolEntry(data, offset, constantPool[i])) {
            PLOGE << "Failed to parse constant pool entry " << i;
            return false;
        }
        
        // Long and Double constants take up two slots
        if (constantPool[i].tag == 5 || constantPool[i].tag == 6) {
            ++i; // Skip next slot
        }
    }
    
    // Read access flags
    uint16_t accessFlags = readU2(data, offset);
    
    // Read class references
    uint16_t thisClass = readU2(data, offset);
    uint16_t superClass = readU2(data, offset);
    
    // Read interfaces
    uint16_t interfacesCount = readU2(data, offset);
    std::vector<uint16_t> interfaces(interfacesCount);
    for (uint16_t i = 0; i < interfacesCount; ++i) {
        interfaces[i] = readU2(data, offset);
    }
    
    // Extract package and class names
    std::string packageName = getPackageNameFromPath(filePath, classRoot);
    std::string className = getClassNameFromConstantPool(constantPool, thisClass);
    
    // Remove package prefix from class name if present
    size_t lastSlash = className.find_last_of('/');
    if (lastSlash != std::string::npos) {
        className = className.substr(lastSlash + 1);
    }
    
    Package* package = findOrCreatePackage(packageName);
    if (!package) {
        PLOGE << "Failed to find or create package: " << packageName;
        return false;
    }
    
    // Remove existing class definition if it exists
    removeClassFromPackage(packageName, className);
    
    // Create new class
    Class newClass;
    newClass.name = className;
    
    // Set inheritance information
    if (superClass != 0) {
        std::string superClassName = getClassNameFromConstantPool(constantPool, superClass);
        size_t lastSlash = superClassName.find_last_of('/');
        if (lastSlash != std::string::npos) {
            superClassName = superClassName.substr(lastSlash + 1);
        }
        newClass.extendsClass = superClassName;
    }
    
    // Set implemented interfaces
    for (uint16_t interfaceIndex : interfaces) {
        std::string interfaceName = getClassNameFromConstantPool(constantPool, interfaceIndex);
        size_t lastSlash = interfaceName.find_last_of('/');
        if (lastSlash != std::string::npos) {
            interfaceName = interfaceName.substr(lastSlash + 1);
        }
        newClass.implementsInterfaces.push_back(interfaceName);
    }
    
    // Read fields
    uint16_t fieldsCount = readU2(data, offset);
    for (uint16_t i = 0; i < fieldsCount; ++i) {
        Variable field;
        if (parseField(data, offset, constantPool, field)) {
            newClass.variables.push_back(field);
        }
    }
    
    // Read methods
    uint16_t methodsCount = readU2(data, offset);
    for (uint16_t i = 0; i < methodsCount; ++i) {
        Method method;
        if (parseMethod(data, offset, constantPool, method)) {
            newClass.methods.push_back(method);
        }
    }
    
    // Skip attributes for now (not needed for basic completion)
    
    package->classes.push_back(newClass);
    
    PLOGD << "Successfully parsed Class file: " << filePath.string() 
          << " (class: " << newClass.name << ", package: " << packageName 
          << ", methods: " << newClass.methods.size() 
          << ", fields: " << newClass.variables.size() << ")";
    return true;
}

bool ProgramStructure::parseArchiveFile(const std::filesystem::path &filePath)
{
    struct archive *a;
    struct archive_entry *entry;
    int r;

    // Create new archive object
    a = archive_read_new();
    if (!a) {
        PLOGE << "Failed to create archive object for: " << filePath.string();
        return false;
    }

    // Enable all filters and formats
    archive_read_support_filter_all(a);
    archive_read_support_format_all(a);

    // Open the archive file
    r = archive_read_open_filename(a, filePath.c_str(), 10240);
    if (r != ARCHIVE_OK) {
        PLOGE << "Failed to open archive: " << filePath.string() << " - " << archive_error_string(a);
        archive_read_free(a);
        return false;
    }

    bool success = true;
    
    // Process each entry in the archive
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        const char* entryName = archive_entry_pathname(entry);
        if (!entryName) {
            continue;
        }

        std::string entryPath(entryName);
        std::filesystem::path entryFilePath(entryPath);
        
        // Check if this is a .class or .java file
        if (entryFilePath.extension() == ".class" || entryFilePath.extension() == ".java") {
            PLOGD << "Found " << entryFilePath.extension().string() << " file in archive: " << entryPath;
            
            // Get the size of the entry
            la_int64_t size = archive_entry_size(entry);
            if (size > 0) {
                // Read the file content into memory
                std::vector<char> buffer(size);
                la_ssize_t bytesRead = archive_read_data(a, buffer.data(), size);
                
                if (bytesRead != size) {
                    PLOGE << "Failed to read complete entry: " << entryPath;
                    success = false;
                    continue;
                }

                // Parse the content directly from memory for .java files
                if (entryFilePath.extension() == ".java") {
                    std::string content(buffer.begin(), buffer.end());
                    
                    // Determine package name from the archive path structure
                    std::string packageName = getPackageNameFromArchivePath(entryFilePath);
                    
                    // Find or create the package
                    Package* package = findOrCreatePackage(packageName);
                    if (!package) {
                        PLOGE << "Failed to find or create package: " << packageName;
                        success = false;
                        continue;
                    }
                    
                    // Extract class name from filename
                    std::string className = entryFilePath.stem().string();
                    
                    // Remove existing class definition if it exists
                    removeClassFromPackage(packageName, className);
                    
                    // Parse the Java content and create the class
                    if (!parseJavaContent(content, className, *package)) {
                        PLOGE << "Failed to parse Java content from archive: " << entryPath;
                        success = false;
                    }
                    
                } else if (entryFilePath.extension() == ".class") {
                    // For .class files, we'll create a minimal class entry based on the path
                    std::string packageName = getPackageNameFromArchivePath(entryFilePath);
                    std::string className = entryFilePath.stem().string();
                    
                    Package* package = findOrCreatePackage(packageName);
                    if (!package) {
                        PLOGE << "Failed to find or create package: " << packageName;
                        success = false;
                        continue;
                    }
                    
                    // Remove existing class definition if it exists
                    removeClassFromPackage(packageName, className);
                    
                    // Create new class (minimal information from .class file)
                    Class newClass;
                    newClass.name = className;
                    
                    // TODO: Implement proper .class file parsing to extract methods, fields, etc.
                    // This would involve parsing the Java bytecode format from the buffer
                    
                    package->classes.push_back(newClass);
                    
                    PLOGD << "Successfully parsed Class file from archive: " << entryPath << " (class: " << newClass.name << ", package: " << packageName << ")";
                }
            }
        } else {
            // Skip this entry by reading its data (required by libarchive)
            archive_read_data_skip(a);
        }
    }

    // Close and free the archive
    r = archive_read_close(a);
    if (r != ARCHIVE_OK) {
        PLOGE << "Warning: Failed to properly close archive: " << archive_error_string(a);
    }
    
    archive_read_free(a);
    
    return success;
}

bool ProgramStructure::parseJarFile(const std::filesystem::path &filePath)
{
    // JAR files are essentially ZIP files, so we can use the same archive processing
    PLOGD << "Parsing JAR file: " << filePath.string();
    return parseArchiveFile(filePath);
}

bool ProgramStructure::parseZipFile(const std::filesystem::path &filePath)
{
    // Use libarchive to process ZIP files
    PLOGD << "Parsing ZIP file: " << filePath.string();
    return parseArchiveFile(filePath);
}

bool ProgramStructure::parseFile(const std::filesystem::path &filePath, const std::filesystem::path &srcRoot)
{
    if(!std::filesystem::exists(filePath))
    {
        PLOGE << "File does not exist: " << filePath.string();
        return false;
    }
    
    // Update file timestamp
    fileTimestamps[filePath.string()] = std::filesystem::last_write_time(filePath);
    
    //check the file extension .java, .class, .jar, .zip
    if (filePath.extension() == ".java")
    {
        return parseJavaFile(filePath, srcRoot);
    }else if (filePath.extension() == ".class")
    {
        return parseClassFile(filePath, srcRoot);
    }else if (filePath.extension() == ".jar")
    {
        return parseJarFile(filePath);
    }else if (filePath.extension() == ".zip")
    {
        return parseZipFile(filePath);
    }

    PLOGD << "Skipping unsupported file type: " << filePath.extension().string();
    return true; // Return true for unsupported files to not fail the whole process
}

bool ProgramStructure::parseDirectory(const std::filesystem::path &dirPath)
{
    if(!std::filesystem::exists(dirPath) || !std::filesystem::is_directory(dirPath))
    {
        PLOGE << "Directory does not exist or is not a directory: " << dirPath.string();
        return false;
    }    
    
    PLOGD << "Parsing directory: " << dirPath.string();
    
    for (const auto &entry : std::filesystem::recursive_directory_iterator(dirPath)){
        if (entry.is_regular_file())
        {
            if (!parseFile(entry.path(), dirPath))
            {
                PLOGE << "Failed to parse file: " << entry.path().string();
                return false;
            }
        }
    }
    return true;
}

// File watching methods
void ProgramStructure::startWatching(const std::vector<std::filesystem::path> &directories)
{
    if (isWatching.load()) {
        PLOGW << "File watcher is already running";
        return;
    }
    
    watchedDirectories = directories;
    
    // Initialize file timestamps for all existing files
    for (const auto& dir : directories) {
        if (std::filesystem::exists(dir) && std::filesystem::is_directory(dir)) {
            parseDirectory(dir); // Parse initially
            
            for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
                if (entry.is_regular_file()) {
                    auto ext = entry.path().extension();
                    if (ext == ".java" || ext == ".class" || ext == ".jar" || ext == ".zip") {
                        fileTimestamps[entry.path().string()] = std::filesystem::last_write_time(entry.path());
                    }
                }
            }
        }
    }
    
    isWatching.store(true);
    watcherThread = std::thread(&ProgramStructure::watchDirectories, this);
    
    PLOGI << "Started watching " << directories.size() << " directories for file changes";
}

void ProgramStructure::stopWatching()
{
    if (!isWatching.load()) {
        return;
    }
    
    isWatching.store(false);
    
    if (watcherThread.joinable()) {
        watcherThread.join();
    }
    
    PLOGI << "Stopped file watching";
}

void ProgramStructure::watchDirectories()
{
    while (isWatching.load()) {
        try {
            for (const auto& dir : watchedDirectories) {
                if (!std::filesystem::exists(dir) || !std::filesystem::is_directory(dir)) {
                    continue;
                }
                
                for (const auto& entry : std::filesystem::recursive_directory_iterator(dir)) {
                    if (!entry.is_regular_file()) {
                        continue;
                    }
                    
                    auto ext = entry.path().extension();
                    if (ext != ".java" && ext != ".class" && ext != ".jar" && ext != ".zip") {
                        continue;
                    }
                    
                    std::string filePath = entry.path().string();
                    auto currentTime = std::filesystem::last_write_time(entry.path());
                    
                    auto it = fileTimestamps.find(filePath);
                    if (it == fileTimestamps.end()) {
                        // New file
                        PLOGI << "New file detected: " << filePath;
                        processFileChange(entry.path());
                    } else if (it->second != currentTime) {
                        // Modified file
                        PLOGI << "Modified file detected: " << filePath;
                        processFileChange(entry.path());
                    }
                }
                
                // Check for deleted files
                auto it = fileTimestamps.begin();
                while (it != fileTimestamps.end()) {
                    std::filesystem::path filePath(it->first);
                    if (!std::filesystem::exists(filePath)) {
                        PLOGI << "Deleted file detected: " << filePath.string();
                        removeFileFromCache(filePath);
                        it = fileTimestamps.erase(it);
                    } else {
                        ++it;
                    }
                }
            }
        } catch (const std::exception& e) {
            PLOGE << "Error in file watcher: " << e.what();
        }
        
        // Sleep for a short time before checking again
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

void ProgramStructure::processFileChange(const std::filesystem::path &filePath)
{
    // Find the appropriate source root for this file
    std::filesystem::path srcRoot;
    for (const auto& watchedDir : watchedDirectories) {
        if (filePath.string().find(watchedDir.string()) == 0) {
            srcRoot = watchedDir;
            break;
        }
    }
    
    if (srcRoot.empty()) {
        PLOGE << "Could not determine source root for file: " << filePath.string();
        return;
    }
    
    // Parse the changed file
    parseFile(filePath, srcRoot);
}

void ProgramStructure::removeFileFromCache(const std::filesystem::path &filePath)
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    // Find the appropriate source root for this file
    std::filesystem::path srcRoot;
    for (const auto& watchedDir : watchedDirectories) {
        if (filePath.string().find(watchedDir.string()) == 0) {
            srcRoot = watchedDir;
            break;
        }
    }
    
    if (srcRoot.empty()) {
        return;
    }
    
    std::string packageName = getPackageNameFromPath(filePath, srcRoot);
    std::string className = filePath.stem().string();
    
    removeClassFromPackage(packageName, className);
    
    PLOGD << "Removed class " << className << " from package " << packageName;
}

// Helper methods
std::string ProgramStructure::getPackageNameFromPath(const std::filesystem::path &filePath, const std::filesystem::path &srcRoot)
{
    std::filesystem::path relativePath = std::filesystem::relative(filePath.parent_path(), srcRoot);
    
    if (relativePath.empty() || relativePath == ".") {
        return ""; // Default package
    }
    
    std::string packageName = relativePath.string();
    
    // Replace path separators with dots
    std::replace(packageName.begin(), packageName.end(), '/', '.');
    std::replace(packageName.begin(), packageName.end(), '\\', '.');
    
    return packageName;
}

std::string ProgramStructure::getPackageNameFromArchivePath(const std::filesystem::path &archivePath)
{
    std::filesystem::path parentPath = archivePath.parent_path();
    
    if (parentPath.empty() || parentPath == ".") {
        return ""; // Default package
    }
    
    std::string packageName = parentPath.string();
    
    // Replace path separators with dots
    std::replace(packageName.begin(), packageName.end(), '/', '.');
    std::replace(packageName.begin(), packageName.end(), '\\', '.');
    
    return packageName;
}

bool ProgramStructure::parseJavaContent(const std::string &content, const std::string &className, Package &package)
{
    // Create new class
    Class newClass;
    newClass.name = className;
    
    // Parse class declaration with inheritance
    std::regex classRegex(R"((?:public\s+)?(?:abstract\s+)?(?:final\s+)?class\s+(\w+)(?:\s+extends\s+(\w+))?(?:\s+implements\s+([^{]+))?)");
    std::smatch classMatch;
    if (std::regex_search(content, classMatch, classRegex)) {
        newClass.name = classMatch[1].str();
        
        // Extract parent class
        if (classMatch[2].matched) {
            newClass.extendsClass = classMatch[2].str();
        }
        
        // Extract implemented interfaces
        if (classMatch[3].matched) {
            std::string interfaceList = classMatch[3].str();
            std::istringstream iss(interfaceList);
            std::string interface;
            while (std::getline(iss, interface, ',')) {
                // Trim whitespace
                interface.erase(0, interface.find_first_not_of(" \t"));
                interface.erase(interface.find_last_not_of(" \t") + 1);
                if (!interface.empty()) {
                    newClass.implementsInterfaces.push_back(interface);
                }
            }
        }
    }
    
    // Parse methods
    std::regex methodRegex(R"((?:(public|private|protected)\s+)?(?:(static)\s+)?(?:(final)\s+)?(?:(abstract)\s+)?(?:(synchronized)\s+)?(?:(native)\s+)?(?:(strictfp)\s+)?(\w+)\s+(\w+)\s*\([^)]*\))");
    std::sregex_iterator methodIter(content.begin(), content.end(), methodRegex);
    std::sregex_iterator methodEnd;
    
    for (; methodIter != methodEnd; ++methodIter) {
        const std::smatch& match = *methodIter;
        Method method;
        
        method.returnType = match[8].str();
        method.name = match[9].str();
        
        // Parse access modifiers
        std::string visibility = match[1].str();
        method.isPublic = (visibility == "public");
        method.isPrivate = (visibility == "private");
        method.isProtected = (visibility == "protected");
        
        method.isStatic = !match[2].str().empty();
        method.isFinal = !match[3].str().empty();
        method.isAbstract = !match[4].str().empty();
        method.isSynchronized = !match[5].str().empty();
        method.isNative = !match[6].str().empty();
        method.isStrictfp = !match[7].str().empty();
        
        newClass.methods.push_back(method);
    }
    
    // Parse fields/variables
    std::regex fieldRegex(R"((?:(public|private|protected)\s+)?(?:(static)\s+)?(?:(final)\s+)?(?:(transient)\s+)?(?:(volatile)\s+)?(\w+)\s+(\w+)(?:\s*=\s*([^;]+))?\s*;)");
    std::sregex_iterator fieldIter(content.begin(), content.end(), fieldRegex);
    std::sregex_iterator fieldEnd;
    
    for (; fieldIter != fieldEnd; ++fieldIter) {
        const std::smatch& match = *fieldIter;
        Variable variable;
        
        variable.type = match[6].str();
        variable.name = match[7].str();
        variable.value = match[8].str();
        
        // Parse access modifiers
        std::string visibility = match[1].str();
        variable.isPublic = (visibility == "public");
        variable.isPrivate = (visibility == "private");
        variable.isProtected = (visibility == "protected");
        
        variable.isStatic = !match[2].str().empty();
        variable.isFinal = !match[3].str().empty();
        variable.isTransient = !match[4].str().empty();
        variable.isVolatile = !match[5].str().empty();
        
        newClass.variables.push_back(variable);
    }
    
    // Add the class to the package
    package.classes.push_back(newClass);
    
    PLOGD << "Successfully parsed Java content (class: " << newClass.name << ", package: " << package.name << ")";
    return true;
}

Package* ProgramStructure::findOrCreatePackage(const std::string &packageName)
{
    if (packageName.empty()) {
        // Default package - find or create root package with empty name
        for (auto& pkg : packages) {
            if (pkg.name.empty()) {
                return &pkg;
            }
        }
        // Create default package
        Package defaultPackage;
        defaultPackage.name = "";
        packages.push_back(defaultPackage);
        return &packages.back();
    }
    
    // Split package name by dots to create hierarchy
    std::vector<std::string> packageParts;
    std::stringstream ss(packageName);
    std::string part;
    while (std::getline(ss, part, '.')) {
        packageParts.push_back(part);
    }
    
    // Start from root packages
    Package* currentPackage = nullptr;
    std::vector<Package>* currentPackageList = &packages;
    
    for (size_t i = 0; i < packageParts.size(); ++i) {
        const std::string& partName = packageParts[i];
        
        // Look for existing package at this level by comparing just the immediate name
        Package* foundPackage = nullptr;
        for (auto& pkg : *currentPackageList) {
            if (pkg.name == partName) {
                foundPackage = &pkg;
                break;
            }
        }
        
        if (foundPackage) {
            currentPackage = foundPackage;
            currentPackageList = &currentPackage->subPackages;
        } else {
            // Create new package with just the immediate name
            Package newPackage;
            newPackage.name = partName; // Only store the immediate segment
            currentPackageList->push_back(newPackage);
            currentPackage = &currentPackageList->back();
            currentPackageList = &currentPackage->subPackages;
        }
    }
    
    return currentPackage;
}

Package* ProgramStructure::findPackageInHierarchy(const std::string &packageName, std::vector<Package> &packageList)
{
    if (packageName.empty()) {
        // Look for default package (empty name)
        for (auto& pkg : packageList) {
            if (pkg.name.empty()) {
                return &pkg;
            }
        }
        return nullptr;
    }
    
    // Split package name by dots to traverse hierarchy
    std::vector<std::string> packageParts;
    std::stringstream ss(packageName);
    std::string part;
    while (std::getline(ss, part, '.')) {
        packageParts.push_back(part);
    }
    
    // Traverse the hierarchy
    std::vector<Package>* currentList = &packageList;
    Package* currentPackage = nullptr;
    
    for (const std::string& partName : packageParts) {
        bool found = false;
        for (auto& pkg : *currentList) {
            if (pkg.name == partName) {
                currentPackage = &pkg;
                currentList = &pkg.subPackages;
                found = true;
                break;
            }
        }
        if (!found) {
            return nullptr;
        }
    }
    
    return currentPackage;
}

const Package* ProgramStructure::findPackageInHierarchyConst(const std::string &packageName, const std::vector<Package> &packageList) const
{
    if (packageName.empty()) {
        // Look for default package (empty name)
        for (const auto& pkg : packageList) {
            if (pkg.name.empty()) {
                return &pkg;
            }
        }
        return nullptr;
    }
    
    // Split package name by dots to traverse hierarchy
    std::vector<std::string> packageParts;
    std::stringstream ss(packageName);
    std::string part;
    while (std::getline(ss, part, '.')) {
        packageParts.push_back(part);
    }
    
    // Traverse the hierarchy
    const std::vector<Package>* currentList = &packageList;
    const Package* currentPackage = nullptr;
    
    for (const std::string& partName : packageParts) {
        bool found = false;
        for (const auto& pkg : *currentList) {
            if (pkg.name == partName) {
                currentPackage = &pkg;
                currentList = &pkg.subPackages;
                found = true;
                break;
            }
        }
        if (!found) {
            return nullptr;
        }
    }
    
    return currentPackage;
}

void ProgramStructure::removeClassFromPackage(const std::string &packageName, const std::string &className)
{
    Package* package = findPackageInHierarchy(packageName, packages);
    if (package) {
        auto it = std::remove_if(package->classes.begin(), package->classes.end(),
            [&className](const Class& cls) { return cls.name == className; });
        package->classes.erase(it, package->classes.end());
    }
}

// Data access methods (existing methods with thread safety)
void ProgramStructure::addType(const Type &type)
{
    std::lock_guard<std::mutex> lock(dataMutex);
    types.push_back(type);
}

void ProgramStructure::addPackage(const Package &package)
{
    std::lock_guard<std::mutex> lock(dataMutex);
    packages.push_back(package);
}

bool ProgramStructure::validType(const std::string &typeName) const
{
    std::lock_guard<std::mutex> lock(dataMutex);
    return std::find(types.begin(), types.end(), typeName) != types.end();
}

bool ProgramStructure::validPackage(const std::string &packageName) const
{
    std::lock_guard<std::mutex> lock(dataMutex);
    return findPackageInHierarchyConst(packageName, packages) != nullptr;
}

const std::vector<Type>& ProgramStructure::getTypes() const
{
    std::lock_guard<std::mutex> lock(dataMutex);
    return types;
}

const std::vector<Package>& ProgramStructure::getPackages() const
{
    std::lock_guard<std::mutex> lock(dataMutex);
    return packages;
}

std::vector<const Package*> ProgramStructure::getAllPackagesFlat() const
{
    std::lock_guard<std::mutex> lock(dataMutex);
    std::vector<const Package*> allPackages;
    
    std::function<void(const std::vector<Package>&)> collectPackages = [&](const std::vector<Package>& packageList) {
        for (const auto& pkg : packageList) {
            allPackages.push_back(&pkg);
            collectPackages(pkg.subPackages);
        }
    };
    
    collectPackages(packages);
    return allPackages;
}

std::string ProgramStructure::getFullPackageName(const Package* package) const
{
    if (!package) {
        return "";
    }
    
    // Build full package name by traversing up the hierarchy
    std::vector<std::string> parts;
    
    std::function<bool(const std::vector<Package>&, const Package*, std::vector<std::string>&)> findPath = 
        [&](const std::vector<Package>& packageList, const Package* target, std::vector<std::string>& currentPath) -> bool {
            for (const auto& pkg : packageList) {
                currentPath.push_back(pkg.name);
                if (&pkg == target) {
                    parts = currentPath;
                    return true;
                }
                if (findPath(pkg.subPackages, target, currentPath)) {
                    return true;
                }
                currentPath.pop_back();
            }
            return false;
        };
    
    std::vector<std::string> currentPath;
    findPath(packages, package, currentPath);
    
    // Join parts with dots, skipping empty names (default package)
    std::string result;
    for (const auto& part : parts) {
        if (!part.empty()) {
            if (!result.empty()) {
                result += ".";
            }
            result += part;
        }
    }
    
    return result.empty() ? "(default)" : result;
}

const Type& ProgramStructure::getType(const std::string &typeName) const
{
    std::lock_guard<std::mutex> lock(dataMutex);
    auto it = std::find(types.begin(), types.end(), typeName);
    if (it != types.end()) {
        return *it;
    }
    static Type empty;
    return empty;
}

const Package& ProgramStructure::getPackage(const std::string &packageName) const
{
    std::lock_guard<std::mutex> lock(dataMutex);
    const Package* package = findPackageInHierarchyConst(packageName, packages);
    if (package) {
        return *package;
    }
    static Package empty;
    return empty;
}

const Class* ProgramStructure::findClass(const std::string &className, const std::string &packageName) const
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    // Helper function to collect all packages recursively without additional locking
    std::vector<const Package*> allPackages;
    std::function<void(const std::vector<Package>&)> collectPackages = [&](const std::vector<Package>& packageList) {
        for (const auto& pkg : packageList) {
            allPackages.push_back(&pkg);
            collectPackages(pkg.subPackages);
        }
    };
    collectPackages(packages);
    
    for (const auto* package : allPackages) {
        // If packageName is specified, only search in that package
        if (!packageName.empty()) {
            std::string fullPackageName = getFullPackageName(package);
            if (fullPackageName != packageName) {
                continue;
            }
        }
        
        for (const auto& cls : package->classes) {
            if (cls.name == className) {
                return &cls;
            }
        }
    }
    
    return nullptr;
}

std::vector<const Method*> ProgramStructure::getAllInheritedMethods(const Class* cls) const
{
    std::vector<const Method*> allMethods;
    if (!cls) return allMethods;
    
    // Add methods from this class
    for (const auto& method : cls->methods) {
        allMethods.push_back(&method);
    }
    
    // Add methods from parent class
    if (!cls->extendsClass.empty()) {
        const Class* parentClass = findClass(cls->extendsClass, ""); // Search globally first
        if (!parentClass) {
            // Try to find in other packages (simplified lookup)
            parentClass = findClass(cls->extendsClass);
        }
        
        if (parentClass) {
            auto parentMethods = getAllInheritedMethods(parentClass);
            for (const auto* method : parentMethods) {
                // Only add if not private and not already overridden
                if (!method->isPrivate) {
                    bool isOverridden = false;
                    for (const auto& thisMethod : cls->methods) {
                        if (thisMethod.name == method->name) {
                            isOverridden = true;
                            break;
                        }
                    }
                    if (!isOverridden) {
                        allMethods.push_back(method);
                    }
                }
            }
        }
    }
    
    // Add methods from implemented interfaces
    for (const auto& interfaceName : cls->implementsInterfaces) {
        const Class* interfaceClass = findClass(interfaceName, "");
        if (!interfaceClass) {
            interfaceClass = findClass(interfaceName);
        }
        
        if (interfaceClass) {
            for (const auto& method : interfaceClass->methods) {
                allMethods.push_back(&method);
            }
        }
    }
    
    return allMethods;
}

std::vector<const Variable*> ProgramStructure::getAllInheritedVariables(const Class* cls) const
{
    std::vector<const Variable*> allVariables;
    if (!cls) return allVariables;
    
    // Add variables from this class
    for (const auto& variable : cls->variables) {
        allVariables.push_back(&variable);
    }
    
    // Add variables from parent class
    if (!cls->extendsClass.empty()) {
        const Class* parentClass = findClass(cls->extendsClass, "");
        if (!parentClass) {
            parentClass = findClass(cls->extendsClass);
        }
        
        if (parentClass) {
            auto parentVariables = getAllInheritedVariables(parentClass);
            for (const auto* variable : parentVariables) {
                // Only add if not private
                if (!variable->isPrivate) {
                    allVariables.push_back(variable);
                }
            }
        }
    }
    
    return allVariables;
}

bool ProgramStructure::parseContentToPackage(const std::string &content, const std::string &className, const std::string &packageName)
{
    std::lock_guard<std::mutex> lock(dataMutex);
    
    Package* package = findOrCreatePackage(packageName);
    if (!package) {
        return false;
    }
    
    // Remove existing class definition
    removeClassFromPackage(packageName, className);
    
    // Parse the content
    return parseJavaContent(content, className, *package);
}

bool ProgramStructure::updateClassFromLiveContent(const std::string &content, const std::string &className, const std::string &packageName)
{
    // Only update if content has valid syntax
    if (!hasValidSyntax(content)) {
        return false;
    }
    
    return parseContentToPackage(content, className, packageName);
}

bool ProgramStructure::hasValidSyntax(const std::string &content) const
{
    // Simple syntax validation - check for balanced braces, parentheses, and brackets
    int braceCount = 0;
    int parenCount = 0;
    int bracketCount = 0;
    bool inString = false;
    bool inChar = false;
    bool inSingleLineComment = false;
    bool inMultiLineComment = false;
    
    for (size_t i = 0; i < content.length(); ++i) {
        char c = content[i];
        char nextC = (i + 1 < content.length()) ? content[i + 1] : '\0';
        char prevC = (i > 0) ? content[i - 1] : '\0';
        
        // Handle string and character literals
        if (!inSingleLineComment && !inMultiLineComment) {
            if (c == '"' && prevC != '\\') {
                inString = !inString;
                continue;
            }
            if (c == '\'' && prevC != '\\') {
                inChar = !inChar;
                continue;
            }
        }
        
        // Handle comments
        if (!inString && !inChar) {
            if (c == '/' && nextC == '/') {
                inSingleLineComment = true;
                continue;
            }
            if (c == '/' && nextC == '*') {
                inMultiLineComment = true;
                continue;
            }
            if (c == '*' && nextC == '/') {
                inMultiLineComment = false;
                i++; // Skip the '/'
                continue;
            }
            if (c == '\n') {
                inSingleLineComment = false;
            }
        }
        
        // Count brackets only when not in strings or comments
        if (!inString && !inChar && !inSingleLineComment && !inMultiLineComment) {
            switch (c) {
                case '{': braceCount++; break;
                case '}': braceCount--; break;
                case '(': parenCount++; break;
                case ')': parenCount--; break;
                case '[': bracketCount++; break;
                case ']': bracketCount--; break;
            }
            
            // Early exit if counts go negative (unmatched closing brackets)
            if (braceCount < 0 || parenCount < 0 || bracketCount < 0) {
                return false;
            }
        }
    }
    
    // All counts should be zero for valid syntax
    return braceCount == 0 && parenCount == 0 && bracketCount == 0;
}

ProgramStructure::CursorContext ProgramStructure::analyzeCursorContext(const std::string &content, size_t line, size_t column) const
{
    CursorContext context;
    
    // Split content into lines
    std::vector<std::string> lines;
    std::stringstream ss(content);
    std::string currentLine;
    while (std::getline(ss, currentLine)) {
        lines.push_back(currentLine);
    }
    
    if (line >= lines.size()) {
        return context;
    }
    
    // Parse package and class declarations
    for (const auto& lineContent : lines) {
        std::regex packageRegex(R"(package\s+([\w\.]+)\s*;)");
        std::smatch packageMatch;
        if (std::regex_search(lineContent, packageMatch, packageRegex)) {
            context.currentPackageName = packageMatch[1].str();
        }
        
        std::regex classRegex(R"((?:public\s+)?(?:abstract\s+)?(?:final\s+)?class\s+(\w+))");
        std::smatch classMatch;
        if (std::regex_search(lineContent, classMatch, classRegex)) {
            context.currentClassName = classMatch[1].str();
        }
    }
    
    // Analyze cursor position
    int braceDepth = 0;
    bool inClassBody = false;
    bool inMethodBody = false;
    std::string currentMethod;
    bool currentMethodIsStatic = false;
    
    for (size_t i = 0; i <= line && i < lines.size(); ++i) {
        const std::string& lineContent = lines[i];
        size_t endCol = (i == line) ? std::min(column, lineContent.length()) : lineContent.length();
        
        for (size_t j = 0; j < endCol; ++j) {
            char c = lineContent[j];
            
            if (c == '{') {
                braceDepth++;
                if (!inClassBody && lineContent.find("class") != std::string::npos) {
                    inClassBody = true;
                    context.isInClass = true;
                }
            } else if (c == '}') {
                braceDepth--;
                if (braceDepth == 1 && inMethodBody) {
                    inMethodBody = false;
                    currentMethod.clear();
                    currentMethodIsStatic = false;
                } else if (braceDepth == 0 && inClassBody) {
                    inClassBody = false;
                    context.isInClass = false;
                }
            }
        }
        
        // Check for method declarations
        if (inClassBody && !inMethodBody) {
            std::regex methodRegex(R"((?:(public|private|protected)\s+)?(?:(static)\s+)?(?:(final)\s+)?(?:(abstract)\s+)?(?:(synchronized)\s+)?(?:(native)\s+)?(?:(strictfp)\s+)?(\w+)\s+(\w+)\s*\([^)]*\)\s*\{?)");
            std::smatch methodMatch;
            if (std::regex_search(lineContent, methodMatch, methodRegex)) {
                currentMethod = methodMatch[9].str();
                currentMethodIsStatic = !methodMatch[2].str().empty();
                if (lineContent.find('{') != std::string::npos) {
                    inMethodBody = true;
                    context.isInMethod = true;
                    context.currentMethodName = currentMethod;
                    context.hasStaticContext = currentMethodIsStatic;
                }
            }
        }
    }
    
    context.isInClassScope = inClassBody;
    if (inMethodBody) {
        context.isInMethod = true;
        context.currentMethodName = currentMethod;
        context.hasStaticContext = currentMethodIsStatic;
    }
    
    return context;
}

std::vector<const Method*> ProgramStructure::getAccessibleMethods(const Class* cls, bool staticContext) const
{
    std::vector<const Method*> methods;
    if (!cls) return methods;
    
    // Get all inherited methods
    auto allMethods = getAllInheritedMethods(cls);
    
    for (const auto* method : allMethods) {
        // In static context, only static methods are accessible
        if (staticContext && !method->isStatic) {
            continue;
        }
        
        // All methods are accessible in instance context
        methods.push_back(method);
    }
    
    return methods;
}

std::vector<const Variable*> ProgramStructure::getAccessibleVariables(const Class* cls, bool staticContext) const
{
    std::vector<const Variable*> variables;
    if (!cls) return variables;
    
    // Get all inherited variables
    auto allVariables = getAllInheritedVariables(cls);
    
    for (const auto* variable : allVariables) {
        // In static context, only static variables are accessible
        if (staticContext && !variable->isStatic) {
            continue;
        }
        
        // All variables are accessible in instance context
        variables.push_back(variable);
    }
    
    return variables;
}

void ProgramStructure::clear()
{
    std::lock_guard<std::mutex> lock(dataMutex);
    types.clear();
    packages.clear();
    fileTimestamps.clear();
}

// Class file parsing helper methods
uint16_t ProgramStructure::readU2(const std::vector<uint8_t>& data, size_t& offset)
{
    if (offset + 2 > data.size()) {
        throw std::runtime_error("Unexpected end of data while reading U2");
    }
    uint16_t value = (static_cast<uint16_t>(data[offset]) << 8) | data[offset + 1];
    offset += 2;
    return value;
}

uint32_t ProgramStructure::readU4(const std::vector<uint8_t>& data, size_t& offset)
{
    if (offset + 4 > data.size()) {
        throw std::runtime_error("Unexpected end of data while reading U4");
    }
    uint32_t value = (static_cast<uint32_t>(data[offset]) << 24) |
                     (static_cast<uint32_t>(data[offset + 1]) << 16) |
                     (static_cast<uint32_t>(data[offset + 2]) << 8) |
                     static_cast<uint32_t>(data[offset + 3]);
    offset += 4;
    return value;
}

bool ProgramStructure::parseConstantPoolEntry(const std::vector<uint8_t>& data, size_t& offset, ConstantPoolEntry& entry)
{
    if (offset >= data.size()) {
        return false;
    }
    
    entry.tag = data[offset++];
    
    switch (entry.tag) {
        case 1: // CONSTANT_Utf8
        {
            uint16_t length = readU2(data, offset);
            if (offset + length > data.size()) {
                return false;
            }
            entry.stringValue = std::string(reinterpret_cast<const char*>(&data[offset]), length);
            offset += length;
            break;
        }
        case 3: // CONSTANT_Integer
            entry.intValue = readU4(data, offset);
            break;
        case 4: // CONSTANT_Float
            entry.intValue = readU4(data, offset); // Store as int for simplicity
            break;
        case 5: // CONSTANT_Long
            readU4(data, offset); // High bytes
            readU4(data, offset); // Low bytes
            break;
        case 6: // CONSTANT_Double
            readU4(data, offset); // High bytes
            readU4(data, offset); // Low bytes
            break;
        case 7: // CONSTANT_Class
            entry.nameIndex = readU2(data, offset);
            break;
        case 8: // CONSTANT_String
            entry.nameIndex = readU2(data, offset);
            break;
        case 9: // CONSTANT_Fieldref
        case 10: // CONSTANT_Methodref
        case 11: // CONSTANT_InterfaceMethodref
            entry.classIndex = readU2(data, offset);
            entry.nameAndTypeIndex = readU2(data, offset);
            break;
        case 12: // CONSTANT_NameAndType
            entry.nameIndex = readU2(data, offset);
            entry.descriptorIndex = readU2(data, offset);
            break;
        default:
            PLOGE << "Unknown constant pool tag: " << static_cast<int>(entry.tag);
            return false;
    }
    
    return true;
}

std::string ProgramStructure::getUtf8FromConstantPool(const std::vector<ConstantPoolEntry>& constantPool, uint16_t index)
{
    if (index == 0 || index >= constantPool.size()) {
        return "";
    }
    
    const auto& entry = constantPool[index];
    if (entry.tag == 1) { // CONSTANT_Utf8
        return entry.stringValue;
    }
    
    return "";
}

std::string ProgramStructure::getClassNameFromConstantPool(const std::vector<ConstantPoolEntry>& constantPool, uint16_t index)
{
    if (index == 0 || index >= constantPool.size()) {
        return "";
    }
    
    const auto& entry = constantPool[index];
    if (entry.tag == 7) { // CONSTANT_Class
        return getUtf8FromConstantPool(constantPool, entry.nameIndex);
    }
    
    return "";
}

std::string ProgramStructure::parseFieldDescriptor(const std::string& descriptor)
{
    if (descriptor.empty()) {
        return "unknown";
    }
    
    switch (descriptor[0]) {
        case 'B': return "byte";
        case 'C': return "char";
        case 'D': return "double";
        case 'F': return "float";
        case 'I': return "int";
        case 'J': return "long";
        case 'S': return "short";
        case 'Z': return "boolean";
        case 'V': return "void";
        case 'L': {
            // Object reference
            size_t semicolon = descriptor.find(';');
            if (semicolon != std::string::npos) {
                std::string className = descriptor.substr(1, semicolon - 1);
                std::replace(className.begin(), className.end(), '/', '.');
                size_t lastDot = className.find_last_of('.');
                if (lastDot != std::string::npos) {
                    return className.substr(lastDot + 1);
                }
                return className;
            }
            return "Object";
        }
        case '[': {
            // Array
            std::string elementType = parseFieldDescriptor(descriptor.substr(1));
            return elementType + "[]";
        }
        default:
            return "unknown";
    }
}

std::string ProgramStructure::parseMethodDescriptor(const std::string& descriptor)
{
    if (descriptor.empty() || descriptor[0] != '(') {
        return "unknown";
    }
    
    size_t closeParen = descriptor.find(')');
    if (closeParen == std::string::npos) {
        return "unknown";
    }
    
    // Extract return type (after the closing parenthesis)
    std::string returnTypeDescriptor = descriptor.substr(closeParen + 1);
    return parseFieldDescriptor(returnTypeDescriptor);
}

bool ProgramStructure::parseField(const std::vector<uint8_t>& data, size_t& offset, 
                                  const std::vector<ConstantPoolEntry>& constantPool, Variable& field)
{
    try {
        uint16_t accessFlags = readU2(data, offset);
        uint16_t nameIndex = readU2(data, offset);
        uint16_t descriptorIndex = readU2(data, offset);
        uint16_t attributesCount = readU2(data, offset);
        
        // Parse field name and type
        field.name = getUtf8FromConstantPool(constantPool, nameIndex);
        std::string descriptor = getUtf8FromConstantPool(constantPool, descriptorIndex);
        field.type = parseFieldDescriptor(descriptor);
        
        // Parse access flags
        field.isPublic = (accessFlags & 0x0001) != 0;
        field.isPrivate = (accessFlags & 0x0002) != 0;
        field.isProtected = (accessFlags & 0x0004) != 0;
        field.isStatic = (accessFlags & 0x0008) != 0;
        field.isFinal = (accessFlags & 0x0010) != 0;
        field.isVolatile = (accessFlags & 0x0040) != 0;
        field.isTransient = (accessFlags & 0x0080) != 0;
        
        // Skip attributes for now
        for (uint16_t i = 0; i < attributesCount; ++i) {
            uint16_t attributeNameIndex = readU2(data, offset);
            uint32_t attributeLength = readU4(data, offset);
            offset += attributeLength; // Skip attribute data
        }
        
        return true;
    } catch (const std::exception& e) {
        PLOGE << "Error parsing field: " << e.what();
        return false;
    }
}

bool ProgramStructure::parseMethod(const std::vector<uint8_t>& data, size_t& offset,
                                   const std::vector<ConstantPoolEntry>& constantPool, Method& method)
{
    try {
        uint16_t accessFlags = readU2(data, offset);
        uint16_t nameIndex = readU2(data, offset);
        uint16_t descriptorIndex = readU2(data, offset);
        uint16_t attributesCount = readU2(data, offset);
        
        // Parse method name and return type
        method.name = getUtf8FromConstantPool(constantPool, nameIndex);
        std::string descriptor = getUtf8FromConstantPool(constantPool, descriptorIndex);
        method.returnType = parseMethodDescriptor(descriptor);
        
        // Parse access flags
        method.isPublic = (accessFlags & 0x0001) != 0;
        method.isPrivate = (accessFlags & 0x0002) != 0;
        method.isProtected = (accessFlags & 0x0004) != 0;
        method.isStatic = (accessFlags & 0x0008) != 0;
        method.isFinal = (accessFlags & 0x0010) != 0;
        method.isSynchronized = (accessFlags & 0x0020) != 0;
        method.isNative = (accessFlags & 0x0100) != 0;
        method.isAbstract = (accessFlags & 0x0400) != 0;
        method.isStrictfp = (accessFlags & 0x0800) != 0;
        
        // Skip attributes for now
        for (uint16_t i = 0; i < attributesCount; ++i) {
            uint16_t attributeNameIndex = readU2(data, offset);
            uint32_t attributeLength = readU4(data, offset);
            offset += attributeLength; // Skip attribute data
        }
        
        return true;
    } catch (const std::exception& e) {
        PLOGE << "Error parsing method: " << e.what();
        return false;
    }
}