#include <doctest/doctest.h>
#include "scene_camera.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>

TEST_CASE("camera rig: add assigns unique, never-reused ids") {
    CameraRig rig;
    SceneCamera c;
    int a = rig.addCamera(c);
    int b = rig.addCamera(c);
    int d = rig.addCamera(c);
    CHECK((a != b && b != d && a != d));
    REQUIRE(rig.removeCamera(b));
    // A removed id is never handed out again (stable key for the game).
    int e = rig.addCamera(c);
    CHECK(e != b);
    CHECK(e != a);
    CHECK(e != d);
    CHECK(rig.cameras().size() == 3);
}

TEST_CASE("camera rig: addCameraWithId keeps the id and bumps the counter") {
    CameraRig rig;
    SceneCamera c;
    c.id = 7;
    rig.addCameraWithId(c);
    CHECK(rig.findCamera(7) != nullptr);
    // The next user-added camera must not collide with the restored id.
    SceneCamera n;
    CHECK(rig.addCamera(n) == 8);
}

TEST_CASE("camera rig: removal fixes the active camera") {
    CameraRig rig;
    SceneCamera c;
    int a = rig.addCamera(c);
    int b = rig.addCamera(c);
    rig.setActive(b);
    CHECK(rig.activeId() == b);
    REQUIRE(rig.removeCamera(b));
    CHECK(rig.activeId() == -1);
    // setActive validates the id.
    rig.setActive(12345);
    CHECK(rig.activeId() == -1);
    rig.setActive(a);
    CHECK(rig.activeId() == a);
    CHECK(rig.active() != nullptr);
}

TEST_CASE("camera rig: clear resets everything") {
    CameraRig rig;
    SceneCamera c;
    rig.addCamera(c);
    rig.addCamera(c);
    rig.setActive(0);
    rig.clear();
    CHECK(rig.cameras().empty());
    CHECK(rig.activeId() == -1);
    // Ids restart from zero in a fresh rig (scene load recreates from file).
    CHECK(rig.addCamera(c) == 0);
}

TEST_CASE("scene camera: view matrix looks at the target") {
    SceneCamera c;
    c.position = glm::vec3(3.0f, 4.0f, 5.0f);
    c.target   = glm::vec3(-1.0f, 0.5f, 2.0f);
    glm::mat4 v = c.viewMatrix();
    // The target maps to the view-space -Z axis (x=y=0, z<0).
    glm::vec4 t = v * glm::vec4(c.target, 1.0f);
    CHECK(t.x == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(t.y == doctest::Approx(0.0f).epsilon(1e-4));
    CHECK(t.z < 0.0f);
    // Camera position maps to the view-space origin.
    glm::vec4 p = v * glm::vec4(c.position, 1.0f);
    CHECK(glm::length(glm::vec3(p)) == doctest::Approx(0.0f).epsilon(1e-4));
}

TEST_CASE("scene camera: degenerate poses stay finite") {
    SceneCamera c;
    c.position = c.target;                 // zero forward
    glm::mat4 v0 = c.viewMatrix();
    CHECK(std::isfinite(v0[3][3]));

    c.target = c.position + glm::vec3(0.0f, 5.0f, 0.0f);   // straight up
    glm::mat4 v1 = c.viewMatrix();
    CHECK(std::isfinite(v1[3][3]));
    glm::vec4 t = v1 * glm::vec4(c.target, 1.0f);
    CHECK(t.z < 0.0f);   // still looking down -Z at the target
}

TEST_CASE("scene camera: projection respects fov and aspect") {
    SceneCamera c;
    c.fov = 90.0f;
    c.nearPlane = 0.5f;
    c.farPlane = 100.0f;
    glm::mat4 p = c.projectionMatrix(2.0f);
    // GLM perspective: [0][0] = 1/(aspect*tan(fov/2)), [1][1] = 1/tan(fov/2).
    CHECK(p[1][1] == doctest::Approx(1.0f).epsilon(1e-4));
    CHECK(p[0][0] == doctest::Approx(0.5f).epsilon(1e-4));
    // Insane values are clamped instead of producing NaN/Inf.
    SceneCamera bad;
    bad.fov = 500.0f;
    bad.nearPlane = -3.0f;
    bad.farPlane = -1.0f;
    glm::mat4 pb = bad.projectionMatrix(0.0f);
    CHECK(std::isfinite(pb[0][0]));
    CHECK(std::isfinite(pb[1][1]));
}
