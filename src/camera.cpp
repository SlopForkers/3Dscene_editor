#include "camera.h"
#include <algorithm>

Camera::Camera() {
    updatePosition();
}

void Camera::setViewport(int width, int height) {
    viewportWidth_ = width;
    viewportHeight_ = height;
}

void Camera::updatePosition() {
    float cp = std::cos(pitch_);
    float sp = std::sin(pitch_);
    float cy = std::cos(yaw_);
    float sy = std::sin(yaw_);

    // Spherical -> cartesian offset from target.
    glm::vec3 offset(
        distance_ * cp * sy,
        distance_ * sp,
        distance_ * cp * cy
    );
    position_ = target_ + offset;
}

void Camera::orbit(float deltaYaw, float deltaPitch) {
    yaw_   += deltaYaw;
    pitch_ += deltaPitch;
    pitch_ = std::clamp(pitch_, minPitch_, maxPitch_);
    updatePosition();
}

void Camera::pan(float dxScreen, float dyScreen) {
    // Move the target along the camera's right and up vectors, scaled by distance.
    glm::vec3 forward = glm::normalize(target_ - position_);
    glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
    glm::vec3 up    = glm::normalize(glm::cross(right, forward));

    float scale = distance_ * 0.0015f;
    glm::vec3 delta = right * (-dxScreen * scale) + up * (dyScreen * scale);
    target_ += delta;
    updatePosition();
}

void Camera::zoom(float delta) {
    distance_ *= (1.0f + delta);
    distance_ = std::clamp(distance_, minDistance_, maxDistance_);
    updatePosition();
}

void Camera::moveTarget(const glm::vec3& worldDelta) {
    target_ += worldDelta;
    updatePosition();
}

glm::mat4 Camera::view() const {
    return glm::lookAt(position_, target_, glm::vec3(0.0f, 1.0f, 0.0f));
}

glm::mat4 Camera::projection() const {
    float aspect = (viewportHeight_ != 0) ? float(viewportWidth_) / float(viewportHeight_) : 1.0f;
    return glm::perspective(glm::radians(fov_), aspect, nearPlane_, farPlane_);
}

void Camera::screenToRay(float screenX, float screenY, glm::vec3& outOrigin, glm::vec3& outDir) const {
    // Normalised device coordinates (y flipped).
    float x = (2.0f * screenX) / float(viewportWidth_) - 1.0f;
    float y = 1.0f - (2.0f * screenY) / float(viewportHeight_);
    glm::vec4 rayClip(x, y, -1.0f, 1.0f);

    glm::mat4 projM = projection();
    glm::mat4 viewM = view();

    glm::vec4 rayEye = glm::inverse(projM) * rayClip;
    rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0f, 0.0f);
    glm::vec3 rayWorld = glm::normalize(glm::vec3(glm::inverse(viewM) * rayEye));

    outOrigin = position_;
    outDir = rayWorld;
}
