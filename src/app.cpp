#include "app.h"
#include "input.h"
#include "model.h"
#include "file_dialog.h"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cmath>
#include <filesystem>

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

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    terrain_.create();
    terrain_.generateHills();
    brushCursor_.create();
    brushCursor_.setShape(brush_.radius);
    gizmo_.create();

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

    // Import any models passed on the command line (useful for testing).
    for (const auto& p : importArgs) importModel(p);

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

    // Zoom (scroll) — only when not over UI
    if (!overUI && g_input.scrollDelta() != 0.0f) {
        camera_.zoom(-g_input.scrollDelta() * 0.1f);
    }

    // Tab toggles between terrain brush and prop tools.
    if (g_input.keyPressed(GLFW_KEY_TAB)) {
        toolMode_ = (toolMode_ == ToolPaint) ? ToolProp : ToolPaint;
        painting_ = false;
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
                float amount = continuousStroke_ ? brush_.strength * dt * 60.0f
                                                  : brush_.strength;
                Terrain::BrushParams step = brush_;
                step.strength = amount;
                terrain_.applyBrush(step, hit);
                lastPaintPoint_ = hit;
                hasPaintPoint_ = true;
            }
        }
    } else {
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
    }

    // Keyboard shortcuts for brush types
    if (g_input.keyPressed(GLFW_KEY_1)) brush_.type = Terrain::BrushParams::Raise;
    if (g_input.keyPressed(GLFW_KEY_2)) brush_.type = Terrain::BrushParams::Lower;
    if (g_input.keyPressed(GLFW_KEY_3)) brush_.type = Terrain::BrushParams::Smooth;
    if (g_input.keyPressed(GLFW_KEY_4)) brush_.type = Terrain::BrushParams::Flatten;
    if (g_input.keyPressed(GLFW_KEY_5)) brush_.type = Terrain::BrushParams::Noise;
    if (g_input.keyPressed(GLFW_KEY_F)) wireframe_ = !wireframe_;
    if (g_input.keyPressed(GLFW_KEY_H)) showHelp_ = !showHelp_;

    // Gizmo mode shortcuts (only relevant in prop tool).
    if (toolMode_ == ToolProp) {
        if (g_input.keyPressed(GLFW_KEY_T)) gizmo_.setMode(Gizmo::Translate);
        if (g_input.keyPressed(GLFW_KEY_R)) gizmo_.setMode(Gizmo::Rotate);
        if (g_input.keyPressed(GLFW_KEY_S)) gizmo_.setMode(Gizmo::Scale);
    }
}

void App::renderScene() {
    if (wireframe_) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
    else            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glm::mat4 view = camera_.view();
    glm::mat4 proj = camera_.projection();
    glm::mat4 vp = proj * view;

    terrainShader_.use();
    terrainShader_.setMat4("uViewProj", vp);
    terrainShader_.setMat4("uModel", glm::mat4(1.0f));
    glm::vec3 lightDir = lightDirFromAngles(lightAzimuth_, lightElevation_);
    terrainShader_.setVec3("uLightDir", lightDir);
    terrainShader_.setVec3("uCamPos", camera_.position());
    terrainShader_.setVec3("uBaseColor", glm::vec3(0.30f, 0.55f, 0.28f));
    terrainShader_.setVec3("uLowColor",  glm::vec3(0.20f, 0.18f, 0.14f));
    terrainShader_.setFloat("uMaxHeight", std::max(1.0f, terrain_.maxHeight()));

    terrain_.draw();

    // Props (after terrain, before cursor overlay). Props use their own shader.
    if (props_.count() > 0) {
        propShader_.use();
        props_.render(propShader_, vp, lightDir, camera_.position());
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

    // Brush cursor — only relevant in paint mode.
    if (showCursor_ && toolMode_ == ToolPaint) {
        double sx = g_input.mouseX() * (double)fbWidth_  / (double)winWidth_;
        double sy = g_input.mouseY() * (double)fbHeight_ / (double)winHeight_;
        glm::vec3 origin, dir;
        camera_.screenToRay((float)sx, (float)sy, origin, dir);
        glm::vec3 hit;
        if (terrain_.raycast(origin, dir, hit)) {
            glm::vec3 c(cursorColor_[0], cursorColor_[1], cursorColor_[2]);
            lineShader_.use();
            glm::mat4 model(1.0f);
            model = glm::translate(model, hit);
            lineShader_.setMat4("uViewProj", vp * model);
            lineShader_.setVec3("uColor", c);
            brushCursor_.draw(vp * model, hit, c, false);
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

    drawMainPanel();
    drawPropsPanel();
    if (showHelp_) drawHelpOverlay();

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void App::drawMainPanel() {
    ImGui::SetNextWindowPos(ImVec2(10, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(290, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Scene Editor");

    if (ImGui::CollapsingHeader("Brush", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* names[] = { "Raise", "Lower", "Smooth", "Flatten", "Noise", "Set Height" };
        for (int i = 0; i < 6; ++i) {
            bool sel = (brush_.type == i);
            if (ImGui::RadioButton(names[i], sel)) brush_.type = (Terrain::BrushParams::Type)i;
            if (i < 4 && (i + 1) % 2 != 0) ImGui::SameLine();
        }
        ImGui::Separator();

        ImGui::SliderFloat("Radius",   &brush_.radius,   1.0f, terrain_.worldSize() * 0.4f, "%.1f");
        ImGui::SliderFloat("Strength",  &brush_.strength, 0.01f, 5.0f, "%.2f");

        const char* fnames[] = { "Smooth", "Linear", "Constant" };
        ImGui::Combo("Falloff", &brush_.falloff, fnames, 3);

        if (brush_.type == Terrain::BrushParams::Flatten ||
            brush_.type == Terrain::BrushParams::Set) {
            ImGui::SliderFloat("Target Height", &brush_.target,
                               -20.0f, 40.0f, "%.1f");
        }

        if (brush_.radius != 0.0f) brushCursor_.setShape(brush_.radius);
    }

    if (ImGui::CollapsingHeader("Terrain", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Grid: %d x %d", terrain_.gridX(), terrain_.gridZ());
        ImGui::Text("World size: %.0f m", terrain_.worldSize());
        ImGui::Text("Height range: %.2f .. %.2f", terrain_.minHeight(), terrain_.maxHeight());
        if (ImGui::Button("Flatten")) terrain_.flatten(0.0f);
        ImGui::SameLine();
        if (ImGui::Button("Generate Hills")) terrain_.generateHills();
    }

    if (ImGui::CollapsingHeader("Display", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Wireframe", &wireframe_);
        ImGui::Checkbox("Show cursor", &showCursor_);
        ImGui::Checkbox("Show help (H)", &showHelp_);
        ImGui::ColorEdit3("Cursor color", cursorColor_);
        ImGui::Separator();
        ImGui::SliderFloat("Light azimuth",   &lightAzimuth_,   0.0f, 6.28f);
        ImGui::SliderFloat("Light elevation", &lightElevation_, 0.1f, 1.55f);
    }

    if (ImGui::CollapsingHeader("Camera")) {
        ImGui::Text("Distance: %.1f", camera_.distance());
        ImGui::Text("Target: (%.1f, %.1f, %.1f)",
                    camera_.target().x, camera_.target().y, camera_.target().z);
        if (ImGui::Button("Reset View")) {
            camera_ = Camera();
            camera_.setViewport(fbWidth_, fbHeight_);
        }
    }

    ImGui::End();
}

void App::drawHelpOverlay() {
    ImGui::SetNextWindowPos(ImVec2(float(fbWidth_) - 290, 10),
                            ImGuiCond_Always);
    ImGui::Begin("Help", &showHelp_,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("Controls:");
    ImGui::BulletText("Tab: toggle Brush / Prop tool");
    ImGui::BulletText("Right-drag: orbit camera");
    ImGui::BulletText("Middle-drag: pan camera");
    ImGui::BulletText("Scroll: zoom");
    if (toolMode_ == ToolPaint) {
        ImGui::Separator();
        ImGui::TextUnformatted("Brush tool:");
        ImGui::BulletText("Left-drag: paint terrain");
        ImGui::BulletText("1..5: brush type");
    } else {
        ImGui::Separator();
        ImGui::TextUnformatted("Prop tool:");
        ImGui::BulletText("Left-click prop: select");
        ImGui::BulletText("Left-click ground: move selected");
        ImGui::BulletText("Left-drag ground: drag selected");
    }
    ImGui::Separator();
    ImGui::BulletText("F: wireframe   H: help");
    ImGui::BulletText("ESC: quit");
    ImGui::Separator();
    if (toolMode_ == ToolPaint) {
        ImGui::Text("Brush: %s (%s)  R=%.1f  S=%.2f",
                    brushTypeName(brush_.type), falloffName(brush_.falloff),
                    brush_.radius, brush_.strength);
    } else {
        ImGui::Text("Props: %d   Selected: %s",
                    props_.count(),
                    props_.selectedId() >= 0 ? std::to_string(props_.selectedId()).c_str() : "none");
    }
    ImGui::End();
}

void App::drawPropsPanel() {
    ImGui::SetNextWindowPos(ImVec2(310, 10), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(300, 0), ImGuiCond_FirstUseEver);
    ImGui::Begin("Props");

    if (ImGui::Button("Import glTF / VRM...")) {
        std::string path = openFileDialog();
        if (!path.empty()) importModel(path);
    }
    ImGui::SameLine();
    ImGui::SliderFloat("Size", &propTargetSize_, 1.0f, 40.0f, "%.1f");

    ImGui::Separator();
    ImGui::Text("Tool: %s", toolMode_ == ToolPaint ? "Brush" : "Prop");
    ImGui::SameLine();
    if (ImGui::Button(toolMode_ == ToolPaint ? "Switch to Prop" : "Switch to Brush")) {
        toolMode_ = (toolMode_ == ToolPaint) ? ToolProp : ToolPaint;
    }

    if (toolMode_ == ToolProp) {
        ImGui::Text("Gizmo:");
        ImGui::SameLine();
        int mode = gizmo_.mode();
        if (ImGui::RadioButton("Move", mode == Gizmo::Translate)) gizmo_.setMode(Gizmo::Translate);
        ImGui::SameLine();
        if (ImGui::RadioButton("Rotate", mode == Gizmo::Rotate)) gizmo_.setMode(Gizmo::Rotate);
        ImGui::SameLine();
        if (ImGui::RadioButton("Scale", mode == Gizmo::Scale)) gizmo_.setMode(Gizmo::Scale);
    }

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
        ImGui::SliderFloat("Yaw", &sel->rotationEuler.y, -3.14159f, 3.14159f, "%.2f");
        ImGui::SliderFloat("Pitch", &sel->rotationEuler.x, -1.5708f, 1.5708f, "%.2f");
        ImGui::SliderFloat("Roll", &sel->rotationEuler.z, -3.14159f, 3.14159f, "%.2f");
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
