#pragma once
#include <glad/gl.h>
#include <glfw/glfw3.h>
#include "camera.h"
#include "terrain.h"
#include "brush.h"
#include "shader.h"
#include <string>

class App {
public:
    App();
    ~App();

    int run();

private:
    bool initWindow();
    bool initOpenGL();
    void initImGui();
    void shutdown();

    void handleInput(float dt);
    void renderScene();
    void renderImGui();

    // ImGui helpers
    void drawMainPanel();
    void drawHelpOverlay();

    GLFWwindow* window_ = nullptr;
    int fbWidth_  = 0;
    int fbHeight_ = 0;
    int winWidth_  = 1280;
    int winHeight_ = 720;

    Camera camera_;
    Terrain terrain_;
    BrushCursor brushCursor_;
    Shader terrainShader_;
    Shader lineShader_;

    // Brush state
    Terrain::BrushParams brush_;

    // Interaction flags (captured at press time so drags survive hovering ImGui)
    bool painting_ = false;
    bool orbiting_ = false;
    bool panning_  = false;
    glm::vec3 lastPaintPoint_ = glm::vec3(0.0f);
    bool  hasPaintPoint_ = false;

    // Display options
    bool wireframe_ = false;
    bool showGrid_  = true;
    bool showCursor_ = true;
    bool showHelp_  = true;
    float cursorColor_[3] = {1.0f, 0.85f, 0.2f};

    // Lighting
    float lightAzimuth_ = 0.6f;
    float lightElevation_ = 0.9f;

    // Timing
    double lastTime_ = 0.0;

    // Stroke accumulation: scale strength by frame time so behaviour is
    // framerate-independent.
    bool continuousStroke_ = true;

    std::string shaderDir_;
};
