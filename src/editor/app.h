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
#include "scene_camera.h"
#include "spawn.h"
#include "sim.h"
#include "gl_resource.h"
#include "tools.h"
#include "history.h"
#include "commands.h"
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
        CatCameras, CatCamView,
        CatSpawns, CatSim,
        CatCount
    };

    enum ToolMode { ToolPaint = 0, ToolProp = 1, ToolVertex = 2, ToolBuild = 3,
                    ToolCamera = 4, ToolSpawn = 5 };

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
    CameraRig cameraRig_;
    int selectedCameraId_ = -1;
    SpawnManager spawns_;
    int selectedSpawnId_ = -1;
    SimController sim_;   // in-editor spawn logic simulation (Play/Stop)
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
    void drawCamerasContent();
    void drawCameraViewContent();
    void drawCameraToolContent();
    void drawSpawnsContent();
    void drawSpawnToolContent();
    void drawSpawnLogicContent();
    void drawSimulationContent();
    // World (editor camera) -> main-window pixel position, for overlays.
    bool worldToScreen(const glm::vec3& p, float& sx, float& sy) const;

    // Scene camera interaction: jump the editor orbit camera to a scene
    // camera's pose; cycle steps through the rig ([ / ] hotkeys).
    void activateSceneCamera(int id);
    void cycleSceneCamera(int dir);
    void markCamPreviewsStale();
    // Add a camera at the current editor view (Cameras panel + camera tool).
    void addCameraFromView();
    // Viewport picking (camera tool): nearest camera whose position/target/
    // frustum corners are close to the ray; -1 when nothing is near.
    int pickSceneCamera(const glm::vec3& ro, const glm::vec3& rd) const;
    // Spawn markers: add at a world position (tool/panel), ray-pick nearest.
    void addSpawnAt(const glm::vec3& worldPos);
    int pickSpawn(const glm::vec3& ro, const glm::vec3& rd) const;
    // Logic-graph edit with undo: capture before, mutate, then push.
    void pushSpawnGraphEdit(int spawnId, const char* name, bool mergeable,
                            const SpawnGraphCommand::State& before);

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
    CameraTool  cameraTool_;
    SpawnTool   spawnTool_;
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
    bool showCameras_   = false;
    bool showCameraView_= false;
    bool showSpawns_    = false;
    bool showSpawnLogic_= false;
    bool showSimulation_= false;

    // Scene cameras: frustum visualisation + preview window options.
    bool showCamFrustums_ = true;
    bool camPreviewsLive_ = true;

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

    // Camera panel edit capture (same Activated/Deactivated pattern).
    bool camEditActive_ = false;
    int  camEditId_ = -1;
    SceneCamera camEditBefore_;

    // Spawn panel field-edit capture (same pattern; graph has its own).
    bool spawnEditActive_ = false;
    int  spawnEditId_ = -1;
    SpawnEditCommand::Fields spawnEditBefore_;

    // Camera focus pull animation (simulation CameraFocus actions): lerps
    // the orbit camera pose over camAnimDur_ seconds.
    bool camAnimActive_ = false;
    float camAnimT_ = 0.0f, camAnimDur_ = 1.0f;
    glm::vec3 camAnimFromTarget_ = glm::vec3(0.0f), camAnimToTarget_ = glm::vec3(0.0f);
    float camAnimFromYaw_ = 0.0f, camAnimToYaw_ = 0.0f;
    float camAnimFromPitch_ = 0.0f, camAnimToPitch_ = 0.0f;
    float camAnimFromDist_ = 0.0f, camAnimToDist_ = 0.0f;
    void updateCamAnim(float dt);

private:
    bool initWindow();
    bool initOpenGL();
    void initImGui();
    void shutdown();

    void handleInput(float dt);
    // Simulation per-frame step + camera focus pull animation.
    void updateSimulation(float dt);
    void startCamAnim(const glm::vec3& target, float yaw, float pitch,
                      float dist, float duration);
    void renderScene();
    // The shadow + skybox + terrain + props + details + blocks passes,
    // parameterised so both the main viewport and camera previews can use it.
    // The target FBO must be bound and cleared by the caller.
    void renderWorld(const glm::mat4& view, const glm::mat4& proj,
                     const glm::vec3& camPos, const glm::vec3& shadowCenter,
                     GLuint targetFbo, int targetW, int targetH,
                     bool withShadows);
    void renderDepthPass(const glm::mat4& lvp);
    void renderImGui();
    void drawSelectionBox();
    void drawCameraFrustums(const glm::mat4& vp);
    void drawSpawnMarkers(const glm::mat4& vp);
    void selectCategory(int cat);

    // Spawn Logic node-editor window state (canvas is redrawn every frame).
    glm::vec2 nodeEdScroll_ = glm::vec2(0.0f);
    int  nodeEdSelectedNode_ = -1;    // node id inside the selected spawn
    int  nodeEdLinkFrom_ = -1;        // link-drag source node id (-1 = none)
    bool nodeEdLinkFalse_ = false;    // dragging from the false pin
    int  nodeEdDragNode_ = -1;        // node being dragged (-1 = none)
    glm::vec2 nodeEdDragOff_ = glm::vec2(0.0f);
    bool nodeEdPanning_ = false;
    int  nodeEdCtxNode_ = -1;         // node under the right-click (-1 = empty)
    glm::vec2 nodeEdCtxPos_ = glm::vec2(0.0f);  // canvas pos of the right-click
    bool nodeEdEditActive_ = false;   // param-widget undo capture in progress
    SpawnGraphCommand::State nodeEdBefore_;
    void nodeEdParamsPanel(SpawnPoint& sp);
    void nodeEdCanvas(SpawnPoint& sp);

    // Camera preview window: small per-camera FBOs, rendered one camera per
    // frame (round-robin) — never all cameras in a single frame.
    struct CamPreview {
        GLuint fbo = 0;
        GLuint depthRbo = 0;
        GlTexture color;
        bool stale = true;
    };
    static constexpr int kCamPreviewW = 256;
    static constexpr int kCamPreviewH = 144;
    std::vector<CamPreview> camPreviews_;
    size_t camPreviewCursor_ = 0;
    void updateCameraPreviews();
    void ensureCamPreviewFbos();
    void renderCameraPreview(const SceneCamera& cam, CamPreview& pv);

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
    void drawCamerasWindow();
    void drawCameraViewWindow();
    void drawSpawnsWindow();
    void drawSpawnLogicWindow();
    void drawSimulationWindow();
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
