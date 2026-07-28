#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <memory>
#include <string>
#include <vector>

class Model;
class Shader;
class Camera;

// A placed instance of a Model in the scene. Props are NOT part of the
// terrain; they sit on top of it with their own transform.
struct Prop {
    std::shared_ptr<Model> model;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 rotationEuler = glm::vec3(0.0f);   // radians
    glm::vec3 scale = glm::vec3(1.0f);
    std::string displayName;
    int id = 0;

    glm::mat4 worldMatrix() const;
    // World-space AABB (8 corners) for ray picking.
    void worldAabb(glm::vec3& outMin, glm::vec3& outMax) const;
};

class PropManager {
public:
    // Place a new prop centred at worldPos, snapped to terrain height h.
    // The model is auto-scaled so its largest AABB dimension ~= targetSize.
    int addProp(std::shared_ptr<Model> model, const glm::vec3& worldPos,
                 float terrainHeight, float targetSize = 6.0f,
                 const std::string& name = "");

    // Re-insert a prop with its ORIGINAL id (undo/redo restore). Keeps the
    // id counter ahead of the restored id so future adds never collide.
    int addPropWithId(const Prop& p);

    void removeProp(int id);
    void clear();
    void render(const Shader& shader, const glm::mat4& viewProj,
                const glm::vec3& lightDir, const glm::vec3& camPos) const;

    int  count() const { return (int)props_.size(); }
    const std::vector<Prop>& props() const { return props_; }
    Prop* findProp(int id);
    int  selectedId() const { return selectedId_; }
    void select(int id) { selectedId_ = id; }
    Prop* selected();

    // Ray-pick against props; returns closest hit id or -1.
    int pick(const glm::vec3& rayOrigin, const glm::vec3& rayDir) const;

private:
    std::vector<Prop> props_;
    int nextId_ = 1;
    int selectedId_ = -1;
};
