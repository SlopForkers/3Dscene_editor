#include "prop.h"
#include "model.h"
#include "shader.h"
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_inverse.hpp>
#include <iostream>

glm::mat4 Prop::worldMatrix() const {
    glm::mat4 m(1.0f);
    m = glm::translate(m, position);
    m = glm::rotate(m, rotationEuler.y, glm::vec3(0, 1, 0));
    m = glm::rotate(m, rotationEuler.x, glm::vec3(1, 0, 0));
    m = glm::rotate(m, rotationEuler.z, glm::vec3(0, 0, 1));
    m = glm::scale(m, scale);
    return m;
}

void Prop::worldAabb(glm::vec3& outMin, glm::vec3& outMax) const {
    if (!model || !model->valid()) { outMin = outMax = position; return; }
    glm::vec3 mn = model->aabbMin();
    glm::vec3 mx = model->aabbMax();
    glm::mat4 w = worldMatrix();
    // Transform all 8 corners and take the bounds.
    glm::vec3 corners[8] = {
        {mn.x, mn.y, mn.z}, {mx.x, mn.y, mn.z},
        {mn.x, mx.y, mn.z}, {mx.x, mx.y, mn.z},
        {mn.x, mn.y, mx.z}, {mx.x, mn.y, mx.z},
        {mn.x, mx.y, mx.z}, {mx.x, mx.y, mx.z},
    };
    outMin = glm::vec3(1e30f); outMax = glm::vec3(-1e30f);
    for (auto& c : corners) {
        glm::vec4 wc = w * glm::vec4(c, 1.0f);
        outMin = glm::min(outMin, glm::vec3(wc));
        outMax = glm::max(outMax, glm::vec3(wc));
    }
}

int PropManager::addProp(std::shared_ptr<Model> model, const glm::vec3& worldPos,
                          float terrainHeight, float targetSize, const std::string& name) {
    if (!model || !model->valid()) return -1;
    Prop p;
    p.model = model;
    p.position = glm::vec3(worldPos.x, terrainHeight, worldPos.z);
    p.id = nextId_++;
    p.displayName = name.empty() ? "Prop " + std::to_string(p.id) : name;

    // Auto-scale so the largest AABB dimension matches targetSize.
    // targetSize <= 0 means "keep native scale" (used by the scene loader,
    // which restores the saved scale right after).
    glm::vec3 sz = model->aabbSize();
    float maxDim = std::max({sz.x, sz.y, sz.z});
    if (maxDim > 1e-4f && targetSize > 0.0f) {
        p.scale = glm::vec3(targetSize / maxDim);
    }

    // Sit the model so its bottom rests on the terrain (bottom of AABB at y=0
    // in model space -> raise by -aabbMin.y * scaleY).
    float bottom = model->aabbMin().y;
    p.position.y = terrainHeight - bottom * p.scale.y;

    props_.push_back(p);
    selectedId_ = p.id;
    return p.id;
}

void PropManager::removeProp(int id) {
    props_.erase(std::remove_if(props_.begin(), props_.end(),
        [id](const Prop& p) { return p.id == id; }), props_.end());
    if (selectedId_ == id) selectedId_ = props_.empty() ? -1 : props_.back().id;
}

void PropManager::clear() {
    props_.clear();
    selectedId_ = -1;
}

Prop* PropManager::findProp(int id) {
    for (auto& p : props_) if (p.id == id) return &p;
    return nullptr;
}

Prop* PropManager::selected() { return findProp(selectedId_); }

void PropManager::render(const Shader& shader, const glm::mat4& viewProj,
                          const glm::vec3& lightDir, const glm::vec3& camPos) const {
    shader.setMat4("uViewProj", viewProj);
    shader.setVec3("uLightDir", lightDir);
    shader.setVec3("uCamPos", camPos);
    for (const auto& p : props_) {
        if (!p.model || !p.model->valid()) continue;
        shader.setMat4("uInstance", p.worldMatrix());
        p.model->render(shader);
    }
}

// Ray-AABB intersection (slab method). Returns t >= 0 if hit, else -1.
static float rayAabb(const glm::vec3& o, const glm::vec3& d,
                      const glm::vec3& mn, const glm::vec3& mx) {
    float tmin = -1e30f, tmax = 1e30f;
    for (int i = 0; i < 3; ++i) {
        if (fabs(d[i]) < 1e-8f) {
            if (o[i] < mn[i] || o[i] > mx[i]) return -1.0f;
        } else {
            float inv = 1.0f / d[i];
            float t1 = (mn[i] - o[i]) * inv;
            float t2 = (mx[i] - o[i]) * inv;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return -1.0f;
        }
    }
    return tmin >= 0.0f ? tmin : (tmax >= 0.0f ? tmax : -1.0f);
}

int PropManager::pick(const glm::vec3& rayOrigin, const glm::vec3& rayDir) const {
    glm::vec3 d = glm::normalize(rayDir);
    float bestT = 1e30f;
    int best = -1;
    for (const auto& p : props_) {
        if (!p.model || !p.model->valid()) continue;
        glm::vec3 mn, mx;
        p.worldAabb(mn, mx);
        float t = rayAabb(rayOrigin, d, mn, mx);
        if (t >= 0.0f && t < bestT) { bestT = t; best = p.id; }
    }
    return best;
}
