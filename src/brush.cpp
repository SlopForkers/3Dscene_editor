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
    // Loop: vertices[0..segments-1] = ring, then duplicate first to close.
    std::vector<glm::vec3> ring(segments + 1);
    for (int i = 0; i < segments; ++i) {
        float a = float(i) / float(segments) * 2.0f * 3.14159265f;
        ring[i] = glm::vec3(std::cos(a) * radius, 0.0f, std::sin(a) * radius);
    }
    ring[segments] = ring[0];
    count_ = (int)ring.size();

    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, ring.size() * sizeof(glm::vec3),
                 ring.data(), GL_STATIC_DRAW);
}

void BrushCursor::draw(const glm::mat4& viewProj, const glm::vec3& worldPos,
                       const glm::vec3& color, bool filled) const {
    if (!vao_) return;
    // Translate the ring to worldPos; model matrix is pure translation.
    glm::mat4 model(1.0f);
    model[3] = glm::vec4(worldPos, 1.0f);
    // (setShape already encodes radius; we use the stored ring.)

    // The caller is responsible for binding the line shader and setting the
    // combined MVP + color uniforms; we just issue the draw call here.
    // (Kept simple: this method expects a shader already active.)
    glBindVertexArray(vao_);
    if (filled) glDrawArrays(GL_TRIANGLE_FAN, 0, count_);
    else        glDrawArrays(GL_LINE_STRIP, 0, count_);
    glBindVertexArray(0);
}
