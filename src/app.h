#pragma once
#include <glad/gl.h>
#include <glfw/glfw3.h>
#include "camera.h"
#include "terrain.h"
#include "brush.h"
#include "shader.h"
#include "prop.h"
#include "gizmo.h"
#include "skybox.h"
#include "vertex_edit.h"
#include "detail.h"
#include "build.h"
#include <memory>
#include <string>
#include <vector>

class Model;

class App {
public:
    App();
    ~App();

    int run(const std::vector<std::string>& importArgs = {});

    // Left-rail categories (group tool modes and setting panels). Public so
    // the file-scope icon helpers in app.cpp can switch on them.
    enum Category {
        CatBrush = 0, CatVertex, CatProps, CatVegetation,  // tool categories
        CatBuild,                                          // building blocks
        CatTerrain, CatLayers, CatEnv, CatView,            // setting categories
        CatFile,                                           // file operations
        CatCount
    };

private:
    bool initWindow();
    bool initOpenGL();
    void initImGui();
    void shutdown();

    void handleInput(float dt);
    void renderScene();
    void renderImGui();

    // ImGui helpers
    void drawLeftPanel();
    void drawBrushBar();
    void drawHelpOverlay();
    void drawSelectionBox();
    void selectCategory(int cat);
    void drawBrushContent();
    void drawVertexContent();
    void drawPropsContent();
    void drawVegetationContent();
    void drawBuildContent();
    void drawTerrainContent();
    void drawLayersContent();
    void drawEnvContent();
    void drawViewContent();
    void drawFileContent();

    // Scene save/load (implemented in scene.cpp).
    bool saveScene(const std::string& path);
    bool loadScene(const std::string& path);

    // Tool modes: 0 = terrain brush, 1 = prop select/place, 2 = vertex edit,
    // 3 = build blocks.
    enum ToolMode { ToolPaint = 0, ToolProp = 1, ToolVertex = 2, ToolBuild = 3 };
    int toolMode_ = ToolPaint;

    int activeCategory_ = CatBrush;

    // Import a glTF/VRM file and spawn a prop at the terrain centre.
    void importModel(const std::string& path);

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
    Shader propShader_;
    Shader skyboxShader_;
    Shader skyboxConvertShader_;
    Shader blockShader_;
    Skybox skybox_;
    float skyExposure_ = 1.0f;

    // Brush state
    Terrain::BrushParams brush_;

    // Interaction flags (captured at press time so drags survive hovering ImGui)
    bool painting_ = false;
    bool orbiting_ = false;
    bool panning_  = false;
    glm::vec3 lastPaintPoint_ = glm::vec3(0.0f);
    bool  hasPaintPoint_ = false;

    // Prop interaction
    Gizmo gizmo_;

    // Vertex editing
    VertexEditor vertexEditor_;

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

    // Props
    PropManager props_;
    // Shared model library keeps loaded models alive while referenced by props.
    std::vector<std::shared_ptr<Model>> modelLibrary_;
    float propTargetSize_ = 6.0f;

    // Detail/vegetation system (instanced painting).
    DetailSystem details_;

    // Block building system.
    BuildSystem build_;
    int  selectedBlockId_ = -1;
    // Ghost placement computed each frame from the cursor ray (Build tool).
    bool  hasGhost_ = false;
    glm::vec3 ghostCenter_ = glm::vec3(0.0f);
    glm::vec3 ghostSize_   = glm::vec3(2.0f);
    BuildSystem::BlockType ghostType_ = BuildSystem::Foundation;
    // Drag-rectangle area fill: start cell recorded on press, fill on release.
    bool  buildDragging_ = false;
    glm::vec2 buildDragStart_ = glm::vec2(0.0f); // (x, z) world grid coords

    // Selection box wireframe VAO/VBO (unit cube, reused per frame).
    GLuint boxVao_ = 0, boxVbo_ = 0;
};

