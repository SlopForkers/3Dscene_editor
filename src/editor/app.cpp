#include "app.h"
#include "input.h"
#include "model.h"
#include "file_dialog.h"
#include "skybox.h"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#ifdef _WIN32
#define GLFW_EXPOSE_NATIVE_WIN32
#include <glfw/glfw3native.h>
#endif
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cmath>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <cctype>
#include <random>
#include <chrono>
#include <cfloat>
#include <string>

static glm::vec3 lightDirFromAngles(float azimuth, float elevation) {
    float ce = std::cos(elevation);
    return glm::normalize(glm::vec3(ce * std::cos(azimuth),
                                    std::sin(elevation),
                                    ce * std::sin(azimuth)));
}

App::App()
    : terrain_(256, 256, 200.0f) {
    brush_.radius = 12.0f;
    brush_.strength = 0.30f;
    brush_.type = Terrain::BrushParams::Raise;
    brush_.falloff = Terrain::BrushParams::FalloffSmooth;
}

App::~App() {
    shutdown();
}

bool App::initWindow() {
    if (!glfwInit()) {
        std::cerr << "glfwInit failed\n";
        return false;
    }
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_SAMPLES, 4);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    window_ = glfwCreateWindow(winWidth_, winHeight_, "Scene Editor", nullptr, nullptr);
    if (!window_) {
        std::cerr << "glfwCreateWindow failed\n";
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1); // vsync
    return true;
}

bool App::initOpenGL() {
    int version = gladLoadGL(glfwGetProcAddress);
    if (version == 0) {
        std::cerr << "Failed to initialize OpenGL context (glad)\n";
        return false;
    }
    std::cout << "OpenGL " << GLAD_VERSION_MAJOR(version) << "."
              << GLAD_VERSION_MINOR(version) << "\n";

    glfwGetFramebufferSize(window_, &fbWidth_, &fbHeight_);
    camera_.setViewport(fbWidth_, fbHeight_);

    // Resolve shader directory relative to executable.
    auto exeDir = std::filesystem::current_path();
    shaderDir_ = (exeDir / "shaders").string();
    if (!std::filesystem::exists(shaderDir_)) {
        // Fall back to source-relative path for running from build dir.
        shaderDir_ = "shaders";
    }

    if (!terrainShader_.loadFromFile(shaderDir_ + "/terrain.vert",
                                      shaderDir_ + "/terrain.frag")) {
        return false;
    }
    if (!lineShader_.loadFromFile(shaderDir_ + "/line.vert",
                                   shaderDir_ + "/line.frag")) {
        return false;
    }
    if (!propShader_.loadFromFile(shaderDir_ + "/prop.vert",
                                    shaderDir_ + "/prop.frag")) {
        return false;
    }
    if (!skyboxShader_.loadFromFile(shaderDir_ + "/skybox.vert",
                                     shaderDir_ + "/skybox.frag")) {
        return false;
    }
    if (!skyboxConvertShader_.loadFromFile(shaderDir_ + "/skybox_convert.vert",
                                            shaderDir_ + "/skybox_convert.frag")) {
        return false;
    }
    if (!blockShader_.loadFromFile(shaderDir_ + "/block.vert",
                                      shaderDir_ + "/block.frag")) {
        return false;
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Shadow map: depth-only framebuffer, 2048x2048.
    shadowMap_.create();
    glBindTexture(GL_TEXTURE_2D, shadowMap_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
                 kShadowSize, kShadowSize, 0, GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);
    // sampler2DShadow requires compare mode, otherwise texture() is UB.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenFramebuffers(1, &shadowFbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowMap_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    terrain_.create();
    terrain_.generateHills();
    brushCursor_.create();
    brushCursor_.setShape(brush_.radius);
    gizmo_.create();
    skybox_.create();
    vertexEditor_.create();
    details_.create();
    build_.create();

    // Unit-cube wireframe (24 vertices, 12 edges) for prop selection boxes.
    {
        static const float cubeEdges[24][3] = {
            {0,0,0},{1,0,0}, {1,0,0},{1,1,0}, {1,1,0},{0,1,0}, {0,1,0},{0,0,0},
            {0,0,1},{1,0,1}, {1,0,1},{1,1,1}, {1,1,1},{0,1,1}, {0,1,1},{0,0,1},
            {0,0,0},{0,0,1}, {1,0,0},{1,0,1}, {1,1,0},{1,1,1}, {0,1,0},{0,1,1},
        };
        boxVao_.create();
        boxVbo_.create();
        glBindVertexArray(boxVao_);
        glBindBuffer(GL_ARRAY_BUFFER, boxVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cubeEdges), cubeEdges, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glBindVertexArray(0);
    }

    // Persistent buffers for the build-drag preview (reused each frame).
    dragVao_.create();
    dragVbo_.create();
    glBindVertexArray(dragVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dragVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glBindVertexArray(0);
    return true;
}

void* App::nativeWindow() const {
#ifdef _WIN32
    return window_ ? (void*)glfwGetWin32Window(window_) : nullptr;
#else
    return nullptr;
#endif
}

void App::cursorRay(glm::vec3& outOrigin, glm::vec3& outDir) const {
    // Mouse is in WINDOW pixels; the scene lives inside the viewport window's
    // framebuffer-pixel FBO. Remap: window px -> viewport-relative -> FBO px.
    double sx = (g_input.mouseX() - (double)vpWinX_) * (double)vpScaleX_;
    double sy = (g_input.mouseY() - (double)vpWinY_) * (double)vpScaleY_;
    camera_.screenToRay((float)sx, (float)sy, outOrigin, outDir);
}

void App::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    // Docking + multi-viewport require the ImGui DOCKING branch build
    // (CMake pins vX.Y.Z-docking); they are not in mainline 1.9x.
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;  // detachable OS windows
    // io.IniFilename defaults to "imgui.ini" — the dock layout persists there.

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    // Rounded corners read badly on OS-level viewport windows.
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
        style.WindowRounding = 0.0f;

    ImGui_ImplGlfw_InitForOpenGL(window_, true);
    ImGui_ImplOpenGL3_Init("#version 330");
    imguiInitialized_ = true;
}

void App::shutdown() {
    if (window_) {
        // ImGui may never have been initialized (early initOpenGL failure) —
        // shutting it down without a context would crash.
        if (imguiInitialized_) {
            ImGui_ImplOpenGL3_Shutdown();
            ImGui_ImplGlfw_Shutdown();
            ImGui::DestroyContext();
            imguiInitialized_ = false;
        }
        // Release GL resources while the context is still current. Shaders
        // have no owner calling destroy() elsewhere, so do it here; their
        // destructors would otherwise call glDeleteProgram after
        // glfwTerminate().
        terrainShader_.destroy();
        lineShader_.destroy();
        propShader_.destroy();
        skyboxShader_.destroy();
        skyboxConvertShader_.destroy();
        blockShader_.destroy();
        brushCursor_.destroy();
        gizmo_.destroy();
        terrain_.destroy();
        skybox_.destroy();
        vertexEditor_.destroy();
        details_.destroy();
        build_.destroy();
        boxVao_.destroy();
        boxVbo_.destroy();
        dragVao_.destroy();
        dragVbo_.destroy();
        if (shadowFbo_) { glDeleteFramebuffers(1, &shadowFbo_); shadowFbo_ = 0; }
        shadowMap_.destroy();
        if (viewportFbo_) { glDeleteFramebuffers(1, &viewportFbo_); viewportFbo_ = 0; }
        viewportColor_.destroy();
        if (viewportDepthRbo_) { glDeleteRenderbuffers(1, &viewportDepthRbo_); viewportDepthRbo_ = 0; }
        noiseTex_.destroy();
        for (auto& p : camPreviews_) {
            if (p.fbo)      glDeleteFramebuffers(1, &p.fbo);
            if (p.depthRbo) glDeleteRenderbuffers(1, &p.depthRbo);
            p.color.destroy();
        }
        camPreviews_.clear();
        spawnModels_.clear();
        spawnModelFailed_.clear();
        props_.clear();
        modelLibrary_.clear();
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwTerminate();
    }
}

int App::run(const std::vector<std::string>& importArgs) {
    if (!initWindow()) return 1;
    g_input.init(window_);
    if (!initOpenGL()) { shutdown(); return 1; }
    initImGui();

    // Import any models / sky passed on the command line (useful for testing).
    for (const auto& p : importArgs) {
        std::string ext;
        auto dot = p.find_last_of('.');
        if (dot != std::string::npos) ext = p.substr(dot);
        // Lowercase compare
        std::string extl; extl.reserve(ext.size());
        for (char c : ext) extl.push_back((char)std::tolower((unsigned char)c));
        if (extl == ".hdr" || extl == ".png" || extl == ".jpg" ||
            extl == ".jpeg" || extl == ".tga" || extl == ".bmp") {
            skybox_.loadEquirect(skyboxConvertShader_, p);
        } else if (extl == ".scene") {
            loadScene(p);
        } else if (extl == ".savetest") {
            std::string out = p.substr(0, dot) + ".scene";
            saveScene(out);
            // Immediately reload to verify round-trip.
            terrain_.flatten(0.0f);
            props_.clear();
            details_.clearInstances();
            details_.clearPrototypes();
            build_.clear();
            selectedBlockId_ = -1;
            selectedBlockFace_ = -1;
            loadScene(out);
        } else {
            importModel(p);
        }
    }

    lastTime_ = glfwGetTime();
    while (!glfwWindowShouldClose(window_)) {
        double now = glfwGetTime();
        float dt = float(now - lastTime_);
        lastTime_ = now;

        // Reset per-frame input state BEFORE polling so callbacks can
        // populate fresh deltas / press events for this frame. (If newFrame()
        // ran after pollEvents it would wipe everything before handleInput.)
        g_input.newFrame();
        glfwPollEvents();
        handleInput(dt);
        updateSimulation(dt);

        // Resize handling
        int curFbW = 0, curFbH = 0;
        glfwGetFramebufferSize(window_, &curFbW, &curFbH);
        int curWinW = 0, curWinH = 0;
        glfwGetWindowSize(window_, &curWinW, &curWinH);
        if (curFbW != fbWidth_ || curFbH != fbHeight_ ||
            curWinW != winWidth_ || curWinH != winHeight_) {
            fbWidth_ = curFbW;
            fbHeight_ = curFbH;
            winWidth_ = curWinW > 0 ? curWinW : winWidth_;
            winHeight_ = curWinH > 0 ? curWinH : winHeight_;
        }

        // 3D scene into the viewport FBO (sized to the viewport window).
        renderScene();
        // Camera View thumbnails: at most one small FBO render per frame.
        updateCameraPreviews();

        // UI (dockspace + windows incl. the viewport image) into the default
        // framebuffer.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, fbWidth_, fbHeight_);
        glClearColor(0.10f, 0.12f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        renderImGui();

        glfwSwapBuffers(window_);
    }
    shutdown();
    return 0;
}

void App::undoEdit() {
    if (!history_.undo()) return;
    // Selections may point at objects the undo just removed — reset them.
    selectedBlockId_ = -1;
    selectedBlockFace_ = -1;
    if (props_.selectedId() >= 0 && !props_.findProp(props_.selectedId()))
        props_.select(-1);
    if (selectedCameraId_ >= 0 && !cameraRig_.findCamera(selectedCameraId_))
        selectedCameraId_ = -1;
    if (selectedSpawnId_ >= 0 && !spawns_.findSpawn(selectedSpawnId_))
        selectedSpawnId_ = -1;
    markCamPreviewsStale();
}

void App::redoEdit() {
    if (!history_.redo()) return;
    selectedBlockId_ = -1;
    selectedBlockFace_ = -1;
    if (props_.selectedId() >= 0 && !props_.findProp(props_.selectedId()))
        props_.select(-1);
    if (selectedCameraId_ >= 0 && !cameraRig_.findCamera(selectedCameraId_))
        selectedCameraId_ = -1;
    if (selectedSpawnId_ >= 0 && !spawns_.findSpawn(selectedSpawnId_))
        selectedSpawnId_ = -1;
    markCamPreviewsStale();
}

void App::importModel(const std::string& path) {
    auto model = std::make_shared<Model>();
    if (!model->loadFromFile(path)) {
        std::cerr << "Failed to load model: " << path << "\n";
        return;
    }
    modelLibrary_.push_back(model);

    glm::vec3 spawn = camera_.target();
    float h = terrain_.heightAtWorld(spawn.x, spawn.z);
    std::filesystem::path fp(path);
    props_.addProp(model, spawn, h, propTargetSize_, fp.filename().string());
    // Switch to the prop tool fully — toolMode_ alone would draw the gizmo
    // while a different activeTool_ still owns the input.
    toolMode_ = ToolProp;
    activeCategory_ = CatProps;
    activeTool_ = &propTool_;
}

void App::handleInput(float dt) {
    ImGuiIO& io = ImGui::GetIO();

    // Sub-gizmos poll g_input themselves; give them the viewport rect so
    // their screenToRay math matches cursorRay.
    gizmo_.setViewportRect(vpWinX_, vpWinY_, vpScaleX_, vpScaleY_);
    vertexEditor_.setViewportRect(vpWinX_, vpWinY_, vpScaleX_, vpScaleY_);

    const bool typing = io.WantTextInput;
    if (!typing && g_input.keyPressed(GLFW_KEY_ESCAPE))
        glfwSetWindowShouldClose(window_, GLFW_TRUE);

    // The mouse is "over UI" for editing purposes unless it's over the 3D
    // viewport image — docked windows report WantCaptureMouse for everything.
    bool overUI = io.WantCaptureMouse && !viewportHovered_;

    // Camera
    if (g_input.mousePressed(Input::Right))   orbiting_ = !overUI;
    if (g_input.mouseReleased(Input::Right))  orbiting_ = false;
    if (orbiting_) {
        camera_.orbit(float(g_input.mouseDeltaX()) * 0.005f,
                      float(g_input.mouseDeltaY()) * 0.005f);
    }
    if (g_input.mousePressed(Input::Middle))  panning_ = !overUI;
    if (g_input.mouseReleased(Input::Middle)) panning_ = false;
    if (panning_) {
        camera_.pan(float(g_input.mouseDeltaX()), float(g_input.mouseDeltaY()));
    }

    // WASD
    if (!io.WantTextInput) {
        float yaw = camera_.yaw();
        glm::vec3 fwdXZ(-std::sin(yaw), 0.0f, -std::cos(yaw));
        glm::vec3 rightXZ(std::cos(yaw), 0.0f, -std::sin(yaw));
        float speed = camera_.distance() * 1.0f * dt;
        glm::vec3 move(0.0f);
        if (g_input.keyDown(GLFW_KEY_W)) move += fwdXZ;
        if (g_input.keyDown(GLFW_KEY_S)) move -= fwdXZ;
        if (g_input.keyDown(GLFW_KEY_D)) move += rightXZ;
        if (g_input.keyDown(GLFW_KEY_A)) move -= rightXZ;
        if (move.x != 0.0f || move.z != 0.0f)
            camera_.moveTarget(move * speed);
    }

    // Scroll
    if (g_input.scrollDelta() != 0.0f) {
        bool shift = g_input.keyDown(GLFW_KEY_LEFT_SHIFT) ||
                     g_input.keyDown(GLFW_KEY_RIGHT_SHIFT);
        bool ctrl  = g_input.keyDown(GLFW_KEY_LEFT_CONTROL) ||
                     g_input.keyDown(GLFW_KEY_RIGHT_CONTROL);
        if (shift) {
            brush_.radius = std::clamp(brush_.radius + g_input.scrollDelta() * 1.5f,
                                       1.0f, terrain_.worldSize() * 0.4f);
            brushCursor_.setShape(brush_.radius);
        } else if (ctrl) {
            brush_.strength = std::clamp(brush_.strength + g_input.scrollDelta() * 0.05f,
                                         0.01f, 5.0f);
        } else if (!overUI) {
            camera_.zoom(-g_input.scrollDelta() * 0.1f);
        }
    }

    // Tab cycle
    if (!typing && g_input.keyPressed(GLFW_KEY_TAB)) {
        bool wasVertex = (toolMode_ == ToolVertex);
        activeTool_->cancelDrag();
        // Sub-gizmos own their drag state and poll g_input only while their
        // tool is active — cancel explicitly or a stale drag applies later.
        gizmo_.cancelDrag();
        vertexEditor_.cancelDrag();
        toolMode_ = (toolMode_ == ToolPaint)  ? ToolProp   :
                    (toolMode_ == ToolProp)   ? ToolVertex :
                    (toolMode_ == ToolVertex) ? ToolBuild  :
                    (toolMode_ == ToolBuild)  ? ToolCamera :
                    (toolMode_ == ToolCamera) ? ToolSpawn : ToolPaint;
        if (wasVertex && toolMode_ != ToolVertex) wireframe_ = false;
        if (toolMode_ == ToolPaint) {
            activeTool_ = &terrainTool_; activeCategory_ = CatBrush;
        } else if (toolMode_ == ToolProp) {
            activeTool_ = &propTool_; activeCategory_ = CatProps;
        } else if (toolMode_ == ToolVertex) {
            activeTool_ = &vertexTool_; activeCategory_ = CatVertex; wireframe_ = true;
        } else if (toolMode_ == ToolCamera) {
            activeTool_ = &cameraTool_; activeCategory_ = CatCameras;
            showCameras_ = true;
        } else if (toolMode_ == ToolSpawn) {
            activeTool_ = &spawnTool_; activeCategory_ = CatSpawns;
            showSpawns_ = true;
        } else { // ToolBuild
            activeTool_ = &buildTool_; activeCategory_ = CatBuild;
        }
    }

    // Cursor ray → terrain hit for overlay text.
    {
        glm::vec3 origin, dir;
        cursorRay(origin, dir);
        brushHasHit_ = terrain_.raycast(origin, dir, brushHit_);
    }

    // Delegate to the active tool.
    activeTool_->handleInput(*this, dt, io, overUI, typing);

    // Global hotkeys (suppressed while typing).
    if (!typing) {
        if (g_input.keyPressed(GLFW_KEY_1)) brush_.type = Terrain::BrushParams::Raise;
        if (g_input.keyPressed(GLFW_KEY_2)) brush_.type = Terrain::BrushParams::Lower;
        if (g_input.keyPressed(GLFW_KEY_3)) brush_.type = Terrain::BrushParams::Smooth;
        if (g_input.keyPressed(GLFW_KEY_4)) brush_.type = Terrain::BrushParams::Flatten;
        if (g_input.keyPressed(GLFW_KEY_5)) brush_.type = Terrain::BrushParams::Noise;
        if (g_input.keyPressed(GLFW_KEY_6)) brush_.type = Terrain::BrushParams::Set;
        if (g_input.keyPressed(GLFW_KEY_7)) brush_.type = Terrain::BrushParams::Texture;
        if (g_input.keyPressed(GLFW_KEY_8)) brush_.type = Terrain::BrushParams::Vegetation;
        if (g_input.keyPressed(GLFW_KEY_F)) wireframe_ = !wireframe_;
        if (g_input.keyPressed(GLFW_KEY_H)) showHelp_ = !showHelp_;
        // Cycle through scene cameras (jump the editor view to their pose).
        if (g_input.keyPressed(GLFW_KEY_LEFT_BRACKET))  cycleSceneCamera(-1);
        if (g_input.keyPressed(GLFW_KEY_RIGHT_BRACKET)) cycleSceneCamera(+1);

        // Undo/redo. Ctrl+Z undoes, Ctrl+Shift+Z or Ctrl+Y redoes.
        bool ctrl = g_input.keyDown(GLFW_KEY_LEFT_CONTROL) ||
                    g_input.keyDown(GLFW_KEY_RIGHT_CONTROL);
        bool shift = g_input.keyDown(GLFW_KEY_LEFT_SHIFT) ||
                     g_input.keyDown(GLFW_KEY_RIGHT_SHIFT);
        if (ctrl && g_input.keyPressed(GLFW_KEY_Z)) {
            if (shift) redoEdit();
            else undoEdit();
        }
        if (ctrl && g_input.keyPressed(GLFW_KEY_Y)) redoEdit();

        if (toolMode_ == ToolProp) {
            if (g_input.keyPressed(GLFW_KEY_T)) gizmo_.setMode(Gizmo::Translate);
            if (g_input.keyPressed(GLFW_KEY_R)) gizmo_.setMode(Gizmo::Rotate);
            if (g_input.keyPressed(GLFW_KEY_S)) gizmo_.setMode(Gizmo::Scale);
        }
        if (toolMode_ == ToolVertex) {
            if (g_input.keyPressed(GLFW_KEY_V)) vertexEditor_.setDragMode(VertexEditor::FreeXYZ);
            if (g_input.keyPressed(GLFW_KEY_B)) vertexEditor_.setDragMode(VertexEditor::Vertical);
            if (g_input.keyPressed(GLFW_KEY_N)) vertexEditor_.setDragMode(VertexEditor::Normal);
        }
    }
}

// Heat-scale color from brush strength: green (weak) -> yellow -> red (strong).
static glm::vec3 strengthColor(float strength) {
    float t = std::sqrt(std::clamp((strength - 0.01f) / (5.0f - 0.01f), 0.0f, 1.0f));
    return glm::vec3(std::clamp(t * 2.0f, 0.0f, 1.0f),
                     std::clamp(2.0f - t * 2.0f, 0.0f, 1.0f),
                     0.0f);
}

// Create or resize the viewport FBO to match the viewport window size
// (window px * DPI scale). Called once per frame from renderScene.
void App::ensureViewportFbo() {
    if (!viewportFbo_) {
        glGenFramebuffers(1, &viewportFbo_);
        viewportColor_.create();
        glGenRenderbuffers(1, &viewportDepthRbo_);
    }
    if (viewportW_ <= 0 || viewportH_ <= 0) return;   // window not laid out yet

    GLint cw = 0, ch = 0;
    glBindTexture(GL_TEXTURE_2D, viewportColor_);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &cw);
    glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &ch);
    glBindTexture(GL_TEXTURE_2D, 0);
    if (cw == viewportW_ && ch == viewportH_) return;   // already the right size

    glBindTexture(GL_TEXTURE_2D, viewportColor_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, viewportW_, viewportH_, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);

    glBindRenderbuffer(GL_RENDERBUFFER, viewportDepthRbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          viewportW_, viewportH_);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, viewportFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_2D, viewportColor_, 0);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, viewportDepthRbo_);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void App::renderDepthPass(const glm::mat4& lvp) {
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);

    // Front-face culling reduces peter-panning. Model::render/applyMaterial
    // toggle GL_CULL_FACE internally, so re-assert it before every subsystem.
    auto assertFrontCull = [] {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_FRONT);
    };

    terrainShader_.use();
    terrainShader_.setInt("uEnableShadow", 0);
    terrainShader_.setMat4("uViewProj", lvp);
    terrainShader_.setMat4("uModel", glm::mat4(1.0f));
    assertFrontCull();
    terrain_.draw();

    if (props_.count() > 0) {
        propShader_.use();
        propShader_.setInt("uEnableShadow", 0);
        assertFrontCull();
        props_.render(propShader_, lvp, glm::vec3(0.0f), glm::vec3(0.0f));
    }

    if (details_.instanceCount() > 0) {
        propShader_.use();
        propShader_.setInt("uEnableShadow", 0);
        assertFrontCull();
        details_.render(propShader_, lvp, glm::vec3(0.0f), glm::vec3(0.0f));
    }

    if (build_.count() > 0) {
        blockShader_.use();
        blockShader_.setInt("uEnableShadow", 0);
        assertFrontCull();
        build_.render(blockShader_, lvp, glm::vec3(0.0f), glm::vec3(0.0f));
    }

    if (!spawns_.spawns().empty()) {
        propShader_.use();
        propShader_.setInt("uEnableShadow", 0);
        assertFrontCull();
        renderSpawnModels(lvp, glm::vec3(0.0f), glm::vec3(0.0f));
    }

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    // App default: culling OFF (skybox cube is drawn from inside).
    glDisable(GL_CULL_FACE);
    glCullFace(GL_BACK);
}

// The shadow + main scene passes, shared by the viewport and camera previews.
// Caller binds + clears the target FBO first. Editor overlays (ghost previews,
// selection boxes, gizmos, brush cursor) are NOT drawn here — see renderScene.
void App::renderWorld(const glm::mat4& view, const glm::mat4& proj,
                      const glm::vec3& camPos, const glm::vec3& shadowCenter,
                      GLuint targetFbo, int targetW, int targetH,
                      bool withShadows) {
    glm::mat4 vp = proj * view;

    // --- Shadow pass (skipped entirely when shadows are off) ---
    glm::vec3 lightDir = lightDirFromAngles(lightAzimuth_, lightElevation_);
    glm::mat4 lvp(1.0f);
    if (withShadows) {
        glm::vec3 center = shadowCenter;
        // 0.75 covers the terrain corners (half-diagonal = 0.707 * size).
        float radius = terrain_.worldSize() * 0.75f;
        glm::vec3 lightPos = center - lightDir * radius;
        glm::mat4 lightView = glm::lookAt(lightPos, center, glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightProj = glm::ortho(-radius, radius, -radius, radius, 0.1f, radius * 2.0f);
        lvp = lightProj * lightView;

        // Unbind the shadow map before it becomes the depth attachment —
        // sampling a texture while rendering into it is a feedback loop.
        glActiveTexture(GL_TEXTURE0 + kShadowTexUnit);
        glBindTexture(GL_TEXTURE_2D, 0);

        glViewport(0, 0, kShadowSize, kShadowSize);
        glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
        glClear(GL_DEPTH_BUFFER_BIT);
        renderDepthPass(lvp);
        glBindFramebuffer(GL_FRAMEBUFFER, targetFbo);
        glViewport(0, 0, targetW, targetH);

        glActiveTexture(GL_TEXTURE0 + kShadowTexUnit);
        glBindTexture(GL_TEXTURE_2D, shadowMap_);
    }
    int enableShadow = withShadows ? 1 : 0;

    // --- Main pass ---
    // Skybox first.
    glm::mat4 skyVp = proj * glm::mat4(glm::mat3(view));
    glDepthFunc(GL_LEQUAL);
    skybox_.draw(skyboxShader_, skyVp, skyExposure_);
    glDepthFunc(GL_LESS);

    terrainShader_.use();
    terrainShader_.setMat4("uViewProj", vp);
    terrainShader_.setMat4("uModel", glm::mat4(1.0f));
    terrainShader_.setVec3("uLightDir", lightDir);
    terrainShader_.setVec3("uCamPos", camPos);
    terrainShader_.setFloat("uMaxHeight", terrain_.maxHeight());
    terrainShader_.setMat4("uLightViewProj", lvp);
    terrainShader_.setInt("uShadowMap", kShadowTexUnit);
    terrainShader_.setInt("uEnableShadow", enableShadow);
    terrain_.bindTextures(terrainShader_);
    terrain_.draw();

    if (props_.count() > 0) {
        propShader_.use();
        propShader_.setMat4("uLightViewProj", lvp);
        propShader_.setInt("uShadowMap", kShadowTexUnit);
        propShader_.setInt("uEnableShadow", enableShadow);
        props_.render(propShader_, vp, lightDir, camPos);
    }
    if (details_.instanceCount() > 0) {
        propShader_.use();
        propShader_.setMat4("uLightViewProj", lvp);
        propShader_.setInt("uShadowMap", kShadowTexUnit);
        propShader_.setInt("uEnableShadow", enableShadow);
        details_.render(propShader_, vp, lightDir, camPos);
    }
    if (build_.count() > 0) {
        blockShader_.use();
        blockShader_.setMat4("uLightViewProj", lvp);
        blockShader_.setInt("uShadowMap", kShadowTexUnit);
        blockShader_.setInt("uEnableShadow", enableShadow);
        build_.render(blockShader_, vp, lightDir, camPos);
    }
    if (anySpawnModelVisible()) {
        propShader_.use();
        propShader_.setMat4("uLightViewProj", lvp);
        propShader_.setInt("uShadowMap", kShadowTexUnit);
        propShader_.setInt("uEnableShadow", enableShadow);
        renderSpawnModels(vp, lightDir, camPos);
    }
}

void App::renderScene() {
    // Scene renders into the viewport FBO; the UI displays it as an image.
    ensureViewportFbo();
    if (viewportW_ <= 0 || viewportH_ <= 0) return;  // not laid out yet
    syncSpawnModels();   // lazy load/unload of spawn marker models
    glBindFramebuffer(GL_FRAMEBUFFER, viewportFbo_);
    glViewport(0, 0, viewportW_, viewportH_);
    glClearColor(0.10f, 0.12f, 0.15f, 1.0f);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    camera_.setViewport(viewportW_, viewportH_);

    if (wireframe_) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    else            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glm::mat4 view = camera_.view();
    glm::mat4 proj = camera_.projection();
    glm::mat4 vp = proj * view;

    renderWorld(view, proj, camera_.position(), camera_.target(),
                viewportFbo_, viewportW_, viewportH_, showShadows_);

    // --- Editor-only overlays below (never drawn into camera previews) ---
    glm::vec3 lightDir = lightDirFromAngles(lightAzimuth_, lightElevation_);
    // Ghost preview for the next block (build tool only).
    if (toolMode_ == ToolBuild && hasGhost_) {
        // Walls have yaw=0 — size encodes orientation. Foundation has no yaw.
        float ghostYaw = 0.0f;
        // Semi-transparent fill.
        blockShader_.use();
        blockShader_.setInt("uEnableShadow", 0);
        build_.renderGhost(blockShader_, vp, lightDir, camera_.position(),
                           ghostCenter_, ghostSize_, build_.color(), ghostYaw);
        // Wireframe outline on top.
        glDisable(GL_DEPTH_TEST);
        build_.renderWireframeBox(lineShader_, vp, ghostCenter_, ghostSize_,
                                  glm::vec3(1.0f, 0.95f, 0.3f), ghostYaw);
        glEnable(GL_DEPTH_TEST);
    }
    // Drag preview (build tool only, while dragging).
    if (toolMode_ == ToolBuild && buildTool_.dragging()) {
        glm::vec3 ro, rd;
        cursorRay(ro, rd);
        glm::vec3 tHit;
        if (terrain_.raycast(ro, rd, tHit)) {
            float gs = build_.gridStep();
            float gx = std::round(tHit.x / gs) * gs;
            float gz = std::round(tHit.z / gs) * gs;
            bool ctrl = g_input.keyDown(GLFW_KEY_LEFT_CONTROL) ||
                         g_input.keyDown(GLFW_KEY_RIGHT_CONTROL);
            glm::vec3 rectCol;
            if (ctrl) rectCol = glm::vec3(1.0f, 0.3f, 0.2f);
            else if (build_.mode() == BuildSystem::ModeTexture) rectCol = glm::vec3(0.8f, 0.4f, 1.0f);
            else if (buildTool_.buildDragOnBlocks_) rectCol = glm::vec3(0.3f, 0.8f, 1.0f);
            else rectCol = glm::vec3(1.0f, 0.95f, 0.3f);

            // Persistent VAO/VBO — only the vertex data is re-uploaded.
            glBindVertexArray(dragVao_);
            glBindBuffer(GL_ARRAY_BUFFER, dragVbo_);
            lineShader_.use();
            lineShader_.setMat4("uViewProj", vp);
            lineShader_.setVec3("uColor", rectCol);
            lineShader_.setFloat("uAlpha", 1.0f);
            glDisable(GL_DEPTH_TEST);

            if (buildTool_.buildDragOnBlocks_) {
                // Wall line drag: draw a single line along the chosen axis at
                // the fixed edge coordinate.
                float startC = buildTool_.buildDragAlongX_ ? buildTool_.buildDragStart_.x
                                                  : buildTool_.buildDragStart_.y;
                float curC   = buildTool_.buildDragAlongX_ ? gx : gz;
                float y = buildTool_.buildDragBaseY_ + 0.05f;
                float pts[2][3];
                if (buildTool_.buildDragAlongX_) {
                    pts[0][0] = startC; pts[0][1] = y; pts[0][2] = buildTool_.buildDragFixed_;
                    pts[1][0] = curC;   pts[1][1] = y; pts[1][2] = buildTool_.buildDragFixed_;
                } else {
                    pts[0][0] = buildTool_.buildDragFixed_; pts[0][1] = y; pts[0][2] = startC;
                    pts[1][0] = buildTool_.buildDragFixed_; pts[1][1] = y; pts[1][2] = curC;
                }
                glBufferData(GL_ARRAY_BUFFER, sizeof(pts), pts, GL_DYNAMIC_DRAW);
                glDrawArrays(GL_LINES, 0, 2);
            } else {
                // Foundation rectangle drag.
                float x0 = std::min(buildTool_.buildDragStart_.x, gx) - gs * 0.5f;
                float x1 = std::max(buildTool_.buildDragStart_.x, gx) + gs * 0.5f;
                float z0 = std::min(buildTool_.buildDragStart_.y, gz) - gs * 0.5f;
                float z1 = std::max(buildTool_.buildDragStart_.y, gz) + gs * 0.5f;
                auto yAt = [&](float x, float z) {
                    return terrain_.heightAtWorld(x, z) + 0.5f;
                };
                float pts[5][3] = {
                    {x0, yAt(x0, z0), z0},
                    {x1, yAt(x1, z0), z0},
                    {x1, yAt(x1, z1), z1},
                    {x0, yAt(x0, z1), z1},
                    {x0, yAt(x0, z0), z0},
                };
                glBufferData(GL_ARRAY_BUFFER, sizeof(pts), pts, GL_DYNAMIC_DRAW);
                glDrawArrays(GL_LINE_STRIP, 0, 5);
            }

            glEnable(GL_DEPTH_TEST);
            glBindVertexArray(0);
        }
    }
    // Selected block wireframe highlight (build tool only).
    if (toolMode_ == ToolBuild && selectedBlockId_ >= 0) {
        glDisable(GL_DEPTH_TEST);
        build_.renderWireframe(lineShader_, vp, selectedBlockId_,
                                glm::vec3(1.0f, 0.6f, 0.2f));
        glEnable(GL_DEPTH_TEST);
    }
    // Selection box for the currently selected prop.
    drawSelectionBox();

    // Gizmo for the selected prop (prop tool only).
    if (toolMode_ == ToolProp) {
        Prop* sel = props_.selected();
        if (sel) {
            // Draw gizmo on top of everything, ignoring depth so it stays visible.
            glDisable(GL_DEPTH_TEST);
            gizmo_.draw(camera_, sel->position, lineShader_);
            glEnable(GL_DEPTH_TEST);
        }
    }

    // Vertex editor selection markers + gizmo (vertex tool only).
    if (toolMode_ == ToolVertex && wireframe_ && vertexEditor_.hasSelection()) {
        glDisable(GL_DEPTH_TEST);
        vertexEditor_.draw(camera_, terrain_, lineShader_);
        glEnable(GL_DEPTH_TEST);
    }

    // Brush cursor — only relevant in paint mode.
    brushHasHit_ = false;
    if (showCursor_ && toolMode_ == ToolPaint) {
        glm::vec3 origin, dir;
        cursorRay(origin, dir);
        glm::vec3 hit;
        if (terrain_.raycast(origin, dir, hit)) {
            brushHasHit_ = true;
            brushHit_ = hit;
            // Filled disk uses the strength heat colour; the ring outline
            // uses the user's cursor colour preference (View panel).
            glm::vec3 heat = strengthColor(brush_.strength);
            glm::vec3 ring(cursorColor_[0], cursorColor_[1], cursorColor_[2]);
            lineShader_.use();
            glm::mat4 model(1.0f);
            model = glm::translate(model, hit);
            lineShader_.setMat4("uViewProj", vp * model);
            lineShader_.setVec3("uColor", heat);
            // Filled disk with opacity proportional to strength.
            float alpha = std::clamp(brush_.strength / 5.0f * 0.4f, 0.0f, 0.4f);
            lineShader_.setFloat("uAlpha", alpha);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            brushCursor_.draw(vp * model, hit, heat, true, alpha);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            // Ring outline at full opacity.
            lineShader_.setVec3("uColor", ring);
            lineShader_.setFloat("uAlpha", 1.0f);
            brushCursor_.draw(vp * model, hit, ring, false, 0.0f);
        }
    }

    drawCameraFrustums(vp);
    drawSpawnMarkers(vp);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

// ---------------------------------------------------------------------------
// Scene cameras: frustum lines, activation, picking, preview FBOs.

// The 4 far-rect corners of a camera's visualisation frustum (fixed 6m
// depth, 16:9 — the game's target aspect; the preview FBOs use it too).
// False for a degenerate pose (position == target).
static bool cameraFrustumCorners(const SceneCamera& c, glm::vec3 out[4]) {
    glm::vec3 fwd = c.target - c.position;
    if (glm::dot(fwd, fwd) < 1e-8f) return false;
    glm::vec3 f = glm::normalize(fwd);
    glm::vec3 up(0.0f, 1.0f, 0.0f);
    if (std::abs(glm::dot(f, up)) > 0.999f) up = glm::vec3(1.0f, 0.0f, 0.0f);
    glm::vec3 right = glm::normalize(glm::cross(f, up));
    glm::vec3 upv = glm::cross(right, f);
    const float depth = 6.0f;
    const float aspect = 16.0f / 9.0f;
    float hh = depth * std::tan(glm::radians(c.fov) * 0.5f);
    float hw = hh * aspect;
    glm::vec3 ctr = c.position + f * depth;
    out[0] = ctr - right * hw - upv * hh;
    out[1] = ctr + right * hw - upv * hh;
    out[2] = ctr + right * hw + upv * hh;
    out[3] = ctr - right * hw + upv * hh;
    return true;
}

void App::drawCameraFrustums(const glm::mat4& vp) {
    if (!showCamFrustums_ || cameraRig_.cameras().empty()) return;

    lineShader_.use();
    lineShader_.setMat4("uViewProj", vp);
    lineShader_.setFloat("uAlpha", 1.0f);
    glDisable(GL_DEPTH_TEST);   // stay visible through geometry (level design)
    glBindVertexArray(dragVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dragVbo_);

    glm::vec3 corners[4];
    for (const auto& c : cameraRig_.cameras()) {
        if (!cameraFrustumCorners(c, corners)) continue;
        glm::vec3 upv = glm::normalize(corners[2] - corners[1]);   // far-rect up
        const glm::vec3& c0 = corners[0];
        const glm::vec3& c1 = corners[1];
        const glm::vec3& c2 = corners[2];
        const glm::vec3& c3 = corners[3];
        const glm::vec3& P = c.position;
        const glm::vec3& T = c.target;
        float hh = glm::length(corners[2] - corners[1]) * 0.5f;
        float pts[20][3] = {
            {P.x,P.y,P.z}, {c0.x,c0.y,c0.z},   {P.x,P.y,P.z}, {c1.x,c1.y,c1.z},
            {P.x,P.y,P.z}, {c2.x,c2.y,c2.z},   {P.x,P.y,P.z}, {c3.x,c3.y,c3.z},
            {c0.x,c0.y,c0.z}, {c1.x,c1.y,c1.z}, {c1.x,c1.y,c1.z}, {c2.x,c2.y,c2.z},
            {c2.x,c2.y,c2.z}, {c3.x,c3.y,c3.z}, {c3.x,c3.y,c3.z}, {c0.x,c0.y,c0.z},
            {P.x,P.y,P.z}, {T.x,T.y,T.z},      // optical axis
            // "up" tick on the frustum rect so orientation reads correctly.
            {c2.x,c2.y,c2.z}, {c2.x + upv.x * hh * 0.4f,
                               c2.y + upv.y * hh * 0.4f,
                               c2.z + upv.z * hh * 0.4f},
        };

        glm::vec3 col(0.55f, 0.65f, 0.85f);                    // normal
        if (c.id == cameraRig_.activeId()) col = glm::vec3(0.35f, 1.0f, 0.45f);
        if (c.id == selectedCameraId_)     col = glm::vec3(1.0f, 0.9f, 0.2f);
        lineShader_.setVec3("uColor", col);
        glBufferData(GL_ARRAY_BUFFER, sizeof(pts), pts, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, 18);
    }

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

// Squared distance from a ray to a point; FLT_MAX when the closest approach
// lies behind the ray origin.
static float rayPointDist2(const glm::vec3& ro, const glm::vec3& rd,
                           const glm::vec3& p, float& outT) {
    glm::vec3 w = p - ro;
    float t = glm::dot(w, rd);
    if (t < 0.0f) return FLT_MAX;
    glm::vec3 q = ro + rd * t;
    outT = t;
    glm::vec3 d = p - q;
    return glm::dot(d, d);
}

int App::pickSceneCamera(const glm::vec3& ro, const glm::vec3& rd) const {
    int bestId = -1;
    float bestT = FLT_MAX;
    auto consider = [&](const glm::vec3& p, float radius, int id) {
        float t = 0.0f;
        if (rayPointDist2(ro, rd, p, t) < radius * radius && t < bestT) {
            bestT = t;
            bestId = id;
        }
    };
    glm::vec3 corners[4];
    for (const auto& c : cameraRig_.cameras()) {
        consider(c.position, 2.0f, c.id);
        consider(c.target, 1.5f, c.id);
        if (cameraFrustumCorners(c, corners))
            for (int i = 0; i < 4; ++i) consider(corners[i], 1.0f, c.id);
    }
    return bestId;
}

void App::addCameraFromView() {
    SceneCamera c;
    c.name = "Camera " + std::to_string(cameraRig_.cameras().size() + 1);
    c.position = camera_.position();
    c.target   = camera_.target();
    c.fov      = camera_.fov();
    int id = cameraRig_.addCamera(c);
    // Fetch AFTER addCamera: the vector push_back may have reallocated.
    const SceneCamera* added = cameraRig_.findCamera(id);
    if (!added) return;
    history_.push(std::make_unique<CameraCommand>(
        cameraRig_, *added, true, "Add Camera"));
    selectedCameraId_ = id;
    // The first camera becomes the game's initial camera by default.
    if (cameraRig_.cameras().size() == 1) cameraRig_.setActive(id);
    markCamPreviewsStale();
}

// ---------------------------------------------------------------------------
// Spawn markers: viz, picking, placement, graph-edit undo helper.

void App::drawSpawnMarkers(const glm::mat4& vp) {
    if (spawns_.spawns().empty()) return;

    lineShader_.use();
    lineShader_.setMat4("uViewProj", vp);
    lineShader_.setFloat("uAlpha", 1.0f);
    glDisable(GL_DEPTH_TEST);
    glBindVertexArray(dragVao_);
    glBindBuffer(GL_ARRAY_BUFFER, dragVbo_);

    for (const auto& s : spawns_.spawns()) {
        const glm::vec3& P = s.position;
        float sc = s.scale;
        float h  = 1.7f * sc;                     // stylised person height
        float hy = h + 0.14f * sc;                // head square height
        float hw = 0.13f * sc;                    // head half size
        glm::vec3 dir(std::sin(s.yaw), 0.0f, std::cos(s.yaw));
        glm::vec3 side(dir.z, 0.0f, -dir.x);
        glm::vec3 chest(P.x, P.y + h * 0.55f, P.z);
        glm::vec3 tip = chest + dir * (0.8f * sc);
        glm::vec3 barb = tip - dir * (0.22f * sc);
        glm::vec3 b1 = barb + side * (0.14f * sc);
        glm::vec3 b2 = barb - side * (0.14f * sc);
        // body, head square, facing arrow with two barbs = 8 lines.
        float pts[16][3] = {
            {P.x, P.y, P.z}, {P.x, P.y + h, P.z},
            {P.x - hw, hy, P.z - hw}, {P.x + hw, hy, P.z - hw},
            {P.x + hw, hy, P.z - hw}, {P.x + hw, hy, P.z + hw},
            {P.x + hw, hy, P.z + hw}, {P.x - hw, hy, P.z + hw},
            {P.x - hw, hy, P.z + hw}, {P.x - hw, hy, P.z - hw},
            {chest.x, chest.y, chest.z}, {tip.x, tip.y, tip.z},
            {tip.x, tip.y, tip.z}, {b1.x, b1.y, b1.z},
            {tip.x, tip.y, tip.z}, {b2.x, b2.y, b2.z},
        };
        glm::vec3 col(0.3f, 0.85f, 0.9f);                  // teal (editing)
        if (sim_.running()) {
            // Simulation feedback: spawned = green, absent = grey.
            const SimController::SpawnSim* ss = sim_.simFor(s.id);
            bool spawned = ss && ss->spawned;
            col = spawned ? glm::vec3(0.35f, 1.0f, 0.45f)
                          : glm::vec3(0.45f, 0.45f, 0.5f);
        }
        if (s.id == selectedSpawnId_) col = glm::vec3(1.0f, 0.9f, 0.2f);
        lineShader_.setVec3("uColor", col);
        glBufferData(GL_ARRAY_BUFFER, sizeof(pts), pts, GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, 16);
    }

    glBindVertexArray(0);
    glEnable(GL_DEPTH_TEST);
}

int App::pickSpawn(const glm::vec3& ro, const glm::vec3& rd) const {
    int bestId = -1;
    float bestT = FLT_MAX;
    for (const auto& s : spawns_.spawns()) {
        float r = std::max(0.9f, 1.1f * s.scale);
        float t = 0.0f;
        if (rayPointDist2(ro, rd, s.position, t) < r * r && t < bestT) {
            bestT = t; bestId = s.id;
        }
        glm::vec3 head = s.position + glm::vec3(0.0f, 1.84f * s.scale, 0.0f);
        if (rayPointDist2(ro, rd, head, t) < r * r && t < bestT) {
            bestT = t; bestId = s.id;
        }
    }
    return bestId;
}

void App::addSpawnAt(const glm::vec3& worldPos) {
    SpawnPoint sp;
    sp.name = "Spawn " + std::to_string(spawns_.spawns().size() + 1);
    sp.position = worldPos;
    // Face the editor camera by default (yaw convention: (sin, 0, cos)).
    glm::vec3 toCam = camera_.position() - worldPos;
    sp.yaw = std::atan2(toCam.x, toCam.z);
    int id = spawns_.addSpawn(sp);
    // Fetch AFTER addSpawn: the vector push_back may have reallocated.
    const SpawnPoint* added = spawns_.findSpawn(id);
    if (!added) return;
    history_.push(std::make_unique<SpawnCommand>(
        spawns_, *added, true, "Add Spawn"));
    selectedSpawnId_ = id;
}

void App::pushSpawnGraphEdit(int spawnId, const char* name, bool mergeable,
                             const SpawnGraphCommand::State& before) {
    SpawnPoint* sp = spawns_.findSpawn(spawnId);
    if (!sp) return;
    history_.push(std::make_unique<SpawnGraphCommand>(
        spawns_, spawnId, before, SpawnGraphCommand::capture(*sp),
        name, mergeable));
}

// ---------------------------------------------------------------------------
// Spawn marker models (bind pose): lazy per-marker loading + rendering.

// Model::loadFromFile normalises its source path to forward slashes, while
// SpawnPoint::modelPath keeps the raw form (Windows file dialog returns
// backslashes). Compare with slashes normalised — a byte-wise compare
// mismatches every frame and causes a per-frame full model reload.
static std::string fwdSlashes(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}

void App::syncSpawnModels() {
    // Drop models whose marker vanished or whose path changed.
    for (auto it = spawnModels_.begin(); it != spawnModels_.end();) {
        const SpawnPoint* sp = spawns_.findSpawn(it->first);
        if (!sp || fwdSlashes(sp->modelPath) != it->second->sourcePath())
            it = spawnModels_.erase(it);
        else ++it;
    }
    for (auto it = spawnModelFailed_.begin(); it != spawnModelFailed_.end();) {
        const SpawnPoint* sp = spawns_.findSpawn(it->first);
        if (!sp || sp->modelPath != it->second)
            it = spawnModelFailed_.erase(it);
        else ++it;
    }
    // Load missing (once per path; failures are remembered until it changes).
    for (const auto& s : spawns_.spawns()) {
        if (s.modelPath.empty() || spawnModels_.count(s.id) ||
            spawnModelFailed_.count(s.id))
            continue;
        auto m = std::make_shared<Model>();
        if (m->loadFromFile(s.modelPath)) {
            spawnModels_[s.id] = std::move(m);
        } else {
            std::cerr << "Spawn model failed to load: " << s.modelPath << "\n";
            spawnModelFailed_[s.id] = s.modelPath;
        }
    }
}

bool App::spawnModelVisible(const SpawnPoint& s) const {
    if (!sim_.running()) return true;   // editing: all marker models shown
    const SimController::SpawnSim* ss = sim_.simFor(s.id);
    return ss != nullptr && ss->spawned;
}

bool App::anySpawnModelVisible() const {
    for (const auto& s : spawns_.spawns()) {
        if (!spawnModelVisible(s)) continue;
        auto it = spawnModels_.find(s.id);
        if (it != spawnModels_.end() && it->second && it->second->valid())
            return true;
    }
    return false;
}

// The prop shader must be in use with the shadow uniforms already set;
// this sets uViewProj/uLightDir/uCamPos itself (works for both the main and
// the shadow depth pass).
void App::renderSpawnModels(const glm::mat4& viewProj,
                            const glm::vec3& lightDir,
                            const glm::vec3& camPos) {
    propShader_.use();
    propShader_.setMat4("uViewProj", viewProj);
    propShader_.setVec3("uLightDir", lightDir);
    propShader_.setVec3("uCamPos", camPos);
    for (const auto& s : spawns_.spawns()) {
        if (!spawnModelVisible(s)) continue;
        auto it = spawnModels_.find(s.id);
        if (it == spawnModels_.end() || !it->second || !it->second->valid())
            continue;
        const Model* m = it->second.get();
        // Marker yaw convention matches the marker arrow: facing (sin,0,cos)
        // = model +Z. Feet rest on the marker point (AABB bottom at y=0).
        glm::mat4 w(1.0f);
        w = glm::translate(w, s.position);
        w = glm::rotate(w, s.yaw, glm::vec3(0.0f, 1.0f, 0.0f));
        w = glm::scale(w, glm::vec3(s.scale));
        w = glm::translate(w, glm::vec3(0.0f, -m->aabbMin().y, 0.0f));
        propShader_.setMat4("uInstance", w);
        m->render(propShader_);
    }
}

// ---------------------------------------------------------------------------
// Simulation of spawn logic graphs (in-editor testing).

bool App::worldToScreen(const glm::vec3& p, float& sx, float& sy) const {
    glm::vec4 clip = camera_.projection() * camera_.view() * glm::vec4(p, 1.0f);
    if (clip.w <= 0.0f) return false;
    // NDC -> viewport FBO pixels -> main-window pixels.
    float ndcX = clip.x / clip.w;
    float ndcY = clip.y / clip.w;
    float fx = (ndcX * 0.5f + 0.5f) * float(viewportW_);
    float fy = (1.0f - (ndcY * 0.5f + 0.5f)) * float(viewportH_);
    sx = vpWinX_ + fx / vpScaleX_;
    sy = vpWinY_ + fy / vpScaleY_;
    return true;
}

void App::startCamAnim(const glm::vec3& target, float yaw, float pitch,
                       float dist, float duration) {
    camAnimFromTarget_ = camera_.target();
    camAnimFromYaw_ = camera_.yaw();
    camAnimFromPitch_ = camera_.pitch();
    camAnimFromDist_ = camera_.distance();
    camAnimToTarget_ = target;
    // Shortest-arc yaw interpolation (wrap the delta to [-pi, pi]).
    float dyaw = yaw - camAnimFromYaw_;
    camAnimToYaw_ = camAnimFromYaw_ + std::atan2(std::sin(dyaw), std::cos(dyaw));
    camAnimToPitch_ = pitch;
    camAnimToDist_ = dist;
    camAnimDur_ = std::max(duration, 0.01f);
    camAnimT_ = 0.0f;
    camAnimActive_ = true;
}

void App::updateCamAnim(float dt) {
    if (!camAnimActive_) return;
    camAnimT_ += dt / camAnimDur_;
    float t = std::clamp(camAnimT_, 0.0f, 1.0f);
    float k = t * t * (3.0f - 2.0f * t);   // smoothstep
    camera_.setTarget(glm::mix(camAnimFromTarget_, camAnimToTarget_, k));
    camera_.setYaw(camAnimFromYaw_ + (camAnimToYaw_ - camAnimFromYaw_) * k);
    camera_.setPitch(camAnimFromPitch_ + (camAnimToPitch_ - camAnimFromPitch_) * k);
    camera_.setDistance(camAnimFromDist_ + (camAnimToDist_ - camAnimFromDist_) * k);
    if (camAnimT_ >= 1.0f) camAnimActive_ = false;
}

void App::updateSimulation(float dt) {
    if (sim_.running()) {
        // The camera target plays the role of "the player" for PlayerNear.
        sim_.update(dt, spawns_, camera_.target());
        int camId = -1;
        glm::vec3 markerPos(0.0f);
        float blend = 0.0f;
        while (sim_.takeCamRequest(camId, markerPos, blend)) {
            if (camId >= 0) {
                const SceneCamera* c = cameraRig_.findCamera(camId);
                if (!c) continue;
                // Same inverse-orbit math as activateSceneCamera.
                glm::vec3 off = c->position - c->target;
                float dist = glm::length(off);
                if (dist < 1e-3f) continue;
                float pitch = std::asin(std::clamp(off.y / dist, -1.0f, 1.0f));
                pitch = std::clamp(pitch, 0.05f, 1.55f);
                float yaw = std::atan2(off.x, off.z);
                startCamAnim(c->target, yaw, pitch, dist, blend);
            } else {
                // Focus the marker itself (chest height), keep the orbit pose.
                startCamAnim(markerPos + glm::vec3(0.0f, 1.4f, 0.0f),
                             camera_.yaw(), camera_.pitch(), camera_.distance(),
                             blend);
            }
        }
    }
    updateCamAnim(dt);
}

// Jump the editor orbit camera to a scene camera's pose. The orbit camera is
// spherical around its target, so invert that parameterisation (see
// Camera::updatePosition: offset = d * (cp*sy, sp, cp*cy)).
void App::activateSceneCamera(int id) {
    const SceneCamera* c = cameraRig_.findCamera(id);
    if (!c) return;
    glm::vec3 off = c->position - c->target;
    float dist = glm::length(off);
    if (dist < 1e-3f) return;   // degenerate pose — nothing to look through
    float pitch = std::asin(std::clamp(off.y / dist, -1.0f, 1.0f));
    pitch = std::clamp(pitch, 0.05f, 1.55f);   // Camera's min/max pitch
    float yaw = std::atan2(off.x, off.z);
    camera_.setTarget(c->target);
    camera_.setYaw(yaw);
    camera_.setPitch(pitch);
    camera_.setDistance(dist);
    selectedCameraId_ = id;
}

void App::cycleSceneCamera(int dir) {
    const auto& cams = cameraRig_.cameras();
    int n = (int)cams.size();
    if (n == 0) return;
    int idx = -1;
    for (int i = 0; i < n; ++i)
        if (cams[i].id == selectedCameraId_) { idx = i; break; }
    if (idx < 0) idx = (dir > 0) ? 0 : n - 1;
    else         idx = (idx + dir + n) % n;
    activateSceneCamera(cams[idx].id);
}

void App::markCamPreviewsStale() {
    for (auto& p : camPreviews_) p.stale = true;
}

void App::ensureCamPreviewFbos() {
    size_t want = cameraRig_.cameras().size();
    while (camPreviews_.size() > want) {
        CamPreview& p = camPreviews_.back();
        if (p.fbo)      glDeleteFramebuffers(1, &p.fbo);
        if (p.depthRbo) glDeleteRenderbuffers(1, &p.depthRbo);
        p.color.destroy();
        camPreviews_.pop_back();
    }
    while (camPreviews_.size() < want) {
        camPreviews_.emplace_back();
        CamPreview& p = camPreviews_.back();
        p.color.create();
        glBindTexture(GL_TEXTURE_2D, p.color);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kCamPreviewW, kCamPreviewH, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);

        glGenRenderbuffers(1, &p.depthRbo);
        glBindRenderbuffer(GL_RENDERBUFFER, p.depthRbo);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                              kCamPreviewW, kCamPreviewH);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);

        glGenFramebuffers(1, &p.fbo);
        glBindFramebuffer(GL_FRAMEBUFFER, p.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                               GL_TEXTURE_2D, p.color, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                                  GL_RENDERBUFFER, p.depthRbo);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }
}

void App::renderCameraPreview(const SceneCamera& cam, CamPreview& pv) {
    glBindFramebuffer(GL_FRAMEBUFFER, pv.fbo);
    glViewport(0, 0, kCamPreviewW, kCamPreviewH);
    glClearColor(0.10f, 0.12f, 0.15f, 1.0f);
    glClearDepthf(1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glm::mat4 view = cam.viewMatrix();
    glm::mat4 proj = cam.projectionMatrix(float(kCamPreviewW) / float(kCamPreviewH));
    // Thumbnails skip the shadow pass entirely (cost + feedback-loop care).
    renderWorld(view, proj, cam.position, cam.target,
                pv.fbo, kCamPreviewW, kCamPreviewH, false);
}

// At most ONE camera preview renders per frame (round-robin). Live mode
// cycles continuously; otherwise only previews marked stale are re-rendered.
void App::updateCameraPreviews() {
    if (!showCameraView_ || cameraRig_.cameras().empty()) return;
    ensureCamPreviewFbos();
    size_t n = cameraRig_.cameras().size();
    camPreviewCursor_ %= n;
    if (!camPreviewsLive_) {
        size_t start = camPreviewCursor_;
        while (!camPreviews_[camPreviewCursor_].stale) {
            camPreviewCursor_ = (camPreviewCursor_ + 1) % n;
            if (camPreviewCursor_ == start) return;   // nothing to refresh
        }
    }
    renderCameraPreview(cameraRig_.cameras()[camPreviewCursor_],
                        camPreviews_[camPreviewCursor_]);
    camPreviews_[camPreviewCursor_].stale = false;
    camPreviewCursor_ = (camPreviewCursor_ + 1) % n;
}

void App::drawSelectionBox() {
    const Prop* p = props_.selectedId() >= 0 ? props_.findProp(props_.selectedId()) : nullptr;
    if (!p || !p->model || !p->model->valid()) return;

    glm::vec3 mn, mx;
    p->worldAabb(mn, mx);
    (void)mn; (void)mx;  // mn/mx used in scale matrix below, center unused
    glm::vec3 size = mx - mn;

    // Scale the unit cube [0..1]^3 to the world AABB box.
    glm::mat4 m(1.0f);
    m = glm::translate(m, mn);
    m = glm::scale(m, size);

    glm::mat4 vp = camera_.projection() * camera_.view();
    lineShader_.use();
    lineShader_.setMat4("uViewProj", vp * m);
    lineShader_.setVec3("uColor", glm::vec3(1.0f, 0.9f, 0.2f));
    lineShader_.setFloat("uAlpha", 1.0f);

    glBindVertexArray(boxVao_);
    glDrawArrays(GL_LINES, 0, 24);
    glBindVertexArray(0);
}
