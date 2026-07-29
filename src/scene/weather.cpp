#include "weather.h"
#include "weather_sys.h"
#include "shader.h"
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

// ---------------------------------------------------------------------------
// WeatherSystem: precipitation particles (rain streaks / snow flakes).
// Particles live in an axis-aligned box wrapped around the view camera, so
// any camera motion keeps the scene "inside" the weather. Rain draws as
// GL_LINES (velocity-aligned streaks), snow as GL_POINTS (soft sprites).
// ---------------------------------------------------------------------------

static float frand() { return float(std::rand()) / float(RAND_MAX); }

void WeatherSystem::create() {
    if (created_) return;
    for (int i = 0; i < kMaxParticles; ++i) {
        Particle& p = particles_[i];
        p.pos = glm::vec3((frand() - 0.5f) * kBoxX,
                          (frand() - 0.5f) * kBoxY,
                          (frand() - 0.5f) * kBoxZ);
        p.speed = 0.6f + 0.8f * frand();    // multiplier on the type's base
        p.phase = frand() * 6.2832f;
    }
    vao_.create();
    vbo_.create();
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * 2 * kMaxParticles,
                 nullptr, GL_STREAM_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3),
                          (void*)0);
    glBindVertexArray(0);
    created_ = true;
}

void WeatherSystem::destroy() {
    vao_.destroy();
    vbo_.destroy();
    created_ = false;
}

// Wrap v into [lo, hi] by modular arithmetic (keeps the box centred on the
// camera even across teleports).
static float wrapRange(float v, float lo, float hi) {
    float span = hi - lo;
    float t = std::fmod(v - lo, span);
    if (t < 0.0f) t += span;
    return lo + t;
}

void WeatherSystem::update(float dt, const WeatherParams& params,
                           const glm::vec3& camPos, float timeSec) {
    if (!created_ || params.precip == WeatherParams::PrecipNone ||
        params.precipIntensity <= 0.0f)
        return;
    anchor_ = camPos;
    glm::vec2 wind = params.windXZ();
    const bool snow = params.precip == WeatherParams::PrecipSnow;
    const float baseFall = snow ? 2.0f : 16.0f;
    // Wind tilts the fall; rain mostly falls, snow drifts a lot.
    const float windK = snow ? 1.0f : 0.35f;

    const int visible = int(params.precipIntensity * kMaxParticles + 0.5f);
    for (int i = 0; i < visible; ++i) {
        Particle& p = particles_[i];
        p.pos.y -= baseFall * p.speed * dt;
        p.pos.x += wind.x * windK * dt;
        p.pos.z += wind.y * windK * dt;
        if (snow) {
            p.pos.x += std::sin(timeSec * 1.3f + p.phase) * 0.6f * dt;
            p.pos.z += std::cos(timeSec * 1.1f + p.phase) * 0.6f * dt;
        }
        p.pos.x = wrapRange(p.pos.x, anchor_.x - kBoxX * 0.5f,
                            anchor_.x + kBoxX * 0.5f);
        p.pos.z = wrapRange(p.pos.z, anchor_.z - kBoxZ * 0.5f,
                            anchor_.z + kBoxZ * 0.5f);
        p.pos.y = wrapRange(p.pos.y, anchor_.y - kBoxY * 0.4f,
                            anchor_.y + kBoxY * 0.6f);
    }
}

void WeatherSystem::render(const Shader& shader, const WeatherParams& params,
                           const glm::mat4& viewProj, int viewportH,
                           float projYScale) const {
    if (!created_ || params.precip == WeatherParams::PrecipNone ||
        params.precipIntensity <= 0.0f)
        return;
    const int visible = int(params.precipIntensity * kMaxParticles + 0.5f);
    if (visible <= 0) return;
    const bool snow = params.precip == WeatherParams::PrecipSnow;
    glm::vec2 wind = params.windXZ();
    const float baseFall = snow ? 2.0f : 16.0f;

    // Upload the visible prefix (rain: 2 verts per drop, snow: 1 per flake).
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    if (snow) {
        std::vector<glm::vec3> verts((size_t)visible);
        for (int i = 0; i < visible; ++i) verts[i] = particles_[i].pos;
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        sizeof(glm::vec3) * verts.size(), verts.data());
    } else {
        std::vector<glm::vec3> verts((size_t)visible * 2);
        for (int i = 0; i < visible; ++i) {
            const Particle& p = particles_[i];
            glm::vec3 vel(wind.x * 0.35f, -baseFall * p.speed, wind.y * 0.35f);
            verts[(size_t)i * 2] = p.pos;
            verts[(size_t)i * 2 + 1] = p.pos - vel * 0.035f;   // streak tail
        }
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        sizeof(glm::vec3) * verts.size(), verts.data());
    }
    glBindBuffer(GL_ARRAY_BUFFER, 0);

    shader.use();
    shader.setMat4("uViewProj", viewProj);
    shader.setInt("uMode", snow ? 0 : 1);
    // Snow: soft white dots; rain: faint blue-grey streaks.
    shader.setVec3("uColor", snow ? glm::vec3(0.95f, 0.97f, 1.0f)
                                  : glm::vec3(0.65f, 0.72f, 0.85f));
    shader.setFloat("uAlpha", snow ? 0.85f : 0.45f);
    // gl_PointSize scaling: size_px = worldSize * h * projYScale / (2 * w).
    float pxPerUnit = float(viewportH) * projYScale * 0.5f;
    shader.setFloat("uPointScale", (snow ? 0.06f : 0.05f) * pxPerUnit);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_FALSE);
    glBindVertexArray(vao_);
    glDrawArrays(snow ? GL_POINTS : GL_LINES, 0,
                 snow ? visible : visible * 2);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
}
