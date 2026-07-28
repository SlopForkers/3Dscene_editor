#pragma once
#include "gl_resource.h"
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
    // strength in 0..1 controls the opacity of the filled disk (0 = ring only).
    void draw(const glm::mat4& viewProj, const glm::vec3& worldPos,
              const glm::vec3& color, bool filled = false, float strength = 0.0f) const;

private:
    GlVertexArray vao_;
    GlBuffer      vbo_;
    int   count_ = 0;
    float radius_ = 1.0f;
    int   segments_ = 0;
};
