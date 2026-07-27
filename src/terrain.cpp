#include "terrain.h"
#include "shader.h"
#include <stb_image.h>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <random>
#include <cstdio>

Terrain::Terrain(int gridX, int gridZ, float worldSize)
    : gridX_(gridX), gridZ_(gridZ), worldSize_(worldSize) {
    heights_.assign((size_t)gridX_ * gridZ_, 0.0f);
    vertices_.resize((size_t)gridX_ * gridZ_);
}

void Terrain::create() {
    // Index buffer: two triangles per grid cell.
    indices_.clear();
    indices_.reserve((size_t)(gridX_ - 1) * (gridZ_ - 1) * 6);
    for (int iz = 0; iz < gridZ_ - 1; ++iz) {
        for (int ix = 0; ix < gridX_ - 1; ++ix) {
            GLuint a = idx(ix,     iz);
            GLuint b = idx(ix + 1, iz);
            GLuint c = idx(ix,     iz + 1);
            GLuint d = idx(ix + 1, iz + 1);
            indices_.push_back(a); indices_.push_back(c); indices_.push_back(b);
            indices_.push_back(b); indices_.push_back(c); indices_.push_back(d);
        }
    }
    indexCount_ = (int)indices_.size();

    recomputeAllNormals();
    updateStats();

    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glGenBuffers(1, &ebo_);
    glBindVertexArray(vao_);

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex),
                 vertices_.data(), GL_DYNAMIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo_);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices_.size() * sizeof(GLuint),
                 indices_.data(), GL_STATIC_DRAW);

    // position
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, position));
    // normal
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex),
                          (void*)offsetof(Vertex, normal));

    glBindVertexArray(0);
    dirty_ = false;

    initTextureLayers();
}

void Terrain::destroy() {
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (ebo_) { glDeleteBuffers(1, &ebo_); ebo_ = 0; }
    if (splatTex_) { glDeleteTextures(1, &splatTex_); splatTex_ = 0; }
    for (auto& l : layers_) {
        if (l.albedo) { glDeleteTextures(1, &l.albedo); l.albedo = 0; }
        if (l.normal) { glDeleteTextures(1, &l.normal); l.normal = 0; }
    }
    layers_.clear();
    splat_.clear();
}

float Terrain::getH(int ix, int iz) const {
    return heights_[idx(clampIX(ix), clampIZ(iz))];
}

void Terrain::setH(int ix, int iz, float h) {
    heights_[idx(ix, iz)] = h;
}

float Terrain::falloff(float dist, float radius, int mode) const {
    if (dist >= radius) return 0.0f;
    float t = dist / radius;            // 0..1
    switch (mode) {
        case BrushParams::FalloffConstant: return 1.0f;
        case BrushParams::FalloffLinear:   return 1.0f - t;
        case BrushParams::FalloffSmooth:
        default: {
            float s = 1.0f - t;
            return s * s * (3.0f - 2.0f * s);   // smoothstep
        }
    }
}

bool Terrain::applyBrush(const BrushParams& bp, const glm::vec3& worldPos) {
    // Find grid range affected by the brush radius.
    float cellX = worldSize_ / float(gridX_ - 1);
    float cellZ = worldSize_ / float(gridZ_ - 1);
    float r = bp.radius;

    int ixCenter = int((worldPos.x / worldSize_ + 0.5f) * (gridX_ - 1));
    int izCenter = int((worldPos.z / worldSize_ + 0.5f) * (gridZ_ - 1));
    int spanX = int(r / cellX) + 1;
    int spanZ = int(r / cellZ) + 1;

    int x0 = std::max(0, ixCenter - spanX);
    int x1 = std::min(gridX_ - 1, ixCenter + spanX);
    int z0 = std::max(0, izCenter - spanZ);
    int z1 = std::min(gridZ_ - 1, izCenter + spanZ);

    if (x0 > x1 || z0 > z1) return false;

    bool changed = false;

    if (bp.type == BrushParams::Texture) {
        // Splat painting: increase the weight of the selected layer under the
        // brush and renormalise the RGBA weights so they keep summing to ~1.
        int layer = std::clamp(bp.textureLayer, 0, 3);
        if (layers_.empty()) return false;
        float add = std::clamp(bp.strength, 0.0f, 1.0f);
        for (int iz = z0; iz <= z1; ++iz) {
            for (int ix = x0; ix <= x1; ++ix) {
                float wx = worldX(ix);
                float wz = worldZ(iz);
                float d = std::sqrt((wx - worldPos.x) * (wx - worldPos.x) +
                                    (wz - worldPos.z) * (wz - worldPos.z));
                float f = falloff(d, r, bp.falloff);
                if (f <= 0.0f) continue;
                uint8_t* px = &splat_[idx(ix, iz) * 4];
                float w[4] = { px[0] / 255.0f, px[1] / 255.0f,
                               px[2] / 255.0f, px[3] / 255.0f };
                w[layer] += f * add;
                float s = w[0] + w[1] + w[2] + w[3];
                if (s > 1e-4f) { for (int k = 0; k < 4; ++k) w[k] /= s; }
                for (int k = 0; k < 4; ++k)
                    px[k] = (uint8_t)std::clamp(w[k] * 255.0f, 0.0f, 255.0f);
                changed = true;
            }
        }
        if (changed) uploadSplat();
        return changed;
    }

    if (bp.type == BrushParams::Smooth) {
        // Two-pass: snapshot the affected region first.
        int w = x1 - x0 + 1;
        int h = z1 - z0 + 1;
        std::vector<float> snap(w * h);
        for (int iz = z0; iz <= z1; ++iz)
            for (int ix = x0; ix <= x1; ++ix)
                snap[(iz - z0) * w + (ix - x0)] = getH(ix, iz);

        for (int iz = z0; iz <= z1; ++iz) {
            for (int ix = x0; ix <= x1; ++ix) {
                float wx = worldX(ix);
                float wz = worldZ(iz);
                float d = std::sqrt((wx - worldPos.x) * (wx - worldPos.x) +
                                    (wz - worldPos.z) * (wz - worldPos.z));
                float f = falloff(d, r, bp.falloff);
                if (f <= 0.0f) continue;
                // Average 3x3 neighbourhood from the snapshot.
                float sum = 0.0f, cnt = 0.0f;
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx) {
                        int sx = ix + dx, sz = iz + dz;
                        sum += heights_[idx(clampIX(sx), clampIZ(sz))];
                        cnt += 1.0f;
                    }
                float avg = sum / cnt;
                float cur = heights_[idx(ix, iz)];
                float nh = cur + (avg - cur) * f * bp.strength;
                setH(ix, iz, nh);
                changed = true;
            }
        }
    } else if (bp.type == BrushParams::Noise) {
        std::mt19937 rng((unsigned)std::hash<long long>{}((long long)worldPos.x * 1000.0 + worldPos.z));
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        for (int iz = z0; iz <= z1; ++iz) {
            for (int ix = x0; ix <= x1; ++ix) {
                float wx = worldX(ix);
                float wz = worldZ(iz);
                float d = std::sqrt((wx - worldPos.x) * (wx - worldPos.x) +
                                    (wz - worldPos.z) * (wz - worldPos.z));
                float f = falloff(d, r, bp.falloff);
                if (f <= 0.0f) continue;
                float cur = heights_[idx(ix, iz)];
                setH(ix, iz, cur + dist(rng) * f * bp.strength);
                changed = true;
            }
        }
    } else {
        for (int iz = z0; iz <= z1; ++iz) {
            for (int ix = x0; ix <= x1; ++ix) {
                float wx = worldX(ix);
                float wz = worldZ(iz);
                float d = std::sqrt((wx - worldPos.x) * (wx - worldPos.x) +
                                    (wz - worldPos.z) * (wz - worldPos.z));
                float f = falloff(d, r, bp.falloff);
                if (f <= 0.0f) continue;
                float cur = heights_[idx(ix, iz)];
                float nh = cur;
                switch (bp.type) {
                    case BrushParams::Raise:  nh = cur + f * bp.strength; break;
                    case BrushParams::Lower:  nh = cur - f * bp.strength; break;
                    case BrushParams::Flatten: nh = cur + (bp.target - cur) * f * bp.strength; break;
                    case BrushParams::Set:     nh = cur + (bp.target - cur) * f * std::min(1.0f, bp.strength); break;
                    default: break;
                }
                setH(ix, iz, nh);
                changed = true;
            }
        }
    }

    if (changed) {
        // Recompute normals over a slightly padded region so edges look right.
        int nx0 = std::max(0, x0 - 1);
        int nx1 = std::min(gridX_ - 1, x1 + 1);
        int nz0 = std::max(0, z0 - 1);
        int nz1 = std::min(gridZ_ - 1, z1 + 1);
        recomputeNormals(nx0, nz0, nx1, nz1);
        uploadVertices(false);
        updateStats();
        dirty_ = false;
    }
    return changed;
}

void Terrain::recomputeNormals(int x0, int z0, int x1, int z1) {
    float cellX = worldSize_ / float(gridX_ - 1);
    float cellZ = worldSize_ / float(gridZ_ - 1);
    for (int iz = z0; iz <= z1; ++iz) {
        for (int ix = x0; ix <= x1; ++ix) {
            float hl = getH(ix - 1, iz);
            float hr = getH(ix + 1, iz);
            float hd = getH(ix, iz - 1);
            float hu = getH(ix, iz + 1);
            // Normal from finite differences.
            glm::vec3 n(-(hr - hl) / (2.0f * cellX),
                        2.0f,
                        -(hu - hd) / (2.0f * cellZ));
            n = glm::normalize(n);
            Vertex& v = vertices_[idx(ix, iz)];
            v.position = glm::vec3(worldX(ix), heights_[idx(ix, iz)], worldZ(iz));
            v.normal = n;
        }
    }
}

void Terrain::recomputeAllNormals() {
    for (int iz = 0; iz < gridZ_; ++iz)
        for (int ix = 0; ix < gridX_; ++ix) {
            float hl = getH(ix - 1, iz);
            float hr = getH(ix + 1, iz);
            float hd = getH(ix, iz - 1);
            float hu = getH(ix, iz + 1);
            float cellX = worldSize_ / float(gridX_ - 1);
            float cellZ = worldSize_ / float(gridZ_ - 1);
            glm::vec3 n(-(hr - hl) / (2.0f * cellX), 2.0f, -(hu - hd) / (2.0f * cellZ));
            n = glm::normalize(n);
            Vertex& v = vertices_[idx(ix, iz)];
            v.position = glm::vec3(worldX(ix), heights_[idx(ix, iz)], worldZ(iz));
            v.normal = n;
        }
}

void Terrain::updateStats() {
    if (heights_.empty()) { statsMin_ = statsMax_ = 0.0f; return; }
    float mn = heights_[0], mx = heights_[0];
    for (float h : heights_) { mn = std::min(mn, h); mx = std::max(mx, h); }
    statsMin_ = mn; statsMax_ = mx;
}

void Terrain::uploadVertices(bool fullReupload) {
    if (fullReupload) {
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex),
                     vertices_.data(), GL_DYNAMIC_DRAW);
    } else {
        // Re-upload everything (simple & robust). For 256x256 this is cheap.
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        // orphan then re-specify to avoid pipeline stalls.
        glBufferData(GL_ARRAY_BUFFER, vertices_.size() * sizeof(Vertex),
                     nullptr, GL_DYNAMIC_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        vertices_.size() * sizeof(Vertex), vertices_.data());
    }
}

void Terrain::draw() const {
    if (!vao_) return;
    glBindVertexArray(vao_);
    glDrawElements(GL_TRIANGLES, indexCount_, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void Terrain::flatten(float height) {
    std::fill(heights_.begin(), heights_.end(), height);
    recomputeAllNormals();
    updateStats();
    uploadVertices(true);
}

void Terrain::generateHills() {
    std::mt19937 rng(1337);
    std::uniform_real_distribution<float> pos(-worldSize_ * 0.35f, worldSize_ * 0.35f);
    std::uniform_real_distribution<float> rad(worldSize_ * 0.08f, worldSize_ * 0.18f);
    std::uniform_real_distribution<float> hgt(6.0f, 16.0f);

    std::fill(heights_.begin(), heights_.end(), 0.0f);
    for (int k = 0; k < 6; ++k) {
        glm::vec2 c(pos(rng), pos(rng));
        float r = rad(rng);
        float H = hgt(rng);
        for (int iz = 0; iz < gridZ_; ++iz) {
            for (int ix = 0; ix < gridX_; ++ix) {
                float wx = worldX(ix);
                float wz = worldZ(iz);
                float d = std::sqrt((wx - c.x) * (wx - c.x) + (wz - c.y) * (wz - c.y));
                if (d < r) {
                    float t = 1.0f - d / r;
                    heights_[idx(ix, iz)] += H * t * t;
                }
            }
        }
    }
    recomputeAllNormals();
    updateStats();
    uploadVertices(true);
}

float Terrain::heightAtWorld(float worldX, float worldZ) const {
    // Map world to grid coordinate (in vertex units, continuous).
    float fx = (worldX / worldSize_ + 0.5f) * (gridX_ - 1);
    float fz = (worldZ / worldSize_ + 0.5f) * (gridZ_ - 1);

    if (fx < 0.0f || fz < 0.0f || fx > gridX_ - 1 || fz > gridZ_ - 1)
        return 0.0f;

    int ix0 = (int)std::floor(fx);
    int iz0 = (int)std::floor(fz);
    int ix1 = clampIX(ix0 + 1);
    int iz1 = clampIZ(iz0 + 1);
    ix0 = clampIX(ix0);
    iz0 = clampIZ(iz0);

    float tx = fx - ix0;
    float tz = fz - iz0;

    float h00 = heights_[idx(ix0, iz0)];
    float h10 = heights_[idx(ix1, iz0)];
    float h01 = heights_[idx(ix0, iz1)];
    float h11 = heights_[idx(ix1, iz1)];

    float h0 = h00 * (1.0f - tx) + h10 * tx;
    float h1 = h01 * (1.0f - tx) + h11 * tx;
    return h0 * (1.0f - tz) + h1 * tz;
}

bool Terrain::raycast(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                      glm::vec3& outPoint) const {
    // March the ray in world space and stop when it crosses the heightfield.
    glm::vec3 d = glm::normalize(rayDir);
    const float tStep = 0.5f;
    const float tMax = 1500.0f;

    auto diffAt = [&](float t) {
        glm::vec3 p = rayOrigin + d * t;
        return p.y - heightAtWorld(p.x, p.z);
    };

    float prevDiff = diffAt(0.0f);
    for (float t = tStep; t < tMax; t += tStep) {
        float curDiff = diffAt(t);
        // A crossing happens when the sign flips. We accept either direction
        // so that a ray starting below terrain (rare) still resolves.
        if ((prevDiff > 0.0f) != (curDiff > 0.0f)) {
            // Bisection between t - tStep and t.
            float lo = t - tStep, hi = t;
            for (int i = 0; i < 20; ++i) {
                float mid = 0.5f * (lo + hi);
                if ((diffAt(lo) > 0.0f) == (diffAt(mid) > 0.0f)) lo = mid;
                else hi = mid;
            }
            outPoint = rayOrigin + d * (0.5f * (lo + hi));
            return true;
        }
        prevDiff = curDiff;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Texture layers + splatmap
// ---------------------------------------------------------------------------

GLuint Terrain::makeProceduralTexture(const glm::vec3& baseColor, float variation) {
    const int W = 64, H = 64;
    std::vector<uint8_t> data(W * H * 3);
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(-variation, variation);
    for (int i = 0; i < W * H; ++i) {
        float n = dist(rng);
        // Add a low-frequency blotch for a less uniform look.
        int x = i % W, y = i / W;
        float blotch = std::sin(x * 0.3f) * std::cos(y * 0.25f) * variation * 0.5f;
        data[i * 3 + 0] = (uint8_t)std::clamp((baseColor.r + n + blotch) * 255.0f, 0.0f, 255.0f);
        data[i * 3 + 1] = (uint8_t)std::clamp((baseColor.g + n + blotch) * 255.0f, 0.0f, 255.0f);
        data[i * 3 + 2] = (uint8_t)std::clamp((baseColor.b + n + blotch) * 255.0f, 0.0f, 255.0f);
    }
    GLuint tex = 0;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, W, H, 0, GL_RGB, GL_UNSIGNED_BYTE, data.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
    return tex;
}

GLuint Terrain::loadTextureFile(const std::string& path) {
    int w = 0, h = 0, ch = 0;
    stbi_uc* pixels = stbi_load(path.c_str(), &w, &h, &ch, 4);
    if (!pixels) {
        std::cerr << "Terrain: failed to load texture: " << path << "\n";
        return 0;
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
    return tex;
}

void Terrain::initTextureLayers() {
    // Four procedural layers so the terrain is paintable immediately.
    layers_.clear();
    layers_.resize(4);
    layers_[0].name = "Grass";  layers_[0].albedo = makeProceduralTexture(glm::vec3(0.30f, 0.55f, 0.28f), 0.10f);
    layers_[1].name = "Dirt";   layers_[1].albedo = makeProceduralTexture(glm::vec3(0.38f, 0.26f, 0.16f), 0.08f);
    layers_[2].name = "Rock";   layers_[2].albedo = makeProceduralTexture(glm::vec3(0.45f, 0.45f, 0.48f), 0.12f);
    layers_[3].name = "Sand";   layers_[3].albedo = makeProceduralTexture(glm::vec3(0.76f, 0.70f, 0.50f), 0.06f);
    layers_[0].tileSize = 8.0f;
    layers_[1].tileSize = 8.0f;
    layers_[2].tileSize = 6.0f;
    layers_[3].tileSize = 10.0f;

    // Splat: layer 0 (grass) everywhere by default.
    splat_.assign((size_t)gridX_ * gridZ_ * 4, 0);
    for (size_t i = 0; i < (size_t)gridX_ * gridZ_; ++i)
        splat_[i * 4 + 0] = 255;   // R = grass full weight

    glGenTextures(1, &splatTex_);
    glBindTexture(GL_TEXTURE_2D, splatTex_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gridX_, gridZ_, 0, GL_RGBA, GL_UNSIGNED_BYTE, splat_.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Terrain::uploadSplat() {
    if (!splatTex_) return;
    glBindTexture(GL_TEXTURE_2D, splatTex_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gridX_, gridZ_, GL_RGBA, GL_UNSIGNED_BYTE, splat_.data());
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Terrain::resetSplat() {
    splat_.assign((size_t)gridX_ * gridZ_ * 4, 0);
    for (size_t i = 0; i < (size_t)gridX_ * gridZ_; ++i)
        splat_[i * 4 + 0] = 255;
    uploadSplat();
}

bool Terrain::loadLayerAlbedo(int layerIndex, const std::string& path) {
    if (layerIndex < 0 || layerIndex >= (int)layers_.size()) return false;
    GLuint t = loadTextureFile(path);
    if (!t) return false;
    if (layers_[layerIndex].albedo) glDeleteTextures(1, &layers_[layerIndex].albedo);
    layers_[layerIndex].albedo = t;
    return true;
}

bool Terrain::loadLayerNormal(int layerIndex, const std::string& path) {
    if (layerIndex < 0 || layerIndex >= (int)layers_.size()) return false;
    GLuint t = loadTextureFile(path);
    if (!t) return false;
    if (layers_[layerIndex].normal) glDeleteTextures(1, &layers_[layerIndex].normal);
    layers_[layerIndex].normal = t;
    layers_[layerIndex].hasNormal = true;
    return true;
}

void Terrain::setLayerTileSize(int layerIndex, float tileSize) {
    if (layerIndex < 0 || layerIndex >= (int)layers_.size()) return;
    layers_[layerIndex].tileSize = std::max(0.25f, tileSize);
}

void Terrain::bindTextures(const Shader& shader) const {
    if (layers_.empty() || !splatTex_) return;
    // Splat at unit 8.
    glActiveTexture(GL_TEXTURE8);
    glBindTexture(GL_TEXTURE_2D, splatTex_);
    shader.setInt("uSplat", 8);

    // Layer albedo at units 9..12, normals at 13..16.
    for (int i = 0; i < 4 && i < (int)layers_.size(); ++i) {
        char uname[32];
        glActiveTexture(GL_TEXTURE9 + i);
        glBindTexture(GL_TEXTURE_2D, layers_[i].albedo ? layers_[i].albedo : 0);
        std::snprintf(uname, sizeof(uname), "uLayerTex%d", i);
        shader.setInt(uname, 9 + i);

        glActiveTexture(GL_TEXTURE13 + i);
        glBindTexture(GL_TEXTURE_2D, layers_[i].normal ? layers_[i].normal : 0);
        std::snprintf(uname, sizeof(uname), "uLayerNormal%d", i);
        shader.setInt(uname, 13 + i);

        std::snprintf(uname, sizeof(uname), "uTileSize%d", i);
        shader.setFloat(uname, layers_[i].tileSize);
        std::snprintf(uname, sizeof(uname), "uHasLayerNormal%d", i);
        shader.setBool(uname, layers_[i].hasNormal);
    }
    shader.setInt("uLayerCount", (int)layers_.size());
    shader.setFloat("uTerrainSize", worldSize_);
    glActiveTexture(GL_TEXTURE0);
}
