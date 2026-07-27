#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

// Orbit camera: target at origin-ish, rotate around target, zoom via distance.
// Movement is in the world XZ plane (terrain plane).
class Camera {
public:
    Camera();

    void setViewport(int width, int height);

    // Rotate the camera around the target (yaw, pitch deltas in radians).
    void orbit(float deltaYaw, float deltaPitch);
    void pan(float dxScreen, float dyScreen);
    void zoom(float delta);
    void moveTarget(const glm::vec3& worldDelta);

    glm::vec3 position() const { return position_; }
    glm::vec3 target() const { return target_; }
    float distance() const { return distance_; }

    glm::mat4 view() const;
    glm::mat4 projection() const;

    int viewportWidth() const { return viewportWidth_; }
    int viewportHeight() const { return viewportHeight_; }

    // Build a ray in world space from screen pixel coordinates.
    // Returns origin and direction.
    void screenToRay(float screenX, float screenY, glm::vec3& outOrigin, glm::vec3& outDir) const;

    // Reposition freely (used when clicking terrain to follow etc.)
    void setTarget(const glm::vec3& t) { target_ = t; }

private:
    void updatePosition();

    glm::vec3 target_   = glm::vec3(0.0f, 0.0f, 0.0f);
    float distance_     = 60.0f;
    float yaw_          = -0.6f;   // radians
    float pitch_        = 0.6f;    // radians (down from above)

    float minDistance_  = 8.0f;
    float maxDistance_  = 400.0f;
    float minPitch_     = 0.05f;
    float maxPitch_     = 1.55f;

    glm::vec3 position_ = glm::vec3(0.0f);

    int viewportWidth_  = 1280;
    int viewportHeight_ = 720;
    float fov_          = 45.0f;
    float nearPlane_    = 0.1f;
    float farPlane_     = 2000.0f;
};
