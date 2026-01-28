#include "WorldMaps/Map/ModelElevationLayer.hpp"
#include "ModelViewer.hpp"
#include "ModelLoader.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <numeric>
#include <cmath>
#include <plog/Log.h>

// Use Assimp to load models for robustness
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

ModelElevationLayer::ModelElevationLayer() = default;
ModelElevationLayer::~ModelElevationLayer() = default;

bool ModelElevationLayer::loadFromFile(const std::string& path){
    ModelLoader::ExportedMesh em;
    if(!ModelLoader::loadGeometryFromFile(path, em)){
        PLOGW << "ModelElevationLayer: failed to load geometry from " << path;
        return false;
    }
    return loadFromExportedMesh(em.vertices, em.indices, em.boundRadius);
}


bool ModelElevationLayer::loadFromModelViewer(ModelViewer* mv){
    if(!mv){ PLOGW << "ModelElevationLayer: null ModelViewer"; return false; }
    // Prefer to obtain parsed geometry and use ModelLoader to export a consistent ExportedMesh
    ModelLoader::ParsedModel pm;
    if(mv->getParsedModel(pm)){
        auto em = ModelLoader::exportFromVBuf(pm.vbuf, pm.ibuf, pm.modelMat, pm.stride);
        if(em.vertices.empty() || em.indices.empty()){ PLOGW << "ModelElevationLayer: ModelLoader exportFromVBuf produced empty mesh"; return false; }
        return loadFromExportedMesh(em.vertices, em.indices, em.boundRadius);
    }
    // Fallback to the viewer's exported snapshot
    auto em2 = mv->getExportedMesh();
    if(!em2){ PLOGW << "ModelElevationLayer: ModelViewer has no exported mesh"; return false; }
    return loadFromExportedMesh(em2->vertices, em2->indices, em2->boundRadius);
}

bool ModelElevationLayer::loadFromExportedMesh(const std::vector<glm::vec3>& verts, const std::vector<unsigned int>& inds, float boundRadius){
    if(verts.empty() || inds.empty()){ PLOGW << "ModelElevationLayer: exported mesh empty"; return false; }

    auto md = std::make_shared<MeshData>();
    md->vertices = verts;
    md->indices = inds;

    std::vector<float> rads; rads.reserve(md->vertices.size());
    for(const auto &v : md->vertices) rads.push_back(glm::length(v));
    std::sort(rads.begin(), rads.end());
    md->baseRadius = boundRadius;
    float minR = rads.front(); float maxR = rads.back();
    md->maxDelta = std::max(std::fabs(maxR - md->baseRadius), std::fabs(md->baseRadius - minR));
    if(md->maxDelta < 1e-5f) md->maxDelta = 1e-5f;

    size_t triCount = md->indices.size() / 3;
    md->triIndices.resize(triCount);
    std::iota(md->triIndices.begin(), md->triIndices.end(), 0u);
    md->triCentroids.resize(triCount);
    for(size_t t=0; t<triCount; ++t){
        unsigned int i0 = md->indices[t*3+0], i1 = md->indices[t*3+1], i2 = md->indices[t*3+2];
        const glm::vec3 &v0 = md->vertices[i0];
        const glm::vec3 &v1 = md->vertices[i1];
        const glm::vec3 &v2 = md->vertices[i2];
        md->triCentroids[t] = (v0 + v1 + v2) / 3.0f;
    }

    // Build BVH (same approach as loadFromOBJ)
    md->triOrder = md->triIndices;
    struct BuildNode { int start, end, parent; };
    std::vector<BuildNode> stack;
    stack.push_back({0, (int)triCount, -1});
    std::vector<typename MeshData::BVHNode> nodes;
    nodes.reserve(triCount*2);

    while(!stack.empty()){
        auto cur = stack.back(); stack.pop_back();
        MeshData::BVHNode node;
        glm::vec3 bmin(1e30f), bmax(-1e30f);
        for(int i=cur.start;i<cur.end;++i){ auto &c = md->triCentroids[md->triOrder[i]]; bmin = glm::min(bmin, c); bmax = glm::max(bmax, c); }
        node.bmin = bmin; node.bmax = bmax; node.left = node.right = -1; node.start = cur.start; node.count = cur.end - cur.start;
        int myIndex = (int)nodes.size(); nodes.push_back(node);
        if(node.count <= 4) continue;
        glm::vec3 ext = bmax - bmin; int axis = (ext.x > ext.y) ? ((ext.x > ext.z) ? 0 : 2) : ((ext.y > ext.z) ? 1 : 2);
        int mid = (cur.start + cur.end) / 2;
        std::nth_element(md->triOrder.begin() + cur.start, md->triOrder.begin() + mid, md->triOrder.begin() + cur.end, [&](unsigned int a, unsigned int b){ return md->triCentroids[a][axis] < md->triCentroids[b][axis]; });
        stack.push_back({cur.start, mid, myIndex});
        stack.push_back({mid, cur.end, myIndex});
    }

    std::function<int(int,int)> buildRec = [&](int start, int end)->int{
        MeshData::BVHNode nn;
        glm::vec3 bmin(1e30f), bmax(-1e30f);
        for(int i=start;i<end;++i){ auto &c = md->triCentroids[md->triOrder[i]]; bmin = glm::min(bmin, c); bmax = glm::max(bmax, c); }
        nn.bmin = bmin; nn.bmax = bmax; nn.left = nn.right = -1; nn.start = start; nn.count = end - start;
        int idx = (int)nodes.size(); nodes.push_back(nn);
        if(nn.count <= 4) return idx;
        glm::vec3 ext = bmax - bmin; int axis = (ext.x > ext.y) ? ((ext.x > ext.z) ? 0 : 2) : ((ext.y > ext.z) ? 1 : 2);
        int mid = (start + end) / 2;
        std::nth_element(md->triOrder.begin() + start, md->triOrder.begin() + mid, md->triOrder.begin() + end, [&](unsigned int a, unsigned int b){ return md->triCentroids[a][axis] < md->triCentroids[b][axis]; });
        int left = buildRec(start, mid);
        int right = buildRec(mid, end);
        nodes[idx].left = left; nodes[idx].right = right; nodes[idx].start = 0; nodes[idx].count = 0;
        return idx;
    };
    nodes.clear();
    if(triCount>0) buildRec(0,(int)triCount);

    md->bvhNodes = std::move(nodes);
    mesh_.store(md);
    PLOGI << "ModelElevationLayer: loaded from ModelViewer (tris=" << triCount << ") bvh_nodes=" << md->bvhNodes.size();
    return true;
}


// Moller-Trumbore ray-triangle for origin ray (ray: dir * t). Returns t if hit and positive, else <=0
static inline float rayTriIntersect_origin(const glm::vec3 &dir, const glm::vec3 &v0, const glm::vec3 &v1, const glm::vec3 &v2){
    const float EPS = 1e-6f;
    glm::vec3 e1 = v1 - v0;
    glm::vec3 e2 = v2 - v0;
    glm::vec3 p = glm::cross(dir, e2);
    float det = glm::dot(e1, p);
    if(std::fabs(det) < EPS) return -1.0f;
    float invDet = 1.0f / det;
    glm::vec3 tvec = -v0; // origin is 0
    float u = glm::dot(tvec, p) * invDet;
    if(u < 0.0f || u > 1.0f) return -1.0f;
    glm::vec3 q = glm::cross(tvec, e1);
    float v = glm::dot(dir, q) * invDet;
    if(v < 0.0f || u + v > 1.0f) return -1.0f;
    float t = glm::dot(e2, q) * invDet;
    if(t <= EPS) return -1.0f;
    return t;
}

SampleData ModelElevationLayer::sample(const World& /*world*/, float longitude, float latitude) const {
    SampleData data;
    auto localMesh = std::atomic_load(&mesh_);
    if(!localMesh){ data.channels.push_back(0.0f); return data; }

    float lonRad = longitude * static_cast<float>(M_PI) / 180.0f;
    float latRad = latitude * static_cast<float>(M_PI) / 180.0f;

    // Raycast helper returning hit t (inf if none)
    auto castRay = [&](const glm::vec3 &dir)->float{
        float bestT = std::numeric_limits<float>::infinity();
        struct StackItem{ int node; };
        std::vector<StackItem> stack;
        if(!localMesh->bvhNodes.empty()) stack.push_back({0});
        while(!stack.empty()){
            int nodeIdx = stack.back().node; stack.pop_back();
            const auto &n = localMesh->bvhNodes[nodeIdx];
            float tmin = 0.0f, tmax = std::numeric_limits<float>::infinity();
            for(int ax=0;ax<3;++ax){
                float invD = 1.0f / (dir[ax] == 0.0f ? 1e-9f : dir[ax]);
                float t0 = (n.bmin[ax]) * invD;
                float t1 = (n.bmax[ax]) * invD;
                if(t0 > t1) std::swap(t0, t1);
                tmin = std::max(tmin, t0);
                tmax = std::min(tmax, t1);
                if(tmax < tmin) break;
            }
            if(tmax < tmin) continue;
            if(n.count > 0){
                for(int i=0;i<n.count;++i){
                    unsigned int triId = localMesh->triOrder[n.start + i];
                    unsigned int i0 = localMesh->indices[triId*3+0], i1 = localMesh->indices[triId*3+1], i2 = localMesh->indices[triId*3+2];
                    const glm::vec3 &v0 = localMesh->vertices[i0];
                    const glm::vec3 &v1 = localMesh->vertices[i1];
                    const glm::vec3 &v2 = localMesh->vertices[i2];
                    float t = rayTriIntersect_origin(dir, v0, v1, v2);
                    if(t > 0.0f && t < bestT) bestT = t;
                }
            } else {
                if(n.left >= 0) stack.push_back({n.left});
                if(n.right >= 0) stack.push_back({n.right});
            }
        }
        return bestT;
    };

    // Supersample around the central direction to reduce aliasing/artifacts
    const int samples = 5;
    const float offsetDeg = 0.25f; // small angular offset in degrees
    const float offRad = offsetDeg * static_cast<float>(M_PI) / 180.0f;
    const std::array<glm::vec2, samples> offs = { glm::vec2(0,0), glm::vec2(offRad,0), glm::vec2(-offRad,0), glm::vec2(0,offRad), glm::vec2(0,-offRad) };

    float sumH = 0.0f; int hitCount = 0;
    for(int s=0;s<samples;++s){
        float lr = lonRad + offs[s].x;
        float la = latRad + offs[s].y;
        glm::vec3 d(std::cos(la) * std::cos(lr), std::sin(la), std::cos(la) * std::sin(lr));
        float t = castRay(d);
        if(t != std::numeric_limits<float>::infinity()){
            float hitRadius = t;
            float h = (hitRadius - localMesh->baseRadius) / localMesh->maxDelta;
            sumH += std::clamp(h, -1.0f, 1.0f);
            ++hitCount;
        }
    }
    if(hitCount == 0){ data.channels.push_back(0.0f); return data; }
    float avgH = sumH / static_cast<float>(hitCount);
    data.channels.push_back(avgH);
    return data;
    // clamp roughly to [-1,1] (handled by supersampling path)
    // data.channels.push_back(h); // removed redundant code
    return data;
}

std::array<uint8_t,4> ModelElevationLayer::getColor(const World& world, float longitude, float latitude) const {
    SampleData s = sample(world, longitude, latitude);
    float elev = 0.0f;
    if(!s.channels.empty()) elev = s.channels[0];
    // map [-1,1] -> [0,255]
    uint8_t v = static_cast<uint8_t>(std::clamp((elev + 1.0f) * 0.5f, 0.0f, 1.0f) * 255.0f);
    return {v,v,v,255};
}