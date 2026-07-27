#include "brush.h"
#include <cmath>
#include <vector>

BrushCursor::~BrushCursor() { destroy(); }

void BrushCursor::create() {
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, 0, nullptr, GL_DYNAMIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
    glBindVertexArray(0);
    setShape(10.0f, 96);
}

void BrushCursor::destroy() {
    if (vao_) { glDeleteVertexArrays(1, &vao_); vao_ = 0; }
    if (vbo_) { glDeleteBuffers(1, &vbo_); vbo_ = 0; }
}

void BrushCursor::setShape(float radius, int segments) {
    radius_ = radius;
    // Geometry: [center, ring[0..segments-1], ring[0]].
    // Vertex 0 = center (for TRIANGLE_FAN); vertices 1..end = ring (for LINE_STRIP).
    std::vector<glm::vec3> verts;
    verts.push_back(glm::vec3(0.0f, 0.0f, 0.0f)); // center
    for (int i = 0; i < segments; ++i) {
        float a = float(i) / float(segments) * 2.0f * 3.14159265f;
        verts.push_back(glm::vec3(std::cos(a) * radius, 0.0f, std::sin(a) * radius));
    }
    verts.push_back(verts[1]); // close ring
    count_ = (int)verts.size(); // 1 + segments + 1

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(glm::vec3),
                 verts.data(), GL_STATIC_DRAW);
}

void BrushCursor::draw(const glm::mat4& viewProj, const glm::vec3& worldPos,
                       const glm::vec3& color, bool filled, float strength) const {
    if (!vao_) return;
    // Translate the ring to worldPos; model matrix is pure translation.
    glm::mat4 model(1.0f);
    model[3] = glm::vec4(worldPos, 1.0f);

    // The caller is responsible for binding the line shader and setting the
    // combined MVP + color uniforms; we just issue the draw call here.
    glBindVertexArray(vao_);
    if (filled && strength > 0.0f) {
        // Filled disk from center (vertex 0) through all ring vertices.
        glDrawArrays(GL_TRIANGLE_FAN, 0, count_);
    }
    // Ring outline: skip center vertex (index 0), draw from 1 to end.
    glDrawArrays(GL_LINE_STRIP, 1, count_ - 1);
    glBindVertexArray(0);
}
