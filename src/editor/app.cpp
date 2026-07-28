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
}

void App::redoEdit() {
    if (!history_.redo()) return;
    selectedBlockId_ = -1;
    selectedBlockFace_ = -1;
    if (props_.selectedId() >= 0 && !props_.findProp(props_.selectedId()))
        props_.select(-1);
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
    toolMode_ = ToolProp;
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
                    (toolMode_ == ToolVertex) ? ToolBuild : ToolPaint;
        if (wasVertex && toolMode_ != ToolVertex) wireframe_ = false;
        if (toolMode_ == ToolPaint) {
            activeTool_ = &terrainTool_; activeCategory_ = CatBrush;
        } else if (toolMode_ == ToolProp) {
            activeTool_ = &propTool_; activeCategory_ = CatProps;
        } else if (toolMode_ == ToolVertex) {
            activeTool_ = &vertexTool_; activeCategory_ = CatVertex; wireframe_ = true;
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

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    // App default: culling OFF (skybox cube is drawn from inside).
    glDisable(GL_CULL_FACE);
    glCullFace(GL_BACK);
}

void App::renderScene() {
    // Scene renders into the viewport FBO; the UI displays it as an image.
    ensureViewportFbo();
    if (viewportW_ <= 0 || viewportH_ <= 0) return;  // not laid out yet
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

    // --- Shadow pass (skipped entirely when shadows are off) ---
    glm::vec3 lightDir = lightDirFromAngles(lightAzimuth_, lightElevation_);
    glm::mat4 lvp(1.0f);
    if (showShadows_) {
        glm::vec3 center = camera_.target();
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
        glBindFramebuffer(GL_FRAMEBUFFER, viewportFbo_);
        glViewport(0, 0, viewportW_, viewportH_);

        glActiveTexture(GL_TEXTURE0 + kShadowTexUnit);
        glBindTexture(GL_TEXTURE_2D, shadowMap_);
    }
    int enableShadow = showShadows_ ? 1 : 0;

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
    terrainShader_.setVec3("uCamPos", camera_.position());
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
        propShader_.setInt("uEnableShadow", 1);
        props_.render(propShader_, vp, lightDir, camera_.position());
    }
    if (details_.instanceCount() > 0) {
        propShader_.use();
        propShader_.setMat4("uLightViewProj", lvp);
        propShader_.setInt("uShadowMap", kShadowTexUnit);
        propShader_.setInt("uEnableShadow", 1);
        details_.render(propShader_, vp, lightDir, camera_.position());
    }
    if (build_.count() > 0) {
        blockShader_.use();
        blockShader_.setMat4("uLightViewProj", lvp);
        blockShader_.setInt("uShadowMap", kShadowTexUnit);
        blockShader_.setInt("uEnableShadow", 1);
        build_.render(blockShader_, vp, lightDir, camera_.position());
    }
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

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
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
