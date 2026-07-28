#pragma once
#include <glfw/glfw3.h>

// Centralised input state. GLFW callbacks feed it; the app polls it.
class Input {
public:
    enum MouseButton { Left = 0, Right = 1, Middle = 2, Count_ = 3 };

    void init(GLFWwindow* window);

    // Called every frame BEFORE processing callbacks so per-frame deltas reset.
    void newFrame();

    // Callbacks (installed via GLFW).
    static void mouseButtonCallback(GLFWwindow* w, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* w, double x, double y);
    static void scrollCallback(GLFWwindow* w, double xoff, double yoff);
    static void keyCallback(GLFWwindow* w, int key, int scancode, int action, int mods);
    static void windowFocusCallback(GLFWwindow* w, int focused);

    // Clear all held-key / held-button state (called on focus loss so keys
    // released while the window was unfocused don't get "stuck" down).
    void releaseAll();

    bool mouseDown(int b) const { return mouseDown_[b]; }
    bool mousePressed(int b) const { return mousePressed_[b]; }
    bool mouseReleased(int b) const { return mouseReleased_[b]; }
    double mouseX() const { return mx_; }
    double mouseY() const { return my_; }
    double mouseDeltaX() const { return dx_; }
    double mouseDeltaY() const { return dy_; }
    float scrollDelta() const { return scroll_; }

    bool keyDown(int k) const { return keys_[k]; }
    bool keyPressed(int k) const { return pressedKeys_[k]; }

private:
    GLFWwindow* window_ = nullptr;

    bool mouseDown_[Count_]    = {};
    bool mousePressed_[Count_] = {};
    bool mouseReleased_[Count_]= {};
    double mx_ = 0, my_ = 0;
    double dx_ = 0, dy_ = 0;
    float  scroll_ = 0.0f;

    bool keys_[512]        = {};
    bool pressedKeys_[512] = {};

    void onButton(int button, int action);
};

extern Input g_input;