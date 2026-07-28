#include "vertex_edit.h"
#include "camera.h"
#include "terrain.h"
#include "shader.h"
#include "input.h"
#include <glm/gtc/matrix_transform.hpp>
#include <cmath>
#include <algorithm>

namespace { const float PI_F = 3.14159265359f; }

// Ray vs finite segment. Returns distance between closest points and the
// parameter t2 along the segment (in [0, segLen]).
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

VertexEditor::~VertexEditor() { destroy(); }

void VertexEditor::create() {
    // Unit line (0,0,0)->(1,0,0) for gizmo axes.
    static const float lineVerts[2][3] = { {0,0,0}, {1,0,0} };
    vaoLine_.create();
    vboLine_.create();
    glBindVertexArray(vaoLine_);
    glBindBuffer(GL_ARRAY_BUFFER, vboLine_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(lineVerts), lineVerts, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (void*)0);
    glBindVertexArray(0);

    vaoPoint_.create();
    vboPoint_.create();
    glBindVertexArray(vaoPoint_);
    glBindBuffer(GL_ARRAY_BUFFER, vboPoint_);
    glBufferData(GL_ARRAY_BUFFER, sizeof(glm::vec3) * 16, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glBindVertexArray(0);
}

void VertexEditor::destroy() {
    vaoLine_.destroy();
    vboLine_.destroy();
    vaoPoint_.destroy();
    vboPoint_.destroy();
}

void VertexEditor::clearSelection() {
    selected_.clear();
    center_ = glm::vec3(0.0f);
    normal_ = glm::vec3(0.0f, 1.0f, 0.0f);
}

float VertexEditor::worldSize(const Camera& cam, const glm::vec3& pos) const {
    float dist = glm::length(cam.position() - pos);
    return 2.0f * dist * std::tan(glm::radians(cam.fov()) * 0.5f) *
           (90.0f / float(cam.viewportHeight()));
}

void VertexEditor::mouseRay(const Camera& cam, glm::vec3& outOrigin, glm::vec3& outDir) const {
    float sx = (float)g_input.mouseX() * dpiScaleX_;
    float sy = (float)g_input.mouseY() * dpiScaleY_;
    cam.screenToRay(sx, sy, outOrigin, outDir);
    outDir = glm::normalize(outDir);
}

glm::vec3 VertexEditor::axisDir(int axis, const glm::vec3& n) const {
    if (axis == 0) return glm::vec3(1, 0, 0);
    if (axis == 1) return glm::vec3(0, 1, 0);
    if (axis == 2) return glm::vec3(0, 0, 1);
    return glm::normalize(n);
}

bool VertexEditor::pickAxis(const Camera& cam, const glm::vec3& pos,
                              const glm::vec3& n, float size, int& outAxis) {
    glm::vec3 ro, rd;
    mouseRay(cam, ro, rd);

    float tol = size * 0.12f;
    float bestDist = 1e30f;
    int best = -1;
    if (dragMode_ == FreeXYZ) {
        for (int a = 0; a < 3; ++a) {
            glm::vec3 dir = axisDir(a, n);
            float dist, t;
            if (raySegment(ro, rd, pos, dir, size, dist, t) && dist < tol && dist < bestDist) {
                bestDist = dist; best = a;
            }
        }
    } else if (dragMode_ == Vertical) {
        glm::vec3 dir = axisDir(1, n);
        float dist, t;
        if (raySegment(ro, rd, pos, dir, size, dist, t) && dist < tol)
            best = 1;
    } else { // Normal
        glm::vec3 dir = axisDir(3, n);
        float dist, t;
        if (raySegment(ro, rd, pos, dir, size, dist, t) && dist < tol)
            best = 3;
    }
    outAxis = best;
    return best >= 0;
}

glm::vec3 VertexEditor::rayPlaneHit(const glm::vec3& ro, const glm::vec3& rd,
                                     const glm::vec3& planePoint,
                                     const glm::vec3& planeNormal) const {
    float denom = glm::dot(rd, planeNormal);
    if (std::abs(denom) < 1e-8f) return planePoint;
    float t = glm::dot(planePoint - ro, planeNormal) / denom;
    if (t < 0.0f) return planePoint;
    return ro + rd * t;
}

void VertexEditor::recomputeCenter(const Terrain& terrain) {
    if (selected_.empty()) return;
    glm::vec3 sumPos(0.0f), sumN(0.0f);
    for (const auto& c : selected_) {
        sumPos += terrain.vertexPos(c.ix, c.iz);
        sumN   += terrain.vertexNormal(c.ix, c.iz);
    }
    center_ = sumPos / float(selected_.size());
    normal_ = glm::normalize(sumN);
}

static float falloffWeight(float dist, float radius, int mode) {
    if (dist >= radius) return 0.0f;
    float t = dist / radius;
    switch (mode) {
        case 2: return 1.0f;                       // Constant
        case 1: return 1.0f - t;                  // Linear
        case 0: default: { float s = 1.0f - t; return s * s * (3.0f - 2.0f * s); } // Smooth
    }
}

bool VertexEditor::handleInput(const Camera& cam, Terrain& terrain,
                                float radius, int falloff,
                                const ImGuiIO& /*io*/, bool overUI) {
    const bool ctrl = g_input.keyDown(GLFW_KEY_LEFT_CONTROL) ||
                      g_input.keyDown(GLFW_KEY_RIGHT_CONTROL);

    // Hover feedback for the gizmo axes (no button held, pointer over scene).
    if (!dragging_ && hasSelection() && !overUI && !g_input.mouseDown(Input::Left)) {
        float size = worldSize(cam, center_);
        pickAxis(cam, center_, normal_, size, hoverAxis_);
    } else if (dragging_) {
        hoverAxis_ = activeAxis_;
    } else {
        hoverAxis_ = -1;
    }

    if (dragging_) {
        if (!g_input.mouseDown(Input::Left)) {
            dragging_ = false;
            activeAxis_ = -1;
            return false;
        }
        // The drag plane must CONTAIN the axis (so motion along the axis stays
        // in the plane) and face the camera (so the ray reliably intersects).
        // Plane normal = camera-forward component perpendicular to the axis.
        glm::vec3 aDir = axisDir(activeAxis_, normal_);
        glm::vec3 ro, rd;
        mouseRay(cam, ro, rd);
        glm::vec3 camFwd = -rd;
        glm::vec3 perp = glm::cross(camFwd, aDir);
        if (glm::dot(perp, perp) < 1e-8f) perp = glm::cross(aDir, glm::vec3(0, 1, 0));
        glm::vec3 planeN = glm::normalize(glm::cross(aDir, perp));
        // Delta is measured from the PRESS-TIME ray/plane hit, not from the
        // selection centre — otherwise vertices jump on the first drag frame
        // by however far off-centre the axis was grabbed.
        glm::vec3 hit = rayPlaneHit(ro, rd, dragStartHit_, planeN);
        glm::vec3 delta = hit - dragStartHit_;
        float scalar = glm::dot(delta, aDir);

        float heightDelta;
        if (activeAxis_ == 3) { // Normal
            float ny = std::max(std::abs(normal_.y), 0.1f);
            heightDelta = scalar / ny;
        } else {
            // A heightfield vertex can only move vertically: horizontal axes
            // contribute through their Y component only (X/Z -> no-op).
            heightDelta = scalar * glm::abs(aDir.y);
            if (glm::abs(aDir.y) < 1e-3f) heightDelta = 0.0f;
        }

        // Apply to every vertex in the saved box using its baseline height.
        int w = boxX1_ - boxX0_ + 1;
        for (int iz = boxZ0_; iz <= boxZ1_; ++iz) {
            for (int ix = boxX0_; ix <= boxX1_; ++ix) {
                glm::vec3 vp = terrain.vertexPos(ix, iz);
                float dx = vp.x - dragStartWorld_.x;
                float dz = vp.z - dragStartWorld_.z;
                float dist = std::sqrt(dx * dx + dz * dz);
                float wgt = falloffWeight(dist, radius, falloff);
                if (wgt <= 0.0f) continue;
                int bi = (iz - boxZ0_) * w + (ix - boxX0_);
                float baseH = startHeights_[bi];
                float newH = baseH + heightDelta * wgt;
                terrain.setHeightRaw(ix, iz, newH);
            }
        }
        terrain.refresh(boxX0_, boxZ0_, boxX1_, boxZ1_);
        // Keep the gizmo at the dragged centre height (absolute, not accumulated).
        center_.y = startCenterY_ + heightDelta;
        return true;
    }

    // Not dragging: check for axis click first (if we have a selection).
    if (hasSelection() && g_input.mousePressed(Input::Left) && !overUI) {
        float size = worldSize(cam, center_);
        int axis;
        if (pickAxis(cam, center_, normal_, size, axis)) {
            dragging_ = true;
            activeAxis_ = axis;
            // Save baseline heights for the affected box.
            float cellX = terrain.worldSize() / float(terrain.gridX() - 1);
            float cellZ = terrain.worldSize() / float(terrain.gridZ() - 1);
            int spanX = int(radius / cellX) + 1;
            int spanZ = int(radius / cellZ) + 1;
            int cx = 0, cz = 0;
            terrain.snapWorldToVertex(center_, cx, cz);
            boxX0_ = std::max(0, cx - spanX);
            boxZ0_ = std::max(0, cz - spanZ);
            boxX1_ = std::min(terrain.gridX() - 1, cx + spanX);
            boxZ1_ = std::min(terrain.gridZ() - 1, cz + spanZ);
            int w = boxX1_ - boxX0_ + 1;
            int h = boxZ1_ - boxZ0_ + 1;
            startHeights_.resize((size_t)w * h);
            for (int iz = boxZ0_; iz <= boxZ1_; ++iz)
                for (int ix = boxX0_; ix <= boxX1_; ++ix)
                    startHeights_[(size_t)(iz - boxZ0_) * w + (ix - boxX0_)] =
                        terrain.getHeight(ix, iz);
            dragStartWorld_ = center_;
            startCenterY_ = center_.y;
            // Record where on the drag plane the axis was grabbed so the
            // first-frame delta is zero (see the dragging_ branch).
            {
                glm::vec3 aDir = axisDir(axis, normal_);
                glm::vec3 ro, rd;
                mouseRay(cam, ro, rd);
                glm::vec3 camFwd = -rd;
                glm::vec3 perp = glm::cross(camFwd, aDir);
                if (glm::dot(perp, perp) < 1e-8f) perp = glm::cross(aDir, glm::vec3(0, 1, 0));
                glm::vec3 planeN = glm::normalize(glm::cross(aDir, perp));
                dragStartHit_ = rayPlaneHit(ro, rd, center_, planeN);
            }
            return true;
        }
    }

    // Click on terrain → select a vertex.
    if (g_input.mousePressed(Input::Left) && !overUI) {
        glm::vec3 ro, rd;
        mouseRay(cam, ro, rd);
        glm::vec3 hit;
        if (terrain.raycast(ro, rd, hit)) {
            int ix, iz;
            terrain.snapWorldToVertex(hit, ix, iz);
            if (!ctrl) selected_.clear();
            // Avoid duplicates when ctrl-adding.
            bool dup = false;
            for (const auto& c : selected_)
                if (c.ix == ix && c.iz == iz) { dup = true; break; }
            if (!dup) selected_.push_back({ix, iz});
            recomputeCenter(terrain);
        } else if (!ctrl) {
            selected_.clear();
        }
        return true;
    }
    return false;
}

void VertexEditor::draw(const Camera& cam, const Terrain& terrain,
                         const Shader& lineShader) {
    if (!vaoLine_ || !vaoPoint_) return;

    // Selection markers as GL_POINTS.
    if (!selected_.empty()) {
        std::vector<glm::vec3> pts;
        pts.reserve(selected_.size());
        for (const auto& c : selected_)
            pts.push_back(terrain.vertexPos(c.ix, c.iz));
        glBindBuffer(GL_ARRAY_BUFFER, vboPoint_);
        glBufferData(GL_ARRAY_BUFFER, pts.size() * sizeof(glm::vec3),
                     pts.data(), GL_DYNAMIC_DRAW);
        glm::mat4 vp = cam.projection() * cam.view();
        lineShader.use();
        lineShader.setMat4("uViewProj", vp);
        lineShader.setVec3("uColor", glm::vec3(1.0f, 0.85f, 0.2f));
        lineShader.setFloat("uAlpha", 1.0f);
        glPointSize(10.0f);
        glBindVertexArray(vaoPoint_);
        glDrawArrays(GL_POINTS, 0, (GLsizei)pts.size());
        glBindVertexArray(0);
        glPointSize(1.0f);   // restore GL default
    }

    // Gizmo axes.
    if (!hasSelection()) return;
    float size = worldSize(cam, center_);
    glm::mat4 vp = cam.projection() * cam.view();
    lineShader.use();
    lineShader.setFloat("uAlpha", 1.0f);

    auto drawAxis = [&](int axis, const glm::vec3& col) {
        glm::vec3 dir = axisDir(axis, normal_);
        glm::mat4 R(1.0f);
        if (axis == 1)      R = glm::rotate(glm::mat4(1.0f), PI_F * 0.5f, glm::vec3(0, 0, 1));
        else if (axis == 2) R = glm::rotate(glm::mat4(1.0f), -PI_F * 0.5f, glm::vec3(0, 1, 0));
        else if (axis == 3) {
            glm::vec3 ref = std::abs(dir.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            glm::vec3 u = glm::normalize(glm::cross(dir, ref));
            glm::vec3 v = glm::normalize(glm::cross(dir, u));
            R[0] = glm::vec4(dir, 0.0f);
            R[1] = glm::vec4(u, 0.0f);
            R[2] = glm::vec4(v, 0.0f);
        }
        glm::mat4 model = glm::translate(glm::mat4(1.0f), center_) * R *
                          glm::scale(glm::mat4(1.0f), glm::vec3(size));
        lineShader.setMat4("uViewProj", vp * model);
        bool act = (axis == activeAxis_ && dragging_);
        bool hov = (axis == hoverAxis_);
        lineShader.setVec3("uColor", act ? glm::vec3(1, 0.95f, 0.2f) :
                                  hov ? glm::vec3(1, 1, 1) : col);
        glBindVertexArray(vaoLine_);
        glLineWidth(3.0f);
        glDrawArrays(GL_LINES, 0, 2);
        glBindVertexArray(0);
    };

    if (dragMode_ == FreeXYZ) {
        drawAxis(0, glm::vec3(0.95f, 0.25f, 0.20f));
        drawAxis(1, glm::vec3(0.30f, 0.85f, 0.35f));
        drawAxis(2, glm::vec3(0.25f, 0.50f, 0.95f));
    } else if (dragMode_ == Vertical) {
        drawAxis(1, glm::vec3(0.30f, 0.85f, 0.35f));
    } else { // Normal
        drawAxis(3, glm::vec3(1.0f, 0.55f, 0.1f));
    }
    glLineWidth(1.0f);
}
