#include "input.h"

Input g_input;

static Input* inputFromWindow(GLFWwindow* w) {
    return reinterpret_cast<Input*>(glfwGetWindowUserPointer(w));
}

void Input::init(GLFWwindow* window) {
    window_ = window;
    glfwSetWindowUserPointer(window, this);
    glfwSetMouseButtonCallback(window, mouseButtonCallback);
    glfwSetCursorPosCallback(window, cursorPosCallback);
    glfwSetScrollCallback(window, scrollCallback);
    glfwSetKeyCallback(window, keyCallback);
    glfwSetWindowFocusCallback(window, windowFocusCallback);
}

void Input::releaseAll() {
    for (int i = 0; i < 512; ++i) keys_[i] = false;
    for (int i = 0; i < Count_; ++i) mouseDown_[i] = false;
}

void Input::windowFocusCallback(GLFWwindow* w, int focused) {
    Input* in = inputFromWindow(w);
    if (in && !focused) in->releaseAll();
}

void Input::newFrame() {
    for (int i = 0; i < Count_; ++i) {
        mousePressed_[i] = false;
        mouseReleased_[i] = false;
    }
    dx_ = 0.0;
    dy_ = 0.0;
    scroll_ = 0.0f;
    for (int i = 0; i < 512; ++i) pressedKeys_[i] = false;
}

void Input::onButton(int button, int action) {
    if (button < 0 || button >= Count_) return;
    if (action == GLFW_PRESS) {
        mouseDown_[button] = true;
        mousePressed_[button] = true;
    } else if (action == GLFW_RELEASE) {
        mouseDown_[button] = false;
        mouseReleased_[button] = true;
    }
}

void Input::mouseButtonCallback(GLFWwindow* w, int button, int action, int) {
    if (Input* in = inputFromWindow(w)) in->onButton(button, action);
}

void Input::cursorPosCallback(GLFWwindow* w, double x, double y) {
    Input* in = inputFromWindow(w);
    if (!in) return;
    // Accumulate deltas across multiple callbacks within one frame so fast
    // mouse motion isn't lost; newFrame() clears them each frame.
    in->dx_ += x - in->mx_;
    in->dy_ += y - in->my_;
    in->mx_ = x;
    in->my_ = y;
}

void Input::scrollCallback(GLFWwindow* w, double, double yoff) {
    if (Input* in = inputFromWindow(w)) in->scroll_ += (float)yoff;
}

void Input::keyCallback(GLFWwindow* w, int key, int, int action, int) {
    if (key < 0 || key >= 512) return;
    Input* in = inputFromWindow(w);
    if (!in) return;
    if (action == GLFW_PRESS) {
        in->keys_[key] = true;
        in->pressedKeys_[key] = true;
    } else if (action == GLFW_RELEASE) {
        in->keys_[key] = false;
    }
}
