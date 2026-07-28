#include "gizmo.h"
#include "camera.h"
#include "shader.h"
#include "input.h"
#include <imgui.h>
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <array>
#include <vector>

static const float PI_F = 3.14159265358979323846f;

Gizmo::~Gizmo() { destroy(); }

void Gizmo::create() {
    // Unit line along +X.
    float lineVerts[] = { 0,0,0, 1,0,0 };
    vaoLine_.create();
    vboLine_.create();
    glBindVertexArray(vaoLine_);
    glBindBuffer(GL_ARRAY_BUFFER, vboLine_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(lineVerts), lineVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);

    // Unit ring in XY plane (normal +Z), 64 segments, closed (65 verts).
    std::vector<glm::vec3> ring(65);
    for (int i = 0; i <= 64; ++i) {
        float a = float(i) / 64.0f * 2.0f * PI_F;
        ring[i] = glm::vec3(std::cos(a), std::sin(a), 0.0f);
    }
    vaoRing_.create();
    vboRing_.create();
    glBindVertexArray(vaoRing_);
    glBindBuffer(GL_ARRAY_BUFFER, vboRing_);
    glBufferData(GL_ARRAY_BUFFER, ring.size() * sizeof(glm::vec3), ring.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glBindVertexArray(0);

    // Unit cube wireframe [0,1]^3.
    static const float cube[24][3] = {
        {0,0,0},{1,0,0}, {1,0,0},{1,1,0}, {1,1,0},{0,1,0}, {0,1,0},{0,0,0},
        {0,0,1},{1,0,1}, {1,0,1},{1,1,1}, {1,1,1},{0,1,1}, {0,1,1},{0,0,1},
        {0,0,0},{0,0,1}, {1,0,0},{1,0,1}, {1,1,0},{1,1,1}, {0,1,0},{0,1,1},
    };
    vaoCube_.create();
    vboCube_.create();
    glBindVertexArray(vaoCube_);
    glBindBuffer(GL_ARRAY_BUFFER, vboCube_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(cube), cube, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void Gizmo::destroy() {
    vaoLine_.destroy();
    vboLine_.destroy();
    vaoRing_.destroy();
    vboRing_.destroy();
    vaoCube_.destroy();
    vboCube_.destroy();
}

float Gizmo::worldSize(const Camera& cam, const glm::vec3& pos) const {
    float dist = glm::length(cam.position() - pos);
    // world height at distance = 2 * dist * tan(fov/2); scale to pixel fraction.
    return 2.0f * dist * tanf(glm::radians(cam.fov()) * 0.5f) *
           (pixelSize_ / float(cam.viewportHeight()));
}

glm::vec3 Gizmo::axisDir(int axis) const {
    return axis == 0 ? glm::vec3(1,0,0) :
           axis == 1 ? glm::vec3(0,1,0) :
                       glm::vec3(0,0,1);
}

glm::vec3 Gizmo::planeBasisU(int axis) const {
    // A second vector perpendicular to the axis, stable per axis.
    glm::vec3 a = axisDir(axis);
    glm::vec3 ref = std::abs(a.y) < 0.9f ? glm::vec3(0,1,0) : glm::vec3(1,0,0);
    return glm::normalize(glm::cross(a, ref));
}

glm::vec3 Gizmo::planeBasisV(int axis) const {
    return glm::normalize(glm::cross(axisDir(axis), planeBasisU(axis)));
}

glm::mat4 Gizmo::axisRotation(int axis) const {
    // Align the +X line geometry to the requested world axis.
    if (axis == 0) return glm::mat4(1.0f);                  // X
    if (axis == 1) return glm::rotate(glm::mat4(1.0f),  PI_F * 0.5f, glm::vec3(0,0,1)); // X->Y
    return glm::rotate(glm::mat4(1.0f), -PI_F * 0.5f, glm::vec3(0,1,0));               // X->Z
}

glm::mat4 Gizmo::ringRotation(int axis) const {
    // Ring normal is +Z; rotate so the normal aligns to the requested axis.
    if (axis == 0) return glm::rotate(glm::mat4(1.0f),  PI_F * 0.5f, glm::vec3(0,1,0)); // Z->X
    if (axis == 1) return glm::rotate(glm::mat4(1.0f), -PI_F * 0.5f, glm::vec3(1,0,0)); // Z->Y
    return glm::mat4(1.0f);                                  // Z
}

glm::vec3 Gizmo::axisColor(int axis, bool active, bool hover) const {
    glm::vec3 base = axis == 0 ? glm::vec3(0.95f, 0.25f, 0.20f) :
                     axis == 1 ? glm::vec3(0.30f, 0.85f, 0.35f) :
                                 glm::vec3(0.25f, 0.50f, 0.95f);
    if (active) return glm::vec3(1.0f, 0.95f, 0.20f);   // yellow while dragged
    if (hover)  return glm::vec3(1.0f, 1.0f, 1.0f);     // white on hover
    return base;
}

// Ray vs finite segment (axis ray). Returns distance between the closest points
// and the parameter along the axis (t2 in [0,size]).
static bool raySegment(const glm::vec3& ro, const glm::vec3& rd,
                       const glm::vec3& segP, const glm::vec3& segD, float segLen,
                       float& outDist, float& outT2) {
    glm::vec3 w0 = ro - segP;
    float a = glm::dot(rd, rd);
    float b = glm::dot(rd, segD);
    float c = glm::dot(segD, segD);
    float d = glm::dot(rd, w0);
    float e = glm::dot(segD, w0);
    float denom = a * c - b * b;
    if (std::abs(denom) < 1e-8f) return false;
    float t1 = (b * e - c * d) / denom;
    float t2 = (a * e - b * d) / denom;
    if (t2 < 0.0f || t2 > segLen) return false;
    glm::vec3 p1 = ro + rd * t1;
    glm::vec3 p2 = segP + segD * t2;
    outDist = glm::length(p1 - p2);
    outT2 = t2;
    return true;
}

// Ray vs plane. Returns hit parameter t (>=0).
static bool rayPlane(const glm::vec3& ro, const glm::vec3& rd,
                     const glm::vec3& planePoint, const glm::vec3& planeNormal,
                     glm::vec3& outHit) {
    float denom = glm::dot(rd, planeNormal);
    if (std::abs(denom) < 1e-8f) return false;
    float t = glm::dot(planePoint - ro, planeNormal) / denom;
    if (t < 0.0f) return false;
    outHit = ro + rd * t;
    return true;
}

void Gizmo::mouseRay(const Camera& cam, glm::vec3& outOrigin, glm::vec3& outDir) const {
    float sx = (float)g_input.mouseX() * dpiScaleX_;
    float sy = (float)g_input.mouseY() * dpiScaleY_;
    cam.screenToRay(sx, sy, outOrigin, outDir);
    outDir = glm::normalize(outDir);
}

bool Gizmo::pickAxis(const Camera& cam, const glm::vec3& pos, float size, int& outAxis) {
    glm::vec3 ro, rd;
    mouseRay(cam, ro, rd);

    float tol = size * 0.12f;
    float bestDist = 1e30f;
    int best = -1;
    for (int a = 0; a < 3; ++a) {
        glm::vec3 dir = axisDir(a);
        float dist, t;
        if (raySegment(ro, rd, pos, dir, size, dist, t) && dist < tol && dist < bestDist) {
            bestDist = dist;
            best = a;
        }
    }
    outAxis = best;
    return best >= 0;
}

bool Gizmo::pickRing(const Camera& cam, const glm::vec3& pos, float size, int& outAxis) {
    glm::vec3 ro, rd;
    mouseRay(cam, ro, rd);

    float tol = size * 0.12f;
    float bestDist = 1e30f;
    int best = -1;
    for (int a = 0; a < 3; ++a) {
        glm::vec3 n = axisDir(a);
        glm::vec3 hit;
        if (!rayPlane(ro, rd, pos, n, hit)) continue;
        float r = glm::length(hit - pos);
        if (std::abs(r - size) < tol) {
            float d = std::abs(r - size);
            if (d < bestDist) { bestDist = d; best = a; }
        }
    }
    outAxis = best;
    return best >= 0;
}

bool Gizmo::handleInput(const Camera& cam, const glm::vec3& pos,
                        const Transform& current, Transform& outNew,
                        const ImGuiIO& io) {
    outNew = current;
    bool overUI = io.WantCaptureMouse;

    // End any active drag on release.
    if (dragging_ && g_input.mouseReleased(Input::Left)) {
        dragging_ = false;
        activeAxis_ = -1;
        return true;
    }

    if (dragging_) {
        float size = worldSize(cam, pos);
        glm::vec3 ro, rd;
        mouseRay(cam, ro, rd);

        if (mode_ == Translate || mode_ == Scale) {
            // Plane through the axis, facing the camera. Normal = camera dir
            // with the axis component removed (so the plane contains the axis).
            glm::vec3 axis = axisDir(activeAxis_);
            glm::vec3 camDir = glm::normalize(cam.position() - pos);
            glm::vec3 n = camDir - axis * glm::dot(camDir, axis);
            // Camera exactly on the axis line -> degenerate normal; fall back
            // to any stable plane containing the axis (avoids NaN).
            if (glm::dot(n, n) < 1e-8f) n = planeBasisU(activeAxis_);
            else n = glm::normalize(n);
            glm::vec3 hit;
            if (!rayPlane(ro, rd, pos, n, hit)) return true;
            float curT = glm::dot(hit - pos, axis);
            float delta = curT - startT_;

            if (mode_ == Translate) {
                outNew.position = startTransform_.position + axis * delta;
            } else { // Scale: uniform scale driven by motion along the axis.
                float s = 1.0f + delta / size;
                s = std::max(0.01f, s);
                outNew.scale = startTransform_.scale * s;
            }
            return true;
        }

        if (mode_ == Rotate) {
            glm::vec3 axis = axisDir(activeAxis_);
            glm::vec3 hit;
            if (!rayPlane(ro, rd, pos, axis, hit)) return true;
            glm::vec3 u = planeBasisU(activeAxis_);
            glm::vec3 v = planeBasisV(activeAxis_);
            glm::vec3 rel = hit - pos;
            float ang = std::atan2(glm::dot(rel, v), glm::dot(rel, u));
            float delta = ang - startAngle_;
            // Apply delta to the matching Euler component.
            glm::vec3 rot = startTransform_.rotationEuler;
            if (activeAxis_ == 0) rot.x += delta;
            else if (activeAxis_ == 1) rot.y += delta;
            else rot.z += delta;
            outNew.rotationEuler = rot;
            return true;
        }
        return true;
    }

    // Not dragging: pick on press, hover otherwise.
    if (!overUI && g_input.mousePressed(Input::Left)) {
        float size = worldSize(cam, pos);
        int picked = -1;
        bool hit = (mode_ == Rotate) ? pickRing(cam, pos, size, picked)
                                      : pickAxis(cam, pos, size, picked);
        if (hit && picked >= 0) {
            activeAxis_ = picked;
            dragging_ = true;
            startTransform_ = current;

            // Record the initial interaction point.
            glm::vec3 ro, rd;
            mouseRay(cam, ro, rd);
            if (mode_ == Rotate) {
                glm::vec3 hitPt;
                if (rayPlane(ro, rd, pos, axisDir(picked), hitPt)) {
                    glm::vec3 u = planeBasisU(picked);
                    glm::vec3 v = planeBasisV(picked);
                    glm::vec3 rel = hitPt - pos;
                    startAngle_ = std::atan2(glm::dot(rel, v), glm::dot(rel, u));
                }
            } else {
                glm::vec3 axis = axisDir(picked);
                glm::vec3 camDir = glm::normalize(cam.position() - pos);
                glm::vec3 n = camDir - axis * glm::dot(camDir, axis);
                if (glm::dot(n, n) < 1e-8f) n = planeBasisU(picked);
                else n = glm::normalize(n);
                glm::vec3 hitPt;
                if (rayPlane(ro, rd, pos, n, hitPt))
                    startT_ = glm::dot(hitPt - pos, axis);
                else
                    startT_ = 0.0f;
            }
            return true;
        }
    }

    // Hover (no button) for visual feedback.
    if (!overUI && !g_input.mouseDown(Input::Left)) {
        float size = worldSize(cam, pos);
        int h = -1;
        if (mode_ == Rotate) pickRing(cam, pos, size, h);
        else                  pickAxis(cam, pos, size, h);
        hoverAxis_ = h;
    } else {
        hoverAxis_ = -1;
    }
    return false;
}

void Gizmo::draw(const Camera& cam, const glm::vec3& pos, const Shader& lineShader) {
    float size = worldSize(cam, pos);
    glm::mat4 vp = cam.projection() * cam.view();

    lineShader.use();
    lineShader.setFloat("uAlpha", 1.0f);
    glLineWidth(3.0f);

    if (mode_ == Rotate) {
        glBindVertexArray(vaoRing_);
        for (int a = 0; a < 3; ++a) {
            glm::vec3 col = axisColor(a, dragging_ && activeAxis_ == a,
                                         hoverAxis_ == a);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), pos) *
                              ringRotation(a) *
                              glm::scale(glm::mat4(1.0f), glm::vec3(size));
            lineShader.setMat4("uViewProj", vp * model);
            lineShader.setVec3("uColor", col);
            glDrawArrays(GL_LINE_STRIP, 0, 65);
        }
        glBindVertexArray(0);
    } else {
        // Translate or Scale: 3 axis lines.
        glBindVertexArray(vaoLine_);
        for (int a = 0; a < 3; ++a) {
            glm::vec3 col = axisColor(a, dragging_ && activeAxis_ == a,
                                         hoverAxis_ == a);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), pos) *
                              axisRotation(a) *
                              glm::scale(glm::mat4(1.0f), glm::vec3(size));
            lineShader.setMat4("uViewProj", vp * model);
            lineShader.setVec3("uColor", col);
            glDrawArrays(GL_LINES, 0, 2);
        }
        glBindVertexArray(0);

        if (mode_ == Scale) {
            // Small cube at the end of each axis line.
            glBindVertexArray(vaoCube_);
            float cubeSize = size * 0.12f;
            for (int a = 0; a < 3; ++a) {
                glm::vec3 col = axisColor(a, dragging_ && activeAxis_ == a,
                                             hoverAxis_ == a);
                glm::mat4 model = glm::translate(glm::mat4(1.0f), pos) *
                                  axisRotation(a) *
                                  glm::translate(glm::mat4(1.0f), glm::vec3(size, 0, 0)) *
                                  glm::scale(glm::mat4(1.0f), glm::vec3(cubeSize));
                lineShader.setMat4("uViewProj", vp * model);
                lineShader.setVec3("uColor", col);
                glDrawArrays(GL_LINES, 0, 24);
            }
            glBindVertexArray(0);
        } else {
            // Translate: a small cube at the origin (centre handle).
            glBindVertexArray(vaoCube_);
            float cubeSize = size * 0.08f;
            glm::mat4 model = glm::translate(glm::mat4(1.0f), pos) *
                              glm::scale(glm::mat4(1.0f), glm::vec3(cubeSize)) *
                              glm::translate(glm::mat4(1.0f), glm::vec3(-0.5f));
            lineShader.setMat4("uViewProj", vp * model);
            lineShader.setVec3("uColor", glm::vec3(0.9f, 0.9f, 0.9f));
            glDrawArrays(GL_LINES, 0, 24);
            glBindVertexArray(0);
        }
    }

    glLineWidth(1.0f);
}
