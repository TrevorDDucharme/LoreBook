#pragma once
#include <WorldMaps/Map/MapLayer.hpp>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>
#include <atomic>

class ModelElevationLayer : public MapLayer {
public:
    ModelElevationLayer();
    ~ModelElevationLayer() override;

    // Load a model geometry from file using the shared ModelLoader. Returns true on success.
    bool loadFromFile(const std::string& path);
    // Load mesh from an already-parsed ModelViewer (more efficient than re-parsing)
    bool loadFromModelViewer(class ModelViewer* mv);
    // Directly load from an exported ModelViewer mesh snapshot
    bool loadFromExportedMesh(const std::vector<glm::vec3>& vertices, const std::vector<unsigned int>& indices, float boundRadius);

    SampleData sample(const World& world, float longitude, float latitude) const override;
    std::array<uint8_t,4> getColor(const World& world, float longitude, float latitude) const override;

private:
    struct MeshData {
        std::vector<glm::vec3> vertices;
        std::vector<unsigned int> indices; // triplets

        // BVH-friendly data
        std::vector<unsigned int> triIndices;   // 0..N-1 triangle indices
        std::vector<glm::vec3> triCentroids;    // centroid per triangle
        std::vector<unsigned int> triOrder;     // permutation used by BVH

        struct BVHNode { glm::vec3 bmin, bmax; int left=-1, right=-1; int start=0, count=0; };
        std::vector<BVHNode> bvhNodes;

        float baseRadius = 1.0f; // estimated "sea level" radius
        float maxDelta = 0.1f;   // max radial displacement used to normalize output
    };

    // Atomic shared pointer so sampling can read without locking while load swaps in new mesh
    std::atomic<std::shared_ptr<MeshData>> mesh_{nullptr};
};