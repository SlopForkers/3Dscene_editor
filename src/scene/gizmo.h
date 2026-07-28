#pragma once
#include "gl_resource.h"
#include <glm/glm.hpp>

class Camera;
class Shader;

// A 3D manipulator gizmo for the selected prop. Supports Translate, Rotate
// and Scale modes with three coloured world axes (X=red, Y=green, Z=blue).
// The gizmo keeps a constant on-screen size regardless of distance.
class Gizmo {
public:
    enum Mode { Translate = 0, Rotate, Scale };

    struct Transform {
        glm::vec3 position;
        glm::vec3 rotationEuler;   // radians
        glm::vec3 scale;
    };

    Gizmo() = default;
    ~Gizmo();

    void create();
    void destroy();

    // Process input for the gizmo at `pos`. `current` is the prop's current
    // transform; on a successful drag `outNew` receives the new transform.
    // `overUI` tells whether the mouse is over an ImGui window (the caller
    // computes it — with docking, io.WantCaptureMouse is true for the whole
    // dockspace). Returns true if the gizmo consumed the input (so the caller
    // should not also do prop picking / terrain painting this frame).
    bool handleInput(const Camera& cam, const glm::vec3& pos,
                     const Transform& current, Transform& outNew,
                     bool overUI);

    void draw(const Camera& cam, const glm::vec3& pos, const Shader& lineShader);

    Mode mode() const { return mode_; }
    void setMode(Mode m) { mode_ = m; }
    bool dragging() const { return dragging_; }
    // Abort an in-progress drag (e.g. the tool was switched mid-drag).
    void cancelDrag() { dragging_ = false; activeAxis_ = -1; }

    // Mouse coordinates come from GLFW in WINDOW pixels; the scene lives in
    // the viewport window's FBO. (winX, winY) = image top-left in window px,
    // (scaleX, scaleY) = window px -> FBO px. The app sets this once per frame.
    void setViewportRect(float winX, float winY, float scaleX, float scaleY) {
        vpOffX_ = winX; vpOffY_ = winY; vpScaleX_ = scaleX; vpScaleY_ = scaleY;
    }

private:
    Mode mode_ = Translate;
    int  activeAxis_ = -1;   // 0=X, 1=Y, 2=Z
    int  hoverAxis_  = -1;
    bool dragging_   = false;
    float vpOffX_ = 0.0f, vpOffY_ = 0.0f;
    float vpScaleX_ = 1.0f, vpScaleY_ = 1.0f;

    // Drag state (captured at press).
    Transform startTransform_;
    float     startT_        = 0.0f;             // signed distance along axis
    float     startAngle_    = 0.0f;             // for rotate

    GlVertexArray vaoLine_;
    GlBuffer      vboLine_;
    GlVertexArray vaoRing_;
    GlBuffer      vboRing_;
    GlVertexArray vaoCube_;
    GlBuffer      vboCube_;

    float pixelSize_ = 90.0f;   // gizmo on-screen size in pixels

    float worldSize(const Camera& cam, const glm::vec3& pos) const;
    // Build a world ray from the current mouse position, applying the DPI
    // scale so window coords land correctly in the framebuffer viewport.
    void  mouseRay(const Camera& cam, glm::vec3& outOrigin, glm::vec3& outDir) const;
    glm::vec3 axisColor(int axis, bool active, bool hover) const;
    glm::mat4 axisRotation(int axis) const;          // align +X to the axis
    glm::mat4 ringRotation(int axis) const;          // ring's normal -> axis
    bool pickAxis(const Camera& cam, const glm::vec3& pos, float size, int& outAxis);
    bool pickRing(const Camera& cam, const glm::vec3& pos, float size, int& outAxis);
    glm::vec3 axisDir(int axis) const;
    glm::vec3 planeBasisU(int axis) const;           // 1st in-plane basis vec
    glm::vec3 planeBasisV(int axis) const;           // 2nd in-plane basis vec
};
