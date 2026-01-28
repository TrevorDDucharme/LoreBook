#pragma once

#include <vector>
#include <glm/glm.hpp>
#include <string>

// Lightweight shared mesh export used by both ModelViewer and ModelElevationLayer
struct ModelLoader {
    struct ExportedMesh {
        std::vector<glm::vec3> vertices; // model-space positions (already centered/scaled)
        std::vector<unsigned int> indices; // triangle indices (triplets)
        float boundRadius = 1.0f;
    };

    // Lightweight parsed model (geometry + transform) produced by the loader
    struct ParsedModel {
        std::vector<float> vbuf; // interleaved pos(3),norm(3),uv(2),tangent(4)
        std::vector<unsigned int> ibuf;
        glm::mat4 modelMat = glm::mat4(1.0f);
        float boundRadius = 1.0f;
        int stride = 12; // floats per-vertex
        std::string name;
    };

    // Load geometry from file using Assimp (triangulated). Returns true on success.
    static bool loadGeometryFromFile(const std::string& path, ExportedMesh& out);

    // Load geometry from memory buffer (same semantics as Assimp ReadFileFromMemory)
    static bool loadGeometryFromMemory(const std::vector<uint8_t>& data, const std::string& name, ExportedMesh& out);

    // Parse a model from memory into a ParsedModel (includes interleaved vbuf + ibuf)
    static bool parseModelFromMemory(const std::vector<uint8_t>& data, const std::string& name, ParsedModel& out);
    static bool parseModelFromFile(const std::string& path, ParsedModel& out);

    // Create an ExportedMesh snapshot from interleaved vertex buffer and element buffer.
    // vbuf stride is number of floats per vertex (e.g., 12 for pos,norm,uv,tangent)
    static ExportedMesh exportFromVBuf(const std::vector<float>& vbuf, const std::vector<unsigned int>& ibuf, const glm::mat4& modelMat, int stride = 12);
};