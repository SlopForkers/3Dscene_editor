#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <string>
#include <unordered_map>
#include <vector>

// A glTF 2.0 model (also covers VRM, which is glTF + extensions). Loaded as a
// collection of meshes/materials/textures/skins. Skinned meshes render in the
// bind (rest) pose. Morph targets are parsed but applied at weight 0.
class Model {
public:
    Model() = default;
    ~Model();

    Model(const Model&) = delete;
    Model& operator=(const Model&) = delete;

    bool loadFromFile(const std::string& path);
    void destroy();

    // Render the whole model. The caller's shader must already be in use and
    // have uViewProj, uModel, uLightDir, uCamPos, uJointMatrices[256] etc. set
    // (except per-primitive material uniforms, which this sets per primitive).
    void render(const class Shader& shader) const;

    // Render the model with hardware instancing. instanceVbo holds an array of
    // mat4 world matrices (one per instance). Uses glDrawElementsInstanced.
    void renderInstanced(const class Shader& shader, GLuint instanceVbo,
                         int instanceCount) const;

    // Axis-aligned bounding box in model space (min/max corners).
    glm::vec3 aabbMin() const { return aabbMin_; }
    glm::vec3 aabbMax() const { return aabbMax_; }
    glm::vec3 aabbCenter() const { return (aabbMin_ + aabbMax_) * 0.5f; }
    glm::vec3 aabbSize() const { return aabbMax_ - aabbMin_; }

    bool valid() const { return loaded_; }
    const std::string& name() const { return name_; }
    const std::string& sourcePath() const { return sourcePath_; }
    int primitiveCount() const { return (int)primitives_.size(); }
    int skinCount() const { return (int)skins_.size(); }

private:
    struct Primitive {
        GLuint vao = 0;
        GLuint vboPos = 0, vboNorm = 0, vboTan = 0, vboUv = 0;
        GLuint vboJoints = 0, vboWeights = 0;
        GLuint ebo = 0;
        int    indexCount = 0;
        GLenum indexType = GL_UNSIGNED_INT;   // EBO is always uploaded 32-bit
        int    materialIndex = -1;     // index into materials_
        bool   hasJoints = false;
        bool   hasTangent = false;
        bool   hasUv = false;
        // Local-space bounds (before any node transform).
        glm::vec3 aabbMin = glm::vec3(0.0f);
        glm::vec3 aabbMax = glm::vec3(0.0f);
    };

    struct Material {
        GLuint baseColorTex = 0;        // 0 if absent
        GLuint metalRoughTex = 0;
        GLuint normalTex = 0;
        GLuint emissiveTex = 0;
        GLuint occlusionTex = 0;
        glm::vec4 baseColorFactor = glm::vec4(1.0f);
        glm::vec3 emissiveFactor = glm::vec3(0.0f);
        float metallic = 0.0f;
        float roughness = 1.0f;
        float normalScale = 1.0f;
        float occlusionStrength = 1.0f;
        float alphaCutoff = 0.5f;
        int  alphaMode = 0;            // 0 opaque, 1 mask, 2 blend
        bool unlit = false;
        bool doubleSided = false;
        bool hasBaseColorTex = false;
        bool hasMetalRoughTex = false;
        bool hasNormalTex = false;
        bool hasEmissiveTex = false;
        bool hasOcclusionTex = false;
    };

    struct Skin {
        std::vector<glm::mat4> inverseBind;
        std::vector<int>       jointNodeIndex;   // index into nodes_
        std::vector<glm::mat4> jointMatrices;    // computed at load (bind pose)
    };

    struct Node {
        glm::mat4 local = glm::mat4(1.0f);
        glm::mat4 global = glm::mat4(1.0f);
        int  meshIndex = -1;       // index into meshes_ (mapped)
        int  skinIndex = -1;
        std::vector<int> children;
        std::string name;
    };

    struct Mesh {
        std::vector<int> primitiveIndices;
    };

    std::vector<Primitive> primitives_;
    std::vector<Material>  materials_;
    std::vector<Skin>      skins_;
    std::vector<Node>      nodes_;
    std::vector<Mesh>      meshes_;
    std::vector<int>       rootNodes_;
    std::vector<GLuint>    textures_;     // GL texture ids for cleanup
    // Per-load de-dup cache: cgltf_image* -> GL texture. Cleared on load.
    std::unordered_map<const void*, GLuint> texCache_;

    glm::vec3 aabbMin_ = glm::vec3(0.0f);
    glm::vec3 aabbMax_ = glm::vec3(0.0f);
    bool hasAabb_ = false;

    bool loaded_ = false;
    std::string name_;
    std::string sourcePath_;

    // Internal helpers.
    GLuint loadTextureFromImage(struct cgltf_image* image, const std::string& baseDir);
    void   computeNodeGlobals();
    void   computeSkinMatrices();
    void   applyMaterial(const Material& m, const class Shader& shader,
                         const glm::vec3& lightDir, const glm::vec3& camPos) const;
    void   applyDefaultMaterial(const class Shader& shader) const;
    // Draw one primitive in the context of a scene node (supplies the global
    // transform and the skin). Called once per (node, mesh) pair so meshes
    // instanced by multiple nodes render at every placement. When
    // instanceCount > 0 the primitive is drawn instanced using the mat4
    // array in instanceVbo (attribute locations 6..9, divisor 1).
    void   renderPrimitive(const class Shader& shader, const Primitive& P,
                           const Node& node, GLuint instanceVbo = 0,
                           int instanceCount = 0) const;
};
