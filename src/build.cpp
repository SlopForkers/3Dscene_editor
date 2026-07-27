#include "build.h"
#include "shader.h"
#include "terrain.h"
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>

BuildSystem::~BuildSystem() { destroy(); }

// --- Cube mesh (24 verts, 6 faces, normals outward) ---
static const float CUBE_POS[24][3] = {
    // +X
    { 0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f},
    // -X
    {-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f,-0.5f},
    // +Y (top)
    {-0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f},
    // -Y (bottom)
    {-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f},
    // +Z
    {-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f},
    // -Z
    { 0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f},
};
static const float CUBE_NRM[6][3] = {
    { 1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0,-1, 0}, {0, 0, 1}, {0, 0,-1},
};
static const unsigned int CUBE_IDX[36] = {
     0, 1, 2,  0, 2, 3,    4, 5, 6,  4, 6, 7,
     8, 9,10,  8,10,11,   12,13,14, 12,14,15,
    16,17,18, 16,18,19,   20,21,22, 20,22,23,
};

void BuildSystem::initCubeMesh() {
    // Interleaved: pos(3) + normal(3) per vertex.
    float verts[24 * 6];
    for (int f = 0; f < 6; ++f) {
        for (int v = 0; v < 4; ++v) {
            int i = (f * 4 + v) * 6;
            verts[i + 0] = CUBE_POS[f * 4 + v][0];
            verts[i + 1] = CUBE_POS[f * 4 + v][1];
            verts[i + 2] = CUBE_POS[f * 4 + v][2];
            verts[i + 3] = CUBE_NRM[f][0];
            verts[i + 4] = CUBE_NRM[f][1];
            verts[i + 5] = CUBE_NRM[f][2];
        }
    }
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ibo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(CUBE_IDX), CUBE_IDX, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
    indexCount_ = 36;
}

// 24 edge vertices for the unit cube [-0.5, 0.5]^3.
static const float CUBE_EDGES[24][3] = {
    {-0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f,-0.5f},
    { 0.5f,-0.5f,-0.5f}, { 0.5f, 0.5f,-0.5f},
    { 0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f,-0.5f},
    {-0.5f, 0.5f,-0.5f}, {-0.5f,-0.5f,-0.5f},
    {-0.5f,-0.5f, 0.5f}, { 0.5f,-0.5f, 0.5f},
    { 0.5f,-0.5f, 0.5f}, { 0.5f, 0.5f, 0.5f},
    { 0.5f, 0.5f, 0.5f}, {-0.5f, 0.5f, 0.5f},
    {-0.5f, 0.5f, 0.5f}, {-0.5f,-0.5f, 0.5f},
    {-0.5f,-0.5f,-0.5f}, {-0.5f,-0.5f, 0.5f},
    { 0.5f,-0.5f,-0.5f}, { 0.5f,-0.5f, 0.5f},
    { 0.5f, 0.5f,-0.5f}, { 0.5f, 0.5f, 0.5f},
    {-0.5f, 0.5f,-0.5f}, {-0.5f, 0.5f, 0.5f},
};

void BuildSystem::initWireCube() {
    glGenVertexArrays(1, &wireVao_);
    glGenBuffers(1, &wireVbo_);
    glBindVertexArray(wireVao_);
    glBindBuffer(GL_ARRAY_BUFFER, wireVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(CUBE_EDGES), CUBE_EDGES, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void BuildSystem::create() {
    initCubeMesh();
    initWireCube();
}

void BuildSystem::destroy() {
    if (vao_)     { glDeleteVertexArrays(1, &vao_);     vao_ = 0; }
    if (vbo_)     { glDeleteBuffers(1, &vbo_);          vbo_ = 0; }
    if (ibo_)     { glDeleteBuffers(1, &ibo_);          ibo_ = 0; }
    if (wireVao_) { glDeleteVertexArrays(1, &wireVao_); wireVao_ = 0; }
    if (wireVbo_) { glDeleteBuffers(1, &wireVbo_);      wireVbo_ = 0; }
    blocks_.clear();
}

float BuildSystem::snapToGrid(float v) const {
    return std::round(v / gridStep_) * gridStep_;
}

bool BuildSystem::computePlacement(const Terrain& terrain,
                                    const glm::vec3& rayOrigin,
                                    const glm::vec3& rayDir,
                                    glm::vec3& outCenter,
                                    glm::vec3& outSize,
                                    BlockType& outType) const {
    // Try to hit an existing block first — placement snaps to the hit face.
    glm::vec3 hitPos, hitNormal;
    int id = pick(rayOrigin, rayDir, hitPos, hitNormal);
    if (id >= 0) {
        const Block* b = findBlock(id);
        if (b) {
            outSize = glm::vec3(blockW_, blockH_, blockW_);
            glm::vec3 center = b->position + hitNormal * (b->size * 0.5f + outSize * 0.5f);
            center.x = snapToGrid(center.x);
            center.z = snapToGrid(center.z);
            if (hitNormal.y > 0.5f) {
                // Top face: wall sits on top of the foundation.
                center.y = b->max().y + blockH_ * 0.5f;
                outType = Wall;
            } else if (hitNormal.y < -0.5f) {
                // Bottom face: stack below.
                center.y = b->min().y - blockH_ * 0.5f;
                outType = Wall;
            } else {
                // Side face: extend the foundation at the same level.
                center.y = b->position.y;
                outType = Foundation;
            }
            outCenter = center;
            return true;
        }
    }

    // No block hit: raycast the terrain and produce a foundation block.
    glm::vec3 tHit;
    if (terrain.raycast(rayOrigin, rayDir, tHit)) {
        outSize = glm::vec3(blockW_, blockH_, blockW_);
        // Snap X/Z to the grid; centre the block on the snapped terrain point.
        float sx = snapToGrid(tHit.x);
        float sz = snapToGrid(tHit.z);

        // Look for an existing foundation block adjacent (within ~1.5 grid
        // steps in XZ) so the new block joins it at the same level instead of
        // being driven by the local terrain height.
        float joinRadius = gridStep_ * 1.5f;
        float joinR2 = joinRadius * joinRadius;
        const Block* neighbor = nullptr;
        float bestD2 = joinR2;
        for (const auto& b : blocks_) {
            if (b.type != Foundation) continue;
            float dx = b.position.x - sx;
            float dz = b.position.z - sz;
            float d2 = dx * dx + dz * dz;
            if (d2 < bestD2) { bestD2 = d2; neighbor = &b; }
        }

        float centerY;
        if (neighbor) {
            // Same level as the neighbour foundation.
            centerY = neighbor->position.y;
        } else {
            float th = terrain.heightAtWorld(sx, sz);
            float topY = th + blockH_ * (1.0f - sunkDepth_);
            centerY = topY - blockH_ * 0.5f;
        }
        outCenter = glm::vec3(sx, centerY, sz);
        outType = Foundation;
        return true;
    }
    return false;
}

int BuildSystem::placeBlock(const glm::vec3& center, const glm::vec3& size,
                             BlockType type, const glm::vec3& color) {
    Block b;
    b.position = center;
    b.size = size;
    b.color = color;
    b.type = type;
    b.id = nextId_++;
    blocks_.push_back(b);
    return b.id;
}

void BuildSystem::removeBlock(int id) {
    for (size_t i = 0; i < blocks_.size(); ++i) {
        if (blocks_[i].id == id) {
            blocks_.erase(blocks_.begin() + i);
            return;
        }
    }
}

void BuildSystem::clear() {
    blocks_.clear();
}

const BuildSystem::Block* BuildSystem::findBlock(int id) const {
    for (const auto& b : blocks_)
        if (b.id == id) return &b;
    return nullptr;
}

// Slab-method ray/AABB test. Returns t (distance along ray) or -1; fills the
// hit face normal (one of the axis-aligned unit normals).
static float rayAabb(const glm::vec3& ro, const glm::vec3& rd,
                     const glm::vec3& mn, const glm::vec3& mx,
                     glm::vec3& outNormal) {
    float tmin = -1e30f, tmax = 1e30f;
    int   axisMin = -1, axisMax = -1;
    float signMin = 0, signMax = 0;
    for (int i = 0; i < 3; ++i) {
        float o = ro[i], d = rd[i];
        if (std::abs(d) < 1e-8f) {
            if (o < mn[i] || o > mx[i]) return -1.0f;
            continue;
        }
        float t1 = (mn[i] - o) / d;
        float t2 = (mx[i] - o) / d;
        float s = -1.0f;
        if (t1 > t2) { std::swap(t1, t2); s = 1.0f; }
        if (t1 > tmin) { tmin = t1; axisMin = i; signMin = s; }
        if (t2 < tmax) { tmax = t2; axisMax = i; signMax = -s; }
        if (tmin > tmax) return -1.0f;
    }
    // Use the entry point's face normal. Negative t means ray origin is inside
    // the box — fall back to the exit face.
    float t = tmin >= 0.0f ? tmin : tmax;
    if (t < 0.0f) return -1.0f;
    glm::vec3 n(0.0f);
    int axis = (tmin >= 0.0f) ? axisMin : axisMax;
    float sign = (tmin >= 0.0f) ? signMin : signMax;
    if (axis < 0) return -1.0f;
    n[axis] = sign;
    outNormal = n;
    return t;
}

int BuildSystem::pick(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                      glm::vec3& outHitPos, glm::vec3& outHitNormal) const {
    float bestT = 1e30f;
    int    bestId = -1;
    glm::vec3 bestN(0.0f);
    for (const auto& b : blocks_) {
        glm::vec3 n;
        float t = rayAabb(rayOrigin, rayDir, b.min(), b.max(), n);
        if (t > 0.0f && t < bestT) {
            bestT = t;
            bestId = b.id;
            bestN = n;
        }
    }
    if (bestId < 0) return -1;
    outHitNormal = bestN;
    outHitPos = rayOrigin + rayDir * bestT;
    return bestId;
}

void BuildSystem::drawCube(const Shader& shader, const glm::mat4& viewProj,
                            const glm::vec3& lightDir, const glm::vec3& camPos,
                            const glm::vec3& center, const glm::vec3& size,
                            const glm::vec3& color, float alpha) const {
    glm::mat4 m(1.0f);
    m = glm::translate(m, center);
    m = glm::scale(m, size);
    shader.setMat4("uModel", m);
    shader.setMat4("uViewProj", viewProj);
    shader.setVec3("uColor", color);
    shader.setVec3("uLightDir", lightDir);
    shader.setVec3("uCamPos", camPos);
    shader.setFloat("uAlpha", alpha);
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void BuildSystem::render(const Shader& shader, const glm::mat4& viewProj,
                         const glm::vec3& lightDir,
                         const glm::vec3& camPos) const {
    if (blocks_.empty()) return;
    shader.use();
    for (const auto& b : blocks_) {
        drawCube(shader, viewProj, lightDir, camPos,
                 b.position, b.size, b.color, 1.0f);
    }
}

void BuildSystem::renderGhost(const Shader& shader, const glm::mat4& viewProj,
                               const glm::vec3& lightDir, const glm::vec3& camPos,
                               const glm::vec3& center, const glm::vec3& size,
                               const glm::vec3& color) const {
    // Semi-transparent fill.
    shader.use();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    drawCube(shader, viewProj, lightDir, camPos, center, size, color, 0.35f);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);

    // Wireframe outline on top (line shader).
    // (The caller passes the lit shader but we need the line shader; the
    // wireframe is drawn by App with the line shader, so this method only
    // renders the transparent fill.)
}

void BuildSystem::renderWireframe(const Shader& shader,
                                    const glm::mat4& viewProj,
                                    int id, const glm::vec3& color) const {
    const Block* b = findBlock(id);
    if (!b) return;
    renderWireframeBox(shader, viewProj, b->position, b->size, color);
}

void BuildSystem::renderWireframeBox(const Shader& shader,
                                        const glm::mat4& viewProj,
                                        const glm::vec3& center,
                                        const glm::vec3& size,
                                        const glm::vec3& color) const {
    glm::mat4 m(1.0f);
    m = glm::translate(m, center);
    m = glm::scale(m, size);
    shader.use();
    shader.setMat4("uViewProj", viewProj * m);
    shader.setVec3("uColor", color);
    shader.setFloat("uAlpha", 1.0f);
    glBindVertexArray(wireVao_);
    glDrawArrays(GL_LINES, 0, 24);
    glBindVertexArray(0);
}

void BuildSystem::reproject(const Terrain& terrain, const glm::vec3& center,
                             float radius) {
    bool all = (radius <= 0.0f);
    float r2 = radius * radius;
    for (auto& b : blocks_) {
        if (b.type != Foundation) continue;
        if (!all) {
            float dx = b.position.x - center.x;
            float dz = b.position.z - center.z;
            if (dx * dx + dz * dz > r2) continue;
        }
        float th = terrain.heightAtWorld(b.position.x, b.position.z);
        float topY = th + b.size.y * (1.0f - sunkDepth_);
        b.position.y = topY - b.size.y * 0.5f;
    }
}
