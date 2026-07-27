#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>

// Renders the on-terrain brush cursor as a flat ring of line segments.
class BrushCursor {
public:
    BrushCursor() = default;
    ~BrushCursor();

    void create();
    void destroy();
    // Rebuild the ring geometry for a new radius / segment count.
    void setShape(float radius, int segments = 64);
    // Draw the ring at worldPos, lying flat on the XZ plane.
    void draw(const glm::mat4& viewProj, const glm::vec3& worldPos,
              const glm::vec3& color, bool filled = false) const;

private:
    GLuint vao_ = 0, vbo_ = 0;
    int   count_ = 0;
    float radius_ = 1.0f;
};
