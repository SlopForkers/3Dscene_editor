#include "build.h"
#include "shader.h"
#include "terrain.h"
#include "sys_util.h"
#include <stb_image.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

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

void BuildSystem::initDefaultTex() {
    // 1x1 opaque white so the sampler is always bound.
    unsigned char px[4] = { 255, 255, 255, 255 };
    glGenTextures(1, &defaultTex_);
    glBindTexture(GL_TEXTURE_2D, defaultTex_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void BuildSystem::create() {
    initCubeMesh();
    initWireCube();
    initDefaultTex();
}

void BuildSystem::destroy() {
    if (vao_)     { glDeleteVertexArrays(1, &vao_);     vao_ = 0; }
    if (vbo_)     { glDeleteBuffers(1, &vbo_);          vbo_ = 0; }
    if (ibo_)     { glDeleteBuffers(1, &ibo_);          ibo_ = 0; }
    if (wireVao_) { glDeleteVertexArrays(1, &wireVao_); wireVao_ = 0; }
    if (wireVbo_) { glDeleteBuffers(1, &wireVbo_);      wireVbo_ = 0; }
    if (defaultTex_) { glDeleteTextures(1, &defaultTex_); defaultTex_ = 0; }
    for (const auto& t : blockTextures_)
        if (t.glId) glDeleteTextures(1, &t.glId);
    blockTextures_.clear();
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
            // Use the world-axis-aligned size (yaw-aware) — for a rotated
            // block the raw size.x/size.z no longer match the world axes.
            glm::vec3 center = b->position + hitNormal * (b->aabbSize() * 0.5f + outSize * 0.5f);
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
            // Never place a foundation into an already-occupied cell —
            // a duplicate would z-fight inside the existing block.
            if (outType == Foundation && cellOccupied(center.x, center.z))
                return false;
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
        // Skip occupied cells (duplicate blocks z-fight inside each other).
        if (cellOccupied(sx, sz)) return false;
        outCenter = glm::vec3(sx, centerY, sz);
        outType = Foundation;
        return true;
    }
    return false;
}

bool BuildSystem::cellOccupied(float x, float z) const {
    float tol = gridStep_ * 0.3f;
    for (const auto& b : blocks_) {
        if (std::abs(b.position.x - x) < tol &&
            std::abs(b.position.z - z) < tol)
            return true;
    }
    return false;
}

int BuildSystem::fillRect(const Terrain& terrain,
                            float x0, float z0, float x1, float z1,
                            BlockType type) {
    if (x1 < x0) std::swap(x0, x1);
    if (z1 < z0) std::swap(z0, z1);
    int placed = 0;
    glm::vec3 size(blockW_, blockH_, blockW_);
    float joinR = gridStep_ * 1.5f;
    float joinR2 = joinR * joinR;
    float seedY = 1e30f;

    for (float cx = snapToGrid(x0); cx <= x1 + 1e-4f; cx += gridStep_) {
        for (float cz = snapToGrid(z0); cz <= z1 + 1e-4f; cz += gridStep_) {
            if (cellOccupied(cx, cz)) continue;

            float centerY;
            if (seedY < 1e29f) {
                centerY = seedY;
            } else {
                float ny = 1e30f;
                for (const auto& b : blocks_) {
                    if (b.type != Foundation) continue;
                    float bx = b.position.x - cx;
                    float bz = b.position.z - cz;
                    if (bx * bx + bz * bz < joinR2) { ny = b.position.y; break; }
                }
                if (ny < 1e29f) {
                    centerY = ny;
                } else {
                    float th = terrain.heightAtWorld(cx, cz);
                    float topY = th + blockH_ * (1.0f - sunkDepth_);
                    centerY = topY - blockH_ * 0.5f;
                }
            }
            placeBlock(glm::vec3(cx, centerY, cz), size, type, color_);
            ++placed;
            if (seedY > 1e29f) seedY = centerY;
        }
    }
    return placed;
}

// Wall sits fully on top of the foundation, inner face flush with the edge.
// Centre is therefore halfSize - wallThickness/2 from the block centre
// (measured inward from the rim).
float BuildSystem::wallEdgeOffset(float halfSize) const {
    float off = halfSize - wallThickness_ * 0.5f;
    return (wallEdge_ == 0 || wallEdge_ == 1) ? off : -off;
}

void BuildSystem::wallLineParamsFor(const Block& support, float& outFixed,
                                     bool& outAlongX) const {
    glm::vec3 sz = support.aabbSize();
    if (wallEdge_ == 0 || wallEdge_ == 2) {
        // +X / -X edge: wall runs along Z, fixed coord is X.
        outAlongX = false;
        // Snap the block centre to the grid, then add the edge offset
        // (un-snapped) so the wall lands on the rim, not the grid cell centre.
        float cx = std::round(support.position.x / gridStep_) * gridStep_;
        outFixed  = cx + wallEdgeOffset(sz.x * 0.5f);
    } else {
        // +Z / -Z edge: wall runs along X, fixed coord is Z.
        outAlongX = true;
        float cz = std::round(support.position.z / gridStep_) * gridStep_;
        outFixed  = cz + wallEdgeOffset(sz.z * 0.5f);
    }
}

int BuildSystem::fillWallLine(float startCoord, float endCoord,
                                float fixedCoord, float baseY, bool alongX) {
    if (endCoord < startCoord) std::swap(startCoord, endCoord);
    int placed = 0;
    // Wall orientation encoded in size: along X -> thin Z, along Z -> thin X.
    glm::vec3 size = alongX
        ? glm::vec3(blockW_, blockH_, wallThickness_)
        : glm::vec3(wallThickness_, blockH_, blockW_);
    float halfThin = wallThickness_ * 0.5f;
    float topTol = 0.05f;

    for (float c = snapToGrid(startCoord); c <= endCoord + 1e-4f; c += gridStep_) {
        float cx = alongX ? c : fixedCoord;
        float cz = alongX ? fixedCoord : c;
        float wallCenterY = baseY + blockH_ * 0.5f;

        // Skip if a wall already occupies this exact spot.
        bool occupied = false;
        for (const auto& b : blocks_) {
            if (b.type != Wall) continue;
            if (std::abs(b.position.x - cx) < 0.05f &&
                std::abs(b.position.z - cz) < 0.05f &&
                std::abs(b.position.y - wallCenterY) < 0.05f) {
                occupied = true; break;
            }
        }
        if (occupied) continue;

        // Find a supporting block whose top ~= baseY AND whose XZ AABB
        // overlaps the wall's XZ footprint (the wall sits on the rim, so its
        // centre is offset from the block centre — a centre-distance test
        // would miss it).
        bool supported = false;
        glm::vec3 wmn(cx - size.x * 0.5f, 0, cz - size.z * 0.5f);
        glm::vec3 wmx(cx + size.x * 0.5f, 0, cz + size.z * 0.5f);
        for (const auto& b : blocks_) {
            if (std::abs(b.max().y - baseY) > topTol) continue;
            glm::vec3 bmn = b.min(), bmx = b.max();
            // XZ overlap test.
            if (wmn.x < bmx.x && wmx.x > bmn.x &&
                wmn.z < bmx.z && wmx.z > bmn.z) {
                supported = true; break;
            }
        }
        if (!supported) continue;

        placeBlock(glm::vec3(cx, wallCenterY, cz), size, Wall, color_, 0.0f);
        ++placed;
    }
    return placed;
}

int BuildSystem::eraseRect(float x0, float z0, float x1, float z1) {
    if (x1 < x0) std::swap(x0, x1);
    if (z1 < z0) std::swap(z0, z1);
    float tol = gridStep_ * 0.5f;
    int before = (int)blocks_.size();
    blocks_.erase(std::remove_if(blocks_.begin(), blocks_.end(),
        [&](const Block& b) {
            return b.position.x >= x0 - tol && b.position.x <= x1 + tol &&
                   b.position.z >= z0 - tol && b.position.z <= z1 + tol;
        }), blocks_.end());
    return before - (int)blocks_.size();
}

int BuildSystem::placeBlock(const glm::vec3& center, const glm::vec3& size,
                             BlockType type, const glm::vec3& color, float yaw) {
    Block b;
    b.position = center;
    b.size = size;
    b.color = color;
    b.type = type;
    b.yaw = yaw;
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

BuildSystem::Block* BuildSystem::findBlockMutable(int id) {
    for (auto& b : blocks_)
        if (b.id == id) return &b;
    return nullptr;
}

int BuildSystem::faceFromNormal(const glm::vec3& n) {
    float ax = std::abs(n.x), ay = std::abs(n.y), az = std::abs(n.z);
    if (ax >= ay && ax >= az) return n.x > 0.0f ? FacePX : FaceNX;
    if (ay >= ax && ay >= az) return n.y > 0.0f ? FacePY : FaceNY;
    return n.z > 0.0f ? FacePZ : FaceNZ;
}

// --- Texture library ---

int BuildSystem::findBlockTextureByPath(const std::string& path) const {
    for (size_t i = 0; i < blockTextures_.size(); ++i)
        if (blockTextures_[i].path == path) return (int)i;
    return -1;
}

int BuildSystem::loadBlockTexture(const std::string& path) {
    int existing = findBlockTextureByPath(path);
    if (existing >= 0) return existing;
    // UTF-8 aware read (stbi_load can't open non-ANSI paths on Windows).
    std::vector<char> bytes;
    if (!readFileBytes(path, bytes)) {
        std::cerr << "BuildSystem: failed to load texture: " << path << "\n";
        return -1;
    }
    int w = 0, h = 0, ch = 0;
    stbi_uc* pixels = stbi_load_from_memory((const stbi_uc*)bytes.data(),
                                            (int)bytes.size(), &w, &h, &ch, 4);
    if (!pixels) {
        std::cerr << "BuildSystem: failed to decode texture: " << path << "\n";
        return -1;
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    stbi_image_free(pixels);

    BlockTexture bt;
    bt.glId = tex;
    bt.path = path;
    // Derive a short display name from the filename.
    size_t slash = path.find_last_of("/\\");
    bt.name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    blockTextures_.push_back(bt);
    return (int)blockTextures_.size() - 1;
}

GLuint BuildSystem::blockTextureId(int i) const {
    if (i < 0 || i >= (int)blockTextures_.size()) return defaultTex_;
    return blockTextures_[i].glId ? blockTextures_[i].glId : defaultTex_;
}

const std::string& BuildSystem::blockTextureName(int i) const {
    static const std::string empty;
    if (i < 0 || i >= (int)blockTextures_.size()) return empty;
    return blockTextures_[i].name;
}

const std::string& BuildSystem::blockTexturePath(int i) const {
    static const std::string empty;
    if (i < 0 || i >= (int)blockTextures_.size()) return empty;
    return blockTextures_[i].path;
}

void BuildSystem::setBlockFaceTexture(int blockId, int textureIdx, int face) {
    Block* b = findBlockMutable(blockId);
    if (!b) return;
    b->textureIdx = textureIdx;
    b->textureFace = face;
}

void BuildSystem::clearBlockFaceTexture(int blockId) {
    Block* b = findBlockMutable(blockId);
    if (!b) return;
    b->textureIdx = -1;
    b->textureFace = -1;
}

void BuildSystem::setBlockTexScale(int blockId, float scale) {
    Block* b = findBlockMutable(blockId);
    if (!b) return;
    b->texScale = std::max(0.01f, scale);
}

void BuildSystem::setBlockTexMode(int blockId, int mode) {
    Block* b = findBlockMutable(blockId);
    if (!b) return;
    b->texMode = (mode == 1) ? 1 : 0;
}

void BuildSystem::paintCurrentTexture(int blockId, int face) {
    if (currentTextureIdx_ < 0) return;
    Block* b = findBlockMutable(blockId);
    if (!b) return;
    // Apply default scale/mode only if the block had no texture before this paint.
    bool wasUntextured = (b->textureIdx < 0);
    b->textureIdx = currentTextureIdx_;
    b->textureFace = face;
    if (wasUntextured) {
        b->texScale = defaultTexScale_;
        b->texMode  = defaultTexMode_;
    }
}

int BuildSystem::applyTextureToRect(float x0, float z0, float x1, float z1, int face) {
    if (currentTextureIdx_ < 0) return 0;
    if (x1 < x0) std::swap(x0, x1);
    if (z1 < z0) std::swap(z0, z1);
    int n = 0;
    for (auto& b : blocks_) {
        if (b.position.x < x0 || b.position.x > x1 ||
            b.position.z < z0 || b.position.z > z1) continue;
        bool wasUntextured = (b.textureIdx < 0);
        b.textureIdx  = currentTextureIdx_;
        b.textureFace = face;
        if (wasUntextured) {
            b.texScale = defaultTexScale_;
            b.texMode  = defaultTexMode_;
        }
        ++n;
    }
    return n;
}

int BuildSystem::applyTextureToLine(float startC, float endC, float fixedC,
                                     bool alongX, int face) {
    if (currentTextureIdx_ < 0) return 0;
    if (endC < startC) std::swap(startC, endC);
    float tol = gridStep_ * 0.5f;
    int n = 0;
    for (auto& b : blocks_) {
        float c  = alongX ? b.position.x : b.position.z;
        float fc = alongX ? b.position.z : b.position.x;
        if (c < startC - tol || c > endC + tol) continue;
        if (std::abs(fc - fixedC) > tol) continue;
        bool wasUntextured = (b.textureIdx < 0);
        b.textureIdx  = currentTextureIdx_;
        b.textureFace = face;
        if (wasUntextured) {
            b.texScale = defaultTexScale_;
            b.texMode  = defaultTexMode_;
        }
        ++n;
    }
    return n;
}

void BuildSystem::removeBlockTexture(int idx) {
    if (idx < 0 || idx >= (int)blockTextures_.size()) return;
    if (blockTextures_[idx].glId) glDeleteTextures(1, &blockTextures_[idx].glId);
    blockTextures_.erase(blockTextures_.begin() + idx);
    // Re-index block references: blocks pointing to the removed entry lose
    // their texture; blocks past it shift down by one.
    for (auto& b : blocks_) {
        if (b.textureIdx == idx) { b.textureIdx = -1; b.textureFace = -1; }
        else if (b.textureIdx > idx) --b.textureIdx;
    }
    if (currentTextureIdx_ == idx) currentTextureIdx_ = -1;
    else if (currentTextureIdx_ > idx) --currentTextureIdx_;
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
                            const glm::vec3& color, float alpha, float yaw,
                            int textureIdx, int textureFace, float texScale,
                            int texMode) const {
    glm::mat4 m(1.0f);
    m = glm::translate(m, center);
    if (std::abs(yaw) > 1e-4f) m = glm::rotate(m, yaw, glm::vec3(0, 1, 0));
    m = glm::scale(m, size);
    shader.setMat4("uModel", m);
    shader.setMat4("uViewProj", viewProj);
    shader.setVec3("uColor", color);
    shader.setVec3("uLightDir", lightDir);
    shader.setVec3("uCamPos", camPos);
    shader.setFloat("uAlpha", alpha);
    bool hasTex = (textureIdx >= 0 && textureFace >= 0 &&
                   textureIdx < (int)blockTextures_.size());
    shader.setInt("uHasTexture", hasTex ? 1 : 0);
    shader.setInt("uTextureFace", hasTex ? textureFace : -1);
    shader.setInt("uTexMode", hasTex ? texMode : 0);
    shader.setFloat("uTexScale", texScale);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, hasTex ? blockTextures_[textureIdx].glId
                                        : defaultTex_);
    shader.setInt("uTex", 0);
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
                 b.position, b.size, b.color, 1.0f, b.yaw,
                 b.textureIdx, b.textureFace, b.texScale, b.texMode);
    }
}

void BuildSystem::renderGhost(const Shader& shader, const glm::mat4& viewProj,
                               const glm::vec3& lightDir, const glm::vec3& camPos,
                               const glm::vec3& center, const glm::vec3& size,
                               const glm::vec3& color, float yaw) const {
    // Semi-transparent fill.
    shader.use();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    drawCube(shader, viewProj, lightDir, camPos, center, size, color, 0.35f, yaw,
             -1, -1, 1.0f, 0);
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
    renderWireframeBox(shader, viewProj, b->position, b->size, color, b->yaw);
}

void BuildSystem::renderWireframeBox(const Shader& shader,
                                        const glm::mat4& viewProj,
                                        const glm::vec3& center,
                                        const glm::vec3& size,
                                        const glm::vec3& color,
                                        float yaw) const {
    glm::mat4 m(1.0f);
    m = glm::translate(m, center);
    if (std::abs(yaw) > 1e-4f) m = glm::rotate(m, yaw, glm::vec3(0, 1, 0));
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
