// Editor UI shell: ImGui frame, dockspace layout, viewport window, toolbar,
// help overlay. The per-panel contents live in app_panels.cpp; the vector
// icons live in ui_icons.cpp.
#include "app.h"
#include "input.h"
#include "ui_icons.h"
#include "ui_common.h"
#include <imgui.h>
#include <imgui_internal.h>   // DockBuilder* (programmatic dock layouts)
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_opengl3.h>
#include <algorithm>
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

    // Full-viewport dockspace. On the first run (no layout in imgui.ini) a
    // default arrangement is built; afterwards the user's layout persists
    // via imgui.ini. Dock nodes from the .ini are instantiated during
    // NewFrame(), so a missing node reliably means "no saved layout".
    ImGuiID dockspaceId = ImGui::GetID("SceneEditorDockspace");
    if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr)
        buildDefaultLayout(dockspaceId);
    ImGui::DockSpaceOverViewport(dockspaceId, ImGui::GetMainViewport());

    drawViewportWindow();
    drawToolbarWindow();
    drawToolsWindow();
    drawHierarchyWindow();
    drawInspectorWindow();
    drawSettingsWindow();
    drawTerrainWindow();
    drawLayersWindow();
    drawHistoryWindow();
    drawFileWindow();
    if (showHelp_) drawHelpOverlay();

    // Brush value overlay: show radius/strength over the viewport image while
    // Shift or Ctrl is held (the modifiers used to scrub them via scroll).
    if (brushHasHit_ && viewportHovered_ && toolMode_ == ToolPaint) {
        bool shift = g_input.keyDown(GLFW_KEY_LEFT_SHIFT) ||
                     g_input.keyDown(GLFW_KEY_RIGHT_SHIFT);
        bool ctrl  = g_input.keyDown(GLFW_KEY_LEFT_CONTROL) ||
                     g_input.keyDown(GLFW_KEY_RIGHT_CONTROL);
        if ((shift || ctrl) && vpScaleX_ > 0.0f && vpScaleY_ > 0.0f) {
            glm::vec4 clip = camera_.projection() * camera_.view() *
                             glm::vec4(brushHit_, 1.0f);
            if (clip.w > 0.0f) {
                // NDC -> viewport FBO pixels -> main-window pixels.
                float ndcX = clip.x / clip.w;
                float ndcY = clip.y / clip.w;
                float fx = (ndcX * 0.5f + 0.5f) * float(viewportW_);
                float fy = (1.0f - (ndcY * 0.5f + 0.5f)) * float(viewportH_);
                float px = vpWinX_ + fx / vpScaleX_;
                float py = vpWinY_ + fy / vpScaleY_;
                char buf[32];
                if (shift)
                    std::snprintf(buf, sizeof(buf), "R %.1f", brush_.radius);
                else
                    std::snprintf(buf, sizeof(buf), "S %.2f", brush_.strength);
                ImDrawList* dl = ImGui::GetForegroundDrawList(ImGui::GetMainViewport());
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

    // Multi-viewport: render any torn-out platform windows into their own
    // OS windows (they have their own GLFW window + GL context).
    ImGuiIO& io = ImGui::GetIO();
    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
        GLFWwindow* backupContext = glfwGetCurrentContext();
        ImGui::UpdatePlatformWindows();
        ImGui::RenderPlatformWindowsDefault();
        glfwMakeContextCurrent(backupContext);
    }
}

// --------------------------------------------------------------------------
// Dock layout (first run only — afterwards imgui.ini owns the layout).

void App::buildDefaultLayout(unsigned int dockspaceId) {
    ImGuiID id = (ImGuiID)dockspaceId;
    ImGui::DockBuilderRemoveNode(id);
    ImGui::DockBuilderAddNode(id, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(id, ImGui::GetMainViewport()->Size);

    ImGuiID remaining = id;
    ImGuiID dockToolbar = ImGui::DockBuilderSplitNode(remaining, ImGuiDir_Left,
                                                      0.045f, nullptr, &remaining);
    ImGuiID dockTools   = ImGui::DockBuilderSplitNode(remaining, ImGuiDir_Left,
                                                      0.24f, nullptr, &remaining);
    ImGuiID dockRight   = ImGui::DockBuilderSplitNode(remaining, ImGuiDir_Right,
                                                      0.26f, nullptr, &remaining);
    // Right column: Hierarchy on top, Inspector below.
    ImGuiID dockHierarchy = ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Up,
                                                        0.5f, nullptr, &dockRight);

    ImGui::DockBuilderDockWindow("Toolbar", dockToolbar);
    ImGui::DockBuilderDockWindow("Tools", dockTools);
    ImGui::DockBuilderDockWindow("Hierarchy", dockHierarchy);
    ImGui::DockBuilderDockWindow("Inspector", dockRight);
    ImGui::DockBuilderDockWindow("Viewport", remaining);
    // Hidden-by-default windows get sensible homes for when they are shown.
    ImGui::DockBuilderDockWindow("Terrain", dockTools);
    ImGui::DockBuilderDockWindow("Layers", dockTools);
    ImGui::DockBuilderDockWindow("File", dockTools);
    ImGui::DockBuilderDockWindow("Skybox/Settings", dockRight);
    ImGui::DockBuilderDockWindow("History", dockRight);
    ImGui::DockBuilderFinish(id);
}

// --------------------------------------------------------------------------
// The 3D viewport: scene renders into viewportFbo_; this window displays it.

void App::drawViewportWindow() {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    ImGui::Begin("Viewport", nullptr,
                 ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 avail = ImGui::GetContentRegionAvail();
    // FBO size = content size in FRAMEBUFFER pixels (HiDPI: fb > window).
    float dpiX = winWidth_  > 0 ? float(fbWidth_)  / float(winWidth_)  : 1.0f;
    float dpiY = winHeight_ > 0 ? float(fbHeight_) / float(winHeight_) : 1.0f;
    int wantW = int(avail.x * dpiX + 0.5f);
    int wantH = int(avail.y * dpiY + 0.5f);
    if (wantW >= 8 && wantH >= 8) {
        viewportW_ = wantW;
        viewportH_ = wantH;
    }

    // Mouse coords from GLFW are relative to the MAIN window client area.
    // ImGui screen coords are viewport-relative; subtracting the main
    // viewport origin converts back to client coords. (With viewports
    // enabled the main viewport origin is the OS-screen pos of our window.)
    ImVec2 imgPos  = ImGui::GetCursorScreenPos();
    ImVec2 mainPos = ImGui::GetMainViewport()->Pos;
    vpWinX_   = imgPos.x - mainPos.x;
    vpWinY_   = imgPos.y - mainPos.y;
    vpScaleX_ = avail.x > 0.0f ? float(viewportW_) / avail.x : 1.0f;
    vpScaleY_ = avail.y > 0.0f ? float(viewportH_) / avail.y : 1.0f;

    if (viewportFbo_ && viewportW_ > 0 && viewportH_ > 0) {
        // OpenGL FBO is bottom-up — flip the V coordinate.
        ImGui::Image((ImTextureID)(intptr_t)viewportColor_.id(), avail,
                     ImVec2(0, 1), ImVec2(1, 0));
        // Scene interaction only works while the viewport lives inside the
        // main OS window; a torn-out platform window has unrelated coords.
        viewportHovered_ = ImGui::IsItemHovered() &&
            ImGui::GetWindowViewport()->ID == ImGui::GetMainViewport()->ID;
    } else {
        viewportHovered_ = false;
    }

    ImGui::End();
    ImGui::PopStyleVar();
}

// --------------------------------------------------------------------------
// Toolbar: vertical icon strip (replaces the old left rail + bottom brush
// bar). Tool buttons switch the active tool; panel buttons toggle windows;
// brush buttons pick the paint brush.

void App::drawToolbarWindow() {
    ImGui::Begin("Toolbar", nullptr, ImGuiWindowFlags_NoCollapse);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float availW = ImGui::GetContentRegionAvail().x;
    const float cell = 36.0f;
    const float pad  = 7.0f;
    const float indent = std::max(0.0f, (availW - cell) * 0.5f);

    // Each cell is a real layout item (InvisibleButton) — it grows the window
    // content rect — with the icon drawn over its rect afterwards.
    auto iconCell = [&](icons::IconFn fn, const char* tooltip, bool active) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
        ImGui::PushID(tooltip);
        ImGui::InvisibleButton("##cell", ImVec2(cell, cell));
        ImGui::PopID();
        ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
        bool hover = ImGui::IsItemHovered();
        if (active || hover)
            dl->AddRectFilled(p0, p1, ImGui::ColorConvertFloat4ToU32(
                ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]), 6.0f);
        ImU32 col = active ? IM_COL32(255, 230, 110, 255) :
                    hover  ? IM_COL32(255, 255, 255, 255) :
                             IM_COL32(210, 210, 210, 255);
        if (fn) fn(dl, ImVec2(p0.x + pad, p0.y + pad),
                      ImVec2(p1.x - pad, p1.y - pad), col);
        if (hover) ImGui::SetTooltip("%s", tooltip);
        return ImGui::IsItemClicked();
    };
    auto separatorCell = [&] {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
        ImGui::Dummy(ImVec2(cell, 6.0f));
        ImVec2 p0 = ImGui::GetItemRectMin(), p1 = ImGui::GetItemRectMax();
        float dy = (p0.y + p1.y) * 0.5f;
        dl->AddLine(ImVec2(p0.x + 4, dy), ImVec2(p1.x - 4, dy),
                    IM_COL32(120, 120, 120, 255), 1.5f);
    };

    // --- Tools ---
    const int toolCats[] = { CatBrush, CatVertex, CatProps, CatVegetation, CatBuild };
    for (int cat : toolCats) {
        if (iconCell(icons::catIcon(cat), icons::catName(cat), activeCategory_ == cat))
            selectCategory(cat);
    }
    separatorCell();

    // --- Panel toggles ---
    struct PanelToggle { int cat; const char* name; bool* flag; };
    PanelToggle panels[] = {
        { CatTerrain, "Terrain",          &showTerrain_  },
        { CatLayers,  "Layers",           &showLayers_   },
        { CatEnv,     "Skybox/Settings",  &showSettings_ },
        { CatHistory, "History",          &showHistory_  },
        { CatFile,    "File",             &showFile_     },
    };
    for (auto& p : panels) {
        if (iconCell(icons::catIcon(p.cat), p.name, *p.flag))
            *p.flag = !*p.flag;
    }

    // --- Brush quick pick (paint mode only) ---
    if (toolMode_ == ToolPaint) {
        separatorCell();
        for (int i = 0; i < 8; ++i) {
            if (iconCell(icons::brushIcon(i), brushTypeName(i),
                         brush_.type == i)) {
                brush_.type = (Terrain::BrushParams::Type)i;
                activeCategory_ = (i == Terrain::BrushParams::Vegetation)
                                  ? CatVegetation : CatBrush;
            }
        }
    }

    ImGui::End();
}

// --------------------------------------------------------------------------
// Docked windows. The *Content functions (app_panels.cpp) are reused nearly
// unchanged — only the shell around them is new.

void App::drawToolsWindow() {
    if (!showTools_) return;
    if (ImGui::Begin("Tools", &showTools_)) {
        switch (activeCategory_) {
            case CatBrush:      drawBrushContent();      break;
            case CatVertex:     drawVertexContent();     break;
            case CatProps:      drawPropToolContent();   break;
            case CatVegetation: drawVegetationContent(); break;
            case CatBuild:      drawBuildContent();      break;
            default:            drawBrushContent();      break;
        }
    }
    ImGui::End();
}

void App::drawHierarchyWindow() {
    if (!showHierarchy_) return;
    if (ImGui::Begin("Hierarchy", &showHierarchy_)) {
        // --- Props ---
        if (ImGui::CollapsingHeader("Props", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (props_.count() == 0)
                ImGui::TextDisabled("(none — import via the Tools window)");
            for (const auto& p : props_.props()) {
                ImGui::PushID(p.id);
                if (ImGui::Selectable(p.displayName.c_str(),
                                      p.id == props_.selectedId())) {
                    selectCategory(CatProps);
                    props_.select(p.id);
                }
                ImGui::PopID();
            }
        }

        // --- Blocks ---
        char blocksHdr[48];
        std::snprintf(blocksHdr, sizeof(blocksHdr), "Blocks (%d)", build_.count());
        if (ImGui::CollapsingHeader(blocksHdr)) {
            if (build_.count() == 0)
                ImGui::TextDisabled("(none)");
            for (const auto& b : build_.blocks()) {
                ImGui::PushID(b.id);
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "Block %d (%s)", b.id,
                              b.type == BuildSystem::Foundation ? "Foundation" : "Wall");
                if (ImGui::Selectable(lbl, b.id == selectedBlockId_)) {
                    selectCategory(CatBuild);
                    selectedBlockId_ = b.id;
                    selectedBlockFace_ = -1;
                }
                ImGui::PopID();
            }
        }

        // --- Terrain texture layers ---
        if (ImGui::CollapsingHeader("Texture Layers")) {
            for (int i = 0; i < terrain_.layerCount(); ++i) {
                ImGui::PushID(i);
                char lbl[80];
                std::snprintf(lbl, sizeof(lbl), "%d: %s", i,
                              terrain_.layers()[i].name.c_str());
                if (ImGui::Selectable(lbl, brush_.textureLayer == i)) {
                    brush_.textureLayer = i;
                    brush_.type = Terrain::BrushParams::Texture;
                    selectCategory(CatBrush);
                }
                ImGui::PopID();
            }
        }
    }
    ImGui::End();
}

void App::drawInspectorWindow() {
    if (!showInspector_) return;
    if (ImGui::Begin("Inspector", &showInspector_))
        drawInspectorContent();
    ImGui::End();
}

void App::drawSettingsWindow() {
    if (!showSettings_) return;
    if (ImGui::Begin("Skybox/Settings", &showSettings_)) {
        drawEnvContent();
        ImGui::Separator();
        drawViewContent();
    }
    ImGui::End();
}

void App::drawTerrainWindow() {
    if (!showTerrain_) return;
    if (ImGui::Begin("Terrain", &showTerrain_)) {
        drawTerrainContent();
        ImGui::Spacing();
        if (ImGui::CollapsingHeader("Noise generator", ImGuiTreeNodeFlags_DefaultOpen))
            drawNoiseContent();
    }
    ImGui::End();
}

void App::drawLayersWindow() {
    if (!showLayers_) return;
    if (ImGui::Begin("Layers", &showLayers_))
        drawLayersContent();
    ImGui::End();
}

void App::drawHistoryWindow() {
    if (!showHistory_) return;
    if (ImGui::Begin("History", &showHistory_))
        drawHistoryContent();
    ImGui::End();
}

void App::drawFileWindow() {
    if (!showFile_) return;
    if (ImGui::Begin("File", &showFile_))
        drawFileContent();
    ImGui::End();
}

// --------------------------------------------------------------------------

void App::selectCategory(int cat) {
    if (cat < 0 || cat >= CatCount) return;
    if (activeCategory_ == CatVertex && cat != CatVertex) wireframe_ = false;
    activeTool_->cancelDrag();
    gizmo_.cancelDrag();
    vertexEditor_.cancelDrag();
    activeCategory_ = cat;
    if (cat == CatBrush) {
        toolMode_ = ToolPaint; activeTool_ = &terrainTool_;
    } else if (cat == CatVertex) {
        toolMode_ = ToolVertex; wireframe_ = true; activeTool_ = &vertexTool_;
    } else if (cat == CatProps) {
        toolMode_ = ToolProp; activeTool_ = &propTool_;
    } else if (cat == CatVegetation) {
        toolMode_ = ToolPaint; brush_.type = Terrain::BrushParams::Vegetation; activeTool_ = &terrainTool_;
    } else if (cat == CatBuild) {
        toolMode_ = ToolBuild; activeTool_ = &buildTool_;
    }
}

void App::drawHelpOverlay() {
    ImGui::SetNextWindowPos(ImVec2(ImGui::GetIO().DisplaySize.x - 290, 10),
                            ImGuiCond_FirstUseEver);
    ImGui::Begin("Help", &showHelp_,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_AlwaysAutoResize |
                 ImGuiWindowFlags_NoCollapse);
    ImGui::TextUnformatted("Controls:");
    ImGui::BulletText("Toolbar: pick tool / toggle panels");
    ImGui::BulletText("Tools window: settings for the active tool");
    ImGui::BulletText("Drag window tabs to re-dock; drag a tab out to");
    ImGui::BulletText("  make a floating OS window (multi-viewport)");
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
