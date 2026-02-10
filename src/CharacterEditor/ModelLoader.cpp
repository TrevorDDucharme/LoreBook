#include <CharacterEditor/ModelLoader.hpp>
#include <CharacterEditor/PartRole.hpp>
#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <plog/Log.h>
#include <algorithm>
#include <functional>
#include <unordered_set>
#include <filesystem>
#include <optional>
#include <fstream>
#include <map>

namespace CharacterEditor {

// Static member initialization
LoadProgressCallback ModelLoader::s_progressCallback = nullptr;

namespace {

// Convert Assimp matrix to glm
glm::mat4 toGlm(const aiMatrix4x4& m) {
    return glm::mat4(
        m.a1, m.b1, m.c1, m.d1,
        m.a2, m.b2, m.c2, m.d2,
        m.a3, m.b3, m.c3, m.d3,
        m.a4, m.b4, m.c4, m.d4
    );
}

glm::vec3 toGlm(const aiVector3D& v) {
    return glm::vec3(v.x, v.y, v.z);
}

glm::vec2 toGlm(const aiVector2D& v) {
    return glm::vec2(v.x, v.y);
}

glm::quat toGlm(const aiQuaternion& q) {
    return glm::quat(q.w, q.x, q.y, q.z);
}

// Infer part role from bone name
PartRole inferRoleFromBoneName(const std::string& name, 
                                const std::map<std::string, PartRole>& overrides) {
    // Check explicit overrides first
    auto it = overrides.find(name);
    if (it != overrides.end()) {
        return it->second;
    }
    
    // Convert to lowercase for matching
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), 
                   [](unsigned char c) { return std::tolower(c); });
    
    // Pattern matching for common naming conventions
    // Head/Face
    if (lower.find("head") != std::string::npos) return PartRole::Head;
    if (lower.find("neck") != std::string::npos) return PartRole::Neck;
    if (lower.find("jaw") != std::string::npos) return PartRole::Jaw;
    if (lower.find("eye") != std::string::npos) return PartRole::Eye;
    if (lower.find("ear") != std::string::npos) return PartRole::Ear;
    
    // Spine
    if (lower.find("spine") != std::string::npos) return PartRole::Spine;
    if (lower.find("chest") != std::string::npos) return PartRole::Chest;
    if (lower.find("pelvis") != std::string::npos || lower.find("hip") != std::string::npos) {
        return PartRole::Hip;
    }
    
    // Arms
    if (lower.find("shoulder") != std::string::npos || lower.find("clavicle") != std::string::npos) {
        return PartRole::Shoulder;
    }
    if (lower.find("upperarm") != std::string::npos || lower.find("upper_arm") != std::string::npos) {
        return PartRole::UpperArm;
    }
    if (lower.find("forearm") != std::string::npos || lower.find("lowerarm") != std::string::npos || 
        lower.find("lower_arm") != std::string::npos) {
        return PartRole::LowerArm;
    }
    if (lower.find("hand") != std::string::npos || lower.find("wrist") != std::string::npos) {
        return PartRole::Hand;
    }
    if (lower.find("thumb") != std::string::npos) return PartRole::Thumb;
    if (lower.find("finger") != std::string::npos || lower.find("index") != std::string::npos ||
        lower.find("middle") != std::string::npos || lower.find("ring") != std::string::npos ||
        lower.find("pinky") != std::string::npos) {
        return PartRole::Finger;
    }
    
    // Legs
    if (lower.find("thigh") != std::string::npos || lower.find("upperleg") != std::string::npos ||
        lower.find("upper_leg") != std::string::npos) {
        return PartRole::UpperLeg;
    }
    if (lower.find("shin") != std::string::npos || lower.find("calf") != std::string::npos ||
        lower.find("lowerleg") != std::string::npos || lower.find("lower_leg") != std::string::npos) {
        return PartRole::LowerLeg;
    }
    if (lower.find("foot") != std::string::npos || lower.find("ankle") != std::string::npos) {
        return PartRole::Foot;
    }
    if (lower.find("toe") != std::string::npos) return PartRole::Toe;
    
    // Non-humanoid
    if (lower.find("tail") != std::string::npos) {
        if (lower.find("tip") != std::string::npos) return PartRole::TailTip;
        return PartRole::Tail;
    }
    if (lower.find("wing") != std::string::npos) {
        if (lower.find("tip") != std::string::npos) return PartRole::WingTip;
        if (lower.find("mid") != std::string::npos) return PartRole::WingMid;
        if (lower.find("membrane") != std::string::npos) return PartRole::WingMembrane;
        return PartRole::WingRoot;
    }
    if (lower.find("tentacle") != std::string::npos) {
        if (lower.find("tip") != std::string::npos) return PartRole::TentacleTip;
        return PartRole::Tentacle;
    }
    
    // Root detection
    if (lower == "root" || lower == "armature" || lower == "skeleton") {
        return PartRole::Root;
    }
    
    return PartRole::Custom;
}

// Helper to find node by name in scene hierarchy
const aiNode* findNode(const aiNode* root, const std::string& name) {
    if (!root) return nullptr;
    if (root->mName.C_Str() == name) return root;
    
    for (unsigned int i = 0; i < root->mNumChildren; ++i) {
        const aiNode* found = findNode(root->mChildren[i], name);
        if (found) return found;
    }
    return nullptr;
}

// Collect ancestors of a node up to root
void collectAncestors(const aiNode* node, const aiNode* root, 
                      std::unordered_set<std::string>& ancestors) {
    // Walk up parent chain by finding each node in the scene
    // Since aiNode doesn't store parent pointer reliably, we'll need to 
    // identify ancestors differently - we'll include the armature root
    (void)node;
    (void)root;
    (void)ancestors;
    // Ancestors will be added during the recursive traversal
}

// Check if a node is an actual skeleton bone
// Uses authoritative data: aiMesh::mBones list
bool isActualBone(const aiNode* node, const aiScene* scene,
                  const std::unordered_set<std::string>& meshBoneNames,
                  const std::string& socketPrefix) {
    std::string name = node->mName.C_Str();
    
    // If this node has meshes attached, it's NOT a bone
    if (node->mNumMeshes > 0) {
        return false;
    }
    
    // Node is a bone if:
    // 1. It's referenced as a bone by mesh skinning data (authoritative)
    if (meshBoneNames.count(name)) return true;
    
    // 2. It's explicitly marked as a socket (starts with socket prefix)
    if (!socketPrefix.empty() && name.rfind(socketPrefix, 0) == 0) return true;
    
    return false;
}

// Z-up to Y-up conversion matrix (-90 degrees around X axis)
// This transforms: X stays X, Y becomes Z, Z becomes -Y
static const glm::mat4 g_zUpToYUpMatrix = glm::mat4(
    1.0f,  0.0f,  0.0f, 0.0f,
    0.0f,  0.0f,  1.0f, 0.0f,
    0.0f, -1.0f,  0.0f, 0.0f,
    0.0f,  0.0f,  0.0f, 1.0f
);

static const glm::mat3 g_zUpToYUpMatrix3 = glm::mat3(g_zUpToYUpMatrix);

// Build skeleton from Assimp scene
Skeleton processSkeleton(const aiScene* scene, const ImportConfig& config) {
    Skeleton skeleton;
    
    if (!scene->mRootNode) {
        return skeleton;
    }
    
    // Dump full Assimp tree to a log file for debugging
    {
        std::ofstream logFile("assimp_tree_dump.txt");
        if (logFile.is_open()) {
            logFile << "=== ASSIMP SCENE DUMP ===" << std::endl;
            logFile << "Meshes: " << scene->mNumMeshes << std::endl;
            logFile << "Animations: " << scene->mNumAnimations << std::endl;
            logFile << "Materials: " << scene->mNumMaterials << std::endl;
            logFile << "Textures: " << scene->mNumTextures << std::endl;
            logFile << "Lights: " << scene->mNumLights << std::endl;
            logFile << "Cameras: " << scene->mNumCameras << std::endl;
            logFile << std::endl;
            
            // Dump all meshes with bone info
            logFile << "=== MESHES ===" << std::endl;
            for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
                const aiMesh* mesh = scene->mMeshes[mi];
                logFile << "Mesh[" << mi << "] '" << mesh->mName.C_Str() << "'" << std::endl;
                logFile << "  Vertices: " << mesh->mNumVertices << std::endl;
                logFile << "  Faces: " << mesh->mNumFaces << std::endl;
                logFile << "  Bones: " << mesh->mNumBones << std::endl;
                logFile << "  HasPositions: " << mesh->HasPositions() << std::endl;
                logFile << "  HasNormals: " << mesh->HasNormals() << std::endl;
                logFile << "  HasTangents: " << mesh->HasTangentsAndBitangents() << std::endl;
                
                // List bone names if any
                for (unsigned int bi = 0; bi < mesh->mNumBones; ++bi) {
                    const aiBone* bone = mesh->mBones[bi];
                    logFile << "    Bone[" << bi << "] '" << bone->mName.C_Str() 
                            << "' weights=" << bone->mNumWeights << std::endl;
                }
            }
            logFile << std::endl;
            
            // Dump full node hierarchy
            logFile << "=== NODE HIERARCHY ===" << std::endl;
            std::function<void(const aiNode*, int)> dumpNode = [&](const aiNode* node, int depth) {
                std::string indent(depth * 2, ' ');
                logFile << indent << "Node: '" << node->mName.C_Str() << "'" << std::endl;
                logFile << indent << "  Meshes: " << node->mNumMeshes;
                if (node->mNumMeshes > 0) {
                    logFile << " [";
                    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
                        if (i > 0) logFile << ", ";
                        logFile << node->mMeshes[i];
                    }
                    logFile << "]";
                }
                logFile << std::endl;
                logFile << indent << "  Children: " << node->mNumChildren << std::endl;
                
                // Dump transform
                const aiMatrix4x4& m = node->mTransformation;
                logFile << indent << "  Transform:" << std::endl;
                logFile << indent << "    [" << m.a1 << ", " << m.a2 << ", " << m.a3 << ", " << m.a4 << "]" << std::endl;
                logFile << indent << "    [" << m.b1 << ", " << m.b2 << ", " << m.b3 << ", " << m.b4 << "]" << std::endl;
                logFile << indent << "    [" << m.c1 << ", " << m.c2 << ", " << m.c3 << ", " << m.c4 << "]" << std::endl;
                logFile << indent << "    [" << m.d1 << ", " << m.d2 << ", " << m.d3 << ", " << m.d4 << "]" << std::endl;
                
                // Recurse to children
                for (unsigned int i = 0; i < node->mNumChildren; ++i) {
                    dumpNode(node->mChildren[i], depth + 1);
                }
            };
            dumpNode(scene->mRootNode, 0);
            
            // Dump animations if any
            if (scene->mNumAnimations > 0) {
                logFile << std::endl << "=== ANIMATIONS ===" << std::endl;
                for (unsigned int ai = 0; ai < scene->mNumAnimations; ++ai) {
                    const aiAnimation* anim = scene->mAnimations[ai];
                    logFile << "Animation[" << ai << "] '" << anim->mName.C_Str() << "'" << std::endl;
                    logFile << "  Duration: " << anim->mDuration << std::endl;
                    logFile << "  TicksPerSecond: " << anim->mTicksPerSecond << std::endl;
                    logFile << "  Channels: " << anim->mNumChannels << std::endl;
                    for (unsigned int ci = 0; ci < anim->mNumChannels && ci < 20; ++ci) {
                        const aiNodeAnim* channel = anim->mChannels[ci];
                        logFile << "    Channel[" << ci << "] '" << channel->mNodeName.C_Str() 
                                << "' pos=" << channel->mNumPositionKeys 
                                << " rot=" << channel->mNumRotationKeys 
                                << " scale=" << channel->mNumScalingKeys << std::endl;
                    }
                    if (anim->mNumChannels > 20) {
                        logFile << "    ... and " << (anim->mNumChannels - 20) << " more channels" << std::endl;
                    }
                }
            }
            
            logFile.close();
            PLOGI << "ModelLoader: Wrote full scene dump to assimp_tree_dump.txt";
        }
    }
    
    // Log scene structure for debugging
    PLOGI << "ModelLoader: Scene has " << scene->mNumMeshes << " meshes, " 
          << scene->mNumAnimations << " animations";
    
    // Collect all bone names referenced by meshes - this is the AUTHORITATIVE source
    // A bone is defined by aiMesh::mBones, not by node names or hierarchy guessing
    std::unordered_set<std::string> meshBoneNames;
    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* mesh = scene->mMeshes[mi];
        for (unsigned int bi = 0; bi < mesh->mNumBones; ++bi) {
            meshBoneNames.insert(mesh->mBones[bi]->mName.C_Str());
        }
    }
    
    PLOGI << "ModelLoader: Found " << meshBoneNames.size() << " bones from mesh skinning data";
    
    // Log some bone names for debugging
    if (!meshBoneNames.empty()) {
        int count = 0;
        for (const auto& name : meshBoneNames) {
            if (count++ < 5) PLOGI << "  Bone: '" << name << "'";
        }
        if (meshBoneNames.size() > 5) {
            PLOGI << "  ... and " << (meshBoneNames.size() - 5) << " more";
        }
    }
    
    // If no bones in meshes, return empty skeleton
    if (meshBoneNames.empty()) {
        PLOGW << "ModelLoader: No skinned bones found in any mesh";
        return skeleton;
    }
    
    std::unordered_map<std::string, int32_t> boneNameToId;
    
    // Recursive function to process nodes, building skeleton
    // accumulatedTransform: transform accumulated from skipped parent nodes
    std::function<void(const aiNode*, uint32_t, const glm::mat4&)> processNode = 
        [&](const aiNode* node, uint32_t parentId, const glm::mat4& accumulatedTransform) {
        
        std::string nodeName = node->mName.C_Str();
        glm::mat4 nodeTransform = toGlm(node->mTransformation);
        
        // Check if this node should be included in skeleton
        bool isBone = isActualBone(node, scene, meshBoneNames, config.socketBonePrefix);
        
        if (isBone) {
            Bone bone;
            bone.name = nodeName;
            bone.parentID = parentId;
            
            // Compute the local transform
            // If we have accumulated transform from skipped nodes, apply it
            glm::mat4 localMat;
            if (parentId == UINT32_MAX) {
                // Root bone: include accumulated transform from skipped hierarchy nodes
                localMat = accumulatedTransform * nodeTransform;
                
                // Apply Z-up to Y-up conversion for root bones
                if (config.convertZUpToYUp) {
                    PLOGI << "Converting root bone '" << nodeName << "' from Z-up to Y-up";
                    localMat = g_zUpToYUpMatrix * localMat;
                }
            } else {
                // Child bone: just use the node's local transform
                localMat = nodeTransform;
            }
            
            bone.localTransform = Transform::fromMatrix(localMat * config.scaleFactor);
            
            // Log bone transform details
            if (skeleton.bones.size() < 15) {
                PLOGI << "Added bone[" << skeleton.bones.size() << "] '" << nodeName 
                      << "': parent=" << (parentId == UINT32_MAX ? -1 : (int)parentId)
                      << ", localPos=(" << bone.localTransform.position.x 
                      << ", " << bone.localTransform.position.y 
                      << ", " << bone.localTransform.position.z << ")";
            }
            
            bone.role = inferRoleFromBoneName(nodeName, config.boneRoleOverrides);
            
            uint32_t boneId = static_cast<uint32_t>(skeleton.bones.size());
            boneNameToId[nodeName] = static_cast<int32_t>(boneId);
            skeleton.bones.push_back(bone);
            
            // Update parent's children list
            if (parentId != UINT32_MAX) {
                skeleton.bones[parentId].childIDs.push_back(boneId);
            }
            
            // Process children with this bone as parent, reset accumulated transform
            for (unsigned int ci = 0; ci < node->mNumChildren; ++ci) {
                processNode(node->mChildren[ci], boneId, glm::mat4(1.0f));
            }
        } else {
            // Not a bone - skip this node but accumulate its transform
            glm::mat4 newAccumulated = accumulatedTransform * nodeTransform;
            
            // Log skipped nodes for debugging
            if (skeleton.bones.empty()) {
                PLOGI << "Skipping non-bone node: '" << nodeName << "' (meshes=" << node->mNumMeshes << ")";
            }
            
            // Continue traversing children, passing accumulated transform
            for (unsigned int ci = 0; ci < node->mNumChildren; ++ci) {
                processNode(node->mChildren[ci], parentId, newAccumulated);
            }
        }
    };
    
    // Start from root with identity accumulated transform
    processNode(scene->mRootNode, UINT32_MAX, glm::mat4(1.0f));
    
    PLOGI << "ModelLoader: Built skeleton with " << skeleton.bones.size() << " bones";
    
    // Now set inverse bind matrices from actual bone data in meshes
    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
        const aiMesh* mesh = scene->mMeshes[mi];
        for (unsigned int bi = 0; bi < mesh->mNumBones; ++bi) {
            const aiBone* aiBone = mesh->mBones[bi];
            std::string boneName = aiBone->mName.C_Str();
            
            auto it = boneNameToId.find(boneName);
            if (it != boneNameToId.end() && it->second >= 0) {
                skeleton.bones[it->second].inverseBindMatrix = 
                    Transform::fromMatrix(toGlm(aiBone->mOffsetMatrix));
            }
        }
    }
    
    skeleton.boneNameToIndex = std::move(boneNameToId);
    skeleton.buildHierarchy();
    
    return skeleton;
}

// Extract sockets from skeleton
std::vector<Socket> extractSockets(const Skeleton& skeleton, const std::string& socketPrefix) {
    std::vector<Socket> sockets;
    
    for (size_t bi = 0; bi < skeleton.bones.size(); ++bi) {
        const auto& bone = skeleton.bones[bi];
        if (bone.name.rfind(socketPrefix, 0) == 0) {
            // Skip "_end" bones - these are Blender bone chain terminators, not actual sockets
            if (bone.name.size() > 4 && bone.name.substr(bone.name.size() - 4) == "_end") {
                continue;
            }
            
            // This bone is a socket
            Socket socket;
            socket.id = "socket_" + std::to_string(sockets.size());
            socket.name = bone.name.substr(socketPrefix.length()); // Remove prefix: "socket_arm_left" -> "arm_left"
            socket.space = SpaceType::Bone;
            socket.boneName = bone.name;
            socket.ownerBoneIndex = static_cast<uint32_t>(bi); // Index of this bone in the skeleton
            socket.localOffset = bone.localTransform;
            
            // Infer socket category from name patterns
            // Common patterns: arm_left, arm_right, leg_left, leg_right, head, tail, etc.
            std::string lower = socket.name;
            std::transform(lower.begin(), lower.end(), lower.begin(),
                           [](unsigned char c) { return std::tolower(c); });
            
            if (lower.find("arm") != std::string::npos) {
                socket.profile.category = "limb";
                socket.profile.profileID = "arm";
            } else if (lower.find("leg") != std::string::npos) {
                socket.profile.category = "limb";
                socket.profile.profileID = "leg";
            } else if (lower.find("head") != std::string::npos) {
                socket.profile.category = "head";
                socket.profile.profileID = "head";
            } else if (lower.find("tail") != std::string::npos) {
                socket.profile.category = "appendage";
                socket.profile.profileID = "tail";
            } else if (lower.find("wing") != std::string::npos) {
                socket.profile.category = "appendage";
                socket.profile.profileID = "wing";
            } else {
                socket.profile.category = "generic";
                socket.profile.profileID = socket.name;
            }
            
            sockets.push_back(socket);
        }
    }
    
    return sockets;
}

// Process a single mesh
Mesh processMesh(const aiMesh* aiMesh, const aiScene* scene, 
                 const Skeleton& skeleton, const ImportConfig& config) {
    Mesh mesh;
    mesh.name = aiMesh->mName.C_Str();
    
    // Build bone name to index for this mesh
    std::unordered_map<std::string, int32_t> meshBoneMap;
    for (unsigned int bi = 0; bi < aiMesh->mNumBones; ++bi) {
        meshBoneMap[aiMesh->mBones[bi]->mName.C_Str()] = bi;
    }
    
    // Reserve space
    mesh.vertices.reserve(aiMesh->mNumVertices);
    
    // Process vertices
    for (unsigned int vi = 0; vi < aiMesh->mNumVertices; ++vi) {
        Vertex vertex;
        glm::vec3 pos = toGlm(aiMesh->mVertices[vi]) * config.scaleFactor;
        
        // Convert Z-up to Y-up if requested
        if (config.convertZUpToYUp) {
            vertex.position = glm::vec3(g_zUpToYUpMatrix * glm::vec4(pos, 1.0f));
        } else {
            vertex.position = pos;
        }
        
        if (aiMesh->HasNormals()) {
            glm::vec3 norm = toGlm(aiMesh->mNormals[vi]);
            if (config.convertZUpToYUp) {
                vertex.normal = g_zUpToYUpMatrix3 * norm;
            } else {
                vertex.normal = norm;
            }
        }
        
        if (aiMesh->HasTextureCoords(0)) {
            vertex.uv = glm::vec2(aiMesh->mTextureCoords[0][vi].x, 
                                  aiMesh->mTextureCoords[0][vi].y);
        }
        
        if (aiMesh->HasTangentsAndBitangents()) {
            glm::vec3 tan = toGlm(aiMesh->mTangents[vi]);
            if (config.convertZUpToYUp) {
                tan = g_zUpToYUpMatrix3 * tan;
            }
            vertex.tangent = glm::vec4(tan, 1.0f);  // w=1.0 for right-handed tangent space
        }
        
        // Initialize bone data
        vertex.boneIDs = glm::ivec4(-1);
        vertex.boneWeights = glm::vec4(0.0f);
        
        mesh.vertices.push_back(vertex);
    }
    
    // Process bone weights
    for (unsigned int bi = 0; bi < aiMesh->mNumBones; ++bi) {
        const aiBone* bone = aiMesh->mBones[bi];
        
        // Find skeleton bone index
        auto skelIt = skeleton.boneNameToIndex.find(bone->mName.C_Str());
        int32_t skeletonBoneId = (skelIt != skeleton.boneNameToIndex.end()) ? skelIt->second : -1;
        
        for (unsigned int wi = 0; wi < bone->mNumWeights; ++wi) {
            unsigned int vertexId = bone->mWeights[wi].mVertexId;
            float weight = bone->mWeights[wi].mWeight;
            
            Vertex& v = mesh.vertices[vertexId];
            
            // Find first empty slot (weight == 0)
            for (int slot = 0; slot < 4; ++slot) {
                if (v.boneWeights[slot] == 0.0f) {
                    v.boneIDs[slot] = skeletonBoneId;
                    v.boneWeights[slot] = weight;
                    break;
                }
            }
        }
    }
    
    // Normalize bone weights
    for (auto& v : mesh.vertices) {
        float sum = v.boneWeights.x + v.boneWeights.y + v.boneWeights.z + v.boneWeights.w;
        if (sum > 0.0001f) {
            v.boneWeights /= sum;
        }
    }
    
    // Process indices
    for (unsigned int fi = 0; fi < aiMesh->mNumFaces; ++fi) {
        const aiFace& face = aiMesh->mFaces[fi];
        for (unsigned int ii = 0; ii < face.mNumIndices; ++ii) {
            mesh.indices.push_back(face.mIndices[ii]);
        }
    }
    
    // Create default submesh
    Submesh submesh;
    submesh.indexOffset = 0;
    submesh.indexCount = static_cast<uint32_t>(mesh.indices.size());
    submesh.materialIndex = aiMesh->mMaterialIndex;
    mesh.submeshes.push_back(submesh);
    
    // Link skeleton
    mesh.skeleton = skeleton;
    
    return mesh;
}

// Process materials
std::vector<MaterialSlot> processMaterials(const aiScene* scene, 
                                            std::vector<EmbeddedTexture>& embeddedTextures,
                                            const std::filesystem::path& basePath) {
    std::vector<MaterialSlot> materials;
    
    for (unsigned int mi = 0; mi < scene->mNumMaterials; ++mi) {
        const aiMaterial* aiMat = scene->mMaterials[mi];
        MaterialSlot mat;
        
        // Get material name
        aiString name;
        if (aiMat->Get(AI_MATKEY_NAME, name) == AI_SUCCESS) {
            mat.name = name.C_Str();
        } else {
            mat.name = "Material_" + std::to_string(mi);
        }
        
        // Get base color
        aiColor4D diffuse;
        if (aiMat->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS) {
            mat.baseColor = glm::vec4(diffuse.r, diffuse.g, diffuse.b, diffuse.a);
        }
        
        // Get metallic/roughness
        float metallic = 0.0f, roughness = 0.5f;
        aiMat->Get(AI_MATKEY_METALLIC_FACTOR, metallic);
        aiMat->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness);
        mat.metallicRoughness = glm::vec2(metallic, roughness);
        
        // Process textures
        auto processTexture = [&](aiTextureType type) -> std::optional<std::string> {
            if (aiMat->GetTextureCount(type) > 0) {
                aiString path;
                if (aiMat->GetTexture(type, 0, &path) == AI_SUCCESS) {
                    std::string texPath = path.C_Str();
                    
                    // Check if embedded texture (starts with '*')
                    if (!texPath.empty() && texPath[0] == '*') {
                        int texIndex = std::atoi(texPath.c_str() + 1);
                        if (texIndex >= 0 && texIndex < static_cast<int>(scene->mNumTextures)) {
                            const aiTexture* aiTex = scene->mTextures[texIndex];
                            
                            EmbeddedTexture embTex;
                            embTex.name = aiTex->mFilename.length > 0 ? 
                                          aiTex->mFilename.C_Str() : 
                                          ("embedded_" + std::to_string(texIndex));
                            embTex.format = aiTex->achFormatHint;
                            
                            if (aiTex->mHeight == 0) {
                                // Compressed texture
                                embTex.data.assign(
                                    reinterpret_cast<const uint8_t*>(aiTex->pcData),
                                    reinterpret_cast<const uint8_t*>(aiTex->pcData) + aiTex->mWidth
                                );
                            } else {
                                // Raw RGBA
                                size_t dataSize = aiTex->mWidth * aiTex->mHeight * 4;
                                embTex.data.assign(
                                    reinterpret_cast<const uint8_t*>(aiTex->pcData),
                                    reinterpret_cast<const uint8_t*>(aiTex->pcData) + dataSize
                                );
                                embTex.width = aiTex->mWidth;
                                embTex.height = aiTex->mHeight;
                            }
                            
                            embeddedTextures.push_back(std::move(embTex));
                            return "embedded:" + std::to_string(embeddedTextures.size() - 1);
                        }
                    }
                    
                    // External texture - resolve relative to model file
                    std::filesystem::path fullPath = basePath / texPath;
                    return fullPath.string();
                }
            }
            return std::nullopt;
        };
        
        mat.albedoTexture = processTexture(aiTextureType_DIFFUSE);
        if (!mat.albedoTexture) {
            mat.albedoTexture = processTexture(aiTextureType_BASE_COLOR);
        }
        mat.normalTexture = processTexture(aiTextureType_NORMALS);
        mat.metallicRoughnessTexture = processTexture(aiTextureType_METALNESS);
        mat.emissiveTexture = processTexture(aiTextureType_EMISSIVE);
        mat.aoTexture = processTexture(aiTextureType_AMBIENT_OCCLUSION);
        
        materials.push_back(mat);
    }
    
    return materials;
}

// Process shape keys (blend shapes / morph targets)
void processShapeKeys(Mesh& mesh, const aiMesh* aiMesh) {
    for (unsigned int ani = 0; ani < aiMesh->mNumAnimMeshes; ++ani) {
        const aiAnimMesh* animMesh = aiMesh->mAnimMeshes[ani];
        
        ShapeKey shapeKey;
        shapeKey.name = animMesh->mName.C_Str();
        shapeKey.type = ShapeKeyType::PartLocal;  // Imported blend shapes are part-local
        shapeKey.weight = 0.0f;
        shapeKey.minWeight = 0.0f;
        shapeKey.maxWeight = 1.0f;
        
        // Calculate deltas
        for (unsigned int vi = 0; vi < animMesh->mNumVertices; ++vi) {
            if (vi < mesh.vertices.size()) {
                glm::vec3 positionDelta = toGlm(animMesh->mVertices[vi]) - mesh.vertices[vi].position;
                glm::vec3 normalDelta(0.0f);
                
                if (animMesh->HasNormals()) {
                    normalDelta = toGlm(animMesh->mNormals[vi]) - mesh.vertices[vi].normal;
                }
                
                // Only store non-zero deltas
                if (glm::length(positionDelta) > 0.0001f || glm::length(normalDelta) > 0.0001f) {
                    VertexDelta delta;
                    delta.vertexIndex = vi;
                    delta.positionDelta = positionDelta;
                    delta.normalDelta = normalDelta;
                    shapeKey.deltas.push_back(delta);
                }
            }
        }
        
        mesh.shapeKeys.push_back(std::move(shapeKey));
    }
}

} // anonymous namespace

// ModelLoader implementation

LoadResult ModelLoader::loadFromFile(const std::string& filePath, const ImportConfig& config) {
    LoadResult result;
    result.success = false;
    
    Assimp::Importer importer;
    
    unsigned int flags = 
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_LimitBoneWeights |
        aiProcess_PopulateArmatureData |
        aiProcess_FlipUVs;  // Flip UVs for OpenGL
    
    // Convert to Y-up coordinate system if requested (most 3D software uses Y-up for rendering)
    // This helps with Blender FBX files which use Z-up
    if (config.leftHandedToRightHanded) {
        flags |= aiProcess_MakeLeftHanded | aiProcess_FlipWindingOrder;
    }
    
    const aiScene* scene = importer.ReadFile(filePath, flags);
    
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        result.errors.push_back("Failed to load model: " + std::string(importer.GetErrorString()));
        return result;
    }
    
    std::filesystem::path basePath = std::filesystem::path(filePath).parent_path();
    
    // Process skeleton first
    result.skeleton = processSkeleton(scene, config);
    
    // Extract sockets from skeleton
    result.extractedSockets = extractSockets(result.skeleton, config.socketBonePrefix);
    
    // Process materials and embedded textures
    result.materials = processMaterials(scene, result.embeddedTextures, basePath);
    
    // Process meshes
    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
        Mesh mesh = processMesh(scene->mMeshes[mi], scene, result.skeleton, config);
        
        // Link materials
        if (mesh.submeshes[0].materialIndex < result.materials.size()) {
            mesh.materials.push_back(result.materials[mesh.submeshes[0].materialIndex]);
        }
        
        // Process shape keys
        processShapeKeys(mesh, scene->mMeshes[mi]);
        
        result.meshes.push_back(std::move(mesh));
    }
    
    // Report socket extraction
    if (!result.extractedSockets.empty()) {
        result.warnings.push_back("Extracted " + std::to_string(result.extractedSockets.size()) + 
                                  " sockets from bones with prefix '" + config.socketBonePrefix + "'");
    }
    
    result.success = true;
    return result;
}

LoadResult ModelLoader::loadFromMemory(const uint8_t* data, size_t size, 
                                        const std::string& formatHint,
                                        const ImportConfig& config) {
    LoadResult result;
    result.success = false;
    
    Assimp::Importer importer;
    
    unsigned int flags = 
        aiProcess_Triangulate |
        aiProcess_GenNormals |
        aiProcess_CalcTangentSpace |
        aiProcess_JoinIdenticalVertices |
        aiProcess_LimitBoneWeights |
        aiProcess_PopulateArmatureData |
        aiProcess_FlipUVs;
    
    if (config.leftHandedToRightHanded) {
        flags |= aiProcess_MakeLeftHanded | aiProcess_FlipWindingOrder;
    }
    
    const aiScene* scene = importer.ReadFileFromMemory(data, size, flags, formatHint.c_str());
    
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
        result.errors.push_back("Failed to load model from memory: " + 
                               std::string(importer.GetErrorString()));
        return result;
    }
    
    // Process skeleton first
    result.skeleton = processSkeleton(scene, config);
    
    // Extract sockets from skeleton
    result.extractedSockets = extractSockets(result.skeleton, config.socketBonePrefix);
    
    // Process materials and embedded textures  
    result.materials = processMaterials(scene, result.embeddedTextures, std::filesystem::path());
    
    // Process meshes
    for (unsigned int mi = 0; mi < scene->mNumMeshes; ++mi) {
        Mesh mesh = processMesh(scene->mMeshes[mi], scene, result.skeleton, config);
        
        if (mesh.submeshes[0].materialIndex < result.materials.size()) {
            mesh.materials.push_back(result.materials[mesh.submeshes[0].materialIndex]);
        }
        
        processShapeKeys(mesh, scene->mMeshes[mi]);
        result.meshes.push_back(std::move(mesh));
    }
    
    result.success = true;
    return result;
}

std::optional<Part> ModelLoader::loadAsPart(const std::string& filePath, 
                                             const ImportConfig& config) {
    LoadResult loadResult = loadFromFile(filePath, config);
    
    if (!loadResult.success || loadResult.meshes.empty()) {
        return std::nullopt;
    }
    
    Part part;
    part.id = std::filesystem::path(filePath).stem().string();
    part.name = part.id;
    part.meshes.push_back(std::move(loadResult.meshes[0]));
    part.socketsOut = std::move(loadResult.extractedSockets);
    
    // Create attachment spec from sockets
    // Look for a socket that could be an attachment point (input socket)
    for (const auto& socket : part.socketsOut) {
        AttachmentSpec spec;
        spec.requiredProfile = socket.profile;
        spec.localAttachPoint = socket.localOffset;
        spec.attachmentBone = socket.boneName;
        part.attachmentSpecs.push_back(spec);
    }
    
    // Extract file information as metadata
    part.metadata["source_file"] = filePath;
    part.metadata["mesh_count"] = std::to_string(loadResult.meshes.size());
    part.metadata["socket_count"] = std::to_string(part.socketsOut.size());
    
    return part;
}

} // namespace CharacterEditor
