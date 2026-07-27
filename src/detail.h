#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

class Model;
class Shader;
class Terrain;

// Paintable instanced details (grass, rocks, trees, ...). The user loads
// "prototypes" (any glTF model) into a palette and paints instances onto the
// terrain with the Vegetation brush. Instances follow terrain height changes
// automatically via reproject().
class DetailSystem {
public:
    struct Prototype {
        std::shared_ptr<Model> model;
        std::string name;
        float targetSize = 2.0f;   // world-space size of the largest AABB dim
        float minScale   = 0.8f;   // random scale jitter range
        float maxScale   = 1.2f;
        float randomYaw  = 1.0f;   // 0 = aligned, 1 = full random yaw
    };

    struct Instance {
        glm::vec3 position;
        float     yaw;
        float     scale;
        int       prototypeIndex;
    };

    DetailSystem() = default;
    ~DetailSystem();

    void create();
    void destroy();

    // Prototype palette management.
    int  addPrototype(std::shared_ptr<Model> model, const std::string& name,
                      float targetSize = 2.0f);
    void removePrototype(int index);
    void clearPrototypes();
    int  prototypeCount() const { return (int)prototypes_.size(); }
    const Prototype& prototype(int i) const { return prototypes_[i]; }
    Prototype* prototypeMutable(int i) { return i >= 0 && i < (int)prototypes_.size() ? &prototypes_[i] : nullptr; }
    int  activePrototype() const { return activePrototype_; }
    void setActivePrototype(int i) { activePrototype_ = i; }

    // Painting. density ~ instances per stroke step (driven by brush strength).
    void paint(const Terrain& terrain, const glm::vec3& worldPos,
               float radius, float density, bool erase);
    void clearInstances();
    int  instanceCount() const { return (int)instances_.size(); }

    // Snap all instances to the current terrain height. Call after terrain
    // edits so details follow the heightfield. Optionally restrict to a
    // circular area for efficiency.
    void reproject(const Terrain& terrain, const glm::vec3& center,
                   float radius);

    // Render all instances grouped by prototype (instanced draw calls).
    void render(const Shader& shader, const glm::mat4& viewProj,
                const glm::vec3& lightDir, const glm::vec3& camPos) const;

private:
    std::vector<Prototype> prototypes_;
    std::vector<Instance>  instances_;
    int  activePrototype_ = -1;

    mutable GLuint instanceVbo_ = 0;
    mutable std::vector<glm::mat4> instanceMatrices_;

    glm::mat4 instanceMatrix(const Instance& inst, const Prototype& proto) const;
    void      uploadInstances(const std::vector<glm::mat4>& mats) const;
};
