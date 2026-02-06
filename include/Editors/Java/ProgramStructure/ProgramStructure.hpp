#pragma once
#include <filesystem>
#include <plog/Log.h>
#include <Editors/Java/ProgramStructure/Type.hpp>
#include <Editors/Java/ProgramStructure/Variable.hpp>
#include <Editors/Java/ProgramStructure/Method.hpp>
#include <Editors/Java/ProgramStructure/Class.hpp>
#include <Editors/Java/ProgramStructure/Package.hpp>
#include <unordered_map>
#include <thread>
#include <atomic>
#include <mutex>
#include <chrono>

class ProgramStructure{
private:
    std::vector<Type> types; // All types in the program
    std::vector<Package> packages; // All packages in the program
    std::unordered_map<std::string, std::filesystem::file_time_type> fileTimestamps; // Track file modification times
    std::vector<std::filesystem::path> watchedDirectories; // Directories being watched for changes
    
    std::thread watcherThread;
    std::atomic<bool> isWatching{false};
    mutable std::mutex dataMutex; // Protect access to packages and types
    
    // Parsing methods
    bool parseJavaFile(const std::filesystem::path &filePath, const std::filesystem::path &srcRoot);
    bool parseClassFile(const std::filesystem::path &filePath, const std::filesystem::path &classRoot);
    bool parseJarFile(const std::filesystem::path &filePath);
    bool parseZipFile(const std::filesystem::path &filePath);
    bool parseArchiveFile(const std::filesystem::path &filePath);
    
    // File watching methods
    void watchDirectories();
    void processFileChange(const std::filesystem::path &filePath);
    void removeFileFromCache(const std::filesystem::path &filePath);
    
    // Helper methods
    std::string getPackageNameFromPath(const std::filesystem::path &filePath, const std::filesystem::path &srcRoot);
    std::string getPackageNameFromArchivePath(const std::filesystem::path &archivePath);
    bool parseJavaContent(const std::string &content, const std::string &className, Package &package);
    Package* findOrCreatePackage(const std::string &packageName);
    Package* findPackageInHierarchy(const std::string &packageName, std::vector<Package> &packageList);
    const Package* findPackageInHierarchyConst(const std::string &packageName, const std::vector<Package> &packageList) const;
    void removeClassFromPackage(const std::string &packageName, const std::string &className);
    
    // Class file parsing specific helpers
    struct ConstantPoolEntry {
        uint8_t tag;
        std::string stringValue;
        uint32_t intValue;
        uint16_t classIndex;
        uint16_t nameAndTypeIndex;
        uint16_t nameIndex;
        uint16_t descriptorIndex;
    };
    
    bool parseClassFileData(const std::vector<uint8_t>& data, const std::filesystem::path &filePath, const std::filesystem::path &classRoot);
    uint16_t readU2(const std::vector<uint8_t>& data, size_t& offset);
    uint32_t readU4(const std::vector<uint8_t>& data, size_t& offset);
    bool parseConstantPoolEntry(const std::vector<uint8_t>& data, size_t& offset, ConstantPoolEntry& entry);
    std::string getClassNameFromConstantPool(const std::vector<ConstantPoolEntry>& constantPool, uint16_t index);
    std::string getUtf8FromConstantPool(const std::vector<ConstantPoolEntry>& constantPool, uint16_t index);
    bool parseField(const std::vector<uint8_t>& data, size_t& offset, const std::vector<ConstantPoolEntry>& constantPool, Variable& field);
    bool parseMethod(const std::vector<uint8_t>& data, size_t& offset, const std::vector<ConstantPoolEntry>& constantPool, Method& method);
    std::string parseFieldDescriptor(const std::string& descriptor);
    std::string parseMethodDescriptor(const std::string& descriptor);

public:
    ProgramStructure() = default;
    ~ProgramStructure();

    // Main interface
    bool parseFile(const std::filesystem::path &filePath, const std::filesystem::path &srcRoot);
    bool parseDirectory(const std::filesystem::path &dirPath);
    
    // File watching
    void startWatching(const std::vector<std::filesystem::path> &directories);
    void stopWatching();
    bool isCurrentlyWatching() const { return isWatching.load(); }
    
    // Data access
    void addType(const Type &type);
    void addPackage(const Package &package);
    
    bool validType(const std::string &typeName) const;
    bool validPackage(const std::string &packageName) const;

    const std::vector<Type>& getTypes() const;
    const std::vector<Package>& getPackages() const;
    std::vector<const Package*> getAllPackagesFlat() const;
    std::string getFullPackageName(const Package* package) const;

    const Type& getType(const std::string &typeName) const;
    const Package& getPackage(const std::string &packageName) const;
    const Class* findClass(const std::string &className, const std::string &packageName = "") const;
    std::vector<const Method*> getAllInheritedMethods(const Class* cls) const;
    std::vector<const Variable*> getAllInheritedVariables(const Class* cls) const;
    
    // Public methods for external parsing
    bool parseContentToPackage(const std::string &content, const std::string &className, const std::string &packageName);
    
    // Live content management
    bool updateClassFromLiveContent(const std::string &content, const std::string &className, const std::string &packageName);
    bool hasValidSyntax(const std::string &content) const;
    
    // Context-aware analysis
    struct CursorContext {
        bool isInClass = false;
        bool isInMethod = false;
        bool isInClassScope = false;
        std::string currentClassName;
        std::string currentPackageName;
        std::string currentMethodName;
        bool hasStaticContext = false;
    };
    
    CursorContext analyzeCursorContext(const std::string &content, size_t line, size_t column) const;
    std::vector<const Method*> getAccessibleMethods(const Class* cls, bool staticContext = false) const;
    std::vector<const Variable*> getAccessibleVariables(const Class* cls, bool staticContext = false) const;

    void clear();
};