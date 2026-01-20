#include "ModelViewer.hpp"
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <GL/glew.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <vector>
#include <string>
#include <iostream>
#include <fstream>
#include <functional>
#include <cstring>
#include <stb_image.h>
// Compatibility macros for Assimp enums across versions
#ifndef aiTextureType_OCCLUSION
#ifdef aiTextureType_AMBIENT_OCCLUSION
#define aiTextureType_OCCLUSION aiTextureType_AMBIENT_OCCLUSION
#endif
#endif
#ifndef aiTextureType_ROUGHNESS
#ifdef aiTextureType_DIFFUSE_ROUGHNESS
#define aiTextureType_ROUGHNESS aiTextureType_DIFFUSE_ROUGHNESS
#endif
#endif
#include <plog/Log.h>
#include <thread>
#include <atomic>
#include <memory>
#include <algorithm>

// Minimal single-file shader helpers
static GLuint compileShader(GLenum type, const char* src){
    GLuint s = glCreateShader(type);
    glShaderSource(s,1,&src,nullptr);
    glCompileShader(s);
    GLint ok=0; glGetShaderiv(s,GL_COMPILE_STATUS,&ok); if(!ok){ char buf[1024]; glGetShaderInfoLog(s,1024,nullptr,buf); std::cerr<<"Shader compile error: "<<buf<<"\n"; glDeleteShader(s); return 0; }
    return s;
}
static GLuint linkProgram(GLuint vs, GLuint fs){
    GLuint p = glCreateProgram(); glAttachShader(p,vs); glAttachShader(p,fs); glLinkProgram(p); GLint ok=0; glGetProgramiv(p,GL_LINK_STATUS,&ok); if(!ok){ char buf[1024]; glGetProgramInfoLog(p,1024,nullptr,buf); std::cerr<<"Program link error: "<<buf<<"\n"; glDeleteProgram(p); return 0; } return p;
}

// ParsedModel holds CPU-side data produced by parsing (no GL calls)
struct ParsedModel {
    std::vector<float> vbuf; // interleaved pos,norm,uv
    std::vector<unsigned int> ibuf;
    struct Tex { std::vector<uint8_t> pixels; int w=0,h=0; bool present=false; } albedo, normal, mrao;
    // additional texture slots parsed from model
    struct TexExt : Tex { };
    TexExt emissive;
    TexExt occlusion;
    TexExt roughness;
    TexExt metallic;
    TexExt specular;
    TexExt height;
    glm::vec3 color{0.8f,0.8f,0.9f};
    std::string name;

    // Per-material data (for models with multiple materials)
    struct Material {
        Tex albedo, normal, mrao, emissive;
        glm::vec3 baseColor{0.8f,0.8f,0.9f};
        float metallic=0.0f, roughness=1.0f, ao=1.0f;
        bool populated=false;
    };
    std::vector<Material> materials;

    // Per-mesh draw ranges (after flattening meshes we record ranges so we can bind per-mesh materials)
    struct MeshRange { unsigned int startIndex=0; unsigned int indexCount=0; unsigned int materialIndex=0; };
    std::vector<MeshRange> meshRanges;

    // computed transform to center & scale the model to a unit-ish size
    glm::mat4 modelMat = glm::mat4(1.0f);
    float boundRadius = 1.0f; // radius in model space
};

// Compute a model-space transform that recenters and scales the model to fit into a unit-ish
// bounding sphere. outRadius is the radius of the model after scaling (in model space).
static void computeModelTransform(const std::vector<float>& vbuf, glm::mat4& outModel, float& outRadius, int stride = 8){
    if(vbuf.empty()){ outModel = glm::mat4(1.0f); outRadius = 1.0f; return; }
    glm::vec3 mn(1e30f,1e30f,1e30f), mx(-1e30f,-1e30f,-1e30f);
    for(size_t i=0;i+2<vbuf.size(); i+=stride){ glm::vec3 p(vbuf[i], vbuf[i+1], vbuf[i+2]); mn = glm::min(mn,p); mx = glm::max(mx,p); }
    glm::vec3 center = (mn + mx) * 0.5f;
    glm::vec3 ext = mx - mn;
    float maxExtent = std::max(std::max(ext.x, ext.y), ext.z);
    float scale = 1.0f;
    if(maxExtent > 1e-6f) scale = 2.0f / maxExtent; // scale so max extent spans ~2 units
    outModel = glm::translate(glm::mat4(1.0f), -center) * glm::scale(glm::mat4(1.0f), glm::vec3(scale));
    outRadius = (maxExtent * scale) * 0.5f;
    if(outRadius <= 0.0f) outRadius = 1.0f;
} 

// Very small vertex/fragment shader
static const char* vs_src = R"(
#version 330 core
layout(location=0) in vec3 in_pos;
layout(location=1) in vec3 in_normal;
layout(location=2) in vec2 in_uv;
layout(location=3) in vec4 in_tangent; // xyz = tangent, w = bitangent sign
uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat4 uModelView;
uniform mat3 uNormal;
out vec3 vNormal;
out vec3 vPos;
out vec2 vUV;
out vec3 vT;
out vec3 vB;
void main(){ vNormal = normalize(uNormal * in_normal); vec3 t = normalize(uNormal * in_tangent.xyz); vT = t; vB = cross(vNormal, t) * in_tangent.w; vPos = vec3(uModelView * vec4(in_pos,1.0)); vUV = in_uv; gl_Position = uMVP * vec4(in_pos,1.0); }
)";

static const char* fs_src = R"(
#version 330 core
in vec3 vNormal; in vec3 vPos; in vec2 vUV; in vec3 vT; in vec3 vB; out vec4 out_color;
uniform vec3 uLightDir; uniform vec3 uColor;
uniform bool uHasAlbedo; uniform sampler2D uAlbedo;
uniform bool uHasNormal; uniform sampler2D uNormalTex;
uniform bool uHasMRAO; uniform sampler2D uMRAO;
uniform bool uHasEmissive; uniform sampler2D uEmissive;
void main(){
    vec3 N = normalize(vNormal);
    if(uHasNormal){ vec3 ns = texture(uNormalTex, vUV).rgb; ns = ns * 2.0 - 1.0; mat3 tbn = mat3(normalize(vT), normalize(vB), normalize(vNormal)); N = normalize(tbn * ns); }
    vec3 V = normalize(-vPos);
    vec3 L = normalize(-uLightDir);
    float NdotL = max(dot(N,L), 0.0);
    vec3 baseColor = uColor;
    if(uHasAlbedo) baseColor = texture(uAlbedo, vUV).rgb;
    float metallic = 0.0; float roughness = 1.0; float ao = 1.0;
    if(uHasMRAO){ vec4 m = texture(uMRAO, vUV); ao = m.r; roughness = m.g; metallic = m.b; }
    vec3 diffuse = baseColor * (1.0 - metallic);
    // crude lighting: diffuse + simple specular (roughness affects exponent)
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    float NdotV = max(dot(N,V), 0.001);
    float expo = mix(1.0, 128.0, 1.0 - clamp(roughness, 0.0, 1.0));
    float spec = pow(max(dot(reflect(-L, N), V), 0.0), expo);
    vec3 col = diffuse * (0.2 + 0.8 * NdotL) + F0 * spec;
    col *= ao;
    if(uHasEmissive) col += texture(uEmissive, vUV).rgb;
    out_color = vec4(col,1.0);
}
)";

struct ModelViewer::Impl {
    // GL resources
    GLuint vao = 0, vbo = 0, ibo = 0;
    GLsizei indexCount = 0;
    GLuint prog = 0;
    GLuint fbo = 0, fboTex = 0, rbo = 0;
    int fbW=0, fbH=0;
    // Whether GL resources (shaders, VAO/VBO/IBO) have been created lazily
    bool glInitialized = false;

    // Textures for basic PBR (albedo, normal, metallic-roughness+ao) + emissive/specular
    GLuint albedoTex = 0, normalTex = 0, mraoTex = 0, emissiveTex = 0;
    bool hasAlbedo = false, hasNormal = false, hasMrao = false, hasEmissive = false;
    // For multi-material models we store per-material GL textures here
    struct MaterialGL { GLuint albedo=0, normal=0, mrao=0, emissive=0; bool hasAlbedo=false, hasNormal=false, hasMrao=false, hasEmissive=false; glm::vec3 baseColor{0.8f,0.8f,0.9f}; float metallic=0.0f, roughness=1.0f, ao=1.0f; };
    std::vector<MaterialGL> materialGLs;
    std::vector<ParsedModel::MeshRange> meshRanges;
    std::function<std::vector<uint8_t>(const std::string&)> textureLoader;

    // Camera
    float yaw = 0.0f, pitch = 0.0f, distance = 3.0f;
    ImVec2 lastMouse{-1,-1}; bool rotating = false;
    // Panning (in view-space units)
    float panX = 0.0f, panY = 0.0f;

    // Model transform (centers+scales the mesh to a unit-ish size)
    glm::mat4 modelMat = glm::mat4(1.0f);

    // Model loaded flag
    bool hasModel = false;
    std::string name;
    glm::vec3 color{0.8f,0.8f,0.9f};

    // Diagnostics: whether the last load attempt failed
    bool lastLoadFailed = false;

    // Async parsing/upload state
    std::atomic<bool> parsing{false};
    std::atomic<bool> uploading{false};
    std::shared_ptr<ParsedModel> pendingParsedModel;
    std::mutex parsedMutex;
    std::atomic<bool> parseFailed{false};
    std::string parseFailMessage;

    Impl(){
        // Defer GL resource creation to avoid blocking the UI when many ModelViewer objects
        // are created (shader compile and glGen* can be somewhat expensive).
        // Resources will be created lazily in ensureGLInitialized() on the main thread when needed.
        glInitialized = false;
    }

    void ensureGLInitialized(){
        if(glInitialized) return;
        PLOGV << "mv:ensureGLInitialized";
        GLuint vs = compileShader(GL_VERTEX_SHADER, vs_src);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fs_src);
        prog = linkProgram(vs, fs);
        if(vs) glDeleteShader(vs); if(fs) glDeleteShader(fs);
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ibo);
        glInitialized = true;
    }
    ~Impl(){ if(prog) glDeleteProgram(prog); if(vbo) glDeleteBuffers(1,&vbo); if(ibo) glDeleteBuffers(1,&ibo); if(vao) glDeleteVertexArrays(1,&vao); if(fbo) glDeleteFramebuffers(1,&fbo); if(fboTex) glDeleteTextures(1,&fboTex); if(rbo) glDeleteRenderbuffers(1,&rbo); if(albedoTex) glDeleteTextures(1,&albedoTex); if(normalTex) glDeleteTextures(1,&normalTex); if(mraoTex) glDeleteTextures(1,&mraoTex); if(emissiveTex) glDeleteTextures(1,&emissiveTex);
        // delete per-material textures
        for(auto &mg : materialGLs){ if(mg.albedo) glDeleteTextures(1,&mg.albedo); if(mg.normal) glDeleteTextures(1,&mg.normal); if(mg.mrao) glDeleteTextures(1,&mg.mrao); if(mg.emissive) glDeleteTextures(1,&mg.emissive); }
    }

    void ensureFBOSize(int w, int h){
        if(w<=0||h<=0) return;
        if(fbW==w && fbH==h) return;
        if(fbo==0) glGenFramebuffers(1,&fbo);
        if(fboTex==0) glGenTextures(1,&fboTex);
        if(rbo==0) glGenRenderbuffers(1,&rbo);
        fbW=w; fbH=h;
        glBindTexture(GL_TEXTURE_2D,fboTex);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,w,h,0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glBindRenderbuffer(GL_RENDERBUFFER,rbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, w, h);
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fboTex, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, rbo);
        GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
        if(status != GL_FRAMEBUFFER_COMPLETE) std::cerr<<"ModelViewer FBO incomplete\n";
        glBindFramebuffer(GL_FRAMEBUFFER,0);
    }

    GLuint createTextureFromBytes(const uint8_t* bytes, int len, bool srgb=false){
        int w=0,h=0,channels=0; unsigned char* image = stbi_load_from_memory(bytes, len, &w, &h, &channels, 4);
        if(!image) return 0;
        GLuint tid=0; glGenTextures(1,&tid); glBindTexture(GL_TEXTURE_2D, tid);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        GLint internal = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
        glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(image);
        return tid;
    }

    // Decode compressed image bytes into RGBA pixels (worker thread safe)
    static bool decodeCompressedImage(const uint8_t* bytes, int len, std::vector<uint8_t>& outPixels, int& outW, int& outH){
        int channels=0; unsigned char* image = stbi_load_from_memory(bytes, len, &outW, &outH, &channels, 4);
        if(!image) return false;
        outPixels.assign(image, image + (outW*outH*4));
        stbi_image_free(image);
        return true;
    }

    // Decode an Assimp embedded texture (handles compressed mHeight==0 or raw mHeight>0)
    static bool decodeAssimpTexture(const aiTexture* t, std::vector<uint8_t>& outPixels, int& outW, int& outH){
        if(!t) return false;
        if(t->mHeight == 0){
            // compressed bytes
            return decodeCompressedImage(reinterpret_cast<const uint8_t*>(t->pcData), static_cast<int>(t->mWidth), outPixels, outW, outH);
        } else {
            // raw RGBA texels (aiTexel)
            outW = t->mWidth; outH = t->mHeight;
            outPixels.resize(outW * outH * 4);
            memcpy(outPixels.data(), t->pcData, outW * outH * 4);
            return true;
        }
    }

    // Decode external bytes vector into RGBA pixels (worker thread safe)
    static bool decodeBytesToRGBA(const std::vector<uint8_t>& bytes, std::vector<uint8_t>& outPixels, int& outW, int& outH){
        return decodeCompressedImage(bytes.data(), static_cast<int>(bytes.size()), outPixels, outW, outH);
    }

    // Upload raw RGBA pixels (already decoded) into a GL texture (main thread only)
    GLuint createTextureFromRGBA(const uint8_t* rgba, int w, int h, bool srgb=false){
        if(!rgba || w<=0||h<=0) return 0;
        GLuint tid=0; glGenTextures(1,&tid); glBindTexture(GL_TEXTURE_2D, tid);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        GLint internal = srgb ? GL_SRGB8_ALPHA8 : GL_RGBA8;
        glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
        glGenerateMipmap(GL_TEXTURE_2D);
        return tid;
    }

    void drawGL(int w,int h){
        PLOGV << "drawGL:enter hasModel=" << hasModel << " indexCount=" << indexCount << " vao=" << vao << " vbo=" << vbo << " ibo=" << ibo << " fbo=" << fbo << " fboTex=" << fboTex;
        if(!hasModel) return;
        ensureFBOSize(w,h);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        GLint bound=0; glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound); PLOGV << "drawGL:bound_fb=" << bound;
        glViewport(0,0,w,h);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.07f,0.07f,0.08f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Setup matrices
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), w>0? (float)w/(float)h : 1.0f, 0.01f, 100.0f);
        glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0,0,-distance));
        view = glm::rotate(view, glm::radians(pitch), glm::vec3(1,0,0));
        view = glm::rotate(view, glm::radians(yaw), glm::vec3(0,1,0));
        // apply panning (view-space translation)
        view = glm::translate(view, glm::vec3(panX, panY, 0.0f));
        // apply model transform computed from geometry bounds
        glm::mat4 model = this->modelMat;
        glm::mat4 modelView = view * model;
        glm::mat4 mvp = proj * modelView;
        // normal matrix should be computed from modelView so normals are in view space
        glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(modelView)));

        glUseProgram(prog);
        GLint loc = glGetUniformLocation(prog, "uMVP"); if(loc>=0) glUniformMatrix4fv(loc,1,GL_FALSE,glm::value_ptr(mvp));
        loc = glGetUniformLocation(prog, "uModel"); if(loc>=0) glUniformMatrix4fv(loc,1,GL_FALSE,glm::value_ptr(model));
        loc = glGetUniformLocation(prog, "uModelView"); if(loc>=0) glUniformMatrix4fv(loc,1,GL_FALSE,glm::value_ptr(modelView));
        loc = glGetUniformLocation(prog, "uNormal"); if(loc>=0) glUniformMatrix3fv(loc,1,GL_FALSE,glm::value_ptr(normalMat));
        // transform light direction into view space (directional light)
        glm::vec3 lightDir = glm::normalize(glm::vec3(view * glm::vec4(0.4f, 0.4f, 1.0f, 0.0f)));
        loc = glGetUniformLocation(prog, "uLightDir"); if(loc>=0) glUniform3f(loc, lightDir.x, lightDir.y, lightDir.z);
        loc = glGetUniformLocation(prog, "uColor"); if(loc>=0) glUniform3f(loc, color.r, color.g, color.b);

        // Bind PBR textures if present
        GLint hasLoc = glGetUniformLocation(prog, "uHasAlbedo"); if(hasLoc>=0) glUniform1i(hasLoc, hasAlbedo?1:0);
        if(hasAlbedo){ glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, albedoTex); GLint tloc = glGetUniformLocation(prog, "uAlbedo"); if(tloc>=0) glUniform1i(tloc, 0); }
        GLint hasNormLoc = glGetUniformLocation(prog, "uHasNormal"); if(hasNormLoc>=0) glUniform1i(hasNormLoc, hasNormal?1:0);
        if(hasNormal){ glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, normalTex); GLint tloc = glGetUniformLocation(prog, "uNormalTex"); if(tloc>=0) glUniform1i(tloc, 1); }
        GLint hasMraoLoc = glGetUniformLocation(prog, "uHasMRAO"); if(hasMraoLoc>=0) glUniform1i(hasMraoLoc, hasMrao?1:0);
        if(hasMrao){ glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, mraoTex); GLint tloc = glGetUniformLocation(prog, "uMRAO"); if(tloc>=0) glUniform1i(tloc, 2); }
        GLint hasEmLoc = glGetUniformLocation(prog, "uHasEmissive"); if(hasEmLoc>=0) glUniform1i(hasEmLoc, hasEmissive?1:0);
        if(hasEmissive){ glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, emissiveTex); GLint tloc = glGetUniformLocation(prog, "uEmissive"); if(tloc>=0) glUniform1i(tloc, 3); }

        if(!meshRanges.empty()){
            glBindVertexArray(vao);
            for(const auto &r : meshRanges){
                // Bind material for this mesh range
                const MaterialGL *mg = nullptr;
                if(r.materialIndex < materialGLs.size()) mg = &materialGLs[r.materialIndex];
                GLint hasLoc = glGetUniformLocation(prog, "uHasAlbedo"); if(hasLoc>=0) glUniform1i(hasLoc, (mg && mg->hasAlbedo)?1:0);
                if(mg && mg->hasAlbedo){ glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, mg->albedo); GLint tloc = glGetUniformLocation(prog, "uAlbedo"); if(tloc>=0) glUniform1i(tloc, 0); }
                GLint hasNormLoc = glGetUniformLocation(prog, "uHasNormal"); if(hasNormLoc>=0) glUniform1i(hasNormLoc, (mg && mg->hasNormal)?1:0);
                if(mg && mg->hasNormal){ glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, mg->normal); GLint tloc = glGetUniformLocation(prog, "uNormalTex"); if(tloc>=0) glUniform1i(tloc, 1); }
                GLint hasMraoLoc = glGetUniformLocation(prog, "uHasMRAO"); if(hasMraoLoc>=0) glUniform1i(hasMraoLoc, (mg && mg->hasMrao)?1:0);
                if(mg && mg->hasMrao){ glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, mg->mrao); GLint tloc = glGetUniformLocation(prog, "uMRAO"); if(tloc>=0) glUniform1i(tloc, 2); }
                GLint hasEmLoc = glGetUniformLocation(prog, "uHasEmissive"); if(hasEmLoc>=0) glUniform1i(hasEmLoc, (mg && mg->hasEmissive)?1:0);
                if(mg && mg->hasEmissive){ glActiveTexture(GL_TEXTURE3); glBindTexture(GL_TEXTURE_2D, mg->emissive); GLint tloc = glGetUniformLocation(prog, "uEmissive"); if(tloc>=0) glUniform1i(tloc, 3); }
                // set per-material color if available
                if(mg){ GLint locc = glGetUniformLocation(prog, "uColor"); if(locc>=0) glUniform3f(locc, mg->baseColor.r, mg->baseColor.g, mg->baseColor.b); }
                // draw this range
                glDrawElements(GL_TRIANGLES, r.indexCount, GL_UNSIGNED_INT, (void*)(static_cast<size_t>(r.startIndex) * sizeof(unsigned int)));
                // unbind textures
                glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
                glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
                glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
            }
            glBindVertexArray(0);
        } else if(indexCount > 0){
            glBindVertexArray(vao);
            glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
            glBindVertexArray(0);
        } else {
            PLOGW << "drawGL:warning indexCount==0 (nothing to draw)";
        }

        GLenum err = glGetError();
        if(err != GL_NO_ERROR) PLOGW << "drawGL glError=0x" << std::hex << err;

        // Unbind textures/program/FBO and restore state
        glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, 0);
        glUseProgram(0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glDisable(GL_DEPTH_TEST);
        PLOGV << "drawGL:exit final_fb=0";
    }
};

ModelViewer::ModelViewer(){ impl = new Impl(); }
ModelViewer::~ModelViewer(){ delete impl; }

// Very basic Assimp loader: flatten all meshes into single VBO/IBO (positions,normals,uv)
void ModelViewer::setTextureLoader(TextureLoader loader){ if(impl) impl->textureLoader = loader; }

bool ModelViewer::isLoaded() const{ return impl ? impl->hasModel : false; }

bool ModelViewer::loadFailed() const{ return impl ? impl->lastLoadFailed : false; }

bool ModelViewer::isLoading() const{ if(!impl) return false; return impl->parsing.load() || impl->uploading.load() || (impl->pendingParsedModel != nullptr); }

bool ModelViewer::loadFromMemory(const std::vector<uint8_t>& data, const std::string& name){
    // Synchronous legacy path (keeps previous behavior)
    PLOGI << "mv:load (sync) name='" << name << "' data_size=" << data.size();
    clear();
    impl->lastLoadFailed = false;
    // Use the existing logic for compatibility (blocking)
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFileFromMemory(data.data(), data.size(), aiProcess_Triangulate|aiProcess_GenNormals|aiProcess_JoinIdenticalVertices|aiProcess_OptimizeMeshes|aiProcess_PreTransformVertices|aiProcess_CalcTangentSpace, name.c_str());
    if(!scene || !scene->HasMeshes()){ PLOGW << "mv:loadFailed name='" << name << "'"; impl->lastLoadFailed = true; return false; }

    // Synchronous parsing into a ParsedModel (includes tangent + per-material handling)
    ParsedModel parsed; parsed.name = name;
    auto loadMatTexToTex = [&](aiMaterial* mat, aiTextureType type, ParsedModel::Tex &dest)->bool{
        aiString tpStr; if(mat->GetTextureCount(type) <= 0) return false; mat->GetTexture(type, 0, &tpStr); if(tpStr.length <= 0) return false;
        std::string tp(tpStr.C_Str());
        if(tp.size()>0 && tp[0]=='*'){
            int idx = atoi(tp.c_str()+1); if(idx < 0 || idx >= (int)scene->mNumTextures) { PLOGW << "mv:embedded tex idx out of range: " << tp; return false; }
            aiTexture* t = scene->mTextures[idx]; if(!t) return false;
            int w=0,h=0; std::vector<uint8_t> pix;
            if(!Impl::decodeAssimpTexture(t, pix, w, h)) { PLOGW << "mv:failed to decode embedded tex idx="<<idx; return false; }
            dest.present = true; dest.w = w; dest.h = h; dest.pixels = std::move(pix); return true;
        } else {
            if(impl->textureLoader){ auto bytes = impl->textureLoader(tp); if(!bytes.empty()){ int w=0,h=0; std::vector<uint8_t> pix; if(!Impl::decodeBytesToRGBA(bytes, pix, w, h)) { PLOGW << "mv:stb failed to decode external tex: "<<tp; return false; } dest.present = true; dest.w = w; dest.h = h; dest.pixels = std::move(pix); return true; } }
        }
        return false;
    };

    // flatten meshes and record per-mesh ranges + per-material textures/parameters
    unsigned int baseV = 0;
    for(unsigned int m=0;m<scene->mNumMeshes;++m){
        aiMesh* mesh = scene->mMeshes[m];
        unsigned int startIdx = static_cast<unsigned int>(parsed.ibuf.size());
        unsigned int matIdx = (mesh->mMaterialIndex < scene->mNumMaterials) ? mesh->mMaterialIndex : 0;
        if(parsed.materials.size() <= matIdx) parsed.materials.resize(matIdx+1);
        ParsedModel::Material &pmat = parsed.materials[matIdx];
        if(!pmat.populated){
            // load textures for this material
            loadMatTexToTex(scene->mMaterials[matIdx], aiTextureType_BASE_COLOR, pmat.albedo) || loadMatTexToTex(scene->mMaterials[matIdx], aiTextureType_DIFFUSE, pmat.albedo);
            loadMatTexToTex(scene->mMaterials[matIdx], aiTextureType_NORMALS, pmat.normal);
            loadMatTexToTex(scene->mMaterials[matIdx], aiTextureType_EMISSIVE, pmat.emissive);
            ParsedModel::Tex aoT, rT, mT; bool hasAO = loadMatTexToTex(scene->mMaterials[matIdx], aiTextureType_AMBIENT_OCCLUSION, aoT); bool hasR = loadMatTexToTex(scene->mMaterials[matIdx], aiTextureType_DIFFUSE_ROUGHNESS, rT); bool hasM = loadMatTexToTex(scene->mMaterials[matIdx], aiTextureType_METALNESS, mT);
            if(hasAO || hasR || hasM){ int refW = hasAO ? aoT.w : (hasR ? rT.w : mT.w); int refH = hasAO ? aoT.h : (hasR ? rT.h : mT.h); pmat.mrao.present = true; pmat.mrao.w = refW; pmat.mrao.h = refH; pmat.mrao.pixels.assign(refW*refH*4, 255);
                auto sampleFirst = [&](const ParsedModel::Tex &t, int x, int y, int refW, int refH)->uint8_t{ if(!t.present) return 255; int sx = (t.w==refW) ? x : (x * t.w / refW); int sy = (t.h==refH) ? y : (y * t.h / refH); return t.pixels[(sy*t.w + sx)*4 + 0]; };
                for(int y=0;y<refH;++y) for(int x=0;x<refW;++x){ int idx=(y*refW + x)*4; pmat.mrao.pixels[idx+0] = hasAO ? sampleFirst(aoT,x,y,refW,refH) : 255; pmat.mrao.pixels[idx+1] = hasR ? sampleFirst(rT,x,y,refW,refH) : 255; pmat.mrao.pixels[idx+2] = hasM ? sampleFirst(mT,x,y,refW,refH) : 0; pmat.mrao.pixels[idx+3] = 255; }
            } else {
                loadMatTexToTex(scene->mMaterials[matIdx], aiTextureType_METALNESS, pmat.mrao) || loadMatTexToTex(scene->mMaterials[matIdx], aiTextureType_UNKNOWN, pmat.mrao);
            }
            // basic PBR factors
            aiColor3D bc(0.8f,0.8f,0.9f); if(AI_SUCCESS == scene->mMaterials[matIdx]->Get(AI_MATKEY_BASE_COLOR, bc)){ pmat.baseColor = glm::vec3(bc.r, bc.g, bc.b); } else if(AI_SUCCESS == scene->mMaterials[matIdx]->Get(AI_MATKEY_COLOR_DIFFUSE, bc)){ pmat.baseColor = glm::vec3(bc.r, bc.g, bc.b); }
            float mf=0.0f, rf=1.0f; scene->mMaterials[matIdx]->Get(AI_MATKEY_METALLIC_FACTOR, mf); scene->mMaterials[matIdx]->Get(AI_MATKEY_ROUGHNESS_FACTOR, rf); pmat.metallic = mf; pmat.roughness = rf;
            pmat.populated = true;
        }

        for(unsigned int i=0;i<mesh->mNumVertices;++i){
            aiVector3D p = mesh->mVertices[i]; aiVector3D n = mesh->HasNormals() ? mesh->mNormals[i] : aiVector3D(0,0,1);
            aiVector3D uv = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][i] : aiVector3D(0,0,0);
            parsed.vbuf.push_back(p.x); parsed.vbuf.push_back(p.y); parsed.vbuf.push_back(p.z);
            parsed.vbuf.push_back(n.x); parsed.vbuf.push_back(n.y); parsed.vbuf.push_back(n.z);
            parsed.vbuf.push_back(uv.x); parsed.vbuf.push_back(uv.y);
            // tangents (if CalcTangentSpace was used)
            if(mesh->HasTangentsAndBitangents()){
                aiVector3D t = mesh->mTangents[i]; aiVector3D b = mesh->mBitangents[i];
                glm::vec3 nn(n.x,n.y,n.z), tt(t.x,t.y,t.z), bb(b.x,b.y,b.z); float sign = (glm::dot(glm::cross(nn, tt), bb) < 0.0f) ? -1.0f : 1.0f;
                parsed.vbuf.push_back(t.x); parsed.vbuf.push_back(t.y); parsed.vbuf.push_back(t.z); parsed.vbuf.push_back(sign);
            } else { parsed.vbuf.push_back(0.0f); parsed.vbuf.push_back(0.0f); parsed.vbuf.push_back(0.0f); parsed.vbuf.push_back(1.0f); }
        }
        for(unsigned int f=0; f<mesh->mNumFaces; ++f){ aiFace &face = mesh->mFaces[f]; if(face.mNumIndices != 3) continue; parsed.ibuf.push_back(baseV + face.mIndices[0]); parsed.ibuf.push_back(baseV + face.mIndices[1]); parsed.ibuf.push_back(baseV + face.mIndices[2]); }
        unsigned int idxCount = static_cast<unsigned int>(parsed.ibuf.size()) - startIdx;
        parsed.meshRanges.push_back(ParsedModel::MeshRange{startIdx, idxCount, matIdx});
        baseV += mesh->mNumVertices;
    }

    // Compute transform (include stride: 12 floats per vertex)
    float radius = 1.0f; computeModelTransform(parsed.vbuf, parsed.modelMat, radius, 12); parsed.boundRadius = radius;

    // Upload parsed model directly (synchronous upload)
    impl->ensureGLInitialized();
    glBindVertexArray(impl->vao);
    glBindBuffer(GL_ARRAY_BUFFER, impl->vbo);
    glBufferData(GL_ARRAY_BUFFER, parsed.vbuf.size()*sizeof(float), parsed.vbuf.data(), GL_STATIC_DRAW);
    // stride includes tangent = 12 floats
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,12*sizeof(float),(void*)(0));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,12*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,12*sizeof(float),(void*)(6*sizeof(float)));
    glEnableVertexAttribArray(3); glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,12*sizeof(float),(void*)(8*sizeof(float)));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, impl->ibo);
    impl->indexCount = static_cast<GLsizei>(parsed.ibuf.size());
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, parsed.ibuf.size()*sizeof(unsigned int), parsed.ibuf.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);

    impl->materialGLs.clear(); impl->materialGLs.resize(parsed.materials.size());
    for(size_t i=0;i<parsed.materials.size();++i){ auto &m = parsed.materials[i]; auto &mg = impl->materialGLs[i];
        if(m.albedo.present){ if(mg.albedo) glDeleteTextures(1, &mg.albedo); mg.albedo = impl->createTextureFromRGBA(m.albedo.pixels.data(), m.albedo.w, m.albedo.h, true); mg.hasAlbedo = mg.albedo!=0; if(mg.hasAlbedo) PLOGI << "mv:loaded material "<<i<<" albedo w="<<m.albedo.w<<" h="<<m.albedo.h; }
        if(m.normal.present){ if(mg.normal) glDeleteTextures(1, &mg.normal); mg.normal = impl->createTextureFromRGBA(m.normal.pixels.data(), m.normal.w, m.normal.h, false); mg.hasNormal = mg.normal!=0; if(mg.hasNormal) PLOGI << "mv:loaded material "<<i<<" normal w="<<m.normal.w<<" h="<<m.normal.h; }
        if(m.mrao.present){ if(mg.mrao) glDeleteTextures(1, &mg.mrao); mg.mrao = impl->createTextureFromRGBA(m.mrao.pixels.data(), m.mrao.w, m.mrao.h, false); mg.hasMrao = mg.mrao!=0; if(mg.hasMrao) PLOGI << "mv:loaded material "<<i<<" mrao w="<<m.mrao.w<<" h="<<m.mrao.h; }
        if(m.emissive.present){ if(mg.emissive) glDeleteTextures(1, &mg.emissive); mg.emissive = impl->createTextureFromRGBA(m.emissive.pixels.data(), m.emissive.w, m.emissive.h, false); mg.hasEmissive = mg.emissive!=0; if(mg.hasEmissive) PLOGI << "mv:loaded material "<<i<<" emissive w="<<m.emissive.w<<" h="<<m.emissive.h; }
        mg.baseColor = m.baseColor; mg.metallic = m.metallic; mg.roughness = m.roughness; mg.ao = m.ao;
    }
    impl->meshRanges = parsed.meshRanges;

    // apply parsed model transform so the mesh is centered and scaled
    impl->modelMat = parsed.modelMat;
    impl->distance = parsed.boundRadius * 2.5f;
    impl->yaw = 0.0f; impl->pitch = 0.0f; impl->panX = impl->panY = 0.0f;
    impl->hasModel = true; impl->name = name; impl->color = parsed.color; impl->lastLoadFailed = false; PLOGI << "mv:loaded name='" << name << "' indexCount=" << impl->indexCount << " radius=" << impl->distance;
    return true;
}

void ModelViewer::renderToRegion(const ImVec2& size){
    PLOGV << "mv:renderToRegion size=" << size.x << "x" << size.y << " hasModel=" << impl->hasModel << " fbo=" << impl->fbo << " fboTex=" << impl->fboTex;
    if(!impl->hasModel){ ImGui::TextUnformatted("No model loaded"); return; }
    // Mouse controls: simple orbit
    ImVec2 p = ImGui::GetCursorScreenPos();
    std::string btnId = std::string("##model_viewport_") + std::to_string(reinterpret_cast<uintptr_t>(this));
    ImGui::InvisibleButton(btnId.c_str(), size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // handle mouse drag: rotate with left-drag (no modifier), pan with middle-drag or Ctrl+left-drag
    bool hovered = ImGui::IsItemHovered();
    if(ImGui::IsItemActive()){
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        if(ImGui::IsMouseDragging(ImGuiMouseButton_Left) && !ImGui::GetIO().KeyCtrl){
            impl->yaw += delta.x * 0.2f;
            impl->pitch += delta.y * 0.2f;
        } else if(ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || (ImGui::IsMouseDragging(ImGuiMouseButton_Left) && ImGui::GetIO().KeyCtrl)){
            float s = impl->distance * 0.0025f; // pan sensitivity scales with distance
            impl->panX += delta.x * s;
            impl->panY += -delta.y * s; // invert Y so drag-up moves content up
        }
    }
    // zoom only when hovering the image
    if(hovered && ImGui::GetIO().MouseWheel != 0.0f){ impl->distance *= (1.0f - ImGui::GetIO().MouseWheel*0.1f); if(impl->distance < 0.01f) impl->distance = 0.01f; }

    // Process any pending uploads or parse failures (handled centrally)
    processPendingUploads();

    impl->drawGL((int)size.x, (int)size.y);
    // Draw the framebuffer texture flush to the top-left of the reserved area so it appears inline
    ImGui::SetCursorScreenPos(p);
    ImGui::Image((ImTextureID)(intptr_t)impl->fboTex, size, ImVec2(0,1), ImVec2(1,0));
    // Advance cursor to just after the image so subsequent items layout correctly
    ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + size.y));
}

bool ModelViewer::renderWindow(const char* title, bool* p_open){
    bool open = true;
    ImGui::Begin(title, p_open, ImGuiWindowFlags_AlwaysAutoResize);
    if(!impl->hasModel){ ImGui::TextUnformatted("No model loaded"); if(ImGui::Button("Close")){ ImGui::End(); if(p_open) *p_open=false; return false; } ImGui::End(); return false; }
    ImGui::Text("Model: %s", impl->name.c_str());
    ImGui::Separator();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    ImVec2 desired = ImVec2(400,300);
    if(avail.x < desired.x) desired.x = avail.x;
    if(avail.y < desired.y) desired.y = avail.y - 40;
    renderToRegion(desired);
    ImGui::Separator();
    ImGui::Text("Controls: drag to rotate, scroll to zoom");
    if(ImGui::Button("Close")){ open = false; }
    ImGui::End();
    return open;
}

void ModelViewer::processPendingUploads(){
    if(!impl) return;
    std::shared_ptr<ParsedModel> ready;
    {
        std::lock_guard<std::mutex> l(impl->parsedMutex);
        if(impl->pendingParsedModel){ ready = impl->pendingParsedModel; impl->pendingParsedModel.reset(); }
    }
    if(ready){
        impl->uploading = true;
        impl->ensureGLInitialized();
        // Upload geometry
        PLOGV << "mv:uploading geometry v=" << ready->vbuf.size() << " i=" << ready->ibuf.size();
        glBindVertexArray(impl->vao);
        glBindBuffer(GL_ARRAY_BUFFER, impl->vbo);
        glBufferData(GL_ARRAY_BUFFER, ready->vbuf.size()*sizeof(float), ready->vbuf.data(), GL_STATIC_DRAW);
        // stride includes tangent (xyz + normal + uv + tangent(4)) = 12 floats
        glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,12*sizeof(float),(void*)(0));
        glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,12*sizeof(float),(void*)(3*sizeof(float)));
        glEnableVertexAttribArray(2); glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,12*sizeof(float),(void*)(6*sizeof(float)));
        glEnableVertexAttribArray(3); glVertexAttribPointer(3,4,GL_FLOAT,GL_FALSE,12*sizeof(float),(void*)(8*sizeof(float)));
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, impl->ibo);
        impl->indexCount = static_cast<GLsizei>(ready->ibuf.size());
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, ready->ibuf.size()*sizeof(unsigned int), ready->ibuf.data(), GL_STATIC_DRAW);
        glBindVertexArray(0);
        // Upload per-material textures
        impl->materialGLs.clear(); impl->materialGLs.resize(ready->materials.size());
        for(size_t i=0;i<ready->materials.size();++i){
            auto &m = ready->materials[i]; auto &mg = impl->materialGLs[i];
            if(m.albedo.present){ if(mg.albedo) glDeleteTextures(1, &mg.albedo); mg.albedo = impl->createTextureFromRGBA(m.albedo.pixels.data(), m.albedo.w, m.albedo.h, true); mg.hasAlbedo = mg.albedo!=0; if(mg.hasAlbedo) PLOGI << "mv:async uploaded material "<<i<<" albedo w="<<m.albedo.w<<" h="<<m.albedo.h; }
            if(m.normal.present){ if(mg.normal) glDeleteTextures(1, &mg.normal); mg.normal = impl->createTextureFromRGBA(m.normal.pixels.data(), m.normal.w, m.normal.h, false); mg.hasNormal = mg.normal!=0; if(mg.hasNormal) PLOGI << "mv:async uploaded material "<<i<<" normal w="<<m.normal.w<<" h="<<m.normal.h; }
            if(m.mrao.present){ if(mg.mrao) glDeleteTextures(1, &mg.mrao); mg.mrao = impl->createTextureFromRGBA(m.mrao.pixels.data(), m.mrao.w, m.mrao.h, false); mg.hasMrao = mg.mrao!=0; if(mg.hasMrao) PLOGI << "mv:async uploaded material "<<i<<" mrao w="<<m.mrao.w<<" h="<<m.mrao.h; }
            if(m.emissive.present){ if(mg.emissive) glDeleteTextures(1, &mg.emissive); mg.emissive = impl->createTextureFromRGBA(m.emissive.pixels.data(), m.emissive.w, m.emissive.h, false); mg.hasEmissive = mg.emissive!=0; if(mg.hasEmissive) PLOGI << "mv:async uploaded material "<<i<<" emissive w="<<m.emissive.w<<" h="<<m.emissive.h; }
            mg.baseColor = m.baseColor; mg.metallic = m.metallic; mg.roughness = m.roughness; mg.ao = m.ao;
        }
        impl->meshRanges = ready->meshRanges;
        // apply parsed model transform so the mesh is centered and scaled
        impl->modelMat = ready->modelMat;
        impl->distance = ready->boundRadius * 2.5f;
        impl->yaw = 0.0f; impl->pitch = 0.0f; impl->panX = impl->panY = 0.0f;
        impl->hasModel = true; impl->name = ready->name; impl->color = ready->color; impl->uploading = false; impl->parsing = false; impl->lastLoadFailed = false;
        PLOGI << "mv:async upload complete name='" << impl->name << "' indexCount=" << impl->indexCount << " radius=" << ready->boundRadius;

    } else {
        if(impl->parseFailed.load()){
            std::lock_guard<std::mutex> l(impl->parsedMutex);
            impl->lastLoadFailed = true;
            impl->parsing = false;
            PLOGW << "mv:parse failed: " << impl->parseFailMessage;
            impl->parseFailed = false;
            impl->parseFailMessage.clear();
        }
    }
}

void ModelViewer::clear(){ if(impl) { impl->hasModel=false; impl->indexCount=0; impl->name.clear(); impl->hasAlbedo = impl->hasNormal = impl->hasMrao = impl->hasEmissive = false; if(impl->albedoTex) { glDeleteTextures(1,&impl->albedoTex); impl->albedoTex=0; } if(impl->normalTex){ glDeleteTextures(1,&impl->normalTex); impl->normalTex=0; } if(impl->mraoTex){ glDeleteTextures(1,&impl->mraoTex); impl->mraoTex=0; } if(impl->emissiveTex){ glDeleteTextures(1,&impl->emissiveTex); impl->emissiveTex=0; }
    // free per-material GL textures
    for(auto &mg : impl->materialGLs){ if(mg.albedo) glDeleteTextures(1,&mg.albedo); if(mg.normal) glDeleteTextures(1,&mg.normal); if(mg.mrao) glDeleteTextures(1,&mg.mrao); if(mg.emissive) glDeleteTextures(1,&mg.emissive); }
    impl->materialGLs.clear(); impl->meshRanges.clear(); impl->modelMat = glm::mat4(1.0f); impl->distance = 3.0f; } }

void ModelViewer::loadFromMemoryAsync(const std::vector<uint8_t>& data, const std::string& name){
    if(!impl) return;
    // If already parsing/uploading, ignore or set message
    if(impl->parsing.load()) { PLOGW << "mv:already parsing ignore new load"; return; }
    impl->parsing = true;
    PLOGI << "mv:async parse start name='" << name << "' size=" << data.size();
    // Spawn worker to parse (Assimp + decode textures) — no GL calls here
    std::thread([this, data, name]() mutable {
        ParsedModel parsed; parsed.name = name;
        try{
            Assimp::Importer importer;
            const aiScene* scene = importer.ReadFileFromMemory(data.data(), data.size(), aiProcess_Triangulate|aiProcess_GenNormals|aiProcess_JoinIdenticalVertices|aiProcess_OptimizeMeshes|aiProcess_PreTransformVertices|aiProcess_CalcTangentSpace, name.c_str());
            if(!scene || !scene->HasMeshes()){
                PLOGW << "mv:async parse failed name='" << name << "'";
                // mark failure (will be observed on main thread in renderToRegion)
                {
                    std::lock_guard<std::mutex> l(impl->parsedMutex);
                    impl->parseFailed = true;
                    impl->parseFailMessage = std::string("Failed to parse model: ") + name;
                }
                impl->parsing = false;
                return;
            }
            // Per-material parsing: we'll lazily fill material entries as we encounter meshes
            auto loadMatTexToTex = [&](aiMaterial* mat, aiTextureType type, ParsedModel::Tex &dest)->bool{
                aiString tpStr; if(mat->GetTextureCount(type) <= 0) return false; mat->GetTexture(type, 0, &tpStr); if(tpStr.length <= 0) return false;
                std::string tp(tpStr.C_Str());
                if(tp.size()>0 && tp[0]=='*'){
                    int idx = atoi(tp.c_str()+1); if(idx < 0 || idx >= (int)scene->mNumTextures) { PLOGW << "mv:embedded tex idx out of range: " << tp; return false; }
                    aiTexture* t = scene->mTextures[idx]; if(!t) return false;
                    int w=0,h=0; std::vector<uint8_t> pix;
                    if(!Impl::decodeAssimpTexture(t, pix, w, h)) { PLOGW << "mv:failed to decode embedded tex idx="<<idx; return false; }
                    dest.present = true; dest.w = w; dest.h = h; dest.pixels = std::move(pix); return true;
                } else {
                    if(impl->textureLoader){ auto bytes = impl->textureLoader(tp); if(!bytes.empty()){ int w=0,h=0; std::vector<uint8_t> pix; if(!Impl::decodeBytesToRGBA(bytes, pix, w, h)) { PLOGW << "mv:stb failed to decode external tex: "<<tp; return false; } dest.present = true; dest.w = w; dest.h = h; dest.pixels = std::move(pix); return true; } }
                }
                return false;
            };
            // flatten meshes and record per-mesh ranges + per-material textures/parameters
            unsigned int baseV = 0;
            for(unsigned int m=0;m<scene->mNumMeshes;++m){
                aiMesh* mesh = scene->mMeshes[m];
                unsigned int startIdx = static_cast<unsigned int>(parsed.ibuf.size());
                unsigned int matIdx = (mesh->mMaterialIndex < scene->mNumMaterials) ? mesh->mMaterialIndex : 0;
                if(parsed.materials.size() <= matIdx) parsed.materials.resize(matIdx+1);
                ParsedModel::Material &pmat = parsed.materials[matIdx];
                if(!pmat.populated){
                    // load textures for this material
                    loadMatTexToTex(scene->mMaterials[matIdx], aiTextureType_BASE_COLOR, pmat.albedo) || loadMatTexToTex(scene->mMaterials[matIdx], aiTextureType_DIFFUSE, pmat.albedo);
                    loadMatTexToTex(scene->mMaterials[matIdx], aiTextureType_NORMALS, pmat.normal);
                    loadMatTexToTex(scene->mMaterials[matIdx], aiTextureType_EMISSIVE, pmat.emissive);
                    ParsedModel::Tex aoT, rT, mT; bool hasAO = loadMatTexToTex(scene->mMaterials[matIdx], aiTextureType_AMBIENT_OCCLUSION, aoT); bool hasR = loadMatTexToTex(scene->mMaterials[matIdx], aiTextureType_DIFFUSE_ROUGHNESS, rT); bool hasM = loadMatTexToTex(scene->mMaterials[matIdx], aiTextureType_METALNESS, mT);
                    if(hasAO || hasR || hasM){ int refW = hasAO ? aoT.w : (hasR ? rT.w : mT.w); int refH = hasAO ? aoT.h : (hasR ? rT.h : mT.h); pmat.mrao.present = true; pmat.mrao.w = refW; pmat.mrao.h = refH; pmat.mrao.pixels.assign(refW*refH*4, 255);
                        auto sampleFirst = [&](const ParsedModel::Tex &t, int x, int y, int refW, int refH)->uint8_t{ if(!t.present) return 255; int sx = (t.w==refW) ? x : (x * t.w / refW); int sy = (t.h==refH) ? y : (y * t.h / refH); return t.pixels[(sy*t.w + sx)*4 + 0]; };
                        for(int y=0;y<refH;++y) for(int x=0;x<refW;++x){ int idx=(y*refW + x)*4; pmat.mrao.pixels[idx+0] = hasAO ? sampleFirst(aoT,x,y,refW,refH) : 255; pmat.mrao.pixels[idx+1] = hasR ? sampleFirst(rT,x,y,refW,refH) : 255; pmat.mrao.pixels[idx+2] = hasM ? sampleFirst(mT,x,y,refW,refH) : 0; pmat.mrao.pixels[idx+3] = 255; }
                    } else {
                        loadMatTexToTex(scene->mMaterials[matIdx], aiTextureType_METALNESS, pmat.mrao) || loadMatTexToTex(scene->mMaterials[matIdx], aiTextureType_UNKNOWN, pmat.mrao);
                    }
                    // basic PBR factors
                    aiColor3D bc(0.8f,0.8f,0.9f); if(AI_SUCCESS == scene->mMaterials[matIdx]->Get(AI_MATKEY_BASE_COLOR, bc)){ pmat.baseColor = glm::vec3(bc.r, bc.g, bc.b); } else if(AI_SUCCESS == scene->mMaterials[matIdx]->Get(AI_MATKEY_COLOR_DIFFUSE, bc)){ pmat.baseColor = glm::vec3(bc.r, bc.g, bc.b); }
                    float mf=0.0f, rf=1.0f; scene->mMaterials[matIdx]->Get(AI_MATKEY_METALLIC_FACTOR, mf); scene->mMaterials[matIdx]->Get(AI_MATKEY_ROUGHNESS_FACTOR, rf); pmat.metallic = mf; pmat.roughness = rf;
                    pmat.populated = true;
                }

                for(unsigned int i=0;i<mesh->mNumVertices;++i){
                    aiVector3D p = mesh->mVertices[i]; aiVector3D n = mesh->HasNormals() ? mesh->mNormals[i] : aiVector3D(0,0,1);
                    aiVector3D uv = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][i] : aiVector3D(0,0,0);
                    parsed.vbuf.push_back(p.x); parsed.vbuf.push_back(p.y); parsed.vbuf.push_back(p.z);
                    parsed.vbuf.push_back(n.x); parsed.vbuf.push_back(n.y); parsed.vbuf.push_back(n.z);
                    parsed.vbuf.push_back(uv.x); parsed.vbuf.push_back(uv.y);
                    // tangents (if CalcTangentSpace was used)
                    if(mesh->HasTangentsAndBitangents()){
                        aiVector3D t = mesh->mTangents[i]; aiVector3D b = mesh->mBitangents[i];
                        glm::vec3 nn(n.x,n.y,n.z), tt(t.x,t.y,t.z), bb(b.x,b.y,b.z); float sign = (glm::dot(glm::cross(nn, tt), bb) < 0.0f) ? -1.0f : 1.0f;
                        parsed.vbuf.push_back(t.x); parsed.vbuf.push_back(t.y); parsed.vbuf.push_back(t.z); parsed.vbuf.push_back(sign);
                    } else { parsed.vbuf.push_back(0.0f); parsed.vbuf.push_back(0.0f); parsed.vbuf.push_back(0.0f); parsed.vbuf.push_back(1.0f); }
                }
                for(unsigned int f=0; f<mesh->mNumFaces; ++f){ aiFace &face = mesh->mFaces[f]; if(face.mNumIndices != 3) continue; parsed.ibuf.push_back(baseV + face.mIndices[0]); parsed.ibuf.push_back(baseV + face.mIndices[1]); parsed.ibuf.push_back(baseV + face.mIndices[2]); }
                unsigned int idxCount = static_cast<unsigned int>(parsed.ibuf.size()) - startIdx;
                parsed.meshRanges.push_back(ParsedModel::MeshRange{startIdx, idxCount, matIdx});
                baseV += mesh->mNumVertices;
            }
            // Compute transform for the parsed geometry so async upload can apply it on the main thread
            float radius = 1.0f; computeModelTransform(parsed.vbuf, parsed.modelMat, radius, 12); parsed.boundRadius = radius;
            // Parsing done; move parsed into pendingParsedModel protected by mutex
            {
                std::lock_guard<std::mutex> l(impl->parsedMutex);
                impl->pendingParsedModel = std::make_shared<ParsedModel>(std::move(parsed));
            }
            PLOGI << "mv:async parse done name='" << name << "'";
        } catch(...){ PLOGE << "mv:async parse exception"; {
                std::lock_guard<std::mutex> l(impl->parsedMutex);
                impl->parseFailed = true;
                impl->parseFailMessage = std::string("Failed to parse model: ") + name;
            } impl->parsing = false; }
    }).detach();
}

