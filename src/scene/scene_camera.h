#pragma once
#include <glm/glm.hpp>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Scene cameras: named cameras with game metadata, stored in the .scene file.
// The game runtime reads them (id is the stable key) and switches between
// them at runtime. The editor can preview any camera and jump its orbit
// camera to a camera's pose.
// ---------------------------------------------------------------------------

struct SceneCamera {
    int id = -1;                        // stable key, unique within the scene
    std::string name = "Camera";
    std::string tag;                    // free-form game metadata
    glm::vec3 position = glm::vec3(0.0f, 10.0f, -10.0f);
    glm::vec3 target   = glm::vec3(0.0f);
    float fov       = 60.0f;            // vertical field of view, degrees
    float nearPlane = 0.1f;
    float farPlane  = 500.0f;

    // Degenerate poses (position == target, straight up/down) are clamped to
    // something renderable instead of producing NaN matrices.
    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspect) const;
};

class CameraRig {
public:
    int addCamera(const SceneCamera& cam);         // assigns a fresh id
    void addCameraWithId(const SceneCamera& cam);  // undo / scene load
    bool removeCamera(int id);
    SceneCamera* findCamera(int id);
    const SceneCamera* findCamera(int id) const;
    const std::vector<SceneCamera>& cameras() const { return cameras_; }
    void clear();

    // The game's initial camera (-1 = none chosen).
    int activeId() const { return activeId_; }
    void setActive(int id);
    const SceneCamera* active() const { return findCamera(activeId_); }

private:
    std::vector<SceneCamera> cameras_;
    int nextId_ = 0;
    int activeId_ = -1;
};
