#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>
#include <vector>

class Camera;
class Shader;
class Terrain;
struct ImGuiIO;

// Vertex-level terrain editing. Active only in wireframe mode: click selects
// the nearest terrain vertex, Ctrl+click adds to the selection. A gizmo
// appears at the centre of mass of the selection and lets the user drag the
// vertices along one of three modes (Free XYZ, Vertical, Normal). The brush
// radius/falloff from the Brush tab controls how neighbouring vertices follow
// the dragged centre (smooth falloff so the deformation stays organic).
class VertexEditor {
public:
    enum DragMode { FreeXYZ = 0, Vertical, Normal };

    VertexEditor() = default;
    ~VertexEditor();

    void create();
    void destroy();
    void clearSelection();

    // Process input. Returns true if the event was consumed (so App should
    // skip terrain painting / prop picking this frame). Only meaningful when
    // wireframe is on; App is expected to gate this.
    bool handleInput(const Camera& cam, Terrain& terrain,
                     float radius, int falloff,
                     const ImGuiIO& io, bool overUI);

    // Draw selection markers + gizmo. Uses the line shader (depth test off).
    void draw(const Camera& cam, const Terrain& terrain,
              const Shader& lineShader);

    int selectionCount() const { return (int)selected_.size(); }
    bool hasSelection() const { return !selected_.empty(); }
    bool dragging() const { return dragging_; }

    DragMode dragMode() const { return dragMode_; }
    void setDragMode(DragMode m) { dragMode_ = m; }

private:
    struct Cell { int ix, iz; };

    std::vector<Cell> selected_;
    glm::vec3 center_    = glm::vec3(0.0f);
    glm::vec3 normal_    = glm::vec3(0.0f, 1.0f, 0.0f);

    bool dragging_  = false;
    int  activeAxis_ = -1;     // 0=X,1=Y,2=Z, 3=normal
    int  hoverAxis_  = -1;

    // Saved at drag start.
    float startHeightDelta_ = 0.0f;
    float startCenterY_ = 0.0f;
    std::vector<float> startHeights_;   // baseline heights in the affected box
    int boxX0_ = 0, boxZ0_ = 0, boxX1_ = 0, boxZ1_ = 0;
    glm::vec3 dragStartWorld_ = glm::vec3(0.0f);
    glm::vec3 lastGizmoPos_   = glm::vec3(0.0f);

    DragMode dragMode_ = Vertical;

    GLuint vaoLine_ = 0, vboLine_ = 0;
    GLuint vaoPoint_ = 0, vboPoint_ = 0;

    float worldSize(const Camera& cam, const glm::vec3& pos) const;
    int  axisCount() const;
    glm::vec3 axisDir(int axis, const glm::vec3& n) const;
    bool pickAxis(const Camera& cam, const glm::vec3& pos,
                 const glm::vec3& n, float size, int& outAxis);
    void recomputeCenter(const Terrain& terrain);
    glm::vec3 rayPlaneHit(const glm::vec3& ro, const glm::vec3& rd,
                          const glm::vec3& planePoint, const glm::vec3& planeNormal) const;
};
