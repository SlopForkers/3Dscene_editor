#include "app.h"
#include "input.h"
#include "model.h"
#include "file_dialog.h"
#include "skybox.h"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cmath>
#include <filesystem>
#include <algorithm>
#include <cstdio>
#include <cctype>

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
    return true;
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
}

void App::shutdown() {
    if (window_) {
        ImGui_ImplOpenGL3_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        brushCursor_.destroy();
        gizmo_.destroy();
        terrain_.destroy();
        skybox_.destroy();
        vertexEditor_.destroy();
        details_.destroy();
        build_.destroy();
        if (boxVao_) { glDeleteVertexArrays(1, &boxVao_); boxVao_ = 0; }
        if (boxVbo_) { glDeleteBuffers(1, &boxVbo_); boxVbo_ = 0; }
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

    // Escape to quit
    if (g_input.keyPressed(GLFW_KEY_ESCAPE))
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
    if (g_input.keyPressed(GLFW_KEY_TAB)) {
        bool wasVertex = (toolMode_ == ToolVertex);
        toolMode_ = (toolMode_ == ToolPaint)  ? ToolProp   :
                    (toolMode_ == ToolProp)   ? ToolVertex :
                    (toolMode_ == ToolVertex) ? ToolBuild : ToolPaint;
        painting_ = false;
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
            hasPaintPoint_ = false;
        }
        if (g_input.mouseReleased(Input::Left)) {
            painting_ = false;
            hasPaintPoint_ = false;
        }
        if (painting_) {
            double sx = g_input.mouseX() * (double)fbWidth_  / (double)winWidth_;
            double sy = g_input.mouseY() * (double)fbHeight_ / (double)winHeight_;
            glm::vec3 origin, dir;
            camera_.screenToRay((float)sx, (float)sy, origin, dir);
            glm::vec3 hit;
            if (terrain_.raycast(origin, dir, hit)) {
                if (brush_.type == Terrain::BrushParams::Vegetation) {
                    bool erase = g_input.keyDown(GLFW_KEY_LEFT_CONTROL) ||
                                 g_input.keyDown(GLFW_KEY_RIGHT_CONTROL);
                    float density = continuousStroke_
                        ? brush_.strength * dt * 60.0f
                        : brush_.strength;
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
                lastPaintPoint_ = hit;
                hasPaintPoint_ = true;
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
            double sx = g_input.mouseX() * (double)fbWidth_  / (double)winWidth_;
            double sy = g_input.mouseY() * (double)fbHeight_ / (double)winHeight_;
            glm::vec3 origin, dir;
            camera_.screenToRay((float)sx, (float)sy, origin, dir);
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
            vertexEditor_.handleInput(camera_, terrain_, brush_.radius,
                                      brush_.falloff, io, overUI);
        } else if (g_input.mousePressed(Input::Left) && !overUI) {
            // Re-enable wireframe on click so the user can resume editing.
            wireframe_ = true;
        }
    } else if (toolMode_ == ToolBuild) {
        double sx = g_input.mouseX() * (double)fbWidth_  / (double)winWidth_;
        double sy = g_input.mouseY() * (double)fbHeight_ / (double)winHeight_;
        glm::vec3 origin, dir;
        camera_.screenToRay((float)sx, (float)sy, origin, dir);

        bool ctrl = g_input.keyDown(GLFW_KEY_LEFT_CONTROL) ||
                    g_input.keyDown(GLFW_KEY_RIGHT_CONTROL);

        // Press: pick block face (single-block snap) OR start rect on terrain.
        if (g_input.mousePressed(Input::Left) && !overUI) {
            glm::vec3 hp, hn;
            int id = build_.pick(origin, dir, hp, hn);
            if (id >= 0) {
                if (ctrl) {
                    build_.removeBlock(id);
                    if (selectedBlockId_ == id) selectedBlockId_ = -1;
                } else {
                    glm::vec3 gc, gs;
                    BuildSystem::BlockType gt;
                    if (build_.computePlacement(terrain_, origin, dir, gc, gs, gt)) {
                        int nid = build_.placeBlock(gc, gs, gt, build_.color());
                        selectedBlockId_ = nid;
                    }
                }
            } else {
                // Start rectangle drag on the terrain.
                glm::vec3 tHit;
                if (terrain_.raycast(origin, dir, tHit)) {
                    buildDragging_ = true;
                    float gx = std::round(tHit.x / build_.gridStep()) * build_.gridStep();
                    float gz = std::round(tHit.z / build_.gridStep()) * build_.gridStep();
                    buildDragStart_ = glm::vec2(gx, gz);
                }
            }
        }
        if (g_input.mouseReleased(Input::Left)) {
            if (buildDragging_ && !overUI) {
                glm::vec3 tHit;
                if (terrain_.raycast(origin, dir, tHit)) {
                    float gx = std::round(tHit.x / build_.gridStep()) * build_.gridStep();
                    float gz = std::round(tHit.z / build_.gridStep()) * build_.gridStep();
                    if (ctrl) {
                        int n = build_.eraseRect(buildDragStart_.x, buildDragStart_.y,
                                                 gx, gz);
                        if (n > 0) selectedBlockId_ = -1;
                    } else {
                        build_.fillRect(terrain_,
                                        buildDragStart_.x, buildDragStart_.y,
                                        gx, gz, BuildSystem::Foundation);
                    }
                }
            }
            buildDragging_ = false;
        }

        // Ghost preview for single-block placement (only when not dragging).
        if (!buildDragging_) {
            glm::vec3 gc, gs;
            BuildSystem::BlockType gt;
            hasGhost_ = !overUI && build_.computePlacement(terrain_, origin, dir,
                                                            gc, gs, gt);
            if (hasGhost_) { ghostCenter_ = gc; ghostSize_ = gs; ghostType_ = gt; }
        } else {
            hasGhost_ = false;
        }

        if (g_input.keyPressed(GLFW_KEY_DELETE) && selectedBlockId_ >= 0) {
            build_.removeBlock(selectedBlockId_);
            selectedBlockId_ = -1;
        }
    }

    // Keyboard shortcuts for brush types
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
    terrainShader_.setFloat("uMaxHeight", std::max(1.0f, terrain_.maxHeight()));
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
        // Semi-transparent fill.
        build_.renderGhost(blockShader_, vp, lightDir, camera_.position(),
                           ghostCenter_, ghostSize_, build_.color());
        // Wireframe outline on top.
        glDisable(GL_DEPTH_TEST);
        build_.renderWireframeBox(lineShader_, vp, ghostCenter_, ghostSize_,
                                  glm::vec3(1.0f, 0.95f, 0.3f));
        glEnable(GL_DEPTH_TEST);
    }
    // Drag-rectangle preview (build tool only, while dragging on terrain).
    if (toolMode_ == ToolBuild && buildDragging_) {
        double sx = g_input.mouseX() * (double)fbWidth_  / (double)winWidth_;
        double sy = g_input.mouseY() * (double)fbHeight_ / (double)winHeight_;
        glm::vec3 ro, rd;
        camera_.screenToRay((float)sx, (float)sy, ro, rd);
        glm::vec3 tHit;
        if (terrain_.raycast(ro, rd, tHit)) {
            float gs = build_.gridStep();
            float gx = std::round(tHit.x / gs) * gs;
            float gz = std::round(tHit.z / gs) * gs;
            float x0 = std::min(buildDragStart_.x, gx) - gs * 0.5f;
            float x1 = std::max(buildDragStart_.x, gx) + gs * 0.5f;
            float z0 = std::min(buildDragStart_.y, gz) - gs * 0.5f;
            float z1 = std::max(buildDragStart_.y, gz) + gs * 0.5f;
            // Build a 5-vertex loop on the terrain surface at the rectangle
            // border and draw it with the line shader.
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
            GLuint tmpVao = 0, tmpVbo = 0;
            glGenVertexArrays(1, &tmpVao);
            glGenBuffers(1, &tmpVbo);
            glBindVertexArray(tmpVao);
            glBindBuffer(GL_ARRAY_BUFFER, tmpVbo);
            glBufferData(GL_ARRAY_BUFFER, sizeof(pts), pts, GL_DYNAMIC_DRAW);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
            lineShader_.use();
            lineShader_.setMat4("uViewProj", vp);
            lineShader_.setMat4("uModel", glm::mat4(1.0f));
            glm::vec3 rectCol = g_input.keyDown(GLFW_KEY_LEFT_CONTROL) ||
                                 g_input.keyDown(GLFW_KEY_RIGHT_CONTROL)
                                 ? glm::vec3(1.0f, 0.3f, 0.2f)
                                 : glm::vec3(1.0f, 0.95f, 0.3f);
            lineShader_.setVec3("uColor", rectCol);
            lineShader_.setFloat("uAlpha", 1.0f);
            glDisable(GL_DEPTH_TEST);
            glDrawArrays(GL_LINE_STRIP, 0, 5);
            glEnable(GL_DEPTH_TEST);
            glBindVertexArray(0);
            glDeleteBuffers(1, &tmpVbo);
            glDeleteVertexArrays(1, &tmpVao);
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
    if (showCursor_ && toolMode_ == ToolPaint) {
        double sx = g_input.mouseX() * (double)fbWidth_  / (double)winWidth_;
        double sy = g_input.mouseY() * (double)fbHeight_ / (double)winHeight_;
        glm::vec3 origin, dir;
        camera_.screenToRay((float)sx, (float)sy, origin, dir);
        glm::vec3 hit;
        if (terrain_.raycast(origin, dir, hit)) {
            glm::vec3 c = strengthColor(brush_.strength);
            lineShader_.use();
            glm::mat4 model(1.0f);
            model = glm::translate(model, hit);
            lineShader_.setMat4("uViewProj", vp * model);
            lineShader_.setVec3("uColor", c);
            // Filled disk with opacity proportional to strength.
            float alpha = std::clamp(brush_.strength / 5.0f * 0.4f, 0.0f, 0.4f);
            lineShader_.setFloat("uAlpha", alpha);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glDepthMask(GL_FALSE);
            brushCursor_.draw(vp * model, hit, c, true, alpha);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
            // Ring outline at full opacity.
            lineShader_.setFloat("uAlpha", 1.0f);
            brushCursor_.draw(vp * model, hit, c, false, 0.0f);
        }
    }

    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

static const char* brushTypeName(int t) {
    switch (t) {
        case Terrain::BrushParams::Raise:   return "Raise";
        case Terrain::BrushParams::Lower:   return "Lower";
        case Terrain::BrushParams::Smooth:  return "Smooth";
        case Terrain::BrushParams::Flatten: return "Flatten";
        case Terrain::BrushParams::Noise:   return "Noise";
        case Terrain::BrushParams::Set:     return "Set Height";
        case Terrain::BrushParams::Texture: return "Texture";
        case Terrain::BrushParams::Vegetation: return "Vegetation";
        default: return "?";
    }
}

static const char* falloffName(int f) {
    switch (f) {
        case Terrain::BrushParams::FalloffSmooth:   return "Smooth";
        case Terrain::BrushParams::FalloffLinear:   return "Linear";
        case Terrain::BrushParams::FalloffConstant: return "Constant";
        default: return "?";
    }
}

void App::renderImGui() {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    drawLeftPanel();
    drawBrushBar();
    if (showHelp_) drawHelpOverlay();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// --------------------------------------------------------------------------
// Mini icon helpers (drawn via ImDrawList, no external assets).
// Each icon is inscribed in [p0, p1]; col is the stroke/fill colour.
// --------------------------------------------------------------------------
namespace icons {

static void Raise(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float cx = (p0.x + p1.x) * 0.5f;
    float w = (p1.x - p0.x) * 0.18f;
    dl->AddLine(ImVec2(cx, p1.y - (p1.y - p0.y) * 0.2f),
                ImVec2(cx, p0.y + (p1.y - p0.y) * 0.2f), col, 3.0f);
    dl->AddTriangleFilled(
        ImVec2(cx, p0.y + (p1.y - p0.y) * 0.2f),
        ImVec2(cx - w, p0.y + (p1.y - p0.y) * 0.2f + w * 1.5f),
        ImVec2(cx + w, p0.y + (p1.y - p0.y) * 0.2f + w * 1.5f), col);
}
static void Lower(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float cx = (p0.x + p1.x) * 0.5f;
    float w = (p1.x - p0.x) * 0.18f;
    dl->AddLine(ImVec2(cx, p0.y + (p1.y - p0.y) * 0.2f),
                ImVec2(cx, p1.y - (p1.y - p0.y) * 0.2f), col, 3.0f);
    dl->AddTriangleFilled(
        ImVec2(cx, p1.y - (p1.y - p0.y) * 0.2f),
        ImVec2(cx - w, p1.y - (p1.y - p0.y) * 0.2f - w * 1.5f),
        ImVec2(cx + w, p1.y - (p1.y - p0.y) * 0.2f - w * 1.5f), col);
}
static void Smooth(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float x0 = p0.x + (p1.x - p0.x) * 0.2f;
    float x1 = p1.x - (p1.x - p0.x) * 0.2f;
    float cy = (p0.y + p1.y) * 0.5f;
    float amp = (p1.y - p0.y) * 0.18f;
    for (int i = 0; i < 3; ++i) {
        float xa = x0 + (x1 - x0) * (i / 3.0f);
        float xb = x0 + (x1 - x0) * ((i + 1) / 3.0f);
        ImVec2 a = ImVec2(xa, cy + (i % 2 ? amp : -amp));
        ImVec2 b = ImVec2(xb, cy + (i % 2 ? -amp : amp));
        dl->AddLine(a, b, col, 2.5f);
    }
}
static void Flatten(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float cy = (p0.y + p1.y) * 0.5f;
    float x0 = p0.x + (p1.x - p0.x) * 0.2f;
    float x1 = p1.x - (p1.x - p0.x) * 0.2f;
    dl->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), col, 3.0f);
    dl->AddLine(ImVec2(x0, cy - 4), ImVec2(x0, cy + 4), col, 2.0f);
    dl->AddLine(ImVec2(x1, cy - 4), ImVec2(x1, cy + 4), col, 2.0f);
}
static void Noise(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float x0 = p0.x + (p1.x - p0.x) * 0.2f;
    float x1 = p1.x - (p1.x - p0.x) * 0.2f;
    float cy = (p0.y + p1.y) * 0.5f;
    float amp = (p1.y - p0.y) * 0.18f;
    ImVec2 pts[5] = {
        ImVec2(x0, cy),
        ImVec2(x0 + (x1 - x0) * 0.25f, cy - amp),
        ImVec2(x0 + (x1 - x0) * 0.5f, cy + amp),
        ImVec2(x0 + (x1 - x0) * 0.75f, cy - amp),
        ImVec2(x1, cy),
    };
    for (int i = 0; i < 4; ++i) dl->AddLine(pts[i], pts[i + 1], col, 2.5f);
}
static void SetHeight(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float cy = (p0.y + p1.y) * 0.55f;
    float x0 = p0.x + (p1.x - p0.x) * 0.2f;
    float x1 = p1.x - (p1.x - p0.x) * 0.2f;
    dl->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), col, 3.0f);
    dl->AddRectFilled(ImVec2(x0 - 4, cy - 4), ImVec2(x0 + 4, cy + 4), col);
    dl->AddRectFilled(ImVec2(x1 - 4, cy - 4), ImVec2(x1 + 4, cy + 4), col);
}
static void Texture(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float cx = (p0.x + p1.x) * 0.5f;
    float cy = (p0.y + p1.y) * 0.5f;
    float x0 = p0.x + (p1.x - p0.x) * 0.25f;
    float x1 = p1.x - (p1.x - p0.x) * 0.25f;
    float y0 = p0.y + (p1.y - p0.y) * 0.25f;
    float y1 = p1.y - (p1.y - p0.y) * 0.25f;
    float sw = 1.5f;
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), col, 0.0f, 0, sw);
    dl->AddLine(ImVec2(cx, y0), ImVec2(cx, y1), col, sw);
    dl->AddLine(ImVec2(x0, cy), ImVec2(x1, cy), col, sw);
}
static void Vegetation(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float cx = (p0.x + p1.x) * 0.5f;
    float baseY = p1.y - (p1.y - p0.y) * 0.20f;
    float topY  = p0.y + (p1.y - p0.y) * 0.20f;
    float w = (p1.x - p0.x) * 0.05f;
    // Trunk
    dl->AddRectFilled(ImVec2(cx - w, baseY), ImVec2(cx + w, baseY - (baseY - topY) * 0.4f), col);
    // Canopy (triangle)
    float cw = (p1.x - p0.x) * 0.26f;
    float cyTop = topY + (baseY - topY) * 0.4f;
    float cyBot = baseY - (baseY - topY) * 0.4f;
    dl->AddTriangleFilled(ImVec2(cx, cyTop - cw * 0.5f),
                          ImVec2(cx - cw, cyBot),
                          ImVec2(cx + cw, cyBot), col);
}

static void CatBrush(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float x0 = p0.x + (p1.x - p0.x) * 0.25f, x1 = p1.x - (p1.x - p0.x) * 0.25f;
    float y0 = p0.y + (p1.y - p0.y) * 0.25f, y1 = p1.y - (p1.y - p0.y) * 0.25f;
    dl->AddLine(ImVec2(x0, y1), ImVec2(x1, y0), col, 4.0f);
    dl->AddLine(ImVec2(x0 + 4, y1), ImVec2(x1 - 4, y0 - 4), col, 2.0f);
}
static void CatVertex(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float cx = (p0.x + p1.x) * 0.5f, cy = (p0.y + p1.y) * 0.5f;
    float r = (p1.x - p0.x) * 0.10f;
    dl->AddCircleFilled(ImVec2(cx, cy), r, col);
    float arm = (p1.x - p0.x) * 0.22f;
    dl->AddLine(ImVec2(cx, cy), ImVec2(cx + arm, cy), col, 2.0f);
    dl->AddLine(ImVec2(cx, cy), ImVec2(cx - arm * 0.7f, cy + arm * 0.7f), col, 2.0f);
    dl->AddLine(ImVec2(cx, cy), ImVec2(cx, cy - arm), col, 2.0f);
}
static void CatProps(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float s = (p1.x - p0.x) * 0.26f;
    float cx = (p0.x + p1.x) * 0.5f, cy = (p0.y + p1.y) * 0.5f;
    float off = s * 0.35f;
    ImVec2 f0(cx - s, cy + s), f1(cx + s, cy + s),
           f2(cx + s, cy - s), f3(cx - s, cy - s);
    ImVec2 b0(f0.x + off, f0.y - off), b1(f1.x + off, f1.y - off),
           b2(f2.x + off, f2.y - off), b3(f3.x + off, f3.y - off);
    float w = 1.5f;
    dl->AddQuad(f0, f1, f2, f3, col, w);
    dl->AddQuad(b0, b1, b2, b3, col, w);
    dl->AddLine(f0, b0, col, w); dl->AddLine(f1, b1, col, w);
    dl->AddLine(f2, b2, col, w); dl->AddLine(f3, b3, col, w);
}
static void CatVegetation(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float cx = (p0.x + p1.x) * 0.5f;
    float baseY = p1.y - (p1.y - p0.y) * 0.15f;
    float topY = p0.y + (p1.y - p0.y) * 0.15f;
    float trunkW = (p1.x - p0.x) * 0.04f;
    float trunkTop = baseY - (baseY - topY) * 0.45f;
    dl->AddRectFilled(ImVec2(cx - trunkW, baseY), ImVec2(cx + trunkW, trunkTop), col);
    float cw = (p1.x - p0.x) * 0.28f;
    dl->AddTriangleFilled(ImVec2(cx, topY),
                           ImVec2(cx - cw, trunkTop + (baseY - trunkTop) * 0.15f),
                           ImVec2(cx + cw, trunkTop + (baseY - trunkTop) * 0.15f), col);
}
static void CatTerrain(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float x0 = p0.x + (p1.x - p0.x) * 0.2f, x1 = p1.x - (p1.x - p0.x) * 0.2f;
    float base = p1.y - (p1.y - p0.y) * 0.25f;
    float h = (p1.y - p0.y) * 0.35f;
    dl->AddBezierCubic(ImVec2(x0, base), ImVec2(x0 + (x1 - x0) * 0.3f, base - h),
                       ImVec2(x0 + (x1 - x0) * 0.5f, base - h * 0.3f),
                       ImVec2(x0 + (x1 - x0) * 0.55f, base - h * 0.2f), col, 2.5f);
    dl->AddBezierCubic(ImVec2(x0 + (x1 - x0) * 0.55f, base - h * 0.2f),
                       ImVec2(x0 + (x1 - x0) * 0.8f, base - h * 0.5f),
                       ImVec2(x1, base), ImVec2(x1, base), col, 2.5f);
    float sx0 = x0 + (x1 - x0) * 0.05f, sx1 = x0 + (x1 - x0) * 0.5f;
    dl->AddBezierCubic(ImVec2(sx0, base),
                       ImVec2(sx0 + (sx1 - sx0) * 0.4f, base - h * 0.5f),
                       ImVec2(sx0 + (sx1 - sx0) * 0.6f, base - h * 0.5f),
                       ImVec2(sx1, base), col, 2.5f);
    dl->AddLine(ImVec2(x0 - 2, base), ImVec2(x1 + 2, base), col, 1.5f);
}
static void CatLayers(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float w = (p1.x - p0.x) * 0.5f, h = (p1.y - p0.y) * 0.16f;
    float cx = (p0.x + p1.x) * 0.5f, cy = (p0.y + p1.y) * 0.5f;
    float off = w * 0.12f;
    ImVec2 r[] = {
        ImVec2(cx - w * 0.5f, cy - h * 1.5f),
        ImVec2(cx + w * 0.5f, cy - h * 0.5f),
        ImVec2(cx - w * 0.5f + off, cy - h * 0.5f),
        ImVec2(cx + w * 0.5f + off, cy + h * 0.5f),
        ImVec2(cx - w * 0.5f + off * 2, cy + h * 0.5f),
        ImVec2(cx + w * 0.5f + off * 2, cy + h * 1.5f),
    };
    for (int i = 0; i < 3; ++i)
        dl->AddRect(r[i * 2], r[i * 2 + 1], col, 0.0f, 0, 2.0f);
}
static void CatEnv(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    float r = (p1.x - p0.x) * 0.14f;
    dl->AddCircleFilled(c, r, col);
    float rayOut = r * 1.7f, rayIn = r * 1.15f;
    for (int i = 0; i < 8; ++i) {
        float a = i * (3.14159265f * 2.0f / 8.0f);
        float ca = cosf(a), sa = sinf(a);
        dl->AddLine(ImVec2(c.x + rayIn * ca, c.y + rayIn * sa),
                    ImVec2(c.x + rayOut * ca, c.y + rayOut * sa), col, 2.0f);
    }
}
static void CatView(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    ImVec2 c((p0.x + p1.x) * 0.5f, (p0.y + p1.y) * 0.5f);
    float w = (p1.x - p0.x) * 0.28f, h = (p1.y - p0.y) * 0.16f;
    dl->AddBezierCubic(ImVec2(c.x - w, c.y), ImVec2(c.x - w * 0.5f, c.y - h),
                       ImVec2(c.x + w * 0.5f, c.y - h), ImVec2(c.x + w, c.y), col, 2.5f);
    dl->AddBezierCubic(ImVec2(c.x + w, c.y), ImVec2(c.x + w * 0.5f, c.y + h),
                       ImVec2(c.x - w * 0.5f, c.y + h), ImVec2(c.x - w, c.y), col, 2.5f);
    dl->AddCircleFilled(c, h * 0.6f, col);
}
static void CatFile(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    float x0 = p0.x + (p1.x - p0.x) * 0.22f;
    float x1 = p1.x - (p1.x - p0.x) * 0.22f;
    float y0 = p0.y + (p1.y - p0.y) * 0.25f;
    float y1 = p1.y - (p1.y - p0.y) * 0.25f;
    float w = 1.5f;
    // Folder shape.
    dl->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), col, 3.0f);
    dl->AddRect(ImVec2(x0, y0), ImVec2(x1, y1), IM_COL32(0, 0, 0, 80), 3.0f, 0, w);
    // Folder tab.
    dl->AddRectFilled(ImVec2(x0, y0 - (y1 - y0) * 0.3f),
                       ImVec2(x0 + (x1 - x0) * 0.4f, y0), col, 3.0f);
}

static void CatBuild(ImDrawList* dl, ImVec2 p0, ImVec2 p1, ImU32 col) {
    // Isometric block: three visible faces with distinct shades.
    float cx = (p0.x + p1.x) * 0.5f;
    float cy = (p0.y + p1.y) * 0.5f;
    float w = (p1.x - p0.x) * 0.24f;
    float h = (p1.y - p0.y) * 0.18f;
    ImVec2 top(cx, cy - h),
           tl(cx - w, cy - h * 0.5f),
           tr(cx + w, cy - h * 0.5f),
           ce(cx, cy),
           bl(cx - w, cy + h * 0.5f),
           br(cx + w, cy + h * 0.5f),
           bot(cx, cy + h);
    // Darken a colour by multiplying channels (f < 1 darkens).
    auto dim = [](ImU32 c, float f) {
        return ((ImU32)((c & 0xFF) * f)) |
               ((ImU32)(((c >> 8) & 0xFF) * f) << 8) |
               ((ImU32)(((c >> 16) & 0xFF) * f) << 16) |
               (c & 0xFF000000);
    };
    ImU32 topCol = col;
    ImU32 leftCol = dim(col, 0.78f);
    ImU32 rightCol = dim(col, 0.55f);
    // Top diamond (convex): top -> tl -> ce -> tr.
    dl->AddQuadFilled(top, tl, ce, tr, topCol);
    // Left parallelogram: tl -> bl -> bot -> ce.
    dl->AddQuadFilled(tl, bl, bot, ce, leftCol);
    // Right parallelogram: tr -> ce -> bot -> br.
    dl->AddQuadFilled(tr, ce, bot, br, rightCol);
    // Outline edges so the cube reads even at small sizes.
    dl->AddLine(top, tl, IM_COL32(0, 0, 0, 120), 1.0f);
    dl->AddLine(top, tr, IM_COL32(0, 0, 0, 120), 1.0f);
    dl->AddLine(ce, bl, IM_COL32(0, 0, 0, 120), 1.0f);
    dl->AddLine(ce, br, IM_COL32(0, 0, 0, 120), 1.0f);
    dl->AddLine(bot, bl, IM_COL32(0, 0, 0, 120), 1.0f);
    dl->AddLine(bot, br, IM_COL32(0, 0, 0, 120), 1.0f);
}


typedef void (*IconFn)(ImDrawList*, ImVec2, ImVec2, ImU32);
static IconFn brushIcon(int type) {
    switch (type) {
        case Terrain::BrushParams::Raise:    return &Raise;
        case Terrain::BrushParams::Lower:    return &Lower;
        case Terrain::BrushParams::Smooth:   return &Smooth;
        case Terrain::BrushParams::Flatten:  return &Flatten;
        case Terrain::BrushParams::Noise:    return &Noise;
        case Terrain::BrushParams::Set:      return &SetHeight;
        case Terrain::BrushParams::Texture:  return &Texture;
        case Terrain::BrushParams::Vegetation: return &Vegetation;
        default: return nullptr;
    }
}
static IconFn catIcon(int cat) {
    switch (cat) {
        case App::CatBrush:      return &CatBrush;
        case App::CatVertex:     return &CatVertex;
        case App::CatProps:      return &CatProps;
        case App::CatVegetation: return &CatVegetation;
        case App::CatBuild:      return &CatBuild;
        case App::CatTerrain:    return &CatTerrain;
        case App::CatLayers:     return &CatLayers;
        case App::CatEnv:        return &CatEnv;
        case App::CatView:       return &CatView;
        case App::CatFile:       return &CatFile;
        default: return nullptr;
    }
}
static const char* catName(int cat) {
    switch (cat) {
        case App::CatBrush:    return "Brush";
        case App::CatVertex:   return "Vertex";
        case App::CatProps:      return "Props";
        case App::CatVegetation: return "Vegetation";
        case App::CatBuild:      return "Build";
        case App::CatTerrain:    return "Terrain";
        case App::CatLayers:   return "Layers";
        case App::CatEnv:      return "Environment";
        case App::CatView:       return "View";
        case App::CatFile:       return "File";
        default: return "?";
    }
}

} // namespace icons

void App::selectCategory(int cat) {
    if (cat < 0 || cat >= CatCount) return;
    // Leaving the vertex panel turns wireframe off (it was enabled for vertex
    // picking); entering it forces wireframe on.
    if (activeCategory_ == CatVertex && cat != CatVertex) wireframe_ = false;
    activeCategory_ = cat;
    if (cat == CatBrush)  toolMode_ = ToolPaint;
    else if (cat == CatVertex) { toolMode_ = ToolVertex; wireframe_ = true; }
    else if (cat == CatProps)  toolMode_ = ToolProp;
    else if (cat == CatVegetation) {
        toolMode_ = ToolPaint;
        brush_.type = Terrain::BrushParams::Vegetation;
    }
    else if (cat == CatBuild) toolMode_ = ToolBuild;
}

void App::drawLeftPanel() {
    const float railW = 46.0f;
    float panelH = float(std::max(200, fbHeight_ - 80));
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(320, panelH), ImGuiCond_Always);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove;
    ImGui::Begin("##leftpanel", nullptr, wf);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImGuiStyle& style = ImGui::GetStyle();

    const float cellH = 40.0f;
    const float cellPad = 3.0f;
    const float iconPad = 7.0f;
    const ImVec4 activeBg = style.Colors[ImGuiCol_ButtonHovered];
    const ImVec4 hoverBg  = style.Colors[ImGuiCol_ButtonHovered];
    const ImVec4 idleBg   = style.Colors[ImGuiCol_ChildBg];

    auto drawCell = [&](int idx, const char* label, icons::IconFn fn,
                        ImVec2 p0, ImVec2 p1) {
        bool active = (activeCategory_ == idx);
        bool hover = ImGui::IsMouseHoveringRect(p0, p1) &&
                     ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByPopup);
        ImU32 bg = active ? ImGui::ColorConvertFloat4ToU32(activeBg) :
                   hover  ? ImGui::ColorConvertFloat4ToU32(hoverBg) :
                            ImGui::ColorConvertFloat4ToU32(idleBg);
        if (active || hover) dl->AddRectFilled(p0, p1, bg, 6.0f);
        ImU32 col = active ? IM_COL32(255, 230, 110, 255) :
                    hover  ? IM_COL32(255, 255, 255, 255) :
                            IM_COL32(210, 210, 210, 255);
        if (fn) fn(dl, ImVec2(p0.x + iconPad, p0.y + iconPad),
                       ImVec2(p1.x - iconPad, p1.y - iconPad), col);
        ImGui::SetCursorScreenPos(p0);
        ImGui::InvisibleButton(label, ImVec2(p1.x - p0.x, p1.y - p0.y));
        if (hover) ImGui::SetTooltip("%s", label);
        if (ImGui::IsItemClicked()) selectCategory(idx);
    };

    // --- Rail (left column, full height) ---
    ImGui::BeginChild("##rail", ImVec2(railW, 0), true, ImGuiWindowFlags_NoScrollbar);
    ImVec2 railStart = ImGui::GetCursorScreenPos();
    int order[CatCount + 2] = { CatBrush, CatVertex, CatProps, CatVegetation,
                                 CatBuild, -1,
                                 CatTerrain, CatLayers, CatEnv, CatView, -1,
                                 CatFile };
    ImVec2 cursor = railStart;
    for (int o = 0; o < CatCount + 2; ++o) {
        int cat = order[o];
        ImVec2 p0 = cursor;
        ImVec2 p1 = ImVec2(p0.x + railW - 2 * cellPad, p0.y + cellH);
        if (cat < 0) {
            float dy = p0.y + cellH * 0.5f;
            dl->AddLine(ImVec2(p0.x + 6, dy), ImVec2(p1.x - 6, dy),
                        IM_COL32(120, 120, 120, 255), 1.5f);
        } else {
            char lbl[16];
            std::snprintf(lbl, sizeof(lbl), "##cat%d", cat);
            drawCell(cat, icons::catName(cat), icons::catIcon(cat), p0, p1);
        }
        cursor.y += cellH + cellPad;
    }
    ImGui::EndChild();

    // --- Content (right column, fills remaining width) ---
    ImGui::SameLine();
    ImGui::BeginChild("##content", ImVec2(0, 0), true);
    switch (activeCategory_) {
        case CatBrush:      drawBrushContent();      break;
        case CatVertex:     drawVertexContent();     break;
        case CatProps:      drawPropsContent();      break;
        case CatVegetation: drawVegetationContent(); break;
        case CatBuild:      drawBuildContent();      break;
        case CatTerrain:    drawTerrainContent();     break;
        case CatLayers:     drawLayersContent();      break;
        case CatEnv:        drawEnvContent();         break;
        case CatView:       drawViewContent();        break;
        case CatFile:       drawFileContent();        break;
    }
    ImGui::EndChild();

    ImGui::End();
}

void App::drawBrushBar() {
    const float cellSize = 44.0f;
    const float gap = 4.0f;
    const float pad = 8.0f;
    const int count = 8;
    float totalW = float(count) * cellSize + float(count - 1) * gap + 2 * pad;
    float x = float(fbWidth_) * 0.5f - totalW * 0.5f;
    float y = float(fbHeight_) - cellSize - 2 * pad - 14.0f;

    ImGui::SetNextWindowPos(ImVec2(x, y), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(totalW, cellSize + 2 * pad), ImGuiCond_Always);
    ImGuiWindowFlags wf = ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse;
    ImGui::Begin("##brushbar", nullptr, wf);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec4 activeBg = ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered];
    const ImVec4 idleBg   = ImGui::GetStyle().Colors[ImGuiCol_FrameBg];
    const ImVec2 wPos = ImGui::GetWindowPos();
    const float iconPad = 7.0f;
    const char* names[] = { "Raise", "Lower", "Smooth", "Flatten",
                            "Noise", "Set Height", "Texture", "Vegetation" };

    for (int i = 0; i < count; ++i) {
        ImVec2 p0 = ImVec2(wPos.x + pad + float(i) * (cellSize + gap),
                            wPos.y + pad);
        ImVec2 p1 = ImVec2(p0.x + cellSize, p0.y + cellSize);
        bool active = (brush_.type == i && toolMode_ == ToolPaint);
        bool hover = ImGui::IsMouseHoveringRect(p0, p1) &&
                     ImGui::IsWindowHovered();
        ImU32 bg = active ? ImGui::ColorConvertFloat4ToU32(activeBg) :
                   hover  ? ImGui::ColorConvertFloat4ToU32(activeBg) :
                            ImGui::ColorConvertFloat4ToU32(idleBg);
        dl->AddRectFilled(p0, p1, bg, 6.0f);
        ImU32 col = active ? IM_COL32(255, 230, 110, 255) :
                    hover  ? IM_COL32(255, 255, 255, 255) :
                             IM_COL32(210, 210, 210, 255);
        auto fn = icons::brushIcon(i);
        if (fn) fn(dl, ImVec2(p0.x + iconPad, p0.y + iconPad),
                       ImVec2(p1.x - iconPad, p1.y - iconPad), col);
        ImGui::SetCursorScreenPos(p0);
        char lbl[16];
        std::snprintf(lbl, sizeof(lbl), "##b%d", i);
        ImGui::InvisibleButton(lbl, ImVec2(cellSize, cellSize));
        if (hover) ImGui::SetTooltip("%s", names[i]);
        if (ImGui::IsItemClicked()) {
            brush_.type = (Terrain::BrushParams::Type)i;
            toolMode_ = ToolPaint;
            activeCategory_ = (i == Terrain::BrushParams::Vegetation)
                              ? CatVegetation : CatBrush;
        }
    }
    ImGui::End();
}

// --------------------------------------------------------------------------
// Content panels (one per rail category).
// --------------------------------------------------------------------------
void App::drawBrushContent() {
    ImGui::TextDisabled("Brush %s", brushTypeName(brush_.type));
    ImGui::Separator();
    ImGui::SliderFloat("Radius",   &brush_.radius,   1.0f, terrain_.worldSize() * 0.4f, "%.1f");
    ImGui::SliderFloat("Strength", &brush_.strength, 0.01f, 5.0f, "%.2f");
    const char* fnames[] = { "Smooth", "Linear", "Constant" };
    ImGui::Combo("Falloff", &brush_.falloff, fnames, 3);

    if (brush_.type == Terrain::BrushParams::Flatten ||
        brush_.type == Terrain::BrushParams::Set) {
        ImGui::SliderFloat("Target Height", &brush_.target,
                           -20.0f, 40.0f, "%.1f");
    }
    if (brush_.type == Terrain::BrushParams::Texture) {
        ImGui::Separator();
        ImGui::Text("Texture layer:");
        for (int i = 0; i < terrain_.layerCount(); ++i) {
            bool sel = (brush_.textureLayer == i);
            const std::string& ln = terrain_.layers()[i].name;
            if (ImGui::RadioButton(ln.c_str(), sel)) brush_.textureLayer = i;
            if ((i + 1) % 2 != 0) ImGui::SameLine();
        }
        brush_.strength = std::clamp(brush_.strength, 0.01f, 1.0f);
    }
    if (brush_.type == Terrain::BrushParams::Vegetation) {
        ImGui::Separator();
        ImGui::TextWrapped("Open the Vegetation panel (rail) to import models "
                           "and pick which one to paint.");
        ImGui::Text("Active prototype: %s",
                    details_.activePrototype() >= 0
                        ? details_.prototype(details_.activePrototype()).name.c_str()
                        : "(none)");
        ImGui::Text("Instances: %d", details_.instanceCount());
        brush_.strength = std::clamp(brush_.strength, 0.05f, 2.0f);
    }
    if (brush_.radius != 0.0f) brushCursor_.setShape(brush_.radius);
}

void App::drawVertexContent() {
    ImGui::TextDisabled("Vertex editing (wireframe)");
    ImGui::Separator();
    const char* modes[] = { "Free XYZ", "Vertical (Y)", "Normal" };
    int dm = (int)vertexEditor_.dragMode();
    if (ImGui::Combo("Drag mode", &dm, modes, 3))
        vertexEditor_.setDragMode((VertexEditor::DragMode)dm);
    ImGui::Text("Shortcuts: V=Free, B=Vertical, N=Normal");
    ImGui::Separator();
    ImGui::Text("Selection: %d vertex%s",
                vertexEditor_.selectionCount(),
                vertexEditor_.selectionCount() == 1 ? "" : "es");
    if (ImGui::Button("Clear selection")) vertexEditor_.clearSelection();
    ImGui::Separator();
    // Falloff is shared with the brush tool; expose it here too so the user
    // does not have to switch categories while vertex editing.
    ImGui::SliderFloat("Radius",  &brush_.radius, 1.0f, terrain_.worldSize() * 0.4f, "%.1f");
    const char* fnames[] = { "Smooth", "Linear", "Constant" };
    ImGui::Combo("Falloff", &brush_.falloff, fnames, 3);
    if (brush_.radius != 0.0f) brushCursor_.setShape(brush_.radius);
    ImGui::Separator();
    ImGui::TextWrapped("Click a vertex to select. Ctrl+click adds to the "
                       "selection. Drag the gizmo to pull vertices; the "
                       "radius/falloff controls falloff.");
}

void App::drawPropsContent() {
    if (ImGui::Button("Import glTF / VRM...")) {
        std::string path = openFileDialog();
        if (!path.empty()) importModel(path);
    }
    ImGui::SameLine();
    ImGui::SliderFloat("Size", &propTargetSize_, 1.0f, 40.0f, "%.1f");
    ImGui::Separator();
    ImGui::Text("Gizmo:");
    ImGui::SameLine();
    int mode = gizmo_.mode();
    if (ImGui::RadioButton("Move", mode == Gizmo::Translate)) gizmo_.setMode(Gizmo::Translate);
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", mode == Gizmo::Rotate)) gizmo_.setMode(Gizmo::Rotate);
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale", mode == Gizmo::Scale)) gizmo_.setMode(Gizmo::Scale);
    ImGui::Separator();
    ImGui::Text("Placed props: %d", props_.count());
    ImGui::BeginChild("proplist", ImVec2(0, 120), true);
    for (const auto& p : props_.props()) {
        bool sel = (p.id == props_.selectedId());
        ImGui::PushID(p.id);
        if (ImGui::Selectable(p.displayName.c_str(), sel)) {
            props_.select(p.id);
            toolMode_ = ToolProp;
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    Prop* sel = props_.selected();
    if (sel) {
        ImGui::Separator();
        ImGui::Text("Selected: %s", sel->displayName.c_str());
        ImGui::DragFloat3("Position", &sel->position[0], 0.5f);
        ImGui::SliderFloat("Yaw",   &sel->rotationEuler.y, -3.14159f, 3.14159f, "%.2f");
        ImGui::SliderFloat("Pitch", &sel->rotationEuler.x, -1.5708f,  1.5708f,  "%.2f");
        ImGui::SliderFloat("Roll",  &sel->rotationEuler.z, -3.14159f, 3.14159f, "%.2f");
        float uniformScale = sel->scale.x;
        if (ImGui::SliderFloat("Scale", &uniformScale, 0.01f, 20.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
            sel->scale = glm::vec3(uniformScale);
        }
        if (ImGui::Button("Snap to ground")) {
            float h = terrain_.heightAtWorld(sel->position.x, sel->position.z);
            float bottom = sel->model ? sel->model->aabbMin().y : 0.0f;
            sel->position.y = h - bottom * sel->scale.y;
        }
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) {
            auto m = sel->model;
            int newId = props_.addProp(m, sel->position,
                                      terrain_.heightAtWorld(sel->position.x, sel->position.z),
                                      0.0f, sel->displayName + " copy");
            if (newId >= 0) {
                Prop* np = props_.findProp(newId);
                if (np) {
                    np->rotationEuler = sel->rotationEuler;
                    np->scale = sel->scale;
                    np->position.y = sel->position.y;
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            props_.removeProp(sel->id);
        }
    }
}

void App::drawVegetationContent() {
    ImGui::TextDisabled("Vegetation / Details");
    ImGui::Separator();

    // Import section.
    if (ImGui::Button("Import Model...")) {
        std::string path = openFileDialog();
        if (!path.empty()) {
            auto model = std::make_shared<Model>();
            if (model->loadFromFile(path)) {
                modelLibrary_.push_back(model);
                std::filesystem::path fp(path);
                // Default size: aim for ~15 world units unless the model is
                // already small (then keep its native size).
                float nativeMax = std::max({
                    model->aabbSize().x, model->aabbSize().y, model->aabbSize().z});
                float defSize = nativeMax > 100.0f ? 15.0f : std::max(2.0f, nativeMax);
                details_.addPrototype(model, fp.filename().string(), defSize);
            } else {
                std::cerr << "Failed to load: " << path << "\n";
            }
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear Instances"))
        details_.clearInstances();
    ImGui::SameLine();
    ImGui::Text("(%d)", details_.instanceCount());

    ImGui::Separator();

    // Prototype palette — pick which model to paint with.
    if (details_.prototypeCount() == 0) {
        ImGui::TextDisabled("No prototypes loaded. Click \"Import Model...\" "
                            "to load a glTF/GLB file.");
    }
    for (int i = 0; i < details_.prototypeCount(); ++i) {
        const auto& p = details_.prototype(i);
        bool sel = (details_.activePrototype() == i);
        ImGui::PushID(i);
        // Header row: selectable name + delete button.
        char hdr[256];
        std::snprintf(hdr, sizeof(hdr), "%s  (%d inst)",
                      p.name.c_str(), 0);
        if (ImGui::Selectable(p.name.c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(0, 0))) {
            if (ImGui::IsMouseDoubleClicked(0)) {
                // Double-click also paints one instance at terrain centre.
            }
            details_.setActivePrototype(i);
            brush_.type = Terrain::BrushParams::Vegetation;
            toolMode_ = ToolPaint;
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) { details_.removePrototype(i); ImGui::PopID(); break; }

        // Show native model size so user understands the scale.
        if (p.model && p.model->valid()) {
            glm::vec3 sz = p.model->aabbSize();
            ImGui::Indent();
            ImGui::TextDisabled("native %.1f x %.1f x %.1f", sz.x, sz.y, sz.z);
            ImGui::Unindent();
        }

        // Per-prototype settings (only for the active prototype).
        if (sel) {
            auto* proto = details_.prototypeMutable(i);
            if (proto) {
                ImGui::Indent();
                ImGui::SliderFloat("Target Size", &proto->targetSize, 0.5f, 80.0f, "%.1f");
                ImGui::SliderFloat("Min Scale",   &proto->minScale,   0.1f, 3.0f, "%.2f");
                ImGui::SliderFloat("Max Scale",   &proto->maxScale,   0.1f, 3.0f, "%.2f");
                if (proto->minScale > proto->maxScale) proto->minScale = proto->maxScale;
                ImGui::SliderFloat("Random Yaw",  &proto->randomYaw,  0.0f, 1.0f, "%.2f");
                ImGui::Unindent();
            }
        }
        ImGui::PopID();
        ImGui::Separator();
    }

    // Brush settings for painting density.
    ImGui::TextDisabled("Brush");
    ImGui::SliderFloat("Radius",   &brush_.radius,   1.0f, terrain_.worldSize() * 0.4f, "%.1f");
    ImGui::SliderFloat("Density",  &brush_.strength, 0.05f, 2.0f, "%.2f");
    const char* fnames[] = { "Smooth", "Linear", "Constant" };
    ImGui::Combo("Falloff", &brush_.falloff, fnames, 3);
    if (brush_.radius != 0.0f) brushCursor_.setShape(brush_.radius);

    ImGui::Separator();
    ImGui::TextWrapped("Left-drag: paint instances. Ctrl+left-drag: erase. "
                       "Density controls how many per stroke step.");
}

void App::drawBuildContent() {
    ImGui::TextDisabled("Build");
    ImGui::Separator();

    float w = build_.blockWidth(), h = build_.blockHeight();
    if (ImGui::SliderFloat("Block width",  &w, 0.5f, 16.0f, "%.2f")) build_.setBlockSize(w, h);
    if (ImGui::SliderFloat("Block height", &h, 0.5f, 16.0f, "%.2f")) build_.setBlockSize(w, h);

    float sunk = build_.sunkDepth();
    if (ImGui::SliderFloat("Foundation sink", &sunk, 0.0f, 0.95f, "%.2f"))
        build_.setSunkDepth(sunk);

    float step = build_.gridStep();
    if (ImGui::SliderFloat("Grid step", &step, 0.25f, 8.0f, "%.2f"))
        build_.setGridStep(step);

    glm::vec3 c = build_.color();
    float cf[3] = { c.r, c.g, c.b };
    if (ImGui::ColorEdit3("Block color", cf))
        build_.setColor(glm::vec3(cf[0], cf[1], cf[2]));

    ImGui::Separator();
    ImGui::Text("Placed blocks: %d", build_.count());
    ImGui::SameLine();
    if (ImGui::Button("Clear All")) {
        build_.clear();
        selectedBlockId_ = -1;
    }

    ImGui::Separator();
    ImGui::TextDisabled("Selected block");
    if (selectedBlockId_ >= 0) {
        const BuildSystem::Block* b = build_.findBlock(selectedBlockId_);
        if (b) {
            ImGui::Text("id=%d  type=%s", b->id,
                        b->type == BuildSystem::Foundation ? "Foundation" : "Wall");
            ImGui::Text("pos (%.1f, %.1f, %.1f)",
                        b->position.x, b->position.y, b->position.z);
            if (ImGui::Button("Delete (Del)")) {
                build_.removeBlock(selectedBlockId_);
                selectedBlockId_ = -1;
            }
        }
    } else {
        ImGui::TextDisabled("(none)");
    }

    ImGui::Separator();
    ImGui::TextWrapped("Click terrain + drag: stretch a rectangle to fill with "
                       "foundation blocks (same level as neighbours). "
                       "Click a block's SIDE face: extend the foundation. "
                       "Click a block's TOP face: place a wall on top. "
                       "Ctrl+drag: erase blocks in the rectangle. Del: remove selected.");
}

void App::drawTerrainContent() {
    ImGui::TextDisabled("Terrain");
    ImGui::Separator();
    ImGui::Text("Grid: %d x %d", terrain_.gridX(), terrain_.gridZ());
    ImGui::Text("World size: %.0f m", terrain_.worldSize());
    ImGui::Text("Height range: %.2f .. %.2f", terrain_.minHeight(), terrain_.maxHeight());
    ImGui::Separator();
    if (ImGui::Button("Flatten")) terrain_.flatten(0.0f);
    ImGui::SameLine();
    if (ImGui::Button("Generate Hills")) terrain_.generateHills();
}

void App::drawLayersContent() {
    ImGui::TextDisabled("Texture layers");
    ImGui::Separator();
    for (int i = 0; i < terrain_.layerCount(); ++i) {
        const auto& l = terrain_.layers()[i];
        ImGui::PushID(i);
        ImGui::Text("%d: %s", i, l.name.c_str());
        if (l.albedo) {
            ImGui::SameLine();
            ImGui::Image((ImTextureID)(intptr_t)l.albedo, ImVec2(48, 48));
        }
        if (ImGui::Button("Albedo...")) {
            std::string p = openFileDialog("Image", "*.png;*.jpg;*.jpeg;*.tga;*.bmp");
            if (!p.empty()) terrain_.loadLayerAlbedo(i, p);
        }
        ImGui::SameLine();
        if (ImGui::Button("Normal...")) {
            std::string p = openFileDialog("Image", "*.png;*.jpg;*.jpeg;*.tga;*.bmp");
            if (!p.empty()) terrain_.loadLayerNormal(i, p);
        }
        float ts = l.tileSize;
        if (ImGui::SliderFloat("Tile size", &ts, 0.5f, 64.0f, "%.1f"))
            terrain_.setLayerTileSize(i, ts);
        ImGui::PopID();
        ImGui::Separator();
    }
    if (ImGui::Button("Reset Splat")) terrain_.resetSplat();
}

void App::drawEnvContent() {
    ImGui::TextDisabled("Environment");
    ImGui::Separator();
    ImGui::Text("Light");
    ImGui::SliderFloat("Light azimuth",   &lightAzimuth_,   0.0f, 6.28f);
    ImGui::SliderFloat("Light elevation", &lightElevation_, 0.1f, 1.55f);
    ImGui::Separator();
    ImGui::Text("Skybox");
    ImGui::SliderFloat("Sky exposure", &skyExposure_, 0.0f, 3.0f, "%.2f");
    if (ImGui::Button("Import sky...")) {
        std::string p = openFileDialog("Sky image",
            "*.hdr;*.png;*.jpg;*.jpeg;*.tga;*.bmp");
        if (!p.empty()) skybox_.loadEquirect(skyboxConvertShader_, p);
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset to default")) skybox_.resetToDefault();
    if (skybox_.isDefault()) {
        ImGui::TextDisabled("Procedural gradient sky");
    } else {
        ImGui::TextDisabled("Imported: %s",
            std::filesystem::path(skybox_.importedPath()).filename().string().c_str());
    }
}

void App::drawViewContent() {
    ImGui::TextDisabled("View");
    ImGui::Separator();
    ImGui::Checkbox("Wireframe", &wireframe_);
    ImGui::Checkbox("Show cursor", &showCursor_);
    ImGui::Checkbox("Show help (H)", &showHelp_);
    ImGui::ColorEdit3("Cursor color", cursorColor_);
    ImGui::Separator();
    ImGui::Text("Camera");
    ImGui::Text("Distance: %.1f", camera_.distance());
    ImGui::Text("Target: (%.1f, %.1f, %.1f)",
                camera_.target().x, camera_.target().y, camera_.target().z);
    if (ImGui::Button("Reset View")) {
        camera_ = Camera();
        camera_.setViewport(fbWidth_, fbHeight_);
    }
}

void App::drawFileContent() {
    ImGui::TextDisabled("File");
    ImGui::Separator();

    if (ImGui::Button("Save Scene...")) {
        std::string path = saveFileDialog("Scene", "*.scene");
        if (!path.empty()) {
            // Ensure .scene extension.
            if (path.find(".scene") == std::string::npos) path += ".scene";
            if (!saveScene(path))
                std::cerr << "Save failed: " << path << "\n";
        }
    }
    if (ImGui::Button("Load Scene...")) {
        std::string path = openFileDialog("Scene", "*.scene");
        if (!path.empty()) {
            if (!loadScene(path))
                std::cerr << "Load failed: " << path << "\n";
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Scene contents");
    ImGui::Text("Props:      %d", props_.count());
    ImGui::Text("Details:    %d", details_.instanceCount());
    ImGui::Text("Blocks:     %d", build_.count());
    ImGui::Text("Terrain:    %d x %d (%.0f m)", terrain_.gridX(), terrain_.gridZ(),
                terrain_.worldSize());
    ImGui::Text("Layers:     %d", terrain_.layerCount());
    ImGui::Text("Skybox:     %s", skybox_.isDefault() ? "procedural" : "imported");

    ImGui::Separator();
    ImGui::TextWrapped("Single binary .scene file: magic + JSON metadata + "
                       "heights + splat, with props/blocks/details embedded in JSON. "
                       "Asset paths are stored relative to the scene file.");
}


void App::drawHelpOverlay() {
    ImGui::SetNextWindowPos(ImVec2(float(fbWidth_) - 290, 10),
                            ImGuiCond_Always);
    ImGui::Begin("Help", &showHelp_,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("Controls:");
    ImGui::BulletText("Left rail: pick tool / settings panel");
    ImGui::BulletText("Bottom bar: pick a brush (switches to Brush mode)");
    ImGui::BulletText("Tab: cycle Brush / Prop / Vertex / Build tool");
    ImGui::BulletText("Right-drag: orbit camera");
    ImGui::BulletText("Middle-drag: pan camera");
    ImGui::BulletText("Scroll: zoom (Shift+scroll: brush size, Ctrl+scroll: strength)");
    if (toolMode_ == ToolPaint) {
        ImGui::Separator();
        ImGui::TextUnformatted("Brush tool:");
        ImGui::BulletText("Left-drag: paint terrain");
        ImGui::BulletText("1..8: brush type");
    } else if (toolMode_ == ToolProp) {
        ImGui::Separator();
        ImGui::TextUnformatted("Prop tool:");
        ImGui::BulletText("Left-click prop: select");
        ImGui::BulletText("Left-click empty: deselect");
        ImGui::BulletText("Drag gizmo axes to transform");
        ImGui::BulletText("T/R/S: gizmo mode");
    } else if (toolMode_ == ToolBuild) {
        ImGui::Separator();
        ImGui::TextUnformatted("Build tool:");
        ImGui::BulletText("Click+drag terrain: rectangle fill");
        ImGui::BulletText("Click block side: extend foundation");
        ImGui::BulletText("Click block top: place wall");
        ImGui::BulletText("Ctrl+drag: erase rectangle");
        ImGui::BulletText("Del: remove selected block");
    } else {
        ImGui::Separator();
        ImGui::TextUnformatted("Vertex tool (wireframe):");
        ImGui::BulletText("Left-click vertex: select");
        ImGui::BulletText("Ctrl+click: add to selection");
        ImGui::BulletText("Drag gizmo to pull vertices");
        ImGui::BulletText("V/B/N: drag mode (Free/Y/Normal)");
    }
    ImGui::Separator();
    ImGui::BulletText("F: wireframe   H: help");
    ImGui::BulletText("ESC: quit");
    ImGui::Separator();
    if (toolMode_ == ToolPaint) {
        ImGui::Text("Brush: %s (%s)  R=%.1f  S=%.2f",
                    brushTypeName(brush_.type), falloffName(brush_.falloff),
                    brush_.radius, brush_.strength);
    } else if (toolMode_ == ToolProp) {
        ImGui::Text("Props: %d   Selected: %s",
                    props_.count(),
                    props_.selectedId() >= 0 ? std::to_string(props_.selectedId()).c_str() : "none");
    } else if (toolMode_ == ToolBuild) {
        ImGui::Text("Blocks: %d   Selected: %s",
                    build_.count(),
                    selectedBlockId_ >= 0 ? std::to_string(selectedBlockId_).c_str() : "none");
    } else {
        const char* dm = vertexEditor_.dragMode() == VertexEditor::FreeXYZ  ? "Free XYZ" :
                         vertexEditor_.dragMode() == VertexEditor::Vertical ? "Vertical" : "Normal";
        ImGui::Text("Vertices: %d   Drag: %s",
                    vertexEditor_.selectionCount(), dm);
    }
    ImGui::End();
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

    glBindVertexArray(boxVao_);
    glDrawArrays(GL_LINES, 0, 24);
    glBindVertexArray(0);
}
