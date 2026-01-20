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
#include <stb_image.h>

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

// Very small vertex/fragment shader
static const char* vs_src = R"(
#version 330 core
layout(location=0) in vec3 in_pos;
layout(location=1) in vec3 in_normal;
layout(location=2) in vec2 in_uv;
uniform mat4 uMVP;
uniform mat4 uModel;
uniform mat3 uNormal;
out vec3 vNormal;
out vec3 vPos;
out vec2 vUV;
void main(){ vNormal = normalize(uNormal * in_normal); vPos = vec3(uModel * vec4(in_pos,1.0)); vUV = in_uv; gl_Position = uMVP * vec4(in_pos,1.0); }
)";

static const char* fs_src = R"(
#version 330 core
in vec3 vNormal; in vec3 vPos; in vec2 vUV; out vec4 out_color;
uniform vec3 uLightDir; uniform vec3 uColor;
uniform bool uHasAlbedo; uniform sampler2D uAlbedo;
uniform bool uHasNormal; uniform sampler2D uNormalTex;
uniform bool uHasMRAO; uniform sampler2D uMRAO;
void main(){
    vec3 N = normalize(vNormal);
    vec3 V = normalize(-vPos);
    vec3 L = normalize(-uLightDir);
    float NdotL = max(dot(N,L), 0.0);
    vec3 baseColor = uColor;
    if(uHasAlbedo) baseColor = texture(uAlbedo, vUV).rgb;
    float metallic = 0.0; float roughness = 1.0; float ao = 1.0;
    if(uHasMRAO){ vec4 m = texture(uMRAO, vUV); ao = m.r; roughness = m.g; metallic = m.b; }
    vec3 diffuse = baseColor * (1.0 - metallic);
    // crude lighting: diffuse + simple specular
    vec3 F0 = mix(vec3(0.04), baseColor, metallic);
    float NdotV = max(dot(N,V), 0.001);
    float spec = pow(max(dot(reflect(-L, N), V), 0.0), 5.0);
    vec3 col = diffuse * (0.2 + 0.8 * NdotL) + F0 * spec;
    col *= ao;
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

    // Textures for basic PBR (albedo, normal, metallic-roughness+ao)
    GLuint albedoTex = 0, normalTex = 0, mraoTex = 0;
    bool hasAlbedo = false, hasNormal = false, hasMrao = false;
    std::function<std::vector<uint8_t>(const std::string&)> textureLoader;

    // Camera
    float yaw = 0.0f, pitch = 0.0f, distance = 3.0f;
    ImVec2 lastMouse{-1,-1}; bool rotating = false;

    // Model loaded flag
    bool hasModel = false;
    std::string name;
    glm::vec3 color{0.8f,0.8f,0.9f};

    Impl(){
        GLuint vs = compileShader(GL_VERTEX_SHADER, vs_src);
        GLuint fs = compileShader(GL_FRAGMENT_SHADER, fs_src);
        prog = linkProgram(vs, fs);
        if(vs) glDeleteShader(vs); if(fs) glDeleteShader(fs);
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ibo);
    }
    ~Impl(){ if(prog) glDeleteProgram(prog); if(vbo) glDeleteBuffers(1,&vbo); if(ibo) glDeleteBuffers(1,&ibo); if(vao) glDeleteVertexArrays(1,&vao); if(fbo) glDeleteFramebuffers(1,&fbo); if(fboTex) glDeleteTextures(1,&fboTex); if(rbo) glDeleteRenderbuffers(1,&rbo); if(albedoTex) glDeleteTextures(1,&albedoTex); if(normalTex) glDeleteTextures(1,&normalTex); if(mraoTex) glDeleteTextures(1,&mraoTex); }

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

    GLuint createTextureFromBytes(const uint8_t* bytes, int len){
        int w=0,h=0,channels=0; unsigned char* image = stbi_load_from_memory(bytes, len, &w, &h, &channels, 4);
        if(!image) return 0;
        GLuint tid=0; glGenTextures(1,&tid); glBindTexture(GL_TEXTURE_2D, tid);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, image);
        glGenerateMipmap(GL_TEXTURE_2D);
        stbi_image_free(image);
        return tid;
    }

    void drawGL(int w,int h){
        if(!hasModel) return;
        ensureFBOSize(w,h);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glViewport(0,0,w,h);
        glEnable(GL_DEPTH_TEST);
        glClearColor(0.07f,0.07f,0.08f,1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        // Setup matrices
        glm::mat4 proj = glm::perspective(glm::radians(45.0f), w>0? (float)w/(float)h : 1.0f, 0.01f, 100.0f);
        glm::mat4 view = glm::translate(glm::mat4(1.0f), glm::vec3(0,0,-distance));
        view = glm::rotate(view, glm::radians(pitch), glm::vec3(1,0,0));
        view = glm::rotate(view, glm::radians(yaw), glm::vec3(0,1,0));
        glm::mat4 model = glm::mat4(1.0f);
        glm::mat4 mvp = proj * view * model;
        glm::mat3 normalMat = glm::transpose(glm::inverse(glm::mat3(model)));

        glUseProgram(prog);
        GLint loc = glGetUniformLocation(prog, "uMVP"); if(loc>=0) glUniformMatrix4fv(loc,1,GL_FALSE,glm::value_ptr(mvp));
        loc = glGetUniformLocation(prog, "uModel"); if(loc>=0) glUniformMatrix4fv(loc,1,GL_FALSE,glm::value_ptr(model));
        loc = glGetUniformLocation(prog, "uNormal"); if(loc>=0) glUniformMatrix3fv(loc,1,GL_FALSE,glm::value_ptr(normalMat));
        loc = glGetUniformLocation(prog, "uLightDir"); if(loc>=0) glUniform3f(loc, 0.4f, 0.4f, 1.0f);
        loc = glGetUniformLocation(prog, "uColor"); if(loc>=0) glUniform3f(loc, color.r, color.g, color.b);

        // Bind PBR textures if present
        GLint hasLoc = glGetUniformLocation(prog, "uHasAlbedo"); if(hasLoc>=0) glUniform1i(hasLoc, hasAlbedo?1:0);
        if(hasAlbedo){ glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, albedoTex); GLint tloc = glGetUniformLocation(prog, "uAlbedo"); if(tloc>=0) glUniform1i(tloc, 0); }
        GLint hasNormLoc = glGetUniformLocation(prog, "uHasNormal"); if(hasNormLoc>=0) glUniform1i(hasNormLoc, hasNormal?1:0);
        if(hasNormal){ glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, normalTex); GLint tloc = glGetUniformLocation(prog, "uNormalTex"); if(tloc>=0) glUniform1i(tloc, 1); }
        GLint hasMraoLoc = glGetUniformLocation(prog, "uHasMRAO"); if(hasMraoLoc>=0) glUniform1i(hasMraoLoc, hasMrao?1:0);
        if(hasMrao){ glActiveTexture(GL_TEXTURE2); glBindTexture(GL_TEXTURE_2D, mraoTex); GLint tloc = glGetUniformLocation(prog, "uMRAO"); if(tloc>=0) glUniform1i(tloc, 2); }

        glBindVertexArray(vao);
        glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
        glUseProgram(0);

        glBindFramebuffer(GL_FRAMEBUFFER,0);
    }
};

ModelViewer::ModelViewer(){ impl = new Impl(); }
ModelViewer::~ModelViewer(){ delete impl; }

// Very basic Assimp loader: flatten all meshes into single VBO/IBO (positions,normals,uv)
void ModelViewer::setTextureLoader(TextureLoader loader){ if(impl) impl->textureLoader = loader; }

bool ModelViewer::isLoaded() const{ return impl ? impl->hasModel : false; }

bool ModelViewer::loadFromMemory(const std::vector<uint8_t>& data, const std::string& name){
    clear();
    Assimp::Importer importer;
    const aiScene* scene = importer.ReadFileFromMemory(data.data(), data.size(), aiProcess_Triangulate|aiProcess_GenNormals|aiProcess_JoinIdenticalVertices|aiProcess_OptimizeMeshes, name.c_str());
    if(!scene || !scene->HasMeshes()) return false;

    // Try to load some textures from materials (embedded or via textureLoader callback)
    // Use first material's diffuse/normal/metalness as a best-effort PBR setup
    if(scene->mNumMaterials > 0){
        aiMaterial* mat = scene->mMaterials[0];
        aiString texPath;
        // Albedo (diffuse/basecolor)
        if(mat->GetTextureCount(aiTextureType_BASE_COLOR) > 0){ mat->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath); }
        else if(mat->GetTextureCount(aiTextureType_DIFFUSE) > 0){ mat->GetTexture(aiTextureType_DIFFUSE, 0, &texPath); }
        if(texPath.length > 0){ std::string tp(texPath.C_Str()); if(tp.size()>0 && tp[0]=='*'){ // embedded
                int idx = atoi(tp.c_str()+1); if(scene->mTextures[idx] && scene->mTextures[idx]->mHeight == 0){ auto* t = scene->mTextures[idx]; impl->albedoTex = impl->createTextureFromBytes(reinterpret_cast<const uint8_t*>(t->pcData), t->mWidth); impl->hasAlbedo = impl->albedoTex!=0; }
            } else {
                if(impl->textureLoader){ auto bytes = impl->textureLoader(tp); if(!bytes.empty()) impl->albedoTex = impl->createTextureFromBytes(bytes.data(), static_cast<int>(bytes.size())), impl->hasAlbedo = impl->albedoTex!=0; }
            }
        }
        // Normal map
        if(mat->GetTextureCount(aiTextureType_NORMALS) > 0){ mat->GetTexture(aiTextureType_NORMALS, 0, &texPath); if(texPath.length>0){ std::string tp(texPath.C_Str()); if(tp.size()>0 && tp[0]=='*'){ int idx = atoi(tp.c_str()+1); if(scene->mTextures[idx] && scene->mTextures[idx]->mHeight==0){ auto* t = scene->mTextures[idx]; impl->normalTex = impl->createTextureFromBytes(reinterpret_cast<const uint8_t*>(t->pcData), t->mWidth); impl->hasNormal = impl->normalTex!=0; } } else { if(impl->textureLoader){ auto bytes = impl->textureLoader(tp); if(!bytes.empty()) impl->normalTex = impl->createTextureFromBytes(bytes.data(), static_cast<int>(bytes.size())), impl->hasNormal = impl->normalTex!=0; } } } }
        // Metallic/Roughness/AO - try METALNESS or UNKNOWN names
        if(mat->GetTextureCount(aiTextureType_METALNESS) > 0){ mat->GetTexture(aiTextureType_METALNESS, 0, &texPath); }
        else if(mat->GetTextureCount(aiTextureType_UNKNOWN) > 0){ mat->GetTexture(aiTextureType_UNKNOWN, 0, &texPath); }
        if(texPath.length > 0){ std::string tp(texPath.C_Str()); if(tp.size()>0 && tp[0]=='*'){ int idx = atoi(tp.c_str()+1); if(scene->mTextures[idx] && scene->mTextures[idx]->mHeight==0){ auto* t = scene->mTextures[idx]; impl->mraoTex = impl->createTextureFromBytes(reinterpret_cast<const uint8_t*>(t->pcData), t->mWidth); impl->hasMrao = impl->mraoTex!=0; } } else { if(impl->textureLoader){ auto bytes = impl->textureLoader(tp); if(!bytes.empty()) impl->mraoTex = impl->createTextureFromBytes(bytes.data(), static_cast<int>(bytes.size())), impl->hasMrao = impl->mraoTex!=0; } } }
    }

    std::vector<float> vbuf;
    std::vector<unsigned int> ibuf;
    unsigned int baseV = 0;
    for(unsigned int m=0;m<scene->mNumMeshes;++m){
        aiMesh* mesh = scene->mMeshes[m];
        for(unsigned int i=0;i<mesh->mNumVertices;++i){
            aiVector3D p = mesh->mVertices[i]; aiVector3D n = mesh->HasNormals() ? mesh->mNormals[i] : aiVector3D(0,0,1);
            aiVector3D uv = mesh->HasTextureCoords(0) ? mesh->mTextureCoords[0][i] : aiVector3D(0,0,0);
            vbuf.push_back(p.x); vbuf.push_back(p.y); vbuf.push_back(p.z);
            vbuf.push_back(n.x); vbuf.push_back(n.y); vbuf.push_back(n.z);
            vbuf.push_back(uv.x); vbuf.push_back(uv.y);
        }
        for(unsigned int f=0; f<mesh->mNumFaces; ++f){
            aiFace &face = mesh->mFaces[f];
            if(face.mNumIndices != 3) continue;
            ibuf.push_back(baseV + face.mIndices[0]);
            ibuf.push_back(baseV + face.mIndices[1]);
            ibuf.push_back(baseV + face.mIndices[2]);
        }
        baseV += mesh->mNumVertices;
    }

    // Upload to GL
    glBindVertexArray(impl->vao);
    glBindBuffer(GL_ARRAY_BUFFER, impl->vbo);
    glBufferData(GL_ARRAY_BUFFER, vbuf.size()*sizeof(float), vbuf.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(0));
    glEnableVertexAttribArray(1); glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(3*sizeof(float)));
    glEnableVertexAttribArray(2); glVertexAttribPointer(2,2,GL_FLOAT,GL_FALSE,8*sizeof(float),(void*)(6*sizeof(float)));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, impl->ibo);
    impl->indexCount = static_cast<GLsizei>(ibuf.size());
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, ibuf.size()*sizeof(unsigned int), ibuf.data(), GL_STATIC_DRAW);
    glBindVertexArray(0);

    impl->hasModel = true;
    impl->name = name;
    return true;
}

void ModelViewer::renderToRegion(const ImVec2& size){
    if(!impl->hasModel){ ImGui::TextUnformatted("No model loaded"); return; }
    // Mouse controls: simple orbit
    ImVec2 p = ImGui::GetCursorScreenPos();
    std::string btnId = std::string("##model_viewport_") + std::to_string(reinterpret_cast<uintptr_t>(this));
    ImGui::InvisibleButton(btnId.c_str(), size);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    // handle mouse drag
    if(ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)){
        ImVec2 delta = ImGui::GetIO().MouseDelta;
        impl->yaw += delta.x * 0.2f;
        impl->pitch += delta.y * 0.2f;
    }
    if(ImGui::GetIO().MouseWheel != 0.0f){ impl->distance *= (1.0f - ImGui::GetIO().MouseWheel*0.1f); if(impl->distance < 0.1f) impl->distance = 0.1f; }

    impl->drawGL((int)size.x, (int)size.y);
    // Draw the framebuffer texture
    ImGui::Image((ImTextureID)(intptr_t)impl->fboTex, size, ImVec2(0,1), ImVec2(1,0));
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

void ModelViewer::clear(){ if(impl) { impl->hasModel=false; impl->indexCount=0; } }
