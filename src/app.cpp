#include "app.h"
#include "input.h"
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

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_MULTISAMPLE);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    terrain_.create();
    terrain_.generateHills();
    brushCursor_.create();
    brushCursor_.setShape(brush_.radius);
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
        terrain_.destroy();
        glfwDestroyWindow(window_);
        window_ = nullptr;
        glfwTerminate();
    }
}

int App::run() {
    if (!initWindow()) return 1;
    g_input.init(window_);
    if (!initOpenGL()) { shutdown(); return 1; }
    initImGui();

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
        // Convert window-space cursor to framebuffer-space for ray casting.
        double sx = g_input.mouseX() * (double)fbWidth_  / (double)winWidth_;
        double sy = g_input.mouseY() * (double)fbHeight_ / (double)winHeight_;
        glm::vec3 origin, dir;
        camera_.screenToRay((float)sx, (float)sy, origin, dir);
        glm::vec3 hit;
        if (terrain_.raycast(origin, dir, hit)) {
            // Scale strength by dt for framerate-independent continuous strokes.
            float amount = continuousStroke_ ? brush_.strength * dt * 60.0f
                                              : brush_.strength;
            Terrain::BrushParams step = brush_;
            step.strength = amount;
            terrain_.applyBrush(step, hit);
            lastPaintPoint_ = hit;
            hasPaintPoint_ = true;
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

    // Brush cursor (show both while hovering and while painting)
    if (showCursor_) {
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
    ImGui::SetNextWindowPos(ImVec2(float(fbWidth_) - 270, 10),
                            ImGuiCond_Always);
    ImGui::Begin("Help", &showHelp_,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("Controls:");
    ImGui::BulletText("Left-drag: paint terrain");
    ImGui::BulletText("Right-drag: orbit camera");
    ImGui::BulletText("Middle-drag: pan camera");
    ImGui::BulletText("Scroll: zoom");
    ImGui::BulletText("1..5: brush type");
    ImGui::BulletText("F: wireframe   H: help");
    ImGui::BulletText("ESC: quit");
    ImGui::Separator();
    ImGui::Text("Brush: %s (%s)  R=%.1f  S=%.2f",
                brushTypeName(brush_.type), falloffName(brush_.falloff),
                brush_.radius, brush_.strength);
    ImGui::End();
}
