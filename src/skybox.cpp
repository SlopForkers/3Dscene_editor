#include "skybox.h"
#include "shader.h"
#include <stb_image.h>
#include <glm/glm.hpp>
#include <iostream>
#include <cmath>
#include <algorithm>

namespace {

// Cube vertices for the skybox mesh. Extent is larger than strictly required
// so the projected cube reliably covers the screen for typical FOVs (the
// xyww trick in the vertex shader pins depth at the far plane regardless).
constexpr float kExtent = 10.0f;
const float kCubeVerts[36][3] = {
    // +X
    { kExtent,-kExtent, kExtent}, { kExtent,-kExtent,-kExtent}, { kExtent, kExtent,-kExtent},
    { kExtent,-kExtent, kExtent}, { kExtent, kExtent,-kExtent}, { kExtent, kExtent, kExtent},
    // -X
    {-kExtent,-kExtent,-kExtent}, {-kExtent,-kExtent, kExtent}, {-kExtent, kExtent, kExtent},
    {-kExtent,-kExtent,-kExtent}, {-kExtent, kExtent, kExtent}, {-kExtent, kExtent,-kExtent},
    // +Y
    {-kExtent, kExtent, kExtent}, { kExtent, kExtent, kExtent}, { kExtent, kExtent,-kExtent},
    {-kExtent, kExtent, kExtent}, { kExtent, kExtent,-kExtent}, {-kExtent, kExtent,-kExtent},
    // -Y
    {-kExtent,-kExtent,-kExtent}, { kExtent,-kExtent,-kExtent}, { kExtent,-kExtent, kExtent},
    {-kExtent,-kExtent,-kExtent}, { kExtent,-kExtent, kExtent}, {-kExtent,-kExtent, kExtent},
    // +Z
    {-kExtent,-kExtent, kExtent}, { kExtent,-kExtent, kExtent}, { kExtent, kExtent, kExtent},
    {-kExtent,-kExtent, kExtent}, { kExtent, kExtent, kExtent}, {-kExtent, kExtent, kExtent},
    // -Z
    { kExtent,-kExtent,-kExtent}, {-kExtent,-kExtent,-kExtent}, {-kExtent, kExtent,-kExtent},
    { kExtent,-kExtent,-kExtent}, {-kExtent, kExtent,-kExtent}, { kExtent, kExtent,-kExtent},
};

// Fullscreen quad (two triangles) in clip-space coords for equirect conversion.
const float kQuadVerts[6][2] = {
    {-1.0f, -1.0f}, { 1.0f, -1.0f}, { 1.0f,  1.0f},
    {-1.0f, -1.0f}, { 1.0f,  1.0f}, {-1.0f,  1.0f},
};

} // namespace

Skybox::~Skybox() {
    destroy();
}

bool Skybox::create() {
    // Skybox cube.
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kCubeVerts), kCubeVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    // Fullscreen quad for equirect -> cubemap conversion.
    glGenVertexArrays(1, &quadVao_);
    glGenBuffers(1, &quadVbo_);
    glBindVertexArray(quadVao_);
    glBindBuffer(GL_ARRAY_BUFFER, quadVbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVerts), kQuadVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    faceSize_ = 256;
    imported_ = false;
    importedPath_.clear();
    allocateCubemap(faceSize_);
    for (int i = 0; i < 6; ++i) uploadProceduralFace(i, faceSize_);
    return true;
}

void Skybox::destroy() {
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
    if (quadVao_) { glDeleteVertexArrays(1, &quadVao_); quadVao_ = 0; }
    if (quadVbo_) { glDeleteBuffers(1, &quadVbo_); quadVbo_ = 0; }
    if (tex_) { glDeleteTextures(1, &tex_); tex_ = 0; }
}

void Skybox::allocateCubemap(int size) {
    if (tex_) { glDeleteTextures(1, &tex_); tex_ = 0; }
    glGenTextures(1, &tex_);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex_);
    for (int i = 0; i < 6; ++i) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB16F,
                     size, size, 0, GL_RGB, GL_FLOAT, nullptr);
    }
    setParams();
    faceSize_ = size;
}

void Skybox::setParams() {
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex_);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, 0);
}

void Skybox::resetToDefault() {
    imported_ = false;
    importedPath_.clear();
    if (!tex_) {
        faceSize_ = 256;
        allocateCubemap(faceSize_);
    }
    for (int i = 0; i < 6; ++i) uploadProceduralFace(i, faceSize_);
}

bool Skybox::loadEquirect(Shader& convertShader, const std::string& path) {
    // stbi_loadf handles both HDR (.hdr/.exr) and LDR images, returning float
    // in 0..1 for LDR and the full HDR range for .hdr. Flip vertically so the
    // resulting OpenGL texture has v=0 at the south pole (matching the
    // latitude formula used in the convert shader).
    stbi_set_flip_vertically_on_load(true);
    int w = 0, h = 0, c = 0;
    float* data = stbi_loadf(path.c_str(), &w, &h, &c, 3);
    stbi_set_flip_vertically_on_load(false);
    if (!data) {
        std::cerr << "Skybox: failed to load equirectangular image: " << path << "\n";
        return false;
    }

    // Temporarily upload the panorama to a 2D texture for GPU sampling.
    GLuint eqTex = 0;
    glGenTextures(1, &eqTex);
    glBindTexture(GL_TEXTURE_2D, eqTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB16F, w, h, 0, GL_RGB, GL_FLOAT, data);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    stbi_image_free(data);

    // Choose a cubemap face resolution from the panorama's height, capped to
    // keep memory and conversion time reasonable.
    const int faceSize = std::min(1024, std::max(64, h));
    allocateCubemap(faceSize);

    // Render each cubemap face by rasterising a fullscreen quad whose
    // fragment shader maps the face texel to a direction and samples the
    // equirectangular panorama.
    GLuint fbo = 0;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);

    GLint savedViewport[4] = {};
    glGetIntegerv(GL_VIEWPORT, savedViewport);

    convertShader.use();
    convertShader.setInt("uEquirect", 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, eqTex);

    glBindVertexArray(quadVao_);
    glViewport(0, 0, faceSize, faceSize);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    for (int face = 0; face < 6; ++face) {
        convertShader.setInt("uFace", face);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, tex_, 0);
        glClear(GL_COLOR_BUFFER_BIT);
        glDrawArrays(GL_TRIANGLES, 0, 6);
    }

    // Restore state.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);
    glDeleteTextures(1, &eqTex);
    glViewport(savedViewport[0], savedViewport[1], savedViewport[2], savedViewport[3]);
    glEnable(GL_DEPTH_TEST);

    imported_ = true;
    importedPath_ = path;
    return true;
}

void Skybox::uploadProceduralFace(int face, int size) {
    std::vector<float> buf;
    genFaceProcedural(face, size, buf);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex_);
    glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0,
                    0, 0, size, size, GL_RGB, GL_FLOAT, buf.data());
}

void Skybox::genFaceProcedural(int face, int size, std::vector<float>& buf) {
    buf.resize((size_t)size * size * 3);

    // Vertical gradient sky: ground -> horizon -> sky-top, keyed by the
    // world-space y of the sampled direction so faces blend seamlessly.
    const glm::vec3 skyTop(0.25f, 0.50f, 0.85f);
    const glm::vec3 horizon(0.72f, 0.78f, 0.85f);
    const glm::vec3 ground(0.38f, 0.34f, 0.28f);

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            const float tx = 2.0f * (x + 0.5f) / size - 1.0f;
            const float ty = 2.0f * (y + 0.5f) / size - 1.0f;
            glm::vec3 dir(0.0f);
            switch (face) {
                case 0: dir = glm::vec3( 1.0f, -ty,  -tx); break; // +X
                case 1: dir = glm::vec3(-1.0f, -ty,   tx); break; // -X
                case 2: dir = glm::vec3( tx,   1.0f, -ty); break; // +Y
                case 3: dir = glm::vec3( tx,  -1.0f,  ty); break; // -Y
                case 4: dir = glm::vec3( tx,  -ty,   1.0f); break; // +Z
                case 5: dir = glm::vec3(-tx,  -ty,  -1.0f); break; // -Z
            }
            dir = glm::normalize(dir);
            float t = glm::clamp((dir.y + 0.2f) / 0.6f, 0.0f, 1.0f);
            glm::vec3 col;
            if (t < 0.5f) col = glm::mix(ground,  horizon, t * 2.0f);
            else          col = glm::mix(horizon, skyTop, (t - 0.5f) * 2.0f);

            const size_t idx = (size_t)y * size * 3 + (size_t)x * 3;
            buf[idx + 0] = col.r;
            buf[idx + 1] = col.g;
            buf[idx + 2] = col.b;
        }
    }
}

void Skybox::draw(Shader& shader, const glm::mat4& viewProj, float exposure) {
    if (!tex_ || !vao_) return;
    shader.use();
    shader.setMat4("uViewProj", viewProj);
    shader.setFloat("uExposure", exposure);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, tex_);
    shader.setInt("uSky", 0);
    glBindVertexArray(vao_);
    glDrawArrays(GL_TRIANGLES, 0, 36);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
}
