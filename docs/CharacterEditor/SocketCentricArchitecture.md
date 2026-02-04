# Socket-Centric Character Editor Architecture

## Document Purpose

This document defines the **socket-centric character editor** architecture for LoreBook. The system abandons the traditional "authoritative rig" paradigm in favor of **socket compatibility** as the sole global authority. Any rig, mesh, or part is valid if it exposes compatible sockets.

---

## Table of Contents

1. [Core Principles](#1-core-principles)
2. [Data Structures](#2-data-structures)
3. [Asset Loading Pipeline](#3-asset-loading-pipeline)
4. [Socket System](#4-socket-system)
5. [Part System](#5-part-system)
6. [Attachment Process](#6-attachment-process)
7. [Adapters](#7-adapters)
8. [Surface-Based Sockets](#8-surface-based-sockets)
9. [Seam Remeshing](#9-seam-remeshing)
10. [Shape Key System](#10-shape-key-system)
11. [Deformation Coupling](#11-deformation-coupling)
12. [Inverse Kinematics & Procedural Animation](#12-inverse-kinematics--procedural-animation)
13. [Editor Responsibilities](#13-editor-responsibilities)
14. [Implementation Phases](#14-implementation-phases)
15. [File Format & Serialization](#15-file-format--serialization)
16. [Explicit Non-Goals](#16-explicit-non-goals)

---

## 1. Core Principles

### 1.1 No Authoritative Rig

- There is **no shared skeleton** across the character system
- Parts retain their own deformation systems (local rigs, shape keys, etc.)
- Socket compatibility is the **only global authority**

### 1.2 Deterministic Attachment

- Attachment is a pure function of socket profile matching
- No implicit rig merging or retargeting
- All behavior is explicit and predictable

### 1.3 Self-Contained Parts

- Each part carries everything it needs to render and deform
- Parts do not depend on external rig topology
- Parts expose typed socket interfaces for composition

---

## 2. Data Structures

### 2.1 Transform

```cpp
struct Transform {
    glm::vec3 position;
    glm::quat rotation;
    glm::vec3 scale;
    
    glm::mat4 toMatrix() const;
    static Transform fromMatrix(const glm::mat4& m);
    Transform compose(const Transform& child) const;
    Transform inverse() const;
};
```

### 2.2 OrientationFrame

Defines a local coordinate system for socket alignment.

```cpp
struct OrientationFrame {
    glm::vec3 normal;      // Primary axis (usually "up" or "out")
    glm::vec3 tangent;     // Secondary axis
    glm::vec3 bitangent;   // Computed or explicit (for surface sockets)
    
    glm::mat3 toBasis() const;
    static OrientationFrame fromNormalAndTangent(glm::vec3 n, glm::vec3 t);
};
```

### 2.3 Vertex

Extended to support all required attributes.

```cpp
struct Vertex {
    // Position
    glm::vec3 position;
    
    // Normal
    glm::vec3 normal;
    
    // Texture coordinates
    glm::vec2 uv;
    
    // Tangent space
    glm::vec4 tangent;  // w = handedness
    glm::vec3 bitangent;
    
    // Skinning (up to 4 bones per vertex)
    glm::ivec4 boneIDs;
    glm::vec4 boneWeights;
};
```

### 2.4 Bone

```cpp
struct Bone {
    uint32_t id;
    std::string name;
    Transform localTransform;
    Transform inverseBindMatrix;
    
    uint32_t parentID;           // 0 or UINT32_MAX = root
    std::vector<uint32_t> childIDs;
    
    // Socket extraction (bones named "socket_*" become sockets)
    bool isSocket() const { return name.rfind("socket_", 0) == 0; }
    std::string socketProfile() const;  // Extract profile from name
};
```

### 2.5 ShapeKey

```cpp
enum class ShapeKeyType {
    PartLocal,       // Internal to part, no external dependencies
    SocketDriven,    // Activated by attachment state
    SemanticShared,  // Name-based matching (optional)
    SeamCorrective   // Auto-generated per attachment
};

struct ShapeKey {
    std::string name;
    ShapeKeyType type;
    
    // Delta vertices (same count as mesh vertices)
    std::vector<glm::vec3> positionDeltas;
    std::vector<glm::vec3> normalDeltas;
    std::vector<glm::vec3> tangentDeltas;
    
    // Driver info (for SocketDriven type)
    std::string driverSocketProfile;
    std::string driverExpression;
    
    float weight;  // Current blend weight [0, 1]
};
```

### 2.6 Material Reference

```cpp
struct MaterialSlot {
    uint32_t index;
    std::string name;
    
    // Texture references
    std::string diffuseTexture;
    std::string normalTexture;
    std::string metallicRoughnessTexture;
    std::string aoTexture;
    std::string emissiveTexture;
    
    // Embedded texture data (if not external file)
    std::vector<uint8_t> embeddedDiffuse;
    std::vector<uint8_t> embeddedNormal;
    // ... etc
    
    // Material properties
    glm::vec4 baseColor;
    float metallic;
    float roughness;
    glm::vec3 emissive;
};
```

### 2.7 Mesh

```cpp
struct Mesh {
    uint32_t id;
    std::string name;
    
    // Geometry
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    
    // Material assignment
    std::vector<MaterialSlot> materials;
    std::vector<uint32_t> materialIndices;  // Per-triangle material index
    
    // Local rig (optional)
    std::vector<Bone> bones;
    std::map<uint32_t, uint32_t> boneNameToIndex;
    
    // Shape keys
    std::vector<ShapeKey> shapeKeys;
    
    // Submesh ranges (for multi-material)
    struct Submesh {
        uint32_t startIndex;
        uint32_t indexCount;
        uint32_t materialIndex;
    };
    std::vector<Submesh> submeshes;
};
```

---

## 3. Asset Loading Pipeline

### 3.1 Supported Formats

Primary: **glTF 2.0** (`.gltf`, `.glb`)
Secondary: **FBX**, **OBJ** (via Assimp fallback)

### 3.2 Loading Stages

```
┌─────────────────────────────────────────────────────────────┐
│                    ASSET LOADING PIPELINE                    │
├─────────────────────────────────────────────────────────────┤
│  1. Parse          │ Read file, extract scene graph         │
│  2. Extract Meshes │ Vertices, indices, materials           │
│  3. Extract Rig    │ Bone hierarchy, bind poses             │
│  4. Extract Sockets│ Bones named "socket_*" → Socket        │
│  5. Extract Keys   │ Morph targets → ShapeKey               │
│  6. Load Textures  │ External or embedded                   │
│  7. Validate       │ Check socket profiles, seam loops      │
│  8. Build Part     │ Assemble into Part structure           │
└─────────────────────────────────────────────────────────────┘
```

### 3.3 Assimp Import Configuration

```cpp
struct ImportConfig {
    bool triangulate = true;
    bool generateNormals = true;
    bool generateTangents = true;
    bool calculateBoneWeights = true;
    bool loadEmbeddedTextures = true;
    bool flipUVs = false;
    bool leftHandedToRightHanded = true;
    
    // Socket detection
    std::string socketBonePrefix = "socket_";
    
    // Texture search paths
    std::vector<std::string> textureSearchPaths;
};
```

### 3.4 Model Loader Interface

```cpp
class ModelLoader {
public:
    struct LoadResult {
        bool success;
        std::string error;
        
        std::vector<Mesh> meshes;
        std::vector<Bone> skeleton;
        std::vector<Socket> extractedSockets;
        std::vector<Texture> textures;
        std::map<std::string, std::vector<uint8_t>> embeddedTextures;
    };
    
    static LoadResult loadFromFile(const std::string& path, const ImportConfig& config);
    static LoadResult loadFromMemory(const std::vector<uint8_t>& data, 
                                     const std::string& formatHint,
                                     const ImportConfig& config);
    
private:
    static void extractSockets(const aiScene* scene, LoadResult& result);
    static void extractShapeKeys(const aiMesh* mesh, Mesh& outMesh);
    static void loadEmbeddedTextures(const aiScene* scene, LoadResult& result);
};
```

### 3.5 Socket Extraction from Bones

```cpp
// Bones named "socket_<profile>_<optional_name>" become sockets
// Example: "socket_hand_left" → profile="hand", name="left"
// Example: "socket_head" → profile="head", name=""

void ModelLoader::extractSockets(const aiScene* scene, LoadResult& result) {
    for (uint32_t i = 0; i < scene->mNumMeshes; ++i) {
        aiMesh* mesh = scene->mMeshes[i];
        for (uint32_t b = 0; b < mesh->mNumBones; ++b) {
            aiBone* bone = mesh->mBones[b];
            std::string name = bone->mName.C_Str();
            
            if (name.rfind("socket_", 0) == 0) {
                Socket socket;
                socket.id = generateSocketID();
                socket.spaceType = SpaceType::Bone;
                socket.transform = convertTransform(bone->mOffsetMatrix);
                socket.profile = parseSocketProfile(name);
                socket.name = parseSocketName(name);
                socket.ownerBoneIndex = b;
                
                result.extractedSockets.push_back(socket);
            }
        }
    }
}
```

---

## 4. Socket System

### 4.1 Socket Definition

```cpp
enum class SpaceType {
    Bone,           // Attached to bone transform
    MeshSurface,    // Attached to mesh surface (barycentric)
    Local           // Static local space
};

struct SocketProfile {
    std::string profileID;       // Unique identifier (e.g., "humanoid_hand_v1")
    std::string category;        // Grouping (e.g., "hand", "head", "accessory")
    uint32_t version;
    
    // Compatibility check
    bool isCompatibleWith(const SocketProfile& other) const;
};

struct SeamSpec {
    std::vector<uint32_t> seamLoopVertexIndices;
    float seamTolerance;
    bool requiresRemesh;
    
    enum class StitchMode {
        Snap,           // Move vertices to match
        Blend,          // Blend positions
        Bridge          // Generate bridging geometry
    } stitchMode;
};

struct Socket {
    uint32_t id;
    std::string name;
    SocketProfile profile;
    
    SpaceType spaceType;
    Transform transform;
    OrientationFrame orientationFrame;
    
    float influenceRadius;      // For deformation and remeshing bounds
    SeamSpec seamSpec;
    
    // Space-specific data
    uint32_t ownerBoneIndex;    // If SpaceType::Bone
    SurfaceReference surfaceRef;// If SpaceType::MeshSurface
    
    // Runtime state
    bool isOccupied = false;
    uint32_t attachedPartID = 0;
};
```

### 4.2 Socket Profile Registry

```cpp
class SocketProfileRegistry {
public:
    static SocketProfileRegistry& instance();
    
    void registerProfile(const SocketProfile& profile);
    const SocketProfile* findProfile(const std::string& profileID) const;
    
    std::vector<SocketProfile> getCompatibleProfiles(const std::string& profileID) const;
    bool areCompatible(const std::string& profileA, const std::string& profileB) const;
    
private:
    std::map<std::string, SocketProfile> profiles;
    std::map<std::string, std::set<std::string>> compatibilityMatrix;
};
```

---

## 5. Part System

### 5.1 Part Structure

```cpp
struct AttachmentSpec {
    SocketProfile requiredSocketProfile;
    std::vector<SpaceType> supportedSpaceTypes;
    
    // Deformation expectations
    struct DeformationExpectation {
        bool expectsSkeletalDeform = false;
        bool expectsSurfaceFollow = false;
        bool expectsPhysics = false;
    } deformationExpectations;
    
    // Seam requirements
    SeamSpec seamRequirements;
    
    // Priority for automatic socket selection
    int priority = 0;
};

struct Part {
    uint32_t id;
    std::string name;
    std::string category;
    
    // Geometry
    Mesh mesh;
    
    // Local rig (optional, self-contained)
    std::vector<Bone> localRig;
    
    // Shape keys
    std::vector<ShapeKey> localShapeKeys;
    
    // Sockets this part provides
    std::vector<Socket> socketsOut;
    
    // How this part connects to other sockets
    std::vector<AttachmentSpec> attachmentSpecs;
    
    // Transform relative to attachment socket
    Transform localTransform;
    
    // Metadata
    std::map<std::string, std::string> metadata;
    std::vector<std::string> tags;
};
```

### 5.2 Part Library

```cpp
class PartLibrary {
public:
    void registerPart(std::unique_ptr<Part> part);
    Part* findPartByID(uint32_t id);
    Part* findPartByName(const std::string& name);
    
    std::vector<Part*> findPartsForSocket(const SocketProfile& profile);
    std::vector<Part*> findPartsByCategory(const std::string& category);
    std::vector<Part*> findPartsByTag(const std::string& tag);
    
    // Persistence
    void saveToFile(const std::string& path);
    void loadFromFile(const std::string& path);
    
private:
    std::vector<std::unique_ptr<Part>> parts;
    std::map<uint32_t, Part*> idIndex;
    std::map<std::string, std::vector<Part*>> categoryIndex;
    std::map<std::string, std::vector<Part*>> socketProfileIndex;
};
```

---

## 6. Attachment Process

### 6.1 Attachment Steps (Deterministic)

```
┌─────────────────────────────────────────────────────────────┐
│                    ATTACHMENT PROCESS                        │
├─────────────────────────────────────────────────────────────┤
│  Step 1: Validate socket profile compatibility              │
│          └─ If incompatible, check for adapter              │
│                                                             │
│  Step 2: Resolve transform alignment                        │
│          └─ Compute part-to-socket transform                │
│                                                             │
│  Step 3: Bind part root to socket frame                     │
│          └─ Set part.localTransform relative to socket      │
│                                                             │
│  Step 4: Activate seam + deformation logic                  │
│          └─ Enable seam corrective shape keys               │
│          └─ Set up deformation field sampling               │
│                                                             │
│  Step 5: Generate or enable corrective data                 │
│          └─ Compute seam-corrective shape keys              │
│          └─ Initialize adapter if present                   │
└─────────────────────────────────────────────────────────────┘
```

### 6.2 Attachment Controller

```cpp
struct AttachmentResult {
    bool success;
    std::string error;
    
    uint32_t attachmentID;
    Transform resolvedTransform;
    std::vector<ShapeKey> generatedCorrectiveKeys;
    
    // If adapter was used
    bool usedAdapter = false;
    uint32_t adapterID = 0;
};

class AttachmentController {
public:
    AttachmentResult attach(Socket& socket, Part& part, 
                           const AttachmentSpec& spec);
    
    void detach(uint32_t attachmentID);
    
    void updateTransforms();  // Call each frame
    
private:
    AttachmentResult validateCompatibility(const Socket& socket, 
                                          const AttachmentSpec& spec);
    Transform resolveAlignment(const Socket& socket, const Part& part);
    void activateSeamLogic(Socket& socket, Part& part);
    void generateCorrectiveShapeKeys(Socket& socket, Part& part, 
                                    AttachmentResult& result);
    
    struct ActiveAttachment {
        uint32_t id;
        Socket* socket;
        Part* part;
        Transform resolvedTransform;
        Adapter* adapter;  // nullable
    };
    std::vector<ActiveAttachment> activeAttachments;
};
```

---

## 7. Adapters

Adapters translate between mismatched socket expectations. Adapters are **assets**, not implicit logic.

### 7.1 Adapter Definition

```cpp
struct TransformMapping {
    // How to map input socket transform to output
    glm::mat4 transformMatrix;
    
    // Optional: axis remapping
    glm::ivec3 axisRemap;  // e.g., {0, 2, 1} for Y↔Z swap
    bool flipHandedness = false;
};

struct DeformationMapping {
    // How to map deformation fields
    std::map<std::string, std::string> boneNameMapping;
    std::map<std::string, std::string> shapeKeyMapping;
    
    float influenceScale = 1.0f;
};

struct Adapter {
    uint32_t id;
    std::string name;
    
    SocketProfile inputSocketProfile;
    SocketProfile outputSocketProfile;
    
    TransformMapping transformMapping;
    DeformationMapping deformationMapping;
    
    SeamSpec seamStrategy;
    
    // Optional: adapter geometry (e.g., neck adapter between different body types)
    std::optional<Mesh> bridgeMesh;
    
    bool isValid(const Socket& input, const AttachmentSpec& output) const;
    Transform adapt(const Transform& input) const;
};
```

### 7.2 Adapter Registry

```cpp
class AdapterRegistry {
public:
    static AdapterRegistry& instance();
    
    void registerAdapter(std::unique_ptr<Adapter> adapter);
    
    Adapter* findAdapter(const SocketProfile& input, 
                        const SocketProfile& output);
    
    std::vector<Adapter*> findAdapterChain(const SocketProfile& input,
                                          const SocketProfile& output,
                                          int maxChainLength = 3);
    
private:
    std::vector<std::unique_ptr<Adapter>> adapters;
    
    // Graph for multi-step adaptation
    std::map<std::string, std::vector<std::pair<std::string, Adapter*>>> adaptationGraph;
};
```

---

## 8. Surface-Based Sockets

Surface sockets attach parts to mesh surfaces rather than bones. Critical for wings, growths, jewelry, etc.

### 8.1 Surface Reference

```cpp
struct BarycentricCoord {
    uint32_t triangleIndex;
    glm::vec3 barycentrics;  // u, v, w where u + v + w = 1
};

struct UVCoord {
    float u, v;
    uint32_t uvChannel = 0;
};

struct LandmarkReference {
    std::string landmarkName;
    glm::vec3 offset;
};

struct SurfaceReference {
    enum class Type {
        Barycentric,
        UV,
        Landmark
    } type;
    
    BarycentricCoord barycentric;
    UVCoord uv;
    LandmarkReference landmark;
    
    // Stability: must survive topology edits
    // UV or landmark references preferred over barycentric
};
```

### 8.2 Surface Socket

```cpp
struct SeamLoopDefinition {
    // Closed loop of vertices defining the seam boundary
    std::vector<uint32_t> vertexIndices;
    
    // Alternative: parametric definition
    std::string parametricExpression;  // e.g., "uv_circle(0.5, 0.5, 0.1)"
};

enum class RemeshPolicy {
    None,               // No remeshing allowed
    LocalOnly,          // Remesh within influence radius
    GenerateBridge,     // Generate bridging geometry
    ShrinkWrap          // Project onto surface
};

struct SurfaceSocket : public Socket {
    SurfaceReference surfaceReference;
    SeamLoopDefinition seamLoopDefinition;
    RemeshPolicy remeshPolicy;
    
    // Computed at runtime
    glm::vec3 computePosition(const Mesh& mesh) const;
    OrientationFrame computeOrientation(const Mesh& mesh) const;
    
    // Stability check
    bool isStable(const Mesh& oldMesh, const Mesh& newMesh) const;
    void migrate(const Mesh& oldMesh, const Mesh& newMesh);
};
```

### 8.3 Surface Socket Stability

Requirements for surface sockets to survive topology edits:

1. **Never rely on vertex indices** - indices change with topology edits
2. **Prefer UV-based references** - UV space is stable
3. **Use landmark references** - named landmarks survive edits
4. **Implement migration logic** - when topology changes, migrate references

```cpp
class SurfaceSocketMigrator {
public:
    // Migrate surface socket when topology changes
    static bool migrate(SurfaceSocket& socket,
                       const Mesh& oldMesh,
                       const Mesh& newMesh);
    
private:
    static BarycentricCoord uvToBarycentric(const UVCoord& uv, const Mesh& mesh);
    static UVCoord barycentricToUV(const BarycentricCoord& bary, const Mesh& mesh);
    static bool findNearestTriangle(const glm::vec3& worldPos,
                                   const Mesh& mesh,
                                   BarycentricCoord& outBary);
};
```

---

## 9. Seam Remeshing

Remeshing is **constrained** to the seam region only.

### 9.1 Seam Remesh Specification

```cpp
struct SeamRemesh {
    // Input seam loops
    std::vector<uint32_t> bodySeamLoop;    // Vertices on body mesh
    std::vector<uint32_t> partSeamLoop;    // Vertices on part mesh
    
    // Bounded remesh volume
    float remeshRadius;   // Derived from socket.influenceRadius
    
    // Projection method
    enum class ProjectionMethod {
        Nearest,          // Project to nearest surface point
        Barycentric,      // Interpolate via barycentric coords
        Cage              // Use deformation cage
    } projectionMethod;
    
    float stitchTolerance;  // Max distance for auto-stitching
};
```

### 9.2 Seam Remesh Rules

1. **No global remesh** - only within `remeshRadius`
2. **No topology changes outside remesh volume** - body mesh topology preserved
3. **Vertex count mismatch allowed** - seam loops can have different vertex counts
4. **Bridging geometry acceptable** - generate triangles to connect loops

### 9.3 Seam Processor

```cpp
class SeamProcessor {
public:
    struct SeamResult {
        bool success;
        std::string error;
        
        // Modified vertices (within influence radius only)
        std::vector<uint32_t> modifiedBodyVertices;
        std::vector<uint32_t> modifiedPartVertices;
        
        // Generated bridging geometry (if needed)
        std::vector<Vertex> bridgeVertices;
        std::vector<uint32_t> bridgeIndices;
    };
    
    static SeamResult processSeam(Mesh& bodyMesh,
                                 Mesh& partMesh,
                                 const SeamRemesh& spec);
    
private:
    // Match seam loop vertices
    static std::vector<std::pair<uint32_t, uint32_t>> 
        matchSeamVertices(const std::vector<uint32_t>& bodyLoop,
                         const std::vector<uint32_t>& partLoop,
                         const Mesh& bodyMesh,
                         const Mesh& partMesh,
                         float tolerance);
    
    // Generate bridging triangles
    static void generateBridge(const std::vector<std::pair<uint32_t, uint32_t>>& matches,
                              SeamResult& result);
    
    // Smooth seam region
    static void smoothSeamRegion(Mesh& mesh,
                                const std::vector<uint32_t>& seam,
                                float radius);
};
```

---

## 10. Shape Key System

### 10.1 Shape Key Classification

| Type | Description | Driver | Visibility |
|------|-------------|--------|------------|
| **Part-Local** | Internal to part | Manual weight | Visible |
| **Socket-Driven** | Activated by attachment | Socket state | Hidden/Auto |
| **Semantic Shared** | Name-based matching | External driver | Visible |
| **Seam-Corrective** | Generated per attachment | Auto | Hidden |

### 10.2 Shape Key Manager

```cpp
class ShapeKeyManager {
public:
    // Classify shape key from name/metadata
    static ShapeKeyType classifyShapeKey(const ShapeKey& key);
    
    // Generate seam-corrective keys
    static ShapeKey generateSeamCorrective(const Socket& socket,
                                          const Part& part,
                                          const Mesh& bodyMesh);
    
    // Apply shape keys to mesh
    void applyShapeKeys(Mesh& mesh, 
                       const std::vector<ShapeKey>& keys,
                       const std::vector<float>& weights);
    
    // Semantic matching
    std::map<std::string, std::vector<ShapeKey*>> 
        matchSemanticKeys(const std::vector<Part*>& parts);
    
private:
    // Compute delta
    static void computeDeltas(const Mesh& base,
                             const Mesh& target,
                             ShapeKey& outKey);
};
```

### 10.3 Socket-Driven Shape Keys

Socket-driven shape keys activate based on attachment state:

```cpp
struct SocketDrivenKey {
    ShapeKey baseKey;
    
    // Driver configuration
    std::string targetSocketProfile;
    
    enum class DriverMode {
        Binary,        // 0 or 1 based on occupancy
        Transform,     // Driven by socket transform
        Distance       // Driven by distance to socket
    } driverMode;
    
    // For Transform mode
    std::string transformChannel;  // "tx", "ty", "tz", "rx", "ry", "rz"
    float minValue, maxValue;
    
    float evaluate(const Socket& socket) const;
};
```

---

## 11. Deformation Coupling

Motion transfer is **field-based**, not bone-based. Parts sample deformation fields to drive secondary motion.

### 11.1 Deformation Field

```cpp
struct DeformationField {
    // Reference frames in world space
    std::vector<Transform> referenceFrames;
    
    // Per-frame deltas
    std::vector<glm::quat> rotationDeltas;
    std::vector<glm::vec3> translationDeltas;
    
    // Velocity (for secondary motion)
    std::vector<glm::vec3> velocityVectors;
    
    // Falloff volumes (spherical)
    std::vector<float> falloffRadii;
    
    // Sample the field at a point
    Transform sample(const glm::vec3& worldPosition) const;
    glm::vec3 sampleVelocity(const glm::vec3& worldPosition) const;
};
```

### 11.2 Deformation Field Generator

```cpp
class DeformationFieldGenerator {
public:
    // Generate field from skeleton animation
    static DeformationField generateFromSkeleton(
        const std::vector<Bone>& skeleton,
        const std::vector<Transform>& currentPose,
        const std::vector<Transform>& previousPose);
    
    // Generate field from mesh deformation
    static DeformationField generateFromMesh(
        const Mesh& restMesh,
        const Mesh& deformedMesh);
};
```

### 11.3 Part Deformation Sampler

```cpp
class PartDeformationSampler {
public:
    // Sample deformation field and apply to part
    void sampleAndApply(Part& part,
                       const DeformationField& field,
                       const Transform& socketTransform);
    
private:
    // Per-vertex sampling with falloff
    void sampleVertices(std::vector<Vertex>& vertices,
                       const DeformationField& field,
                       float influenceRadius);
    
    // Secondary motion simulation
    void applySecondaryMotion(std::vector<Vertex>& vertices,
                             const std::vector<glm::vec3>& velocities,
                             float dt);
};
```

---

## 12. Inverse Kinematics & Procedural Animation

Once a character/creature is composed from parts, the IK system enables **procedural animation** without requiring pre-baked animations. Each part's local rig participates in IK solving while respecting the socket-centric architecture.

### 12.1 IK Design Principles

- **Part-local IK chains** - IK operates within part boundaries by default
- **Cross-part IK via sockets** - Sockets define IK bridge points between parts
- **No global skeleton assumption** - IK chains are defined per-character, not per-rig
- **Composable effectors** - Effectors can target world positions, other sockets, or procedural goals
- **Part-type-aware solving** - IK behavior adapts based on anatomical role (head, limb, digit, tail, wing)
- **Surface-conforming digits** - Fingers and toes deform to wrap around contact surfaces

### 12.2 Part Type Classification

Parts are classified by anatomical role, which affects IK solving behavior, default constraints, and procedural animation.

```cpp
enum class PartRole {
    // Core body
    Root,                 // Pelvis, center of mass
    Spine,                // Vertebral segments
    Chest,                // Ribcage/thorax
    Neck,                 // Cervical spine
    Head,                 // Skull + face
    
    // Upper limb
    Shoulder,             // Clavicle/scapula
    UpperArm,             // Humerus
    LowerArm,             // Radius/ulna
    Hand,                 // Carpals + metacarpals
    Finger,               // Phalanges (digits)
    Thumb,                // Thumb (special 2-DOF base)
    
    // Lower limb
    Hip,                  // Pelvis attachment
    UpperLeg,             // Femur
    LowerLeg,             // Tibia/fibula
    Foot,                 // Tarsals + metatarsals
    Toe,                  // Phalanges (digits)
    
    // Appendages
    Tail,                 // Caudal vertebrae
    TailTip,              // End of tail
    WingRoot,             // Wing attachment
    WingMid,              // Wing elbow/wrist
    WingTip,              // Wing tip
    WingMembrane,         // Membrane between bones
    
    // Specialized
    Ear,                  // Ear (for expressive characters)
    Jaw,                  // Mandible
    Eye,                  // Eye (for look-at)
    Tentacle,             // Tentacle segment
    TentacleTip,          // Tentacle end
    
    // Generic
    Appendage,            // Unspecified appendage
    Accessory,            // Non-anatomical attachment
    Custom                // User-defined
};

struct PartRoleInfo {
    PartRole role;
    std::string customRoleName;  // For Custom role
    
    // IK behavior hints
    bool isDigit() const { 
        return role == PartRole::Finger || role == PartRole::Thumb || 
               role == PartRole::Toe || role == PartRole::TentacleTip; 
    }
    
    bool isLimbSegment() const {
        return role == PartRole::UpperArm || role == PartRole::LowerArm ||
               role == PartRole::UpperLeg || role == PartRole::LowerLeg;
    }
    
    bool isSpineSegment() const {
        return role == PartRole::Spine || role == PartRole::Neck || 
               role == PartRole::Tail;
    }
    
    bool isWingSegment() const {
        return role == PartRole::WingRoot || role == PartRole::WingMid || 
               role == PartRole::WingTip;
    }
    
    bool supportsSurfaceConform() const {
        return isDigit();
    }
};
```

### 12.3 Part-Role-Aware IK Constraints

Default constraints and solver hints based on part role:

```cpp
struct PartRoleConstraints {
    PartRole role;
    
    // Default joint limits (can be overridden per-joint)
    glm::vec3 defaultRotationMin;
    glm::vec3 defaultRotationMax;
    IKJoint::JointType defaultJointType;
    
    // Solver hints
    float defaultStiffness;
    bool preferHinge;             // Prefer hinge over ball for this role
    glm::vec3 preferredHingeAxis; // If preferHinge is true
    
    // Behavioral flags
    bool propagatesMotion;        // Motion should flow through (spine, tail)
    bool isEndEffector;           // Typically targeted by effectors
    bool supportsLookAt;          // Can participate in look-at
    bool supportsSurfaceConform;  // Can wrap around surfaces
};

class PartRoleConstraintLibrary {
public:
    static const PartRoleConstraints& getDefaults(PartRole role);
    
    static const std::map<PartRole, PartRoleConstraints> defaults;
};

// Example defaults
const std::map<PartRole, PartRoleConstraints> PartRoleConstraintLibrary::defaults = {
    {PartRole::Head, {
        .role = PartRole::Head,
        .defaultRotationMin = {-40, -70, -30},
        .defaultRotationMax = {30, 70, 30},
        .defaultJointType = IKJoint::JointType::Ball,
        .defaultStiffness = 0.3f,
        .preferHinge = false,
        .supportsLookAt = true,
        .supportsSurfaceConform = false
    }},
    
    {PartRole::Neck, {
        .role = PartRole::Neck,
        .defaultRotationMin = {-20, -30, -15},
        .defaultRotationMax = {20, 30, 15},
        .defaultJointType = IKJoint::JointType::Ball,
        .defaultStiffness = 0.2f,
        .propagatesMotion = true,
        .supportsLookAt = true
    }},
    
    {PartRole::UpperLeg, {
        .role = PartRole::UpperLeg,
        .defaultRotationMin = {-30, -45, -45},
        .defaultRotationMax = {120, 45, 45},
        .defaultJointType = IKJoint::JointType::Ball,
        .defaultStiffness = 0.4f
    }},
    
    {PartRole::LowerLeg, {
        .role = PartRole::LowerLeg,
        .defaultRotationMin = {0, 0, 0},
        .defaultRotationMax = {150, 0, 0},
        .defaultJointType = IKJoint::JointType::Hinge,
        .preferHinge = true,
        .preferredHingeAxis = {1, 0, 0},
        .defaultStiffness = 0.5f
    }},
    
    {PartRole::Finger, {
        .role = PartRole::Finger,
        .defaultRotationMin = {-10, -15, 0},
        .defaultRotationMax = {90, 15, 0},
        .defaultJointType = IKJoint::JointType::Hinge,
        .preferHinge = true,
        .preferredHingeAxis = {1, 0, 0},
        .defaultStiffness = 0.1f,
        .isEndEffector = true,
        .supportsSurfaceConform = true
    }},
    
    {PartRole::Thumb, {
        .role = PartRole::Thumb,
        .defaultRotationMin = {-20, -30, -30},
        .defaultRotationMax = {60, 30, 30},
        .defaultJointType = IKJoint::JointType::Saddle,  // CMC joint
        .defaultStiffness = 0.15f,
        .isEndEffector = true,
        .supportsSurfaceConform = true
    }},
    
    {PartRole::Toe, {
        .role = PartRole::Toe,
        .defaultRotationMin = {-30, -10, 0},
        .defaultRotationMax = {60, 10, 0},
        .defaultJointType = IKJoint::JointType::Hinge,
        .preferHinge = true,
        .defaultStiffness = 0.1f,
        .supportsSurfaceConform = true
    }},
    
    {PartRole::Tail, {
        .role = PartRole::Tail,
        .defaultRotationMin = {-30, -45, -30},
        .defaultRotationMax = {30, 45, 30},
        .defaultJointType = IKJoint::JointType::Ball,
        .defaultStiffness = 0.1f,
        .propagatesMotion = true
    }},
    
    {PartRole::WingRoot, {
        .role = PartRole::WingRoot,
        .defaultRotationMin = {-30, -10, -90},
        .defaultRotationMax = {120, 45, 30},
        .defaultJointType = IKJoint::JointType::Ball,
        .defaultStiffness = 0.4f
    }},
    
    {PartRole::WingTip, {
        .role = PartRole::WingTip,
        .defaultRotationMin = {0, 0, -10},
        .defaultRotationMax = {0, 0, 90},
        .defaultJointType = IKJoint::JointType::Hinge,
        .preferHinge = true,
        .defaultStiffness = 0.2f,
        .isEndEffector = true
    }},
    
    {PartRole::Tentacle, {
        .role = PartRole::Tentacle,
        .defaultRotationMin = {-60, -60, -60},
        .defaultRotationMax = {60, 60, 60},
        .defaultJointType = IKJoint::JointType::Ball,
        .defaultStiffness = 0.05f,
        .propagatesMotion = true
    }}
};
```

### 12.4 IK Chain Definition

```cpp
struct IKJoint {
    uint32_t boneIndex;           // Index into part's local rig
    uint32_t partID;              // Which part owns this bone
    PartRole partRole;            // Anatomical role of this part
    
    // Constraints
    glm::vec3 rotationMin;        // Euler min limits (degrees)
    glm::vec3 rotationMax;        // Euler max limits (degrees)
    glm::vec3 preferredAxis;      // Hinge joint preferred axis
    
    enum class JointType {
        Ball,                     // Full 3-DOF rotation
        Hinge,                    // 1-DOF rotation around axis
        Saddle,                   // 2-DOF (like thumb CMC)
        Fixed                     // No rotation allowed
    } jointType;
    
    float stiffness;              // Resistance to movement [0, 1]
    float weight;                 // Influence in solver
    
    // Surface conform settings (for digits)
    bool enableSurfaceConform;    // If true, digit can wrap surfaces
    float surfaceConformStrength; // How strongly to conform [0, 1]
};

struct IKChain {
    uint32_t id;
    std::string name;
    PartRole primaryRole;         // Dominant role of this chain
    
    // Chain topology
    std::vector<IKJoint> joints;  // Ordered root to tip
    uint32_t rootJointIndex;      // First movable joint
    uint32_t tipJointIndex;       // End effector joint
    
    // Cross-part bridging
    struct SocketBridge {
        uint32_t socketID;        // Socket connecting two parts
        uint32_t jointIndexBefore;// Last joint before socket
        uint32_t jointIndexAfter; // First joint after socket
    };
    std::vector<SocketBridge> socketBridges;
    
    // Solver settings
    enum class SolverType {
        CCD,                      // Cyclic Coordinate Descent
        FABRIK,                   // Forward And Backward Reaching IK
        Jacobian,                 // Jacobian transpose/pseudoinverse
        Hybrid,                   // FABRIK + CCD refinement
        SurfaceConform            // Special solver for digit surface wrapping
    } solverType;
    
    uint32_t maxIterations;       // Default: 10
    float tolerance;              // Position error threshold
    float dampingFactor;          // For Jacobian methods
    
    // Part-role-aware settings
    bool useRoleDefaults;         // Apply defaults from PartRoleConstraintLibrary
    bool isDigitChain;            // Chain represents fingers/toes
    bool allowsSurfaceConform;    // Can this chain wrap around surfaces
};
```

### 12.5 IK Effectors

```cpp
struct IKEffector {
    uint32_t id;
    std::string name;
    
    // Target chain
    uint32_t chainID;
    
    // Target specification
    enum class TargetType {
        WorldPosition,            // Fixed world coordinate
        Socket,                   // Another socket's transform
        Bone,                     // Another bone's transform
        Procedural,               // Computed each frame
        LookAt,                   // Orientation toward target
        Surface                   // Target is a surface (for digits)
    } targetType;
    
    // Target data (union-like)
    glm::vec3 worldPosition;
    uint32_t targetSocketID;
    uint32_t targetBoneID;
    uint32_t targetPartID;
    std::string proceduralDriver; // Name of procedural system
    
    // Surface target (for digit conform)
    struct SurfaceTarget {
        uint32_t meshID;          // Target mesh to conform to
        glm::vec3 contactPoint;   // Initial contact point
        glm::vec3 surfaceNormal;  // Surface normal at contact
        float wrapRadius;         // Radius for surface sampling
    } surfaceTarget;
    
    // Blending
    float weight;                 // [0, 1] blend with FK pose
    float smoothTime;             // Interpolation time (seconds)
    
    // Position + rotation control
    bool affectPosition;          // Default: true
    bool affectRotation;          // Default: false
    glm::vec3 positionOffset;     // Offset from target
    glm::quat rotationOffset;     // Rotation offset
    
    // Pole vector (for elbow/knee direction)
    bool usePoleVector;
    glm::vec3 poleTarget;
};
```

### 12.4 IK Solver Interface

```cpp
class IKSolver {
public:
    struct SolveResult {
        bool converged;
        uint32_t iterations;
        float finalError;
        std::vector<Transform> jointTransforms;
    };
    
    virtual ~IKSolver() = default;
    
    // Solve single chain
    virtual SolveResult solve(const IKChain& chain,
                              const IKEffector& effector,
                              const std::vector<Transform>& currentPose) = 0;
    
    // Batched solve for multiple chains
    virtual std::vector<SolveResult> solveBatch(
        const std::vector<std::pair<IKChain*, IKEffector*>>& chains,
        const std::vector<Transform>& currentPose) = 0;
};

// Concrete solvers
class CCDSolver : public IKSolver {
public:
    SolveResult solve(const IKChain& chain,
                     const IKEffector& effector,
                     const std::vector<Transform>& currentPose) override;
};

class FABRIKSolver : public IKSolver {
public:
    SolveResult solve(const IKChain& chain,
                     const IKEffector& effector,
                     const std::vector<Transform>& currentPose) override;

private:
    void forwardPass(std::vector<glm::vec3>& positions, const glm::vec3& target);
    void backwardPass(std::vector<glm::vec3>& positions, const glm::vec3& root);
    void applyConstraints(std::vector<glm::vec3>& positions, const IKChain& chain);
};

class JacobianSolver : public IKSolver {
public:
    enum class Method { Transpose, Pseudoinverse, DampedLeastSquares };
    Method method = Method::DampedLeastSquares;
    
    SolveResult solve(const IKChain& chain,
                     const IKEffector& effector,
                     const std::vector<Transform>& currentPose) override;

private:
    glm::mat4 computeJacobian(const IKChain& chain,
                             const std::vector<Transform>& pose);
};

// Specialized solver for digit surface conforming
class SurfaceConformSolver : public IKSolver {
public:
    struct SurfaceConformResult : public SolveResult {
        std::vector<glm::vec3> contactPoints;
        std::vector<glm::vec3> contactNormals;
        std::vector<bool> jointInContact;
        float conformScore;       // How well the digit conforms [0, 1]
    };
    
    // Sphere cast callback for surface detection
    std::function<bool(glm::vec3 origin, glm::vec3 direction, float radius,
                       glm::vec3& outPoint, glm::vec3& outNormal,
                       uint32_t& outMeshID)> sphereCast;
    
    // Surface conform parameters
    float contactRadius = 0.01f;
    float conformStrength = 0.8f;
    float curlFalloff = 0.3f;     // How much curl decreases per joint
    
    SolveResult solve(const IKChain& chain,
                     const IKEffector& effector,
                     const std::vector<Transform>& currentPose) override;
    
    // Extended solve with surface info
    SurfaceConformResult solveWithContact(
        const IKChain& chain,
        const IKEffector& effector,
        const std::vector<Transform>& currentPose);

private:
    // Solve digit to wrap around surface
    void solveDigitConform(const IKChain& chain,
                          const glm::vec3& contactPoint,
                          const glm::vec3& contactNormal,
                          std::vector<Transform>& outTransforms);
    
    // Compute curl rotation for joint following surface
    glm::quat computeCurlRotation(const glm::vec3& jointAxis,
                                 const glm::vec3& surfaceNormal,
                                 float curlAmount);
    
    // Sample surface around joint
    bool sampleSurfaceAroundJoint(const glm::vec3& jointPos,
                                 const glm::vec3& jointDir,
                                 glm::vec3& outContactPoint,
                                 glm::vec3& outContactNormal);
};
```

### 12.5 Procedural Animation Systems

Procedural systems generate IK targets dynamically based on environment and character state.

#### 12.5.1 Foot Placement (Ground IK)

```cpp
struct FootPlacementConfig {
    float raycastHeight;          // Start height above character
    float raycastDepth;           // Max depth below character
    float footOffset;             // Vertical offset from ground
    
    float adaptationSpeed;        // How fast feet adapt to terrain
    float maxStepHeight;          // Max height difference per step
    float maxSlopeAngle;          // Max slope before sliding
    
    // Foot rotation
    bool alignToSurface;          // Rotate foot to match ground normal
    float rotationLimit;          // Max foot rotation (degrees)
};

class FootPlacementSystem {
public:
    struct FootState {
        glm::vec3 targetPosition;
        glm::quat targetRotation;
        glm::vec3 groundNormal;
        bool isGrounded;
        float groundDistance;
    };
    
    void configure(const FootPlacementConfig& config);
    
    // Update foot targets based on raycasts
    FootState updateFoot(const Transform& hipTransform,
                        const Transform& currentFootTransform,
                        const std::function<bool(glm::vec3, glm::vec3, float&, glm::vec3&)>& raycast,
                        float dt);
    
    // Adjust pelvis height based on foot positions
    float computePelvisOffset(const std::vector<FootState>& feet);
    
private:
    FootPlacementConfig config;
    std::map<uint32_t, FootState> footStates;
};
```

#### 12.5.2 Look-At System

```cpp
struct LookAtConfig {
    // Chain specification
    uint32_t headChainID;
    std::vector<uint32_t> spineChainIDs;  // For upper body follow
    
    // Limits
    float maxHeadYaw;             // Degrees
    float maxHeadPitch;           // Degrees
    float maxSpineContribution;   // [0, 1] how much spine helps
    
    // Smoothing
    float eyeSpeed;               // Degrees per second
    float headSpeed;
    float spineSpeed;
};

class LookAtSystem {
public:
    void configure(const LookAtConfig& config);
    
    // Set look target
    void setTarget(const glm::vec3& worldTarget);
    void setTargetSocket(uint32_t socketID);
    void clearTarget();
    
    // Update IK effectors
    void update(float dt);
    
    // Get effector targets for IK solver
    std::vector<IKEffector> getEffectors() const;
    
private:
    LookAtConfig config;
    glm::vec3 currentTarget;
    glm::vec3 smoothedTarget;
    bool hasTarget;
};
```

#### 12.5.3 Reach / Grab System

```cpp
struct ReachConfig {
    uint32_t armChainID;
    float maxReachDistance;
    float reachSpeed;
    
    // Hand orientation
    bool autoOrientHand;
    glm::quat graspOrientation;   // Orientation when grasping
};

class ReachSystem {
public:
    enum class ReachState {
        Idle,
        Reaching,
        Holding,
        Releasing
    };
    
    void configure(const ReachConfig& config);
    
    // Commands
    void reachTo(const glm::vec3& worldTarget);
    void reachToSocket(uint32_t targetSocketID);
    void grab();                  // Lock hand to current target
    void release();
    
    // Update
    void update(float dt);
    
    ReachState getState() const;
    IKEffector getEffector() const;
    
private:
    ReachConfig config;
    ReachState state;
    glm::vec3 target;
    float reachProgress;
};
```

#### 12.5.4 Procedural Locomotion

```cpp
struct LocomotionConfig {
    // Gait parameters
    float strideLength;
    float stepHeight;
    float stepDuration;
    
    // Timing
    std::vector<float> footPhaseOffsets;  // Per-foot phase [0, 1]
    
    // Body motion
    float bodyBobAmount;
    float bodySway;
    float bodyLean;               // Lean into turns
};

class ProceduralLocomotion {
public:
    void configure(const LocomotionConfig& config);
    
    // Set movement input
    void setVelocity(const glm::vec3& worldVelocity);
    void setTargetPosition(const glm::vec3& position);
    
    // Update locomotion state
    void update(float dt);
    
    // Get foot targets
    std::vector<glm::vec3> getFootTargets() const;
    
    // Get body adjustments
    Transform getBodyOffset() const;
    
private:
    LocomotionConfig config;
    glm::vec3 velocity;
    float phase;                  // Current gait phase [0, 1]
    std::vector<glm::vec3> footTargets;
    std::vector<glm::vec3> footCurrentPositions;
};
```

#### 12.5.5 Surface-Conforming Digit System

Specialized IK for fingers and toes that wrap around contact surfaces.

```cpp
struct DigitConfig {
    // Chain references
    std::vector<uint32_t> digitChainIDs;  // Finger/toe chains
    uint32_t palmOrSoleChainID;           // Parent hand/foot chain
    
    // Contact detection
    float contactRadius;          // Raycast sphere radius per joint
    float contactThreshold;       // Distance to trigger surface conform
    
    // Conform behavior
    float conformStrength;        // [0, 1] how tightly to wrap
    float spreadAngle;            // Natural spread between digits
    float curlFalloff;            // How curl decreases along digit
    
    // Surface sampling
    uint32_t samplesPerJoint;     // Raycasts per digit joint
    float sampleSpread;           // Angular spread for samples
};

struct DigitState {
    uint32_t chainID;
    bool inContact;               // Any joint touching surface
    
    // Per-joint contact info
    struct JointContact {
        bool hasContact;
        glm::vec3 contactPoint;
        glm::vec3 contactNormal;
        float penetrationDepth;
        uint32_t contactMeshID;
    };
    std::vector<JointContact> jointContacts;
    
    // Computed poses
    std::vector<glm::quat> conformedRotations;
    float overallConformWeight;
};

class SurfaceConformDigitSystem {
public:
    void configure(const DigitConfig& config);
    
    // Update digit states based on surface contact
    void update(const std::vector<IKChain>& digitChains,
               const std::vector<Transform>& currentPose,
               const std::function<bool(glm::vec3, glm::vec3, float, 
                                        glm::vec3&, glm::vec3&, uint32_t&)>& sphereCast,
               float dt);
    
    // Get conform targets for IK solver
    std::vector<IKEffector> getDigitEffectors() const;
    
    // Get per-digit state
    const DigitState& getDigitState(uint32_t chainID) const;
    
    // Manual control
    void setCurlOverride(uint32_t chainID, float curlAmount);  // [0, 1]
    void setSpreadOverride(float spreadMultiplier);
    void forceRelax(uint32_t chainID);  // Release grip
    
private:
    DigitConfig config;
    std::map<uint32_t, DigitState> digitStates;
    
    // Solve single digit surface conform
    void solveDigitConform(const IKChain& chain,
                          const std::vector<Transform>& pose,
                          DigitState& state);
    
    // Compute joint rotation to follow surface
    glm::quat computeSurfaceFollowRotation(
        const glm::vec3& jointPos,
        const glm::vec3& nextJointPos,
        const glm::vec3& surfaceNormal,
        const glm::quat& currentRot,
        float strength);
};
```

#### 12.5.6 Toe Ground Conform System

Specialized system for toes conforming to terrain during locomotion.

```cpp
struct ToeConformConfig {
    // Toe chain references
    struct FootToes {
        uint32_t footChainID;
        std::vector<uint32_t> toeChainIDs;
    };
    std::vector<FootToes> feet;
    
    // Ground conform
    float groundConformStrength;  // How much toes bend to terrain
    float slopeAdaptation;        // Spread adjustment on slopes
    float pushOffCurl;            // Extra curl during push-off phase
    
    // Timing (relative to gait phase)
    float liftPhaseStart;         // When toes start lifting [0, 1]
    float plantPhaseStart;        // When toes start planting [0, 1]
};

class ToeGroundConformSystem {
public:
    void configure(const ToeConformConfig& config);
    
    // Integrate with locomotion
    void setLocomotionPhase(uint32_t footIndex, float phase);
    
    // Update toe conform based on foot state
    void update(const FootPlacementSystem::FootState& footState,
               const std::vector<IKChain>& toeChains,
               const std::vector<Transform>& currentPose,
               const std::function<bool(glm::vec3, glm::vec3, float&, glm::vec3&)>& raycast,
               float dt);
    
    // Get toe effectors
    std::vector<IKEffector> getToeEffectors(uint32_t footIndex) const;
    
private:
    ToeConformConfig config;
    std::map<uint32_t, std::vector<DigitState>> toeStates;  // Per-foot
    std::map<uint32_t, float> locomotionPhases;
    
    // Compute push-off curl
    float computePushOffCurl(float phase) const;
};
```

#### 12.5.7 Finger Grip/Grasp System

Advanced finger control for gripping objects.

```cpp
struct GripConfig {
    std::vector<uint32_t> fingerChainIDs;
    uint32_t thumbChainID;
    uint32_t handChainID;
    
    // Grip shapes
    enum class GripType {
        Power,            // Fist around object
        Precision,        // Thumb + index/middle
        Pinch,            // Thumb + index only
        Lateral,          // Side of index finger
        Hook,             // Curled fingers, no thumb
        Relaxed           // Natural rest pose
    };
    
    // Grip parameters
    float gripStrength;           // [0, 1]
    float thumbOpposition;        // How much thumb opposes fingers
    float fingerSpread;           // Spread between fingers
};

struct GripTarget {
    glm::vec3 objectCenter;
    float objectRadius;           // Approximate size
    glm::vec3 graspAxis;          // Primary axis of grasp
    uint32_t objectMeshID;        // For surface conform
};

class FingerGripSystem {
public:
    void configure(const GripConfig& config);
    
    // Set grip type
    void setGripType(GripConfig::GripType type);
    void setGripStrength(float strength);
    
    // Target an object
    void setGripTarget(const GripTarget& target);
    void clearGripTarget();
    
    // Update finger positions
    void update(const std::vector<IKChain>& fingerChains,
               const IKChain& thumbChain,
               const std::vector<Transform>& currentPose,
               const std::function<bool(glm::vec3, glm::vec3, float, 
                                        glm::vec3&, glm::vec3&, uint32_t&)>& sphereCast,
               float dt);
    
    // Get effectors for all fingers
    std::vector<IKEffector> getFingerEffectors() const;
    IKEffector getThumbEffector() const;
    
    // Query state
    bool isGripping() const;
    float getGripSecurityScore() const;  // How secure the grip is [0, 1]
    
private:
    GripConfig config;
    GripConfig::GripType currentGripType;
    std::optional<GripTarget> gripTarget;
    
    SurfaceConformDigitSystem digitConform;
    
    // Compute finger poses for grip type
    std::vector<glm::quat> computeGripPose(GripConfig::GripType type,
                                          const GripTarget* target);
};
```

### 12.6 IK Rig Builder

Automatically constructs IK chains from composed character based on socket topology and part roles.

```cpp
class IKRigBuilder {
public:
    struct IKRig {
        std::vector<IKChain> chains;
        std::vector<IKEffector> defaultEffectors;
        
        // Named chain access
        std::map<std::string, uint32_t> chainNameIndex;
        
        // Part role classification
        std::map<uint32_t, PartRole> chainRoles;
        
        // Procedural systems
        std::unique_ptr<FootPlacementSystem> footPlacement;
        std::unique_ptr<LookAtSystem> lookAt;
        std::vector<std::unique_ptr<ReachSystem>> reachSystems;
        std::unique_ptr<ProceduralLocomotion> locomotion;
        
        // Digit systems
        std::unique_ptr<SurfaceConformDigitSystem> handDigits;
        std::unique_ptr<ToeGroundConformSystem> toeConform;
        std::unique_ptr<FingerGripSystem> fingerGrip;
    };
    
    // Build IK rig from composed character
    static IKRig buildFromCharacter(const std::vector<Part*>& attachedParts,
                                    const std::vector<Socket*>& sockets);
    
    // Detect standard chains (biped, quadruped, etc.)
    static void detectStandardChains(const std::vector<Bone>& allBones,
                                    const std::vector<Socket>& sockets,
                                    IKRig& outRig);
    
    // Detect and configure digit chains
    static void detectDigitChains(const std::vector<Bone>& allBones,
                                 const std::map<uint32_t, PartRole>& boneRoles,
                                 IKRig& outRig);
    
private:
    // Heuristics for chain detection based on PartRole
    static bool isLegChain(const std::vector<Bone>& chain);
    static bool isArmChain(const std::vector<Bone>& chain);
    static bool isSpineChain(const std::vector<Bone>& chain);
    static bool isHeadChain(const std::vector<Bone>& chain);
    static bool isTailChain(const std::vector<Bone>& chain);
    static bool isWingChain(const std::vector<Bone>& chain);
    static bool isDigitChain(const std::vector<Bone>& chain);
    static bool isTentacleChain(const std::vector<Bone>& chain);
    
    // Role inference from bone names and hierarchy
    static PartRole inferBoneRole(const Bone& bone, 
                                  const std::vector<Bone>& skeleton);
};
```

### 12.7 IK Animation Controller

Integrates IK solving with the character's animation state.

```cpp
class IKAnimationController {
public:
    void initialize(const IKRigBuilder::IKRig& rig);
    
    // Enable/disable systems
    void enableFootPlacement(bool enable);
    void enableLookAt(bool enable);
    void enableReach(uint32_t armIndex, bool enable);
    void enableProcedualLocomotion(bool enable);
    void enableToeConform(bool enable);
    void enableFingerConform(bool enable);
    void enableFingerGrip(bool enable);
    
    // Set targets
    void setLookTarget(const glm::vec3& worldPosition);
    void setReachTarget(uint32_t armIndex, const glm::vec3& worldPosition);
    void setLocomotionVelocity(const glm::vec3& velocity);
    
    // Digit control
    void setGripTarget(uint32_t handIndex, const FingerGripSystem::GripTarget& target);
    void setGripType(uint32_t handIndex, GripConfig::GripType type);
    void setGripStrength(uint32_t handIndex, float strength);
    void releaseGrip(uint32_t handIndex);
    
    // Update all IK (call once per frame)
    void update(float dt,
               const Transform& characterWorldTransform,
               std::vector<Transform>& inOutBoneTransforms);
    
    // Raycast callback for foot placement and digit conform
    std::function<bool(glm::vec3 origin, glm::vec3 direction, 
                       float& outDistance, glm::vec3& outNormal)> raycastCallback;
    
    // Sphere cast callback for digit surface conform
    std::function<bool(glm::vec3 origin, glm::vec3 direction, float radius,
                       glm::vec3& outPoint, glm::vec3& outNormal, 
                       uint32_t& outMeshID)> sphereCastCallback;
    
private:
    IKRigBuilder::IKRig rig;
    std::unique_ptr<IKSolver> solver;
    
    // Blending state
    std::map<uint32_t, float> chainBlendWeights;
    
    // Solve order (dependencies)
    std::vector<uint32_t> solveOrder;
    void computeSolveOrder();
};
```

### 12.8 Cross-Part IK Resolution

When IK chains span multiple parts connected by sockets:

```cpp
class CrossPartIKResolver {
public:
    // Resolve transforms across socket boundaries
    static void resolveAcrossSockets(
        const IKChain& chain,
        const std::vector<IKChain::SocketBridge>& bridges,
        const std::map<uint32_t, Socket*>& sockets,
        std::vector<Transform>& inOutLocalTransforms,
        std::vector<Transform>& outWorldTransforms);
    
    // Propagate IK result back to part-local transforms
    static void propagateToPartLocal(
        const std::vector<Transform>& worldTransforms,
        const IKChain& chain,
        const std::map<uint32_t, Part*>& parts,
        std::map<uint32_t, std::vector<Transform>>& outPartLocalTransforms);
};
```

### 12.9 IK Constraint Visualization

```cpp
class IKDebugRenderer {
public:
    // Render IK chain joints and limits
    static void renderChain(const IKChain& chain,
                           const std::vector<Transform>& transforms);
    
    // Render effector targets
    static void renderEffector(const IKEffector& effector);
    
    // Render joint rotation limits
    static void renderJointLimits(const IKJoint& joint,
                                 const Transform& jointTransform);
    
    // Render pole vector
    static void renderPoleVector(const IKEffector& effector,
                                const std::vector<Transform>& chainTransforms);
    
    // Render part role (color-coded by PartRole)
    static void renderPartRole(const IKJoint& joint,
                              const Transform& jointTransform);
    
    // Render digit contact points
    static void renderDigitContacts(const DigitState& state,
                                   const std::vector<Transform>& transforms);
    
    // Render grip state
    static void renderGripState(const FingerGripSystem& gripSystem,
                               const std::vector<Transform>& fingerTransforms);
    
    // Render surface conform influence
    static void renderSurfaceConformInfluence(const IKChain& digitChain,
                                             const DigitState& state);
};
```

---

## 13. Editor Responsibilities

The editor operates on **data**, not meshes directly.

### 13.1 Editor Functions

| Function | Description |
|----------|-------------|
| **Validate socket compatibility** | Check profiles before attachment |
| **Resolve or suggest adapters** | Find adapter chains for incompatible sockets |
| **Preview seam remesh scope** | Visualize influence radius |
| **Preview deformation impact** | Show how part will deform |
| **Prevent destructive edits** | Block edits that break socket stability |
| **Configure IK chains** | Set up IK chains from composed character |
| **Preview IK targets** | Visualize effector targets and constraints |
| **Tune procedural animation** | Adjust foot placement, look-at, locomotion |
| **Test IK interactively** | Drag effectors to test IK behavior |
| **Configure part roles** | Assign anatomical roles (head, arm, tail, wing, etc.) |
| **Configure digit systems** | Set up finger/toe surface conform and grip |
| **Test grip interactively** | Preview finger wrap around target objects |
| **Visualize part role constraints** | Show default joint limits per role |

### 13.2 Character Composition View

```cpp
class CharacterCompositionView {
public:
    void render();
    
    // Socket management
    void showAvailableSockets(const std::vector<Socket*>& sockets);
    void highlightCompatibleParts(const Socket& socket);
    void previewAttachment(Socket& socket, Part& part);
    
    // Part management
    void showPartLibrary();
    void showAttachedParts();
    void showPartProperties(Part& part);
    
    // Validation
    void showCompatibilityWarnings();
    void showAdapterSuggestions();
    
    // IK visualization
    void showIKChains(bool showConstraints = false);
    void showIKEffectors();
    void showProceduralAnimationState();
    
private:
    // Selection state
    Socket* selectedSocket = nullptr;
    Part* selectedPart = nullptr;
    IKChain* selectedIKChain = nullptr;
    IKEffector* selectedEffector = nullptr;
    
    // Preview state
    bool previewMode = false;
    AttachmentResult previewResult;
    
    // IK testing mode
    bool ikTestMode = false;
    glm::vec3 ikTestTarget;
};
```

### 13.3 Editor Commands

```cpp
class CharacterEditorCommands {
public:
    // Attachment operations
    AttachmentResult attachPart(Socket& socket, Part& part);
    void detachPart(uint32_t attachmentID);
    void swapPart(Socket& socket, Part& newPart);
    
    // Socket operations
    void createSurfaceSocket(Mesh& mesh, const SurfaceReference& ref);
    void deleteSocket(Socket& socket);
    void editSocketProfile(Socket& socket, const SocketProfile& newProfile);
    
    // Part operations
    void importPart(const std::string& filePath);
    void exportPart(const Part& part, const std::string& filePath);
    
    // Adapter operations
    void createAdapter(const Socket& input, const AttachmentSpec& output);
    void applyAdapter(Socket& socket, Part& part, Adapter& adapter);
    
    // IK operations
    void createIKChain(const std::vector<uint32_t>& boneIndices,
                      const std::vector<uint32_t>& partIDs);
    void deleteIKChain(uint32_t chainID);
    void editIKChain(IKChain& chain);
    void createIKEffector(uint32_t chainID, IKEffector::TargetType type);
    void deleteIKEffector(uint32_t effectorID);
    void setIKEffectorTarget(uint32_t effectorID, const glm::vec3& target);
    
    // Procedural animation operations
    void enableFootPlacement(bool enable);
    void configureFootPlacement(const FootPlacementConfig& config);
    void enableLookAt(bool enable);
    void configureLookAt(const LookAtConfig& config);
    void enableProceduralLocomotion(bool enable);
    void configureLocomotion(const LocomotionConfig& config);
    void autoDetectIKChains();  // From socket topology
    
    // Part role operations
    void setPartRole(uint32_t partID, PartRole role);
    void setPartRole(uint32_t partID, const std::string& customRole);
    void applyRoleDefaults(uint32_t chainID);  // Apply PartRoleConstraintLibrary defaults
    void autoDetectPartRoles();  // Infer roles from bone names/hierarchy
    
    // Digit and grip operations
    void enableToeConform(bool enable);
    void configureToeConform(const ToeConformConfig& config);
    void enableFingerConform(bool enable);
    void configureFingerConform(const DigitConfig& config);
    void enableFingerGrip(bool enable);
    void configureFingerGrip(const GripConfig& config);
    void setGripType(uint32_t handIndex, GripConfig::GripType type);
    void setGripTarget(uint32_t handIndex, const FingerGripSystem::GripTarget& target);
    void releaseGrip(uint32_t handIndex);
    void testDigitSurfaceConform(uint32_t chainID, const glm::vec3& surfacePoint);
    
    // Undo/Redo
    void undo();
    void redo();
};
```

---

## 14. Implementation Phases

### Phase 1: Foundation (Week 1-2)

- [ ] Implement extended data structures (Transform, Vertex, Bone, Mesh, Socket)
- [ ] Implement ModelLoader with Assimp backend
- [ ] Socket extraction from bones named `socket_*`
- [ ] Basic texture loading (embedded and external)
- [ ] Shape key extraction from morph targets

### Phase 2: Socket System (Week 3-4)

- [ ] Implement SocketProfile and compatibility checking
- [ ] Implement SocketProfileRegistry
- [ ] Implement AttachmentSpec validation
- [ ] Basic transform alignment for bone-space sockets
- [ ] Socket visualization in viewport

### Phase 3: Part System (Week 5-6)

- [ ] Implement Part structure and PartLibrary
- [ ] Part import/export
- [ ] AttachmentController with basic attach/detach
- [ ] Part transform updates each frame
- [ ] Part library browser UI

### Phase 4: Seam Processing (Week 7-8)

- [ ] Implement SeamRemesh specification
- [ ] Seam loop vertex matching
- [ ] Basic seam stitching (snap mode)
- [ ] Influence radius visualization
- [ ] Seam-corrective shape key generation

### Phase 5: Surface Sockets (Week 9-10)

- [ ] Implement SurfaceReference (UV, barycentric, landmark)
- [ ] Surface socket position/orientation computation
- [ ] Socket migration when topology changes
- [ ] Surface socket creation UI

### Phase 6: Adapters (Week 11-12)

- [ ] Implement Adapter structure and AdapterRegistry
- [ ] Adapter chain finding (graph search)
- [ ] Transform and deformation mapping
- [ ] Adapter creation UI
- [ ] Adapter suggestion system

### Phase 7: Deformation Coupling (Week 13-14)

- [ ] Implement DeformationField
- [ ] Field generation from skeleton
- [ ] Field sampling for parts
- [ ] Secondary motion basics

### Phase 8: IK Foundation (Week 15-16)

- [ ] Implement IKChain and IKJoint structures
- [ ] Implement IKEffector with multiple target types
- [ ] Implement CCD solver
- [ ] Implement FABRIK solver
- [ ] Basic joint constraints (rotation limits)
- [ ] IK chain visualization/debug rendering
- [ ] Implement PartRole enum and PartRoleInfo
- [ ] Implement PartRoleConstraintLibrary with defaults for all roles

### Phase 9: Procedural Animation (Week 17-18)

- [ ] Implement FootPlacementSystem with raycasting
- [ ] Implement LookAtSystem
- [ ] Implement ReachSystem
- [ ] Pelvis height adjustment for foot IK
- [ ] Cross-part IK chain resolution
- [ ] Part role auto-detection from bone names/hierarchy

### Phase 10: Procedural Locomotion (Week 19-20)

- [ ] Implement ProceduralLocomotion gait system
- [ ] Implement IKRigBuilder auto-detection with part roles
- [ ] IKAnimationController integration
- [ ] Locomotion parameter tuning UI
- [ ] Multi-leg support (quadruped, hexapod, etc.)
- [ ] Tail and wing chain support

### Phase 11: Digit Surface Conform (Week 21-22)

- [ ] Implement SurfaceConformSolver for digit IK
- [ ] Implement SurfaceConformDigitSystem
- [ ] Implement ToeGroundConformSystem
- [ ] Sphere cast integration for contact detection
- [ ] Per-joint contact tracking
- [ ] Digit conform visualization/debug rendering

### Phase 12: Finger Grip System (Week 23-24)

- [ ] Implement FingerGripSystem
- [ ] Power, precision, pinch, lateral, hook grip types
- [ ] Grip target acquisition and tracking
- [ ] Grip security scoring
- [ ] Thumb opposition logic
- [ ] Grip testing UI

### Phase 13: Polish & Integration (Week 25-26)

- [ ] Full CharacterCompositionView UI
- [ ] Part role assignment UI
- [ ] Digit system configuration UI
- [ ] Undo/redo system
- [ ] Persistence (save/load character compositions with IK and digit settings)
- [ ] Performance optimization
- [ ] Documentation and examples

---

## 15. File Format & Serialization

### 15.1 Part File Format (`.lbpart`)

```json
{
    "version": "1.0",
    "type": "part",
    "name": "Example Helmet",
    "category": "headwear",
    
    "mesh": {
        "vertices": "base64_encoded_binary",
        "indices": "base64_encoded_binary",
        "materials": [...]
    },
    
    "localRig": [...],
    
    "shapeKeys": [
        {
            "name": "visor_open",
            "type": "PartLocal",
            "deltas": "base64_encoded_binary"
        }
    ],
    
    "socketsOut": [
        {
            "name": "visor_hinge",
            "profile": "accessory_small_v1",
            "transform": [...]
        }
    ],
    
    "attachmentSpecs": [
        {
            "requiredSocketProfile": "humanoid_head_v1",
            "supportedSpaceTypes": ["Bone"],
            "seamRequirements": {...}
        }
    ],
    
    "ikHints": {
        "chainRole": "head",
        "partRole": "Head",
        "suggestedJointLimits": [
            {"boneIndex": 0, "rotationMin": [-30, -45, -20], "rotationMax": [30, 45, 20]}
        ],
        "boneRoles": [
            {"boneIndex": 0, "role": "Neck"},
            {"boneIndex": 1, "role": "Head"}
        ]
    },
    
    "digitInfo": {
        "isDigitPart": false,
        "digitType": null,
        "surfaceConformEnabled": false
    }
}
```

### 15.2 Character Composition File (`.lbchar`)

```json
{
    "version": "1.0",
    "type": "character",
    "name": "My Character",
    
    "basePart": "human_body_v1",
    
    "attachments": [
        {
            "socketProfile": "humanoid_head_v1",
            "partName": "helmet_knight_v1",
            "transform": [...],
            "adapterName": null
        },
        {
            "socketProfile": "humanoid_hand_left_v1",
            "partName": "sword_longsword_v1",
            "transform": [...],
            "adapterName": "hand_to_grip_adapter"
        }
    ],
    
    "shapeKeyWeights": {
        "body_muscular": 0.7,
        "face_smile": 0.3
    },
    
    "ikRig": {
        "chains": [
            {
                "name": "left_arm",
                "primaryRole": "UpperArm",
                "joints": [
                    {"boneIndex": 5, "partID": 1, "partRole": "Shoulder", "jointType": "Ball", "rotationMin": [-90, -90, -90], "rotationMax": [90, 45, 90]},
                    {"boneIndex": 6, "partID": 1, "partRole": "UpperArm", "jointType": "Ball", "rotationMin": [-90, -90, -90], "rotationMax": [90, 45, 90]},
                    {"boneIndex": 7, "partID": 1, "partRole": "LowerArm", "jointType": "Hinge", "rotationMin": [0, 0, 0], "rotationMax": [145, 0, 0]},
                    {"boneIndex": 8, "partID": 1, "partRole": "Hand", "jointType": "Ball"}
                ],
                "solverType": "FABRIK",
                "maxIterations": 10
            },
            {
                "name": "left_index_finger",
                "primaryRole": "Finger",
                "isDigitChain": true,
                "allowsSurfaceConform": true,
                "joints": [
                    {"boneIndex": 9, "partID": 1, "partRole": "Finger", "jointType": "Hinge", "enableSurfaceConform": true},
                    {"boneIndex": 10, "partID": 1, "partRole": "Finger", "jointType": "Hinge", "enableSurfaceConform": true},
                    {"boneIndex": 11, "partID": 1, "partRole": "Finger", "jointType": "Hinge", "enableSurfaceConform": true}
                ],
                "solverType": "SurfaceConform",
                "maxIterations": 5
            }
        ],
        "effectors": [
            {
                "name": "left_hand_effector",
                "chainID": 0,
                "targetType": "WorldPosition",
                "weight": 1.0
            }
        ],
        "partRoleMappings": {
            "bone_5": "Shoulder",
            "bone_6": "UpperArm",
            "bone_7": "LowerArm",
            "bone_8": "Hand",
            "bone_9": "Finger",
            "bone_10": "Finger",
            "bone_11": "Finger",
            "bone_12": "Thumb"
        }
    },
    
    "proceduralAnimation": {
        "footPlacement": {
            "enabled": true,
            "raycastHeight": 0.5,
            "raycastDepth": 1.0,
            "footOffset": 0.02,
            "alignToSurface": true
        },
        "lookAt": {
            "enabled": false,
            "maxHeadYaw": 70,
            "maxHeadPitch": 40,
            "maxSpineContribution": 0.3
        },
        "locomotion": {
            "enabled": false,
            "strideLength": 0.8,
            "stepHeight": 0.15,
            "stepDuration": 0.4
        },
        "toeConform": {
            "enabled": true,
            "groundConformStrength": 0.8,
            "slopeAdaptation": 0.5,
            "pushOffCurl": 0.3,
            "liftPhaseStart": 0.1,
            "plantPhaseStart": 0.6
        },
        "fingerConform": {
            "enabled": true,
            "contactRadius": 0.01,
            "contactThreshold": 0.02,
            "conformStrength": 0.85,
            "spreadAngle": 15.0,
            "curlFalloff": 0.3
        },
        "fingerGrip": {
            "enabled": false,
            "defaultGripType": "Power",
            "gripStrength": 0.9,
            "thumbOpposition": 0.7,
            "fingerSpread": 10.0
        }
    }
}
```

---

## 16. Explicit Non-Goals

These are **not supported** by design:

| Non-Goal | Reason |
|----------|--------|
| Shared skeleton | Violates part independence |
| Bone-name matching | Requires implicit conventions |
| Vertex-index-based shape sharing | Fragile, breaks with topology changes |
| Global mesh rebuilds | Destroys socket stability |
| Automatic rig merging | Unpredictable results |
| Implicit retargeting | Should be explicit via adapters |

---

## Appendix A: Socket Profile Examples

```cpp
// Standard humanoid profiles
"humanoid_head_v1"
"humanoid_neck_v1"
"humanoid_shoulder_left_v1"
"humanoid_shoulder_right_v1"
"humanoid_hand_left_v1"
"humanoid_hand_right_v1"
"humanoid_spine_upper_v1"
"humanoid_spine_lower_v1"
"humanoid_hip_left_v1"
"humanoid_hip_right_v1"
"humanoid_foot_left_v1"
"humanoid_foot_right_v1"

// Accessory profiles
"accessory_small_v1"      // Buttons, badges
"accessory_medium_v1"     // Belt items
"accessory_large_v1"      // Backpacks

// Weapon profiles
"grip_sword_v1"
"grip_staff_v1"
"grip_pistol_v1"
"grip_rifle_v1"

// Generic attachment
"generic_bone_v1"
"generic_surface_v1"
```

---

## Appendix B: Example Socket Bone Naming

In your 3D modeling software, name bones to be auto-extracted as sockets:

```
socket_humanoid_head_v1           → Profile: humanoid_head_v1
socket_humanoid_hand_left_v1      → Profile: humanoid_hand_left_v1
socket_accessory_small_belt       → Profile: accessory_small_v1, Name: belt
socket_grip_sword_main            → Profile: grip_sword_v1, Name: main
```

Pattern: `socket_<profile>[_<optional_name>]`

---

## Appendix C: Adapter Example

Adapter to connect a "grip_sword_v1" socket to a "humanoid_hand_left_v1" socket:

```json
{
    "name": "hand_to_sword_grip",
    "inputSocketProfile": "humanoid_hand_left_v1",
    "outputSocketProfile": "grip_sword_v1",
    "transformMapping": {
        "translation": [0.05, 0.0, 0.0],
        "rotation": [0, 0, -0.707, 0.707]
    },
    "deformationMapping": {},
    "seamStrategy": null
}
```
