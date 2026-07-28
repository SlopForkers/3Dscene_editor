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
#include "gl_resource.h"
#include "tools.h"
#include "history.h"
#include <memory>
#include <string>
#include <vector>

class Model;

class App {
public:
    App();
    ~App();

    int run(const std::vector<std::string>& importArgs = {});

    enum Category {
        CatBrush = 0, CatVertex, CatProps, CatVegetation,
        CatBuild,
        CatTerrain, CatNoise, CatLayers, CatEnv, CatView,
        CatHistory,
        CatFile,
        CatCount
    };

    enum ToolMode { ToolPaint = 0, ToolProp = 1, ToolVertex = 2, ToolBuild = 3 };

    // Subsystems (public — tools and panels reference them directly).
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
    Terrain::BrushParams brush_;
    Gizmo gizmo_;
    VertexEditor vertexEditor_;
    PropManager props_;
    DetailSystem details_;
    BuildSystem build_;
    History history_;
    std::vector<std::shared_ptr<Model>> modelLibrary_;
    Noise::Params noiseParams_;
    GlTexture noiseTex_;
    bool noisePreviewDirty_ = true;
    bool realtimeNoise_ = false;
    static constexpr int noisePreviewSize_ = 128;

    // Display options
    bool wireframe_ = false;
    bool showGrid_ = true;
    bool showCursor_ = true;
    bool showHelp_ = true;
    bool showShadows_ = true;
    float cursorColor_[3] = {1.0f, 0.85f, 0.2f};
    float propTargetSize_ = 6.0f;

    float lightAzimuth_ = 0.6f;
    float lightElevation_ = 0.9f;

    bool brushHasHit_ = false;
    glm::vec3 brushHit_ = glm::vec3(0.0f);

    bool hasGhost_ = false;
    glm::vec3 ghostCenter_ = glm::vec3(0.0f);
    glm::vec3 ghostSize_ = glm::vec3(2.0f);
    BuildSystem::BlockType ghostType_ = BuildSystem::Foundation;

    int selectedBlockId_ = -1;
    int selectedBlockFace_ = -1;

    void cursorRay(glm::vec3& outOrigin, glm::vec3& outDir) const;
    void importModel(const std::string& path);

    void drawBrushContent();
    void drawVertexContent();
    void drawVegetationContent();
    void drawBuildContent();
    void drawTerrainContent();
    void drawNoiseContent();
    void drawLayersContent();
    void drawEnvContent();
    void drawViewContent();
    void drawHistoryContent();
    void drawFileContent();
    void drawHelpOverlay();
    void drawPropToolContent();
    void drawInspectorContent();

    bool saveScene(const std::string& path);
    bool loadScene(const std::string& path);

    // Undo/redo entry points (hotkeys + History panel buttons).
    void undoEdit();
    void redoEdit();

    // Concrete tools (one per tool mode).
    TerrainTool terrainTool_;
    PropTool    propTool_;
    VertexTool  vertexTool_;
    BuildTool   buildTool_;
    ITool*      activeTool_ = &terrainTool_;

    int  toolMode_        = ToolPaint;
    int  activeCategory_  = CatBrush;
    bool continuousStroke_ = true;

    // Docked window visibility (layout itself persists via imgui.ini).
    bool showTools_     = true;
    bool showHierarchy_ = true;
    bool showInspector_ = true;
    bool showTerrain_   = false;
    bool showLayers_    = false;
    bool showSettings_  = false;
    bool showHistory_   = false;
    bool showFile_      = false;

    // 3D viewport window state. The scene renders into viewportFbo_; the
    // viewport window displays viewportColor_. vpWinX_/vpWinY_/vpScaleX_/Y_
    // map WINDOW-pixel mouse coords into viewport framebuffer pixels.
    GLuint    viewportFbo_ = 0;
    GlTexture viewportColor_;
    GLuint    viewportDepthRbo_ = 0;
    int       viewportW_ = 0, viewportH_ = 0;      // FBO size (framebuffer px)
    float     vpWinX_ = 0.0f, vpWinY_ = 0.0f;      // image pos (window px)
    float     vpScaleX_ = 1.0f, vpScaleY_ = 1.0f;  // window px -> FBO px
    bool      viewportHovered_ = false;

    // Prop panel transform-edit capture (IsItemActivated/Deactivated pair).
    bool propEditActive_ = false;
    int  propEditId_ = -1;
    glm::vec3 propEditPos_   = glm::vec3(0.0f);
    glm::vec3 propEditRot_   = glm::vec3(0.0f);
    glm::vec3 propEditScale_ = glm::vec3(1.0f);

private:
    bool initWindow();
    bool initOpenGL();
    void initImGui();
    void shutdown();

    void handleInput(float dt);
    void renderScene();
    void renderDepthPass(const glm::mat4& lvp);
    void renderImGui();
    void drawSelectionBox();
    void selectCategory(int cat);

    // Docked UI shell.
    void drawViewportWindow();
    void drawToolbarWindow();
    void drawToolsWindow();
    void drawHierarchyWindow();
    void drawInspectorWindow();
    void drawSettingsWindow();
    void drawTerrainWindow();
    void drawLayersWindow();
    void drawHistoryWindow();
    void drawFileWindow();
    void buildDefaultLayout(unsigned int dockspaceId);
    void ensureViewportFbo();

    bool orbiting_ = false;
    bool panning_  = false;

    GLFWwindow* window_ = nullptr;
    int fbWidth_  = 0, fbHeight_ = 0;
    int winWidth_  = 1280, winHeight_ = 720;

    GlVertexArray boxVao_;
    GlBuffer      boxVbo_;
    GlVertexArray dragVao_;
    GlBuffer      dragVbo_;

    // Shadow map.
    static constexpr int kShadowSize = 2048;
    // Unit 7: terrain owns 0-5, prop materials 0-4, block texture 0 — must
    // not collide with per-material bindings.
    static constexpr int kShadowTexUnit = 7;
    GLuint  shadowFbo_  = 0;
    GlTexture shadowMap_;

    bool imguiInitialized_ = false;

    double lastTime_ = 0.0;
    std::string shaderDir_;

    void* nativeWindow() const;
};
