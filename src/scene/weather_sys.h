#pragma once
#include "weather.h"
#include "gl_resource.h"
#include <glm/glm.hpp>

class Shader;

// Precipitation particle system (rain streaks / snow flakes) rendered as a
// camera-wrapped particle box. Params live in `params` (persisted in the
// scene file); fog is applied by the terrain/prop/block shaders through
// uniforms, not here.
class WeatherSystem {
public:
    void create();
    void destroy();

    // Advance particles around the camera. timeSec drives snow sway.
    void update(float dt, const WeatherParams& params,
                const glm::vec3& camPos, float timeSec);
    // Draw precipitation (blend state is restored on exit). projYScale is
    // proj[1][1] of the projection matrix (= 1/tan(fovY/2)).
    void render(const Shader& shader, const WeatherParams& params,
                const glm::mat4& viewProj, int viewportH,
                float projYScale) const;

    WeatherParams params;

private:
    struct Particle {
        glm::vec3 pos = glm::vec3(0.0f);
        float speed = 1.0f;    // multiplier on the type's base fall speed
        float phase = 0.0f;    // snow sway phase
    };
    static constexpr int kMaxParticles = 4096;
    static constexpr float kBoxX = 44.0f;
    static constexpr float kBoxY = 26.0f;
    static constexpr float kBoxZ = 44.0f;

    Particle particles_[kMaxParticles];
    GlVertexArray vao_;
    GlBuffer vbo_;
    bool created_ = false;
    glm::vec3 anchor_ = glm::vec3(0.0f);
};
