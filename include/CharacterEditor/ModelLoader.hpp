#pragma once
#include <string>
#include <vector>
#include <optional>
#include <functional>
#include <cstdint>
#include <CharacterEditor/Mesh.hpp>
#include <CharacterEditor/Part.hpp>
#include <CharacterEditor/Socket.hpp>

// Forward declarations for Assimp types
struct aiScene;
struct aiNode;
struct aiMesh;
struct aiMaterial;

namespace CharacterEditor {

/**
 * @brief Import configuration for model loading
 */
struct ImportConfig {
    bool triangulate = true;
    bool generateNormals = true;
    bool generateTangents = true;
    bool calculateBoneWeights = true;
    bool loadEmbeddedTextures = true;
    bool flipUVs = false;
    bool leftHandedToRightHanded = true;
    bool convertZUpToYUp = false;  // GLB uses Y-up like OpenGL, no conversion needed
    
    // Socket detection prefix
    std::string socketBonePrefix = "socket_";
    
    // Texture search paths (in addition to model directory)
    std::vector<std::string> textureSearchPaths;
    
    // Scale factor for imported models
    float scaleFactor = 1.0f;
    
    // Bone name to PartRole mapping overrides
    std::map<std::string, PartRole> boneRoleOverrides;
    
    ImportConfig() = default;
};

/**
 * @brief Embedded texture data from model file
 */
struct EmbeddedTexture {
    std::string name;
    std::string format;  // "png", "jpg", etc.
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> data;
    bool isCompressed = true;  // If true, data is compressed (PNG/JPG), otherwise raw RGBA
};

/**
 * @brief Result of model loading operation
 */
struct LoadResult {
    bool success = false;
    std::vector<std::string> errors;
    std::vector<std::string> warnings;
    
    std::vector<Mesh> meshes;
    Skeleton skeleton;
    std::vector<Socket> extractedSockets;
    std::vector<EmbeddedTexture> embeddedTextures;
    std::vector<MaterialSlot> materials;
    
    // Scene metadata
    std::string modelName;
    glm::vec3 sceneMin{0.0f};
    glm::vec3 sceneMax{0.0f};
    
    LoadResult() = default;
    
    explicit LoadResult(const std::string& errorMsg)
        : success(false) {
        errors.push_back(errorMsg);
    }
    
    static LoadResult Success() {
        LoadResult r;
        r.success = true;
        return r;
    }
    
    static LoadResult Error(const std::string& msg) {
        return LoadResult(msg);
    }
    
    /**
     * @brief Check if any meshes were loaded
     */
    bool hasMeshes() const {
        return !meshes.empty();
    }
    
    /**
     * @brief Check if a skeleton was loaded
     */
    bool hasSkeleton() const {
        return !skeleton.empty();
    }
    
    /**
     * @brief Get total vertex count across all meshes
     */
    size_t totalVertexCount() const {
        size_t count = 0;
        for (const auto& m : meshes) {
            count += m.vertexCount();
        }
        return count;
    }
    
    /**
     * @brief Get total triangle count across all meshes
     */
    size_t totalTriangleCount() const {
        size_t count = 0;
        for (const auto& m : meshes) {
            count += m.triangleCount();
        }
        return count;
    }
    
    /**
     * @brief Get first error message or empty string
     */
    std::string getError() const {
        return errors.empty() ? "" : errors[0];
    }
};

/**
 * @brief Progress callback for model loading
 */
using LoadProgressCallback = std::function<void(float progress, const std::string& status)>;

/**
 * @brief Model loader using Assimp
 * 
 * Supports loading 3D models from various formats (glTF, FBX, OBJ, etc.)
 * and extracting meshes, skeletons, shape keys, textures, and sockets.
 */
class ModelLoader {
public:
    ModelLoader() = default;
    ~ModelLoader() = default;
    
    /**
     * @brief Load model from file
     * @param path Path to model file
     * @param config Import configuration
     * @return Load result with meshes, skeleton, and sockets
     */
    static LoadResult loadFromFile(const std::string& path, 
                                   const ImportConfig& config = ImportConfig());
    
    /**
     * @brief Load model from memory buffer
     * @param data Pointer to model file data
     * @param size Size of data in bytes
     * @param formatHint Format hint (e.g., "gltf", "fbx", "obj")
     * @param config Import configuration
     * @return Load result with meshes, skeleton, and sockets
     */
    static LoadResult loadFromMemory(const uint8_t* data,
                                     size_t size,
                                     const std::string& formatHint,
                                     const ImportConfig& config = ImportConfig());
    
    /**
     * @brief Load model and convert to Part
     * @param path Path to model file
     * @param config Import configuration
     * @return Part structure with mesh, sockets, and metadata, or nullopt on failure
     */
    static std::optional<Part> loadAsPart(const std::string& path,
                                          const ImportConfig& config = ImportConfig());
    
    /**
     * @brief Set progress callback for loading operations
     */
    static void setProgressCallback(LoadProgressCallback callback);
    
private:
    static LoadProgressCallback s_progressCallback;
};

} // namespace CharacterEditor
