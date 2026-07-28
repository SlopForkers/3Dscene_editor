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
        glGenVertexArrays(1, &boxVao_);
        glGenBuffers(1, &boxVbo_);
        glBindVertexArray(boxVao_);
        glBindBuffer(GL_ARRAY_BUFFER, boxVbo_);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cubeEdges), cubeEdges, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glBindVertexArray(0);
    }

    // Persistent buffers for the build-drag preview (reused each frame).
    glGenVertexArrays(1, &dragVao_);
    glGenBuffers(1, &dragVbo_);
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
    double sx = g_input.mouseX() * (double)fbWidth_  / (double)winWidth_;
    double sy = g_input.mouseY() * (double)fbHeight_ / (double)winHeight_;
    camera_.screenToRay((float)sx, (float)sy, outOrigin, outDir);
}

void App::initImGui() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.IniFilename = nullptr;

    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;

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
        if (boxVao_) { glDeleteVertexArrays(1, &boxVao_); boxVao_ = 0; }
        if (boxVbo_) { glDeleteBuffers(1, &boxVbo_); boxVbo_ = 0; }
        if (dragVao_) { glDeleteVertexArrays(1, &dragVao_); dragVao_ = 0; }
        if (dragVbo_) { glDeleteBuffers(1, &dragVbo_); dragVbo_ = 0; }
        if (noiseTex_) { glDeleteTextures(1, &noiseTex_); noiseTex_ = 0; }
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
            camera_.setViewport(fbWidth_, fbHeight_);
        }

        glViewport(0, 0, fbWidth_, fbHeight_);
        glClearDepthf(1.0f);
        glClearColor(0.10f, 0.12f, 0.15f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        renderScene();
        renderImGui();

        glfwSwapBuffers(window_);
    }
    shutdown();
    return 0;
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

    // Feed the HiDPI mouse scale to the sub-gizmos (they poll g_input).
    float dpiX = winWidth_  > 0 ? float(fbWidth_)  / float(winWidth_)  : 1.0f;
    float dpiY = winHeight_ > 0 ? float(fbHeight_) / float(winHeight_) : 1.0f;
    gizmo_.setDpiScale(dpiX, dpiY);
    vertexEditor_.setDpiScale(dpiX, dpiY);

    // Global hotkeys must not fire while the user types into an ImGui text
    // field (e.g. renaming a layer: "f" would toggle wireframe, Delete would
    // delete the selected block, Esc would quit the app mid-edit).
    const bool typing = io.WantTextInput;

    // Escape to quit
    if (!typing && g_input.keyPressed(GLFW_KEY_ESCAPE))
        glfwSetWindowShouldClose(window_, GLFW_TRUE);

    bool overUI = io.WantCaptureMouse;

    // Camera orbit (right button) — start only when press is not over UI.
    if (g_input.mousePressed(Input::Right))   orbiting_ = !overUI;
    if (g_input.mouseReleased(Input::Right))   orbiting_ = false;
    if (orbiting_) {
        float sens = 0.005f;
        camera_.orbit(float(g_input.mouseDeltaX()) * sens,
                      float(g_input.mouseDeltaY()) * sens);
    }

    // Pan (middle button)
    if (g_input.mousePressed(Input::Middle))  panning_ = !overUI;
    if (g_input.mouseReleased(Input::Middle))  panning_ = false;
    if (panning_) {
        camera_.pan(float(g_input.mouseDeltaX()), float(g_input.mouseDeltaY()));
    }

    // WASD: move camera target in the XZ plane (skip while typing in a text field).
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

    // Scroll: modifiers change brush radius/strength, plain scroll zooms.
    if (g_input.scrollDelta() != 0.0f) {
        bool shift = g_input.keyDown(GLFW_KEY_LEFT_SHIFT) ||
                     g_input.keyDown(GLFW_KEY_RIGHT_SHIFT);
        bool ctrl  = g_input.keyDown(GLFW_KEY_LEFT_CONTROL) ||
                     g_input.keyDown(GLFW_KEY_RIGHT_CONTROL);
        if (shift) {
            brush_.radius = std::clamp(
                brush_.radius + g_input.scrollDelta() * 1.5f,
                1.0f, terrain_.worldSize() * 0.4f);
            brushCursor_.setShape(brush_.radius);
        } else if (ctrl) {
            brush_.strength = std::clamp(
                brush_.strength + g_input.scrollDelta() * 0.05f,
                0.01f, 5.0f);
        } else if (!overUI) {
            camera_.zoom(-g_input.scrollDelta() * 0.1f);
        }
    }

    // Tab cycles between terrain brush, prop, vertex-edit and build tools.
    if (!typing && g_input.keyPressed(GLFW_KEY_TAB)) {
        bool wasVertex = (toolMode_ == ToolVertex);
        toolMode_ = (toolMode_ == ToolPaint)  ? ToolProp   :
                    (toolMode_ == ToolProp)   ? ToolVertex :
                    (toolMode_ == ToolVertex) ? ToolBuild : ToolPaint;
        painting_ = false;
        // Abort any in-progress drag — its release would otherwise never be
        // seen by the new tool and the stale drag would apply later.
        buildDragging_ = false;
        buildDragOnBlocks_ = false;
        buildTexFace_ = -1;
        gizmo_.cancelDrag();
        vertexEditor_.cancelDrag();
        if (wasVertex && toolMode_ != ToolVertex) wireframe_ = false;
        if (toolMode_ == ToolPaint)       activeCategory_ = CatBrush;
        else if (toolMode_ == ToolProp)    activeCategory_ = CatProps;
        else if (toolMode_ == ToolVertex) { activeCategory_ = CatVertex; wireframe_ = true; }
        else if (toolMode_ == ToolBuild)   activeCategory_ = CatBuild;
    }

    // Left-button behaviour depends on the active tool.
    if (toolMode_ == ToolPaint) {
        // Painting (left button)
        if (g_input.mousePressed(Input::Left)) {
            painting_ = !overUI;
        }
        if (g_input.mouseReleased(Input::Left)) {
            painting_ = false;
        }
        if (painting_) {
            glm::vec3 origin, dir;
            cursorRay(origin, dir);
            glm::vec3 hit;
            if (terrain_.raycast(origin, dir, hit)) {
                if (brush_.type == Terrain::BrushParams::Vegetation) {
                    bool erase = g_input.keyDown(GLFW_KEY_LEFT_CONTROL) ||
                                 g_input.keyDown(GLFW_KEY_RIGHT_CONTROL);
                    // Density has its own sane range; don't mutate the shared
                    // brush strength (it belongs to the sculpt brushes too).
                    float s = std::clamp(brush_.strength, 0.05f, 2.0f);
                    float density = continuousStroke_ ? s * dt * 60.0f : s;
                    details_.paint(terrain_, hit, brush_.radius, density, erase);
                } else {
                    float amount = continuousStroke_ ? brush_.strength * dt * 60.0f
                                                      : brush_.strength;
                    Terrain::BrushParams step = brush_;
                    step.strength = amount;
                    bool changed = terrain_.applyBrush(step, hit);
                    // Keep painted details and foundation blocks glued to the
                    // edited heightfield.
                    if (changed) {
                        details_.reproject(terrain_, hit, brush_.radius * 1.5f);
                        build_.reproject(terrain_, hit, brush_.radius * 1.5f);
                    }
                }
            }
        }
    } else if (toolMode_ == ToolProp) {
        // Prop tool.
        Prop* sel = props_.selected();

        // The gizmo takes priority when a prop is selected.
        bool gizmoConsumed = false;
        if (sel) {
            Gizmo::Transform cur{ sel->position, sel->rotationEuler, sel->scale };
            Gizmo::Transform next;
            if (gizmo_.handleInput(camera_, sel->position, cur, next, io)) {
                gizmoConsumed = true;
                if (gizmo_.dragging()) {
                    sel->position       = next.position;
                    sel->rotationEuler  = next.rotationEuler;
                    sel->scale          = next.scale;
                }
            }
        }

        // If the gizmo did not consume the input, do prop picking on press.
        if (!gizmoConsumed && g_input.mousePressed(Input::Left) && !overUI) {
            glm::vec3 origin, dir;
            cursorRay(origin, dir);
            int picked = props_.pick(origin, dir);
            if (picked >= 0) {
                props_.select(picked);
            } else {
                // Click on empty space deselects.
                props_.select(-1);
            }
        }
    } else if (toolMode_ == ToolVertex) {
        // Vertex editing — only active in wireframe.
        if (wireframe_) {
            bool wasDragging = vertexEditor_.dragging();
            vertexEditor_.handleInput(camera_, terrain_, brush_.radius,
                                      brush_.falloff, io, overUI);
            // Vertex edits change the heightfield: keep details and sunk
            // foundation blocks glued to it (same as brush edits).
            if (vertexEditor_.dragging() || wasDragging) {
                glm::vec3 c = vertexEditor_.selectionCenter();
                details_.reproject(terrain_, c, brush_.radius * 1.5f);
                build_.reproject(terrain_, c, brush_.radius * 1.5f);
            }
        } else if (g_input.mousePressed(Input::Left) && !overUI) {
            // Re-enable wireframe on click so the user can resume editing.
            wireframe_ = true;
        }
    } else if (toolMode_ == ToolBuild) {
        glm::vec3 origin, dir;
        cursorRay(origin, dir);

        bool ctrl = g_input.keyDown(GLFW_KEY_LEFT_CONTROL) ||
                    g_input.keyDown(GLFW_KEY_RIGHT_CONTROL);
        float gs = build_.gridStep();
        BuildSystem::Mode bmode = build_.mode();

        // Hotkeys: Z = foundation, X = wall, C = texture.
        if (!typing && g_input.keyPressed(GLFW_KEY_Z)) build_.setMode(BuildSystem::ModeFoundation);
        if (!typing && g_input.keyPressed(GLFW_KEY_X)) build_.setMode(BuildSystem::ModeWall);
        if (!typing && g_input.keyPressed(GLFW_KEY_C)) build_.setMode(BuildSystem::ModeTexture);

        // R cycles the wall edge rotation (+X -> +Z -> -X -> -Z).
        if (!typing && bmode == BuildSystem::ModeWall && g_input.keyPressed(GLFW_KEY_R))
            build_.rotateWallEdge();

        // Ghost preview (only when not dragging).
        if (!buildDragging_) {
            hasGhost_ = false;
            if (!overUI) {
                glm::vec3 hp, hn;
                int id = build_.pick(origin, dir, hp, hn);
                if (id >= 0) {
                    const BuildSystem::Block* b = build_.findBlock(id);
                    if (b) {
                        if (bmode == BuildSystem::ModeTexture) {
                            // Texture mode: highlight the hovered block + face.
                            selectedBlockId_ = id;
                            selectedBlockFace_ = BuildSystem::faceFromNormal(hn);
                            // No placement ghost — we paint on click instead.
                        } else if (bmode == BuildSystem::ModeWall && hn.y > 0.5f) {
                            // Wall ghost: position on the block edge selected by
                            // the current wall rotation (R cycles 0..3).
                            float fixed; bool alongX;
                            build_.wallLineParamsFor(*b, fixed, alongX);
                            ghostSize_ = alongX
                                ? glm::vec3(build_.blockWidth(), build_.blockHeight(), build_.wallThickness())
                                : glm::vec3(build_.wallThickness(), build_.blockHeight(), build_.blockWidth());
                            // Snap only the along-axis centre; the fixed edge
                            // coordinate already carries the rim offset.
                            float ex = alongX ? b->position.x : fixed;
                            float ez = alongX ? fixed : b->position.z;
                            if (alongX) ex = std::round(ex / gs) * gs;
                            else        ez = std::round(ez / gs) * gs;
                            ghostCenter_ = glm::vec3(ex, b->max().y + build_.blockHeight() * 0.5f, ez);
                            ghostType_ = BuildSystem::Wall;
                            hasGhost_ = true;
                        } else if (bmode == BuildSystem::ModeFoundation && hn.y <= 0.5f) {
                            // Foundation ghost on side face.
                            glm::vec3 gc, gsz;
                            BuildSystem::BlockType gt;
                            if (build_.computePlacement(terrain_, origin, dir, gc, gsz, gt)) {
                                ghostCenter_ = gc; ghostSize_ = gsz; ghostType_ = gt;
                                hasGhost_ = true;
                            }
                        }
                    }
                } else if (bmode == BuildSystem::ModeFoundation) {
                    // Foundation ghost on terrain.
                    glm::vec3 tHit;
                    if (terrain_.raycast(origin, dir, tHit)) {
                        float gx = std::round(tHit.x / gs) * gs;
                        float gz = std::round(tHit.z / gs) * gs;
                        ghostSize_ = glm::vec3(build_.blockWidth(), build_.blockHeight(), build_.blockWidth());
                        float th = terrain_.heightAtWorld(gx, gz);
                        float topY = th + build_.blockHeight() * (1.0f - build_.sunkDepth());
                        ghostCenter_ = glm::vec3(gx, topY - build_.blockHeight() * 0.5f, gz);
                        ghostType_ = BuildSystem::Foundation;
                        hasGhost_ = true;
                    }
                }
            }
        } else {
            hasGhost_ = false;
        }

        // Press: start drag or single-place. The erase flag is captured NOW
        // (at press) so a mid-drag Ctrl change can't flip the operation.
        if (g_input.mousePressed(Input::Left) && !overUI) {
            glm::vec3 hp, hn;
            int id = build_.pick(origin, dir, hp, hn);
            buildDragErase_ = ctrl;
            if (id >= 0) {
                const BuildSystem::Block* b = build_.findBlock(id);
                if (ctrl) {
                    if (bmode == BuildSystem::ModeTexture) {
                        // Ctrl+click in texture mode: clear face texture.
                        build_.clearBlockFaceTexture(id);
                        if (selectedBlockId_ == id) selectedBlockFace_ = -1;
                    } else {
                        build_.removeBlock(id);
                        if (selectedBlockId_ == id) { selectedBlockId_ = -1; selectedBlockFace_ = -1; }
                    }
                } else if (b) {
                    if (bmode == BuildSystem::ModeTexture) {
                        // Record the press; paint happens on release so a click
                        // paints exactly one block and a drag paints a region.
                        int face = BuildSystem::faceFromNormal(hn);
                        bool horizontal = (face == BuildSystem::FacePY ||
                                          face == BuildSystem::FaceNY);
                        buildTexFace_ = face;
                        buildTexLine_ = !horizontal;
                        buildTexPressBlock_ = id;
                        buildTexPressFace_ = face;
                        buildTexPressMX_ = g_input.mouseX();
                        buildTexPressMY_ = g_input.mouseY();
                        buildDragging_ = true;
                        buildDragOnBlocks_ = !horizontal;  // line uses wall-style preview
                        if (horizontal) {
                            buildDragStart_ = glm::vec2(std::round(hp.x / gs) * gs,
                                                        std::round(hp.z / gs) * gs);
                        } else {
                            buildDragAlongX_ = (face == BuildSystem::FacePZ ||
                                                face == BuildSystem::FaceNZ);
                            float fixed = buildDragAlongX_
                                ? std::round(b->position.z / gs) * gs
                                : std::round(b->position.x / gs) * gs;
                            buildDragFixed_ = fixed;
                            buildDragBaseY_ = b->max().y;
                            float startC = buildDragAlongX_
                                ? std::round(hp.x / gs) * gs
                                : std::round(hp.z / gs) * gs;
                            buildDragStart_ = buildDragAlongX_
                                ? glm::vec2(startC, fixed)
                                : glm::vec2(fixed, startC);
                        }
                        selectedBlockId_ = id;
                        selectedBlockFace_ = face;
                    } else if (bmode == BuildSystem::ModeWall && hn.y > 0.5f) {
                        // Start wall LINE drag on block top. The wall sits on
                        // the edge selected by the current wall rotation (R).
                        float fixed; bool alongX;
                        build_.wallLineParamsFor(*b, fixed, alongX);
                        buildDragging_ = true;
                        buildDragOnBlocks_ = true;
                        buildDragBaseY_ = b->max().y;
                        buildDragAlongX_ = alongX;
                        buildDragFixed_ = fixed;
                        buildDragStart_ = alongX
                            ? glm::vec2(std::round(hp.x / gs) * gs, fixed)
                            : glm::vec2(fixed, std::round(hp.z / gs) * gs);
                    } else if (bmode == BuildSystem::ModeFoundation && hn.y <= 0.5f) {
                        // Single foundation on side face.
                        glm::vec3 gc, gsz;
                        BuildSystem::BlockType gt;
                        if (build_.computePlacement(terrain_, origin, dir, gc, gsz, gt)) {
                            int nid = build_.placeBlock(gc, gsz, gt, build_.color());
                            selectedBlockId_ = nid;
                        }
                    }
                }
            } else if (bmode == BuildSystem::ModeFoundation ||
                       (ctrl && bmode != BuildSystem::ModeTexture)) {
                // Terrain: start a foundation rect drag — or, with Ctrl held,
                // an erase drag (available in wall mode too, as the help
                // overlay promises).
                glm::vec3 tHit;
                if (terrain_.raycast(origin, dir, tHit)) {
                    buildDragging_ = true;
                    buildDragOnBlocks_ = false;
                    buildDragStart_ = glm::vec2(std::round(tHit.x / gs) * gs,
                                                std::round(tHit.z / gs) * gs);
                }
            }
        }
        if (g_input.mouseReleased(Input::Left)) {
            if (buildDragging_ && !overUI) {
                glm::vec3 tHit;
                if (terrain_.raycast(origin, dir, tHit)) {
                    float gx = std::round(tHit.x / gs) * gs;
                    float gz = std::round(tHit.z / gs) * gs;
                    if (bmode == BuildSystem::ModeTexture) {
                        // Distinguish click from drag by pixel movement so a
                        // click paints exactly one block (the press-time pick)
                        // and only a real drag stretches across a region.
                        float pdx = (float)(g_input.mouseX() - buildTexPressMX_);
                        float pdy = (float)(g_input.mouseY() - buildTexPressMY_);
                        bool moved = (pdx * pdx + pdy * pdy) > 25.0f; // ~5px
                        if (!moved) {
                            // Click: paint the single block captured at press.
                            if (buildTexPressBlock_ >= 0 && buildTexPressFace_ >= 0)
                                build_.paintCurrentTexture(buildTexPressBlock_,
                                                           buildTexPressFace_);
                        } else if (buildTexLine_) {
                            float startC = buildDragAlongX_ ? buildDragStart_.x
                                                             : buildDragStart_.y;
                            float curC   = buildDragAlongX_ ? gx : gz;
                            build_.applyTextureToLine(startC, curC,
                                                      buildDragFixed_,
                                                      buildDragAlongX_,
                                                      buildTexFace_);
                        } else {
                            build_.applyTextureToRect(buildDragStart_.x,
                                                      buildDragStart_.y, gx, gz,
                                                      buildTexFace_);
                        }
                    } else if (buildDragErase_) {
                        int n = build_.eraseRect(buildDragStart_.x, buildDragStart_.y,
                                                 gx, gz);
                        if (n > 0) { selectedBlockId_ = -1; selectedBlockFace_ = -1; }
                    } else if (buildDragOnBlocks_) {
                        // Wall line: project cursor onto the chosen axis.
                        float startC = buildDragAlongX_ ? buildDragStart_.x
                                                         : buildDragStart_.y;
                        float curC = buildDragAlongX_ ? gx : gz;
                        build_.fillWallLine(startC, curC, buildDragFixed_,
                                             buildDragBaseY_, buildDragAlongX_);
                    } else {
                        build_.fillRect(terrain_,
                                        buildDragStart_.x, buildDragStart_.y,
                                        gx, gz, BuildSystem::Foundation);
                    }
                }
            }
            buildDragging_ = false;
            buildDragOnBlocks_ = false;
            buildTexFace_ = -1;
        }

        if (!typing && g_input.keyPressed(GLFW_KEY_DELETE) && selectedBlockId_ >= 0) {
            build_.removeBlock(selectedBlockId_);
            selectedBlockId_ = -1;
            selectedBlockFace_ = -1;
        }

        // Right-click: select a block and remember the picked face (used by the
        // face-texture UI).
        if (g_input.mousePressed(Input::Right) && !overUI) {
            glm::vec3 hp, hn;
            int id = build_.pick(origin, dir, hp, hn);
            if (id >= 0) {
                selectedBlockId_ = id;
                selectedBlockFace_ = BuildSystem::faceFromNormal(hn);
            } else {
                selectedBlockId_ = -1;
                selectedBlockFace_ = -1;
            }
        }
    }

    // Keyboard shortcuts for brush types (all suppressed while typing).
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

        // Gizmo mode shortcuts (only relevant in prop tool).
        if (toolMode_ == ToolProp) {
            if (g_input.keyPressed(GLFW_KEY_T)) gizmo_.setMode(Gizmo::Translate);
            if (g_input.keyPressed(GLFW_KEY_R)) gizmo_.setMode(Gizmo::Rotate);
            if (g_input.keyPressed(GLFW_KEY_S)) gizmo_.setMode(Gizmo::Scale);
        }
        // Vertex-edit drag-mode shortcuts.
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

void App::renderScene() {
    if (wireframe_) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    else            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glm::mat4 view = camera_.view();
    glm::mat4 proj = camera_.projection();
    glm::mat4 vp = proj * view;

    // Skybox first: strip translation from the view so the cube stays centred
    // on the camera; the vertex shader pins depth at the far plane.
    glm::mat4 skyVp = proj * glm::mat4(glm::mat3(view));
    glDepthFunc(GL_LEQUAL);
    skybox_.draw(skyboxShader_, skyVp, skyExposure_);
    glDepthFunc(GL_LESS);

    terrainShader_.use();
    terrainShader_.setMat4("uViewProj", vp);
    terrainShader_.setMat4("uModel", glm::mat4(1.0f));
    glm::vec3 lightDir = lightDirFromAngles(lightAzimuth_, lightElevation_);
    terrainShader_.setVec3("uLightDir", lightDir);
    terrainShader_.setVec3("uCamPos", camera_.position());
    terrainShader_.setFloat("uMaxHeight", terrain_.maxHeight());
    terrain_.bindTextures(terrainShader_);

    terrain_.draw();

    // Props (after terrain, before cursor overlay). Props use their own shader.
    if (props_.count() > 0) {
        propShader_.use();
        props_.render(propShader_, vp, lightDir, camera_.position());
    }
    // Instanced details (vegetation/rocks/etc.), painted with the Vegetation brush.
    if (details_.instanceCount() > 0) {
        propShader_.use();
        details_.render(propShader_, vp, lightDir, camera_.position());
    }
    // Build blocks (solid cubes).
    if (build_.count() > 0) {
        build_.render(blockShader_, vp, lightDir, camera_.position());
    }
    // Ghost preview for the next block (build tool only).
    if (toolMode_ == ToolBuild && hasGhost_) {
        // Walls have yaw=0 — size encodes orientation. Foundation has no yaw.
        float ghostYaw = 0.0f;
        // Semi-transparent fill.
        build_.renderGhost(blockShader_, vp, lightDir, camera_.position(),
                           ghostCenter_, ghostSize_, build_.color(), ghostYaw);
        // Wireframe outline on top.
        glDisable(GL_DEPTH_TEST);
        build_.renderWireframeBox(lineShader_, vp, ghostCenter_, ghostSize_,
                                  glm::vec3(1.0f, 0.95f, 0.3f), ghostYaw);
        glEnable(GL_DEPTH_TEST);
    }
    // Drag preview (build tool only, while dragging).
    if (toolMode_ == ToolBuild && buildDragging_) {
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
            else if (buildDragOnBlocks_) rectCol = glm::vec3(0.3f, 0.8f, 1.0f);
            else rectCol = glm::vec3(1.0f, 0.95f, 0.3f);

            // Persistent VAO/VBO — only the vertex data is re-uploaded.
            glBindVertexArray(dragVao_);
            glBindBuffer(GL_ARRAY_BUFFER, dragVbo_);
            lineShader_.use();
            lineShader_.setMat4("uViewProj", vp);
            lineShader_.setVec3("uColor", rectCol);
            lineShader_.setFloat("uAlpha", 1.0f);
            glDisable(GL_DEPTH_TEST);

            if (buildDragOnBlocks_) {
                // Wall line drag: draw a single line along the chosen axis at
                // the fixed edge coordinate.
                float startC = buildDragAlongX_ ? buildDragStart_.x
                                                 : buildDragStart_.y;
                float curC   = buildDragAlongX_ ? gx : gz;
                float y = buildDragBaseY_ + 0.05f;
                float pts[2][3];
                if (buildDragAlongX_) {
                    pts[0][0] = startC; pts[0][1] = y; pts[0][2] = buildDragFixed_;
                    pts[1][0] = curC;   pts[1][1] = y; pts[1][2] = buildDragFixed_;
                } else {
                    pts[0][0] = buildDragFixed_; pts[0][1] = y; pts[0][2] = startC;
                    pts[1][0] = buildDragFixed_; pts[1][1] = y; pts[1][2] = curC;
                }
                glBufferData(GL_ARRAY_BUFFER, sizeof(pts), pts, GL_DYNAMIC_DRAW);
                glDrawArrays(GL_LINES, 0, 2);
            } else {
                // Foundation rectangle drag.
                float x0 = std::min(buildDragStart_.x, gx) - gs * 0.5f;
                float x1 = std::max(buildDragStart_.x, gx) + gs * 0.5f;
                float z0 = std::min(buildDragStart_.y, gz) - gs * 0.5f;
                float z1 = std::max(buildDragStart_.y, gz) + gs * 0.5f;
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
    glm::vec3 center = (mn + mx) * 0.5f;
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
