#include "scene_camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>

glm::mat4 SceneCamera::viewMatrix() const {
    glm::vec3 fwd = target - position;
    if (glm::dot(fwd, fwd) < 1e-8f) fwd = glm::vec3(0.0f, 0.0f, -1.0f);
    glm::vec3 f = glm::normalize(fwd);
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(f, up)) > 0.999f) up = glm::vec3(1.0f, 0.0f, 0.0f);
    return glm::lookAt(position, position + f, up);
}

glm::mat4 SceneCamera::projectionMatrix(float aspect) const {
    float f = std::clamp(fov, 1.0f, 179.0f);
    float np = std::max(nearPlane, 1e-4f);
    float fp = std::max(farPlane, np * 2.0f);
    return glm::perspective(glm::radians(f), std::max(aspect, 1e-4f), np, fp);
}

int CameraRig::addCamera(const SceneCamera& cam) {
    SceneCamera c = cam;
    c.id = nextId_++;
    cameras_.push_back(c);
    return c.id;
}

void CameraRig::addCameraWithId(const SceneCamera& cam) {
    cameras_.push_back(cam);
    nextId_ = std::max(nextId_, cam.id + 1);
}

bool CameraRig::removeCamera(int id) {
    auto it = std::find_if(cameras_.begin(), cameras_.end(),
                           [id](const SceneCamera& c) { return c.id == id; });
    if (it == cameras_.end()) return false;
    cameras_.erase(it);
    if (activeId_ == id) activeId_ = -1;
    return true;
}

SceneCamera* CameraRig::findCamera(int id) {
    for (auto& c : cameras_)
        if (c.id == id) return &c;
    return nullptr;
}

const SceneCamera* CameraRig::findCamera(int id) const {
    for (const auto& c : cameras_)
        if (c.id == id) return &c;
    return nullptr;
}

void CameraRig::clear() {
    cameras_.clear();
    nextId_ = 0;
    activeId_ = -1;
}

void CameraRig::setActive(int id) {
    activeId_ = findCamera(id) ? id : -1;
}
