#pragma once
#include <glad/gl.h>
#include <glm/glm.hpp>

class Camera;
class Shader;
struct ImGuiIO;

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
    // Returns true if the gizmo consumed the input (so the caller should not
    // also do prop picking / terrain painting this frame).
    bool handleInput(const Camera& cam, const glm::vec3& pos,
                     const Transform& current, Transform& outNew,
                     const ImGuiIO& io);

    void draw(const Camera& cam, const glm::vec3& pos, const Shader& lineShader);

    Mode mode() const { return mode_; }
    void setMode(Mode m) { mode_ = m; }
    bool dragging() const { return dragging_; }
    // Abort an in-progress drag (e.g. the tool was switched mid-drag).
    void cancelDrag() { dragging_ = false; activeAxis_ = -1; }

    // Mouse coordinates come from GLFW in WINDOW pixels while the camera
    // viewport is in FRAMEBUFFER pixels; on HiDPI displays the ratio != 1.
    // The app sets this once per frame.
    void setDpiScale(float sx, float sy) { dpiScaleX_ = sx; dpiScaleY_ = sy; }

private:
    Mode mode_ = Translate;
    int  activeAxis_ = -1;   // 0=X, 1=Y, 2=Z
    int  hoverAxis_  = -1;
    bool dragging_   = false;
    float dpiScaleX_ = 1.0f, dpiScaleY_ = 1.0f;

    // Drag state (captured at press).
    Transform startTransform_;
    float     startT_        = 0.0f;             // signed distance along axis
    float     startAngle_    = 0.0f;             // for rotate

    GLuint vaoLine_ = 0, vboLine_ = 0;   // unit line (0,0,0)->(1,0,0)
    GLuint vaoRing_ = 0, vboRing_ = 0;   // unit ring in XY plane
    GLuint vaoCube_ = 0, vboCube_ = 0;   // unit cube wireframe [0,1]^3

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
