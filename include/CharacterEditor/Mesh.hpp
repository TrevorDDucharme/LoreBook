#pragma once
#include <string>
#include <vector>
#include <map>
#include <optional>
#include <cstdint>
#include <glm/glm.hpp>
#include <CharacterEditor/Transform.hpp>
#include <CharacterEditor/Vertex.hpp>
#include <CharacterEditor/Bone.hpp>
#include <CharacterEditor/PartRole.hpp>

namespace CharacterEditor {

/**
 * @brief Shape key type classification
 */
enum class ShapeKeyType : uint32_t {
    PartLocal,       // Internal to part, no external dependencies
    SocketDriven,    // Activated by attachment state
    SemanticShared,  // Name-based matching (optional)
    SeamCorrective   // Auto-generated per attachment
};

/**
 * @brief Shape key / morph target
 */
struct ShapeKey {
    std::string name;
    ShapeKeyType type = ShapeKeyType::PartLocal;
    
    // Delta vertices (same count as mesh vertices)
    std::vector<VertexDelta> deltas;
    
    // Driver info (for SocketDriven type)
    std::string driverSocketProfile;
    std::string driverExpression;
    
    // Current blend weight [0, 1]
    float weight = 0.0f;
    
    // Min/max weight limits
    float minWeight = 0.0f;
    float maxWeight = 1.0f;
    
    ShapeKey() = default;
    
    ShapeKey(const std::string& name_, ShapeKeyType type_ = ShapeKeyType::PartLocal)
        : name(name_), type(type_) {}
    
    /**
     * @brief Check if this shape key matches another by name (for semantic sharing)
     */
    bool matchesSemantic(const ShapeKey& other) const {
        return name == other.name && type == ShapeKeyType::SemanticShared;
    }
    
    /**
     * @brief Set weight clamped to [minWeight, maxWeight]
     */
    void setWeight(float w) {
        weight = glm::clamp(w, minWeight, maxWeight);
    }
};

/**
 * @brief Material slot for mesh rendering
 */
struct MaterialSlot {
    uint32_t index = 0;
    std::string name;
    
    // Texture paths (external files or "embedded:N" for embedded textures)
    std::optional<std::string> albedoTexture;
    std::optional<std::string> normalTexture;
    std::optional<std::string> metallicRoughnessTexture;
    std::optional<std::string> aoTexture;
    std::optional<std::string> emissiveTexture;
    
    // Material properties (PBR)
    glm::vec4 baseColor{1.0f, 1.0f, 1.0f, 1.0f};
    glm::vec2 metallicRoughness{0.0f, 0.5f}; // x=metallic, y=roughness
    glm::vec3 emissive{0.0f};
    float emissiveStrength = 1.0f;
    
    // Alpha mode
    enum class AlphaMode : uint32_t {
        Opaque,
        Mask,
        Blend
    } alphaMode = AlphaMode::Opaque;
    float alphaCutoff = 0.5f;
    
    // Rendering flags
    bool doubleSided = false;
    
    MaterialSlot() = default;
    
    MaterialSlot(uint32_t idx, const std::string& name_)
        : index(idx), name(name_) {}
    
    /**
     * @brief Check if this material has any textures
     */
    bool hasTextures() const {
        return albedoTexture.has_value() || normalTexture.has_value();
    }
    
    /**
     * @brief Check if albedo texture is embedded
     */
    bool hasAlbedoEmbedded() const {
        return albedoTexture.has_value() && 
               albedoTexture->rfind("embedded:", 0) == 0;
    }
    
    /**
     * @brief Get metallic value
     */
    float metallic() const { return metallicRoughness.x; }
    
    /**
     * @brief Get roughness value
     */
    float roughness() const { return metallicRoughness.y; }
};

/**
 * @brief Submesh range for multi-material meshes
 */
struct Submesh {
    uint32_t indexOffset = 0;
    uint32_t indexCount = 0;
    uint32_t materialIndex = 0;
    
    Submesh() = default;
    
    Submesh(uint32_t offset, uint32_t count, uint32_t mat)
        : indexOffset(offset), indexCount(count), materialIndex(mat) {}
};

/**
 * @brief Mesh structure for 3D geometry
 */
struct Mesh {
    uint32_t id = 0;
    std::string name;
    
    // Geometry
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    // Materials
    std::vector<MaterialSlot> materials;
    
    // Submesh ranges (for multi-material)
    std::vector<Submesh> submeshes;
    
    // Local skeleton (optional)
    Skeleton skeleton;
    
    // Shape keys / morph targets
    std::vector<ShapeKey> shapeKeys;
    
    // Bounding box (computed)
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
    
    Mesh() = default;
    
    Mesh(uint32_t id_, const std::string& name_)
        : id(id_), name(name_) {}
    
    /**
     * @brief Get vertex count
     */
    size_t vertexCount() const {
        return vertices.size();
    }
    
    /**
     * @brief Get triangle count
     */
    size_t triangleCount() const {
        return indices.size() / 3;
    }
    
    /**
     * @brief Check if mesh has skeleton
     */
    bool hasSkeleton() const {
        return !skeleton.empty();
    }
    
    /**
     * @brief Check if mesh has shape keys
     */
    bool hasShapeKeys() const {
        return !shapeKeys.empty();
    }
    
    /**
     * @brief Find shape key by name
     */
    ShapeKey* findShapeKey(const std::string& name) {
        for (auto& key : shapeKeys) {
            if (key.name == name) return &key;
        }
        return nullptr;
    }
    
    const ShapeKey* findShapeKey(const std::string& name) const {
        for (const auto& key : shapeKeys) {
            if (key.name == name) return &key;
        }
        return nullptr;
    }
    
    /**
     * @brief Compute bounding box from vertices
     */
    void computeBounds() {
        if (vertices.empty()) {
            boundsMin = boundsMax = glm::vec3(0.0f);
            return;
        }
        
        boundsMin = boundsMax = vertices[0].position;
        for (const auto& v : vertices) {
            boundsMin = glm::min(boundsMin, v.position);
            boundsMax = glm::max(boundsMax, v.position);
        }
    }
    
    /**
     * @brief Get bounding box center
     */
    glm::vec3 getBoundsCenter() const {
        return (boundsMin + boundsMax) * 0.5f;
    }
    
    /**
     * @brief Get bounding box size
     */
    glm::vec3 getBoundsSize() const {
        return boundsMax - boundsMin;
    }
    
    /**
     * @brief Apply shape keys to get deformed vertices
     */
    std::vector<Vertex> getDeformedVertices() const {
        std::vector<Vertex> result = vertices;
        
        for (const auto& key : shapeKeys) {
            if (key.weight <= 0.0f) continue;
            if (key.deltas.size() != vertices.size()) continue;
            
            for (size_t i = 0; i < result.size(); ++i) {
                result[i].position += key.deltas[i].positionDelta * key.weight;
                result[i].normal += key.deltas[i].normalDelta * key.weight;
            }
        }
        
        // Re-normalize normals
        for (auto& v : result) {
            v.normal = glm::normalize(v.normal);
        }
        
        return result;
    }
    
    /**
     * @brief Normalize all bone weights in vertices
     */
    void normalizeBoneWeights() {
        for (auto& v : vertices) {
            v.normalizeBoneWeights();
        }
    }
    
    /**
     * @brief Compute bitangents for all vertices
     */
    void computeBitangents() {
        for (auto& v : vertices) {
            v.computeBitangent();
        }
    }
    
    /**
     * @brief Get submesh containing the given triangle index
     */
    const Submesh* getSubmeshForTriangle(uint32_t triangleIndex) const {
        uint32_t indexStart = triangleIndex * 3;
        for (const auto& sub : submeshes) {
            if (indexStart >= sub.indexOffset && 
                indexStart < sub.indexOffset + sub.indexCount) {
                return &sub;
            }
        }
        return nullptr;
    }
};

} // namespace CharacterEditor