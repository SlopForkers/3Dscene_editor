#include "detail.h"
#include "model.h"
#include "shader.h"
#include "terrain.h"
#include <algorithm>
#include <cmath>
#include <random>
#include <glm/gtc/matrix_transform.hpp>

namespace {
// Deterministic per-call RNG so repeated strokes don't shimmer.
std::mt19937& rng() {
    static std::mt19937 g(0xC0FFEEu);
    return g;
}
float rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng()); }
float randRange(float lo, float hi) {
    return std::uniform_real_distribution<float>(lo, hi)(rng());
}
} // namespace

DetailSystem::~DetailSystem() { destroy(); }

void DetailSystem::create() {
    glGenBuffers(1, &instanceVbo_);
}

void DetailSystem::destroy() {
    if (instanceVbo_) { glDeleteBuffers(1, &instanceVbo_); instanceVbo_ = 0; }
    instances_.clear();
    prototypes_.clear();
}

int DetailSystem::addPrototype(std::shared_ptr<Model> model,
                               const std::string& name, float targetSize,
                               const std::string& sourcePath) {
    if (!model || !model->valid()) return -1;
    Prototype p;
    p.model = model;
    p.name = name.empty() ? ("Detail " + std::to_string(prototypes_.size() + 1)) : name;
    p.targetSize = targetSize;
    p.sourcePath = sourcePath;
    prototypes_.push_back(p);
    if (activePrototype_ < 0) activePrototype_ = (int)prototypes_.size() - 1;
    return (int)prototypes_.size() - 1;
}

void DetailSystem::removePrototype(int index) {
    if (index < 0 || index >= (int)prototypes_.size()) return;
    int last = (int)prototypes_.size() - 1;
    // Remove instances that use the prototype being deleted.
    instances_.erase(std::remove_if(instances_.begin(), instances_.end(),
        [index](const Instance& i) { return i.prototypeIndex == index; }),
        instances_.end());
    // Re-point instances that used the last slot to the freed slot.
    if (index != last) {
        for (auto& i : instances_)
            if (i.prototypeIndex == last) i.prototypeIndex = index;
    }
    prototypes_.erase(prototypes_.begin() + index);
    if (activePrototype_ == index || activePrototype_ == last) {
        activePrototype_ = prototypes_.empty() ? -1 :
                           std::min(index, (int)prototypes_.size() - 1);
    } else if (activePrototype_ > index) {
        --activePrototype_;
    }
}

void DetailSystem::clearPrototypes() {
    prototypes_.clear();
    instances_.clear();
    activePrototype_ = -1;
}

void DetailSystem::paint(const Terrain& terrain, const glm::vec3& worldPos,
                         float radius, float density, bool erase) {
    if (erase) {
        // Remove instances whose XZ distance to worldPos < radius.
        float r2 = radius * radius;
        instances_.erase(std::remove_if(instances_.begin(), instances_.end(),
            [&](const Instance& i) {
                float dx = i.position.x - worldPos.x;
                float dz = i.position.z - worldPos.z;
                return dx * dx + dz * dz < r2;
            }), instances_.end());
        return;
    }

    if (activePrototype_ < 0 || activePrototype_ >= (int)prototypes_.size()) return;
    const Prototype& proto = prototypes_[activePrototype_];
    if (!proto.model || !proto.model->valid()) return;

    // density (instances per step) ~ brush strength. Scale so a strength of
    // 1.0 yields ~8 instances per stroke step.
    int count = std::max(1, (int)std::round(density * 8.0f));
    float bottom = proto.model->aabbMin().y;
    glm::vec3 sz = proto.model->aabbSize();
    float maxDim = std::max({sz.x, sz.y, sz.z});
    if (maxDim < 1e-4f) maxDim = 1.0f;
    float baseScale = proto.targetSize / maxDim;

    for (int n = 0; n < count; ++n) {
        // Uniform distribution within the disc.
        float r = radius * std::sqrt(rand01());
        float a = rand01() * 2.0f * 3.14159265f;
        float wx = worldPos.x + std::cos(a) * r;
        float wz = worldPos.z + std::sin(a) * r;
        if (wx < -terrain.worldSize() * 0.5f || wx > terrain.worldSize() * 0.5f ||
            wz < -terrain.worldSize() * 0.5f || wz > terrain.worldSize() * 0.5f)
            continue;
        float h = terrain.heightAtWorld(wx, wz);

        Instance inst;
        inst.position = glm::vec3(wx, h - bottom * baseScale, wz);
        inst.yaw = randRange(0.0f, 2.0f * 3.14159265f) * proto.randomYaw;
        inst.scale = baseScale * randRange(proto.minScale, proto.maxScale);
        inst.prototypeIndex = activePrototype_;
        instances_.push_back(inst);
    }
}

void DetailSystem::clearInstances() { instances_.clear(); }

void DetailSystem::reproject(const Terrain& terrain, const glm::vec3& center,
                             float radius) {
    bool all = (radius <= 0.0f);
    float r2 = radius * radius;
    for (auto& i : instances_) {
        if (!all) {
            float dx = i.position.x - center.x;
            float dz = i.position.z - center.z;
            if (dx * dx + dz * dz > r2) continue;
        }
        int pi = i.prototypeIndex;
        if (pi < 0 || pi >= (int)prototypes_.size()) continue;
        const Prototype& proto = prototypes_[pi];
        if (!proto.model || !proto.model->valid()) continue;
        float bottom = proto.model->aabbMin().y;
        float h = terrain.heightAtWorld(i.position.x, i.position.z);
        i.position.y = h - bottom * i.scale;
    }
}

glm::mat4 DetailSystem::instanceMatrix(const Instance& inst,
                                       const Prototype& proto) const {
    glm::mat4 m(1.0f);
    m = glm::translate(m, inst.position);
    m = glm::rotate(m, inst.yaw, glm::vec3(0, 1, 0));
    m = glm::scale(m, glm::vec3(inst.scale));
    return m;
}

void DetailSystem::uploadInstances(const std::vector<glm::mat4>& mats) const {
    glBindBuffer(GL_ARRAY_BUFFER, instanceVbo_);
    glBufferData(GL_ARRAY_BUFFER, mats.size() * sizeof(glm::mat4),
                 mats.data(), GL_DYNAMIC_DRAW);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
}

void DetailSystem::render(const Shader& shader, const glm::mat4& viewProj,
                          const glm::vec3& lightDir,
                          const glm::vec3& camPos) const {
    if (instances_.empty() || prototypes_.empty() || !instanceVbo_) return;
    shader.use();
    shader.setMat4("uViewProj", viewProj);
    shader.setVec3("uLightDir", lightDir);
    shader.setVec3("uCamPos", camPos);

    // Group instances by prototype and issue one instanced draw per group.
    for (int pi = 0; pi < (int)prototypes_.size(); ++pi) {
        const Prototype& proto = prototypes_[pi];
        if (!proto.model || !proto.model->valid()) continue;
        instanceMatrices_.clear();
        for (const auto& i : instances_)
            if (i.prototypeIndex == pi)
                instanceMatrices_.push_back(instanceMatrix(i, proto));
        if (instanceMatrices_.empty()) continue;
        uploadInstances(instanceMatrices_);
        proto.model->renderInstanced(shader, instanceVbo_,
                                     (int)instanceMatrices_.size());
    }
    // Restore default GL state tweaked by materials.
    glDisable(GL_BLEND);
    glEnable(GL_CULL_FACE);
}
