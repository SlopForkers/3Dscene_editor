#include "terrain.h"
#include "shader.h"
#include "sys_util.h"
#include <stb_image.h>
#include <stb_image_resize2.h>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <random>
#include <cstdio>
#include <cstring>

// Load an image as RGBA8 via the UTF-8 aware file reader (stbi_load itself
// can't open non-ANSI paths on Windows).
static stbi_uc* loadImageRgba(const std::string& path, int& w, int& h) {
    std::vector<char> bytes;
    if (!readFileBytes(path, bytes)) return nullptr;
    int ch = 0;
    return stbi_load_from_memory((const stbi_uc*)bytes.data(), (int)bytes.size(),
                                 &w, &h, &ch, 4);
}

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

    initTextureLayers();
}

void Terrain::destroy() {
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (ebo_) { glDeleteBuffers(1, &ebo_); ebo_ = 0; }
    for (int i = 0; i < NUM_SPLAT_MAPS; ++i) {
        if (splatTex_[i]) { glDeleteTextures(1, &splatTex_[i]); splatTex_[i] = 0; }
    }
    if (albedoArray_) { glDeleteTextures(1, &albedoArray_); albedoArray_ = 0; }
    if (normalArray_) { glDeleteTextures(1, &normalArray_); normalArray_ = 0; }
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

    // Round (not truncate) so the brush footprint stays centred on the
    // cursor instead of being biased by half a cell towards -X/-Z.
    int ixCenter = int(std::round((worldPos.x / worldSize_ + 0.5f) * (gridX_ - 1)));
    int izCenter = int(std::round((worldPos.z / worldSize_ + 0.5f) * (gridZ_ - 1)));
    int spanX = int(r / cellX) + 1;
    int spanZ = int(r / cellZ) + 1;

    int x0 = std::max(0, ixCenter - spanX);
    int x1 = std::min(gridX_ - 1, ixCenter + spanX);
    int z0 = std::max(0, izCenter - spanZ);
    int z1 = std::min(gridZ_ - 1, izCenter + spanZ);

    if (x0 > x1 || z0 > z1) return false;

    bool changed = false;

    if (bp.type == BrushParams::Texture) {
        // Multi-splat painting: 4 RGBA textures x 4 channels = 16 layers.
        // splat_ is planar: [map0 block][map1 block][map2 block][map3 block],
        // each block is gridX*gridZ*4 bytes. Painting a layer modifies ALL 16
        // channels (all 4 maps) so the global weights stay normalised: the
        // target channel moves toward 1, every other channel decays toward 0.
        // With strength=1 and full falloff the result is a clean single-layer
        // splat with no residual weight on other layers.
        int layer = std::clamp(bp.textureLayer, 0, MAX_LAYERS - 1);
        if (layer >= (int)layers_.size()) return false;
        const size_t texels = (size_t)gridX_ * gridZ_;
        float add = std::clamp(bp.strength, 0.0f, 1.0f);
        for (int iz = z0; iz <= z1; ++iz) {
            for (int ix = x0; ix <= x1; ++ix) {
                float wx = worldX(ix);
                float wz = worldZ(iz);
                float d = std::sqrt((wx - worldPos.x) * (wx - worldPos.x) +
                                    (wz - worldPos.z) * (wz - worldPos.z));
                float f = falloff(d, r, bp.falloff);
                if (f <= 0.0f) continue;
                size_t t = (size_t)idx(ix, iz);
                float w[16];
                for (int m = 0; m < NUM_SPLAT_MAPS; ++m) {
                    const uint8_t* px = &splat_[(size_t)m * texels * 4 + t * 4];
                    for (int c = 0; c < 4; ++c)
                        w[m * 4 + c] = px[c] / 255.0f;
                }
                float amt = std::min(1.0f, f * add);
                w[layer] += amt * (1.0f - w[layer]);
                for (int k = 0; k < 16; ++k)
                    if (k != layer) w[k] *= (1.0f - amt);
                for (int m = 0; m < NUM_SPLAT_MAPS; ++m) {
                    uint8_t* px = &splat_[(size_t)m * texels * 4 + t * 4];
                    for (int c = 0; c < 4; ++c)
                        px[c] = (uint8_t)std::clamp(w[m * 4 + c] * 255.0f, 0.0f, 255.0f);
                }
                changed = true;
            }
        }
        if (changed) uploadSplat();
        return changed;
    }

    if (bp.type == BrushParams::Smooth) {
        // Two-pass: snapshot the affected region first and read the 3x3
        // neighbourhood from the SNAPSHOT (reading the live array would make
        // the result depend on traversal order — Gauss-Seidel smearing).
        // Padded by 1 so border vertices see unmodified neighbours.
        int w = x1 - x0 + 1;
        int h = z1 - z0 + 1;
        std::vector<float> snap(w * h);
        for (int iz = z0; iz <= z1; ++iz)
            for (int ix = x0; ix <= x1; ++ix)
                snap[(iz - z0) * w + (ix - x0)] = getH(ix, iz);

        // strength > 1 would make the relaxation diverge (oscillating
        // checkerboard), so clamp the blend factor like the Set brush does.
        float k = std::min(1.0f, bp.strength);
        auto snapH = [&](int ix, int iz) {
            ix = clampIX(ix); iz = clampIZ(iz);
            if (ix < x0 || ix > x1 || iz < z0 || iz > z1) return getH(ix, iz);
            return snap[(iz - z0) * w + (ix - x0)];
        };
        for (int iz = z0; iz <= z1; ++iz) {
            for (int ix = x0; ix <= x1; ++ix) {
                float wx = worldX(ix);
                float wz = worldZ(iz);
                float d = std::sqrt((wx - worldPos.x) * (wx - worldPos.x) +
                                    (wz - worldPos.z) * (wz - worldPos.z));
                float f = falloff(d, r, bp.falloff);
                if (f <= 0.0f) continue;
                float sum = 0.0f, cnt = 0.0f;
                for (int dz = -1; dz <= 1; ++dz)
                    for (int dx = -1; dx <= 1; ++dx) {
                        sum += snapH(ix + dx, iz + dz);
                        cnt += 1.0f;
                    }
                float avg = sum / cnt;
                float cur = snapH(ix, iz);
                setH(ix, iz, cur + (avg - cur) * f * k);
                changed = true;
            }
        }
    } else if (bp.type == BrushParams::Noise) {
        // Seed from the quantized world position (rounding both coordinates
        // BEFORE combining them, so nearby positions in the same cell share
        // a seed instead of colliding due to the cast binding tighter than
        // the multiplication).
        long long sxq = (long long)std::llround(worldPos.x * 1000.0);
        long long szq = (long long)std::llround(worldPos.z * 1000.0);
        std::mt19937 rng((unsigned)std::hash<long long>{}(sxq * 1000003LL + szq));
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
                    // Flatten/Set are relaxation steps h' = h + k*(target-h);
                    // k > 1 makes the iteration diverge (oscillation), so
                    // clamp the blend factor.
                    case BrushParams::Flatten: nh = cur + (bp.target - cur) * f * std::min(1.0f, bp.strength); break;
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

glm::vec3 Terrain::vertexPos(int ix, int iz) const {
    return vertices_[idx(clampIX(ix), clampIZ(iz))].position;
}
glm::vec3 Terrain::vertexNormal(int ix, int iz) const {
    return vertices_[idx(clampIX(ix), clampIZ(iz))].normal;
}
float Terrain::getHeight(int ix, int iz) const {
    return getH(ix, iz);
}
void Terrain::setHeightRaw(int ix, int iz, float h) {
    setH(ix, iz, h);
    int i = idx(clampIX(ix), clampIZ(iz));
    vertices_[i].position.y = h;
}
void Terrain::refresh(int x0, int z0, int x1, int z1) {
    // Expand by 1 so normals at the boundary see the changed heights.
    recomputeNormals(std::max(0, x0 - 1), std::max(0, z0 - 1),
                     std::min(gridX_ - 1, x1 + 1), std::min(gridZ_ - 1, z1 + 1));
    updateStats();
    uploadVertices(false);
}
void Terrain::snapWorldToVertex(const glm::vec3& world, int& outIx, int& outIz) const {
    float fx = (world.x / worldSize_ + 0.5f) * (gridX_ - 1);
    float fz = (world.z / worldSize_ + 0.5f) * (gridZ_ - 1);
    outIx = clampIX(int(std::round(fx)));
    outIz = clampIZ(int(std::round(fz)));
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

void Terrain::generateNoise(const Noise::Params& p) {
    int perm[512];
    Noise::buildPerm(p.seed, perm);
    // Params::frequency is documented as "cycles across the terrain", but
    // the samplers multiply raw world coordinates — normalise it here so
    // frequency=1 really means one period over the whole terrain (sampling
    // world units directly aliases badly at the default density).
    Noise::Params np = p;
    np.frequency = p.frequency / worldSize_;
    for (int iz = 0; iz < gridZ_; ++iz) {
        for (int ix = 0; ix < gridX_; ++ix) {
            float n = Noise::sample2DWithPerm(np, worldX(ix), worldZ(iz), perm);
            float& h = heights_[idx(ix, iz)];
            switch (p.blend) {
                case Noise::Replace:  h = n; break;
                case Noise::Add:      h += n; break;
                case Noise::Subtract: h -= n; break;
                case Noise::Multiply: h *= n; break;
                case Noise::Min:      h = std::min(h, n); break;
                case Noise::Max:      h = std::max(h, n); break;
                case Noise::BlendCount: break;
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
    const float halfW = worldSize_ * 0.5f;

    auto inBounds = [&](const glm::vec3& p) {
        // Slight tolerance so the outermost row of vertices is clickable.
        return p.x >= -halfW - 0.01f && p.x <= halfW + 0.01f &&
               p.z >= -halfW - 0.01f && p.z <= halfW + 0.01f;
    };
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
            float loDiff = prevDiff;
            for (int i = 0; i < 20; ++i) {
                float mid = 0.5f * (lo + hi);
                float midDiff = diffAt(mid);
                if ((loDiff > 0.0f) == (midDiff > 0.0f)) { lo = mid; loDiff = midDiff; }
                else hi = mid;
            }
            glm::vec3 hit = rayOrigin + d * (0.5f * (lo + hi));
            // heightAtWorld() returns 0 outside the grid, which creates a
            // phantom "ground plane" around the terrain. A crossing that
            // lands out of bounds is not a real hit — keep marching (the ray
            // may still re-enter and strike the terrain edge-on).
            if (inBounds(hit)) {
                outPoint = hit;
                return true;
            }
        }
        prevDiff = curDiff;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Texture layers + multi-splat map
// ---------------------------------------------------------------------------

void Terrain::resampleTo512(const uint8_t* src, int sw, int sh,
                            std::vector<uint8_t>& out) {
    out.resize((size_t)ARRAY_SIZE * ARRAY_SIZE * 4);
    stbir_resize_uint8_linear(src, sw, sh, sw * 4,
                             out.data(), ARRAY_SIZE, ARRAY_SIZE, ARRAY_SIZE * 4,
                             STBIR_RGBA);
}

void Terrain::make2DFromPixels(const std::vector<uint8_t>& pix, GLuint& tex) {
    if (tex) glDeleteTextures(1, &tex);
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ARRAY_SIZE, ARRAY_SIZE, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, pix.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void Terrain::fillProceduralAlbedo(std::vector<uint8_t>& out,
                                  const glm::vec3& baseColor, float variation) {
    out.assign((size_t)ARRAY_SIZE * ARRAY_SIZE * 4, 255);
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> dist(-variation, variation);
    for (int y = 0; y < ARRAY_SIZE; ++y) {
        for (int x = 0; x < ARRAY_SIZE; ++x) {
            float n = dist(rng);
            float blotch = std::sin(x * 0.3f) * std::cos(y * 0.25f) * variation * 0.5f;
            size_t i = (size_t(y) * ARRAY_SIZE + x) * 4;
            out[i + 0] = (uint8_t)std::clamp((baseColor.r + n + blotch) * 255.0f, 0.0f, 255.0f);
            out[i + 1] = (uint8_t)std::clamp((baseColor.g + n + blotch) * 255.0f, 0.0f, 255.0f);
            out[i + 2] = (uint8_t)std::clamp((baseColor.b + n + blotch) * 255.0f, 0.0f, 255.0f);
            out[i + 3] = 255;
        }
    }
}

void Terrain::fillFlatNormal(std::vector<uint8_t>& out) {
    out.assign((size_t)ARRAY_SIZE * ARRAY_SIZE * 4, 255);
    for (size_t i = 0; i < (size_t)ARRAY_SIZE * ARRAY_SIZE; ++i) {
        out[i * 4 + 0] = 128;   // X
        out[i * 4 + 1] = 128;   // Y
        out[i * 4 + 2] = 255;   // Z (up)
        out[i * 4 + 3] = 255;
    }
}

void Terrain::rebuildArrays() {
    // (Re)create the albedo + normal array textures, one slice per layer.
    int n = (int)layers_.size();
    if (n == 0) {
        if (albedoArray_) { glDeleteTextures(1, &albedoArray_); albedoArray_ = 0; }
        if (normalArray_) { glDeleteTextures(1, &normalArray_); normalArray_ = 0; }
        return;
    }
    if (albedoArray_) glDeleteTextures(1, &albedoArray_);
    if (normalArray_) glDeleteTextures(1, &normalArray_);
    glGenTextures(1, &albedoArray_);
    glGenTextures(1, &normalArray_);

    glBindTexture(GL_TEXTURE_2D_ARRAY, albedoArray_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, ARRAY_SIZE, ARRAY_SIZE, n,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    std::vector<uint8_t> tmp((size_t)ARRAY_SIZE * ARRAY_SIZE * 4);
    for (int i = 0; i < n; ++i) {
        const auto& pix = layers_[i].albedoPix;
        if (!pix.empty())
            std::memcpy(tmp.data(), pix.data(), tmp.size());
        else
            std::fill(tmp.begin(), tmp.end(), 128);   // mid-grey
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i,
                        ARRAY_SIZE, ARRAY_SIZE, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                        tmp.data());
    }
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D_ARRAY, normalArray_);
    glTexImage3D(GL_TEXTURE_2D_ARRAY, 0, GL_RGBA, ARRAY_SIZE, ARRAY_SIZE, n,
                 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    std::vector<uint8_t> flatN;
    fillFlatNormal(flatN);
    for (int i = 0; i < n; ++i) {
        const auto& pix = layers_[i].normalPix;
        const uint8_t* src = pix.empty() ? flatN.data() : pix.data();
        glTexSubImage3D(GL_TEXTURE_2D_ARRAY, 0, 0, 0, i,
                        ARRAY_SIZE, ARRAY_SIZE, 1, GL_RGBA, GL_UNSIGNED_BYTE,
                        src);
    }
    glGenerateMipmap(GL_TEXTURE_2D_ARRAY);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
    glTexParameteri(GL_TEXTURE_2D_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

    glBindTexture(GL_TEXTURE_2D_ARRAY, 0);
}

void Terrain::initTextureLayers() {
    // Four procedural layers so the terrain is paintable immediately.
    layers_.clear();
    layers_.resize(4);
    struct Def { const char* name; glm::vec3 col; float var; float tile; };
    Def defs[4] = {
        {"Grass", glm::vec3(0.30f, 0.55f, 0.28f), 0.10f, 8.0f},
        {"Dirt",  glm::vec3(0.38f, 0.26f, 0.16f), 0.08f, 8.0f},
        {"Rock",  glm::vec3(0.45f, 0.45f, 0.48f), 0.12f, 6.0f},
        {"Sand",  glm::vec3(0.76f, 0.70f, 0.50f), 0.06f, 10.0f},
    };
    for (int i = 0; i < 4; ++i) {
        layers_[i].name = defs[i].name;
        layers_[i].tileSize = defs[i].tile;
        fillProceduralAlbedo(layers_[i].albedoPix, defs[i].col, defs[i].var);
        make2DFromPixels(layers_[i].albedoPix, layers_[i].albedo);
    }
    rebuildArrays();

    // Splat: layer 0 (grass) everywhere by default. Planar layout: 4 maps.
    splat_.assign((size_t)gridX_ * gridZ_ * 4 * NUM_SPLAT_MAPS, 0);
    size_t map0 = 0;   // map 0 offset
    for (size_t i = 0; i < (size_t)gridX_ * gridZ_; ++i)
        splat_[map0 + i * 4 + 0] = 255;   // R = layer 0 full weight

    for (int m = 0; m < NUM_SPLAT_MAPS; ++m) {
        glGenTextures(1, &splatTex_[m]);
        glBindTexture(GL_TEXTURE_2D, splatTex_[m]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gridX_, gridZ_, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE,
                     &splat_[(size_t)m * gridX_ * gridZ_ * 4]);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void Terrain::uploadSplat() {
    for (int m = 0; m < NUM_SPLAT_MAPS; ++m) {
        if (!splatTex_[m]) continue;
        glBindTexture(GL_TEXTURE_2D, splatTex_[m]);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gridX_, gridZ_, GL_RGBA,
                        GL_UNSIGNED_BYTE, &splat_[(size_t)m * gridX_ * gridZ_ * 4]);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
}

void Terrain::resetSplat() {
    splat_.assign((size_t)gridX_ * gridZ_ * 4 * NUM_SPLAT_MAPS, 0);
    for (size_t i = 0; i < (size_t)gridX_ * gridZ_; ++i)
        splat_[i * 4 + 0] = 255;   // map 0, channel 0 = layer 0
    uploadSplat();
}

bool Terrain::loadLayerAlbedo(int layerIndex, const std::string& path) {
    if (layerIndex < 0 || layerIndex >= (int)layers_.size()) return false;
    int w = 0, h = 0;
    stbi_uc* pixels = loadImageRgba(path, w, h);
    if (!pixels) {
        std::cerr << "Terrain: failed to load texture: " << path << "\n";
        return false;
    }
    resampleTo512(pixels, w, h, layers_[layerIndex].albedoPix);
    stbi_image_free(pixels);
    make2DFromPixels(layers_[layerIndex].albedoPix, layers_[layerIndex].albedo);
    layers_[layerIndex].albedoPath = path;
    rebuildArrays();
    return true;
}

bool Terrain::loadLayerNormal(int layerIndex, const std::string& path) {
    if (layerIndex < 0 || layerIndex >= (int)layers_.size()) return false;
    int w = 0, h = 0;
    stbi_uc* pixels = loadImageRgba(path, w, h);
    if (!pixels) {
        std::cerr << "Terrain: failed to load texture: " << path << "\n";
        return false;
    }
    resampleTo512(pixels, w, h, layers_[layerIndex].normalPix);
    stbi_image_free(pixels);
    make2DFromPixels(layers_[layerIndex].normalPix, layers_[layerIndex].normal);
    layers_[layerIndex].hasNormal = true;
    layers_[layerIndex].normalPath = path;
    rebuildArrays();
    return true;
}

void Terrain::setLayerTileSize(int layerIndex, float tileSize) {
    if (layerIndex < 0 || layerIndex >= (int)layers_.size()) return;
    layers_[layerIndex].tileSize = std::max(0.25f, tileSize);
}

int Terrain::addLayer(const std::string& albedoPath) {
    if ((int)layers_.size() >= MAX_LAYERS) return -1;
    int w = 0, h = 0;
    stbi_uc* pixels = loadImageRgba(albedoPath, w, h);
    if (!pixels) {
        std::cerr << "Terrain: failed to load texture: " << albedoPath << "\n";
        return -1;
    }
    Layer L;
    L.name = "Layer " + std::to_string(layers_.size());
    L.tileSize = 8.0f;
    resampleTo512(pixels, w, h, L.albedoPix);
    stbi_image_free(pixels);
    make2DFromPixels(L.albedoPix, L.albedo);
    L.albedoPath = albedoPath;
    layers_.push_back(std::move(L));
    rebuildArrays();
    return (int)layers_.size() - 1;
}

void Terrain::removeLayer(int layerIndex) {
    if (layerIndex < 0 || layerIndex >= (int)layers_.size()) return;
    if (layers_.size() <= 1) return;   // keep at least one layer
    if (layers_[layerIndex].albedo) glDeleteTextures(1, &layers_[layerIndex].albedo);
    if (layers_[layerIndex].normal) glDeleteTextures(1, &layers_[layerIndex].normal);
    layers_.erase(layers_.begin() + layerIndex);
    rebuildArrays();

    // Splat channels encode ABSOLUTE layer indices (channel = layer), so
    // removing a layer shifts every layer above it down by one. Remap all
    // weights accordingly and renormalise, otherwise painted areas would
    // silently switch to the wrong textures.
    const size_t texels = (size_t)gridX_ * gridZ_;
    for (size_t t = 0; t < texels; ++t) {
        float w[MAX_LAYERS];
        for (int m = 0; m < NUM_SPLAT_MAPS; ++m) {
            const uint8_t* px = &splat_[(size_t)m * texels * 4 + t * 4];
            for (int c = 0; c < 4; ++c) w[m * 4 + c] = px[c] / 255.0f;
        }
        float out[MAX_LAYERS] = {};
        float sum = 0.0f;
        for (int L = 0; L < MAX_LAYERS; ++L) {
            if (L == layerIndex) continue;                    // dropped layer
            int nl = (L > layerIndex) ? L - 1 : L;            // shifted index
            out[nl] += w[L];
        }
        for (int L = 0; L < MAX_LAYERS; ++L) sum += out[L];
        if (sum > 1e-6f) {
            for (int L = 0; L < MAX_LAYERS; ++L) out[L] /= sum;
        } else {
            out[0] = 1.0f;   // everything was the removed layer -> layer 0
        }
        for (int m = 0; m < NUM_SPLAT_MAPS; ++m) {
            uint8_t* px = &splat_[(size_t)m * texels * 4 + t * 4];
            for (int c = 0; c < 4; ++c)
                px[c] = (uint8_t)std::clamp(out[m * 4 + c] * 255.0f + 0.5f, 0.0f, 255.0f);
        }
    }
    uploadSplat();
}

void Terrain::bindTextures(const Shader& shader) const {
    // Albedo array at unit 0, normal array at unit 1.
    if (albedoArray_) {
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D_ARRAY, albedoArray_);
        shader.setInt("uAlbedo", 0);
    }
    if (normalArray_) {
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D_ARRAY, normalArray_);
        shader.setInt("uNormal", 1);
    }
    // Splat maps at units 2..5.
    for (int m = 0; m < NUM_SPLAT_MAPS; ++m) {
        glActiveTexture(GL_TEXTURE2 + m);
        glBindTexture(GL_TEXTURE_2D, splatTex_[m]);
        char uname[32];
        std::snprintf(uname, sizeof(uname), "uSplat%d", m);
        shader.setInt(uname, 2 + m);
    }
    // Per-layer tile sizes.
    for (int i = 0; i < (int)layers_.size(); ++i) {
        char uname[32];
        std::snprintf(uname, sizeof(uname), "uTileSize[%d]", i);
        shader.setFloat(uname, layers_[i].tileSize);
    }
    shader.setInt("uLayerCount", (int)layers_.size());
    shader.setFloat("uTerrainSize", worldSize_);
    glActiveTexture(GL_TEXTURE0);
}

void Terrain::setHeights(const std::vector<float>& h) {
    if ((int)h.size() != gridX_ * gridZ_) return;
    heights_ = h;
    recomputeAllNormals();
    updateStats();
    uploadVertices(true);
}

void Terrain::setSplat(const std::vector<uint8_t>& s) {
    // Expect planar layout: gridX*gridZ*4*NUM_SPLAT_MAPS bytes.
    if ((int)s.size() != gridX_ * gridZ_ * 4 * NUM_SPLAT_MAPS) return;
    splat_ = s;
    uploadSplat();
}
