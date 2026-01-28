#include "ModelLoader.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <plog/Log.h>
#include <algorithm>
#include <numeric>
#include <glm/gtc/matrix_transform.hpp>
#include <fstream>

static void computeModelTransformForExport(const std::vector<glm::vec3>& verts, glm::mat4& outModel, float& outRadius){
    if(verts.empty()){ outModel = glm::mat4(1.0f); outRadius = 1.0f; return; }
    glm::vec3 mn(1e30f), mx(-1e30f,-1e30f,-1e30f);
    for(const auto &v : verts){ mn = glm::min(mn, v); mx = glm::max(mx, v); }
    glm::vec3 center = (mn + mx) * 0.5f;
    glm::vec3 ext = mx - mn;
    float maxExtent = std::max(std::max(ext.x, ext.y), ext.z);
    float scale = 1.0f;
    if(maxExtent > 1e-6f) scale = 2.0f / maxExtent;
    outModel = glm::translate(glm::mat4(1.0f), -center) * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
    outRadius = (maxExtent * scale) * 0.5f;
    if(outRadius <= 0.0f) outRadius = 1.0f;
}

bool ModelLoader::loadGeometryFromFile(const std::string& path, ExportedMesh& out){
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices);
    if(!scene || !scene->HasMeshes()){
        PLOGW << "ModelLoader: failed to load file: " << path << " : " << importer.GetErrorString();
        return false;
    }
    std::vector<glm::vec3> verts;
    std::vector<unsigned int> idx;
    verts.reserve(1024);
    idx.reserve(2048);
    for(unsigned m=0;m<scene->mNumMeshes;++m){ aiMesh* mesh = scene->mMeshes[m]; unsigned int base = (unsigned int)verts.size();
        for(unsigned i=0;i<mesh->mNumVertices;++i){ aiVector3D v = mesh->mVertices[i]; verts.emplace_back(v.x, v.y, v.z); }
        for(unsigned f=0; f<mesh->mNumFaces; ++f){ aiFace &face = mesh->mFaces[f]; if(face.mNumIndices != 3) continue; idx.push_back(base + face.mIndices[0]); idx.push_back(base + face.mIndices[1]); idx.push_back(base + face.mIndices[2]); }
    }
    if(verts.empty() || idx.empty()) return false;
    glm::mat4 modelMat; float radius;
    computeModelTransformForExport(verts, modelMat, radius);
    out.indices = std::move(idx);
    out.vertices.reserve(verts.size());
    for(const auto &v : verts){ glm::vec4 p(v,1.0f); glm::vec4 tp = modelMat * p; out.vertices.emplace_back(tp.x, tp.y, tp.z); }
    out.boundRadius = radius;
    return true;
}

bool ModelLoader::loadGeometryFromMemory(const std::vector<uint8_t>& data, const std::string& name, ExportedMesh& out){
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFileFromMemory(data.data(), data.size(), aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_PreTransformVertices, name.c_str());
    if(!scene || !scene->HasMeshes()){
        PLOGW << "ModelLoader: ReadFileFromMemory failed for " << name << " : " << importer.GetErrorString();
        return false;
    }
    std::vector<glm::vec3> verts;
    std::vector<unsigned int> idx;
    verts.reserve(1024);
    idx.reserve(2048);
    for(unsigned m=0;m<scene->mNumMeshes;++m){ aiMesh* mesh = scene->mMeshes[m]; unsigned int base = (unsigned int)verts.size();
        for(unsigned i=0;i<mesh->mNumVertices;++i){ aiVector3D v = mesh->mVertices[i]; verts.emplace_back(v.x, v.y, v.z); }
        for(unsigned f=0; f<mesh->mNumFaces; ++f){ aiFace &face = mesh->mFaces[f]; if(face.mNumIndices != 3) continue; idx.push_back(base + face.mIndices[0]); idx.push_back(base + face.mIndices[1]); idx.push_back(base + face.mIndices[2]); }
    }
    if(verts.empty() || idx.empty()) return false;
    glm::mat4 modelMat; float radius;
    computeModelTransformForExport(verts, modelMat, radius);
    out.indices = std::move(idx);
    out.vertices.reserve(verts.size());
    for(const auto &v : verts){ glm::vec4 p(v,1.0f); glm::vec4 tp = modelMat * p; out.vertices.emplace_back(tp.x, tp.y, tp.z); }
    out.boundRadius = radius;
    return true;
}

// Parse model into interleaved vbuf (pos,norm,uv,tangent) + ibuf
bool ModelLoader::parseModelFromMemory(const std::vector<uint8_t>& data, const std::string& name, ParsedModel& out){
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFileFromMemory(data.data(), data.size(), aiProcess_Triangulate|aiProcess_GenNormals|aiProcess_JoinIdenticalVertices|aiProcess_OptimizeMeshes|aiProcess_PreTransformVertices|aiProcess_CalcTangentSpace, name.c_str());
    if(!scene || !scene->HasMeshes()){
        PLOGW << "ModelLoader: parse ReadFileFromMemory failed for " << name << " : " << importer.GetErrorString();
        return false;
    }

    out.vbuf.clear(); out.ibuf.clear(); out.name = name;
    out.vbuf.reserve(1024);
    out.ibuf.reserve(2048);

    for(unsigned m=0;m<scene->mNumMeshes;++m){ aiMesh* mesh = scene->mMeshes[m]; unsigned int baseV = (unsigned int)(out.vbuf.size() / 12);
        // ensure per-vertex data present or synthesize
        bool hasNormals = mesh->HasNormals();
        bool hasUV   = mesh->HasTextureCoords(0);
        bool hasTang = mesh->HasTangentsAndBitangents();
        for(unsigned i=0;i<mesh->mNumVertices;++i){ aiVector3D vp = mesh->mVertices[i]; out.vbuf.push_back(vp.x); out.vbuf.push_back(vp.y); out.vbuf.push_back(vp.z);
            if(hasNormals){ aiVector3D n = mesh->mNormals[i]; out.vbuf.push_back(n.x); out.vbuf.push_back(n.y); out.vbuf.push_back(n.z); } else { out.vbuf.push_back(0.0f); out.vbuf.push_back(0.0f); out.vbuf.push_back(1.0f); }
            if(hasUV){ aiVector3D uv = mesh->mTextureCoords[0][i]; out.vbuf.push_back(uv.x); out.vbuf.push_back(uv.y); } else { out.vbuf.push_back(0.0f); out.vbuf.push_back(0.0f); }
            if(hasTang){ aiVector3D t = mesh->mTangents[i]; aiVector3D b = mesh->mBitangents[i]; aiVector3D n = mesh->mNormals[i]; glm::vec3 gt(t.x,t.y,t.z), gb(b.x,b.y,b.z), gn(n.x,n.y,n.z); float w = (glm::dot(glm::cross(gt,gn), gb) < 0.0f) ? -1.0f : 1.0f; out.vbuf.push_back(t.x); out.vbuf.push_back(t.y); out.vbuf.push_back(t.z); out.vbuf.push_back(w); } else { out.vbuf.push_back(1.0f); out.vbuf.push_back(0.0f); out.vbuf.push_back(0.0f); out.vbuf.push_back(1.0f); }
        }
        for(unsigned f=0; f<mesh->mNumFaces; ++f){ aiFace &face = mesh->mFaces[f]; if(face.mNumIndices != 3) continue; out.ibuf.push_back(baseV + face.mIndices[0]); out.ibuf.push_back(baseV + face.mIndices[1]); out.ibuf.push_back(baseV + face.mIndices[2]); }
    }
    if(out.vbuf.empty() || out.ibuf.empty()) return false;
    // compute transform to bring into model-space consistent with other helpers
    glm::mat4 modelMat; float radius;
    // create position-only verts for transform computation
    std::vector<glm::vec3> pos; pos.reserve(out.vbuf.size()/12);
    for(size_t i=0;i+2<out.vbuf.size(); i+=12) pos.emplace_back(out.vbuf[i], out.vbuf[i+1], out.vbuf[i+2]);
    computeModelTransformForExport(pos, modelMat, radius);
    // apply modelMat to vbuf positions
    for(size_t i=0;i+2<out.vbuf.size(); i+=12){ glm::vec4 p(out.vbuf[i], out.vbuf[i+1], out.vbuf[i+2], 1.0f); glm::vec4 tp = modelMat * p; out.vbuf[i] = tp.x; out.vbuf[i+1] = tp.y; out.vbuf[i+2] = tp.z; }
    out.modelMat = modelMat; out.boundRadius = radius; out.stride = 12;
    return true;
}

bool ModelLoader::parseModelFromFile(const std::string& path, ParsedModel& out){
    // Read file by delegating to ReadFile (same flags)
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate|aiProcess_GenNormals|aiProcess_JoinIdenticalVertices|aiProcess_OptimizeMeshes|aiProcess_PreTransformVertices|aiProcess_CalcTangentSpace);
    if(!scene || !scene->HasMeshes()){
        PLOGW << "ModelLoader: parse ReadFile failed for " << path << " : " << importer.GetErrorString();
        return false;
    }
    // Reuse memory path by copying file into buffer (Assimp ReadFile takes filename though, but we want same logic)
    // We'll implement a simple wrapper that reads file bytes and calls parseModelFromMemory
    std::ifstream ifs(path, std::ios::binary | std::ios::ate);
    if(!ifs) return false;
    std::streamsize sz = ifs.tellg(); ifs.seekg(0,std::ios::beg);
    std::vector<uint8_t> buf(static_cast<size_t>(sz));
    if(!ifs.read(reinterpret_cast<char*>(buf.data()), sz)) return false;
    return parseModelFromMemory(buf, path, out);
}

ModelLoader::ExportedMesh ModelLoader::exportFromVBuf(const std::vector<float>& vbuf, const std::vector<unsigned int>& ibuf, const glm::mat4& modelMat, int stride){
    ExportedMesh out;
    if(vbuf.empty() || ibuf.empty()) return out;
    size_t vcount = vbuf.size() / stride;
    out.vertices.reserve(vcount);
    for(size_t i=0;i+2<vbuf.size(); i+=stride){ glm::vec4 p(vbuf[i], vbuf[i+1], vbuf[i+2], 1.0f); glm::vec4 tp = modelMat * p; out.vertices.emplace_back(tp.x, tp.y, tp.z); }
    out.indices = ibuf;
    // compute radius
    std::vector<float> rads; rads.reserve(out.vertices.size());
    for(auto &v : out.vertices) rads.push_back(glm::length(v));
    if(!rads.empty()){
        std::sort(rads.begin(), rads.end());
        out.boundRadius = rads[rads.size()/2];
    }
    return out;
}