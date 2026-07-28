// Editor UI shell: ImGui frame, left tool rail, bottom brush bar, help
// overlay. The per-category panel contents live in app_panels.cpp; the
// vector icons live in ui_icons.cpp.
#include "app.h"
#include "input.h"
#include "ui_icons.h"
#include "ui_common.h"
#include <imgui.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <cstdio>
const char* brushTypeName(int t) {
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

const char* falloffName(int f) {
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

    // Brush value overlay: show radius/strength inside the cursor circle while
    // Shift or Ctrl is held (the modifiers used to scrub them via scroll).
    if (brushHasHit_ && toolMode_ == ToolPaint) {
        bool shift = g_input.keyDown(GLFW_KEY_LEFT_SHIFT) ||
                     g_input.keyDown(GLFW_KEY_RIGHT_SHIFT);
        bool ctrl  = g_input.keyDown(GLFW_KEY_LEFT_CONTROL) ||
                     g_input.keyDown(GLFW_KEY_RIGHT_CONTROL);
        if (shift || ctrl) {
            glm::vec4 clip = camera_.projection() * camera_.view() *
                             glm::vec4(brushHit_, 1.0f);
            if (clip.w > 0.0f) {
                float ndcX = clip.x / clip.w;
                float ndcY = clip.y / clip.w;
                float px = (ndcX * 0.5f + 0.5f) * float(fbWidth_);
                float py = (1.0f - (ndcY * 0.5f + 0.5f)) * float(fbHeight_);
                char buf[32];
                if (shift)
                    std::snprintf(buf, sizeof(buf), "R %.1f", brush_.radius);
                else
                    std::snprintf(buf, sizeof(buf), "S %.2f", brush_.strength);
                ImDrawList* dl = ImGui::GetForegroundDrawList();
                ImVec2 ts = ImGui::CalcTextSize(buf);
                ImVec2 pos(px - ts.x * 0.5f, py - ts.y * 0.5f);
                dl->AddRectFilled(ImVec2(pos.x - 3, pos.y - 2),
                                  ImVec2(pos.x + ts.x + 3, pos.y + ts.y + 2),
                                  IM_COL32(0, 0, 0, 180), 3.0f);
                dl->AddText(pos, IM_COL32(255, 255, 255, 255), buf);
            }
        }
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

// --------------------------------------------------------------------------
// Mini icon helpers (drawn via ImDrawList, no external assets).
// Each icon is inscribed in [p0, p1]; col is the stroke/fill colour.

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
                                 CatTerrain, CatNoise, CatLayers, CatEnv, CatView, -1,
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
        case CatNoise:      drawNoiseContent();      break;
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
    ImGui::BulletText("WASD: move camera (hold)");
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
        ImGui::BulletText("Z: foundation mode  X: wall mode  C: texture mode");
        if (build_.mode() == BuildSystem::ModeFoundation) {
            ImGui::BulletText("Drag terrain: foundation rectangle");
            ImGui::BulletText("Click block side: extend foundation");
            ImGui::BulletText("Ctrl+drag: erase rectangle");
        } else if (build_.mode() == BuildSystem::ModeWall) {
            ImGui::BulletText("Drag block top: wall on edge");
            ImGui::BulletText("R: cycle wall edge (+X/+Z/-X/-Z)");
            ImGui::BulletText("Ctrl+drag: erase rectangle");
        } else {
            ImGui::BulletText("Drag on face: stretch-select region");
            ImGui::BulletText("Horizontal face = rectangle, vertical = line");
            ImGui::BulletText("Ctrl+click: clear face texture");
            ImGui::BulletText("Load+select texture in the panel");
        }
        ImGui::BulletText("Right-click: inspect block");
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
        const char* mn[] = { "Foundation", "Wall", "Texture" };
        ImGui::Text("Blocks: %d  Mode: %s  Selected: %s",
                    build_.count(), mn[(int)build_.mode()],
                    selectedBlockId_ >= 0 ? std::to_string(selectedBlockId_).c_str() : "none");
    } else {
        const char* dm = vertexEditor_.dragMode() == VertexEditor::FreeXYZ  ? "Free XYZ" :
                         vertexEditor_.dragMode() == VertexEditor::Vertical ? "Vertical" : "Normal";
        ImGui::Text("Vertices: %d   Drag: %s",
                    vertexEditor_.selectionCount(), dm);
    }
    ImGui::End();
}
