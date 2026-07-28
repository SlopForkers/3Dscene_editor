// Docked-window panel contents: one draw*Content() per panel (Brush, Vertex,
// PropTool, Inspector, Vegetation, Build, Terrain, Noise, Layers, Env, View,
// History, File). The dockable window wrappers live in app_ui.cpp.
#include "app.h"
#include "ui_common.h"
#include "model.h"
#include "commands.h"
#include "file_dialog.h"
#include <imgui.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <random>
#include <chrono>
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
        ImGui::TextDisabled("Texture layers (click to paint)");
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float cell = 50.0f;
        const float gap2 = 6.0f;
        int nLay = terrain_.layerCount();
        for (int i = 0; i < nLay; ++i) {
            const auto& l = terrain_.layers()[i];
            ImGui::PushID(i);
            bool active = (brush_.textureLayer == i);
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 p1(p0.x + cell, p0.y + cell);
            ImU32 bg = ImGui::ColorConvertFloat4ToU32(
                ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
            dl->AddRectFilled(p0, p1, bg, 4.0f);
            if (l.albedo) {
                ImGui::SetCursorScreenPos(ImVec2(p0.x + 2, p0.y + 2));
                ImGui::Image((ImTextureID)(intptr_t)l.albedo,
                             ImVec2(cell - 4, cell - 4));
            } else {
                dl->AddRectFilled(ImVec2(p0.x + 4, p0.y + 4),
                                  ImVec2(p1.x - 4, p1.y - 4),
                                  IM_COL32(60, 60, 60, 255), 3.0f);
            }
            if (active)
                dl->AddRect(p0, p1, IM_COL32(255, 230, 110, 255), 4.0f, 0, 2.5f);
            char badge[6]; std::snprintf(badge, sizeof(badge), "%d", i);
            dl->AddText(ImVec2(p0.x + 3, p0.y + 1),
                        IM_COL32(255, 255, 255, 220), badge);
            ImGui::SetCursorScreenPos(p0);
            char lbl[16]; std::snprintf(lbl, sizeof(lbl), "##tb%d", i);
            ImGui::InvisibleButton(lbl, ImVec2(cell, cell));
            if (ImGui::IsItemClicked()) brush_.textureLayer = i;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Layer %d: %s", i, l.name.c_str());
            ImGui::PopID();
            if ((i + 1) % 4 != 0) ImGui::SameLine(0, gap2);
        }
        if (nLay < Terrain::MAX_LAYERS) {
            ImGui::Button("Add texture...");
            if (ImGui::IsItemClicked()) {
                std::string p = openFileDialog("Image", "*.png;*.jpg;*.jpeg;*.tga;*.bmp", nativeWindow());
                if (!p.empty()) {
                    int idx = terrain_.addLayer(p);
                    if (idx >= 0) brush_.textureLayer = idx;
                }
            }
        }
        brush_.textureLayer = std::clamp(brush_.textureLayer, 0,
                                         terrain_.layerCount() - 1);
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
    }
    // Keep the cursor ring geometry in sync (no-op unless the radius changed).
    brushCursor_.setShape(brush_.radius);
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
    brushCursor_.setShape(brush_.radius);   // no-op unless the radius changed
    ImGui::Separator();
    ImGui::TextWrapped("Click a vertex to select. Ctrl+click adds to the "
                       "selection. Drag the gizmo to pull vertices; the "
                       "radius/falloff controls falloff.");
}

// Prop TOOL controls (Tools window): import, default size, gizmo mode.
// The prop list lives in the Hierarchy window; the selected prop's transform
// is edited in the Inspector (drawInspectorContent).
void App::drawPropToolContent() {
    if (ImGui::Button("Import glTF / VRM...")) {
        std::string path = openFileDialog("glTF / VRM", "*.gltf;*.glb;*.vrm",
                                          nativeWindow());
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
    ImGui::TextWrapped("Left-click a prop to select it (the Hierarchy window "
                       "lists them all). Drag the gizmo to transform; the "
                       "Inspector edits the values directly.");
}

// Inspector: properties of the current selection (prop, block, or vertices).
void App::drawInspectorContent() {
    Prop* sel = props_.selected();
    if (sel) {
        ImGui::TextDisabled("Prop");
        ImGui::Separator();
        ImGui::Text("Selected: %s", sel->displayName.c_str());

        // Undo capture for direct widget edits: snapshot on activation, push
        // on deactivation-after-edit. PropTransformCommand::merge coalesces
        // back-to-back widget drags of the same prop.
        auto trackWidget = [&]() {
            if (ImGui::IsItemActivated() && !propEditActive_) {
                propEditActive_ = true;
                propEditId_     = sel->id;
                propEditPos_    = sel->position;
                propEditRot_    = sel->rotationEuler;
                propEditScale_  = sel->scale;
            }
            if (ImGui::IsItemDeactivatedAfterEdit() && propEditActive_) {
                Prop* p = props_.findProp(propEditId_);
                if (p && (p->position != propEditPos_ ||
                          p->rotationEuler != propEditRot_ ||
                          p->scale != propEditScale_)) {
                    history_.push(std::make_unique<PropTransformCommand>(
                        props_, propEditId_,
                        propEditPos_, propEditRot_, propEditScale_,
                        p->position, p->rotationEuler, p->scale));
                }
                propEditActive_ = false;
            }
        };

        ImGui::DragFloat3("Position", &sel->position[0], 0.5f);
        trackWidget();
        ImGui::SliderFloat("Yaw",   &sel->rotationEuler.y, -3.14159f, 3.14159f, "%.2f");
        trackWidget();
        ImGui::SliderFloat("Pitch", &sel->rotationEuler.x, -1.5708f,  1.5708f,  "%.2f");
        trackWidget();
        ImGui::SliderFloat("Roll",  &sel->rotationEuler.z, -3.14159f, 3.14159f, "%.2f");
        trackWidget();
        float uniformScale = sel->scale.x;
        if (ImGui::SliderFloat("Scale", &uniformScale, 0.01f, 20.0f, "%.2f", ImGuiSliderFlags_Logarithmic)) {
            sel->scale = glm::vec3(uniformScale);
        }
        trackWidget();
        if (ImGui::Button("Snap to ground")) {
            glm::vec3 oldPos = sel->position;
            float h = terrain_.heightAtWorld(sel->position.x, sel->position.z);
            float bottom = sel->model ? sel->model->aabbMin().y : 0.0f;
            sel->position.y = h - bottom * sel->scale.y;
            if (sel->position != oldPos)
                history_.push(std::make_unique<PropTransformCommand>(
                    props_, sel->id, oldPos, sel->rotationEuler, sel->scale,
                    sel->position, sel->rotationEuler, sel->scale));
        }
        ImGui::SameLine();
        if (ImGui::Button("Duplicate")) {
            // Copy everything BEFORE addProp(): addProp does a vector
            // push_back that may reallocate and dangle `sel`.
            auto m = sel->model;
            glm::vec3 pos = sel->position;
            glm::vec3 rot = sel->rotationEuler;
            glm::vec3 scl = sel->scale;
            std::string nm = sel->displayName + " copy";
            int newId = props_.addProp(m, pos,
                                       terrain_.heightAtWorld(pos.x, pos.z),
                                       0.0f, nm);
            if (newId >= 0) {
                Prop* np = props_.findProp(newId);
                if (np) {
                    np->rotationEuler = rot;
                    np->scale = scl;
                    np->position = pos;
                    history_.push(std::make_unique<PropCommand>(
                        props_, *np, true, "Duplicate Prop"));
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            Prop copy = *sel;
            int id = sel->id;
            history_.push(std::make_unique<PropCommand>(
                props_, copy, false, "Delete Prop"));
            props_.removeProp(id);
        }
        return;
    }

    // --- Selected block (build tool) ---
    if (selectedBlockId_ >= 0) {
        const BuildSystem::Block* b = build_.findBlock(selectedBlockId_);
        if (!b) {   // stale selection (e.g. after an undo)
            selectedBlockId_ = -1;
            selectedBlockFace_ = -1;
        } else {
            ImGui::TextDisabled("Block");
            ImGui::Separator();
            const char* faceNames[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
            ImGui::Text("id=%d  type=%s", b->id,
                        b->type == BuildSystem::Foundation ? "Foundation" : "Wall");
            ImGui::Text("pos (%.1f, %.1f, %.1f)",
                        b->position.x, b->position.y, b->position.z);
            if (selectedBlockFace_ >= 0)
                ImGui::Text("Picked face: %s", faceNames[selectedBlockFace_]);
            if (b->textureIdx >= 0) {
                ImGui::Text("Texture: %s on %s",
                            build_.blockTextureName(b->textureIdx).c_str(),
                            (b->textureFace >= 0 && b->textureFace < 6)
                                ? faceNames[b->textureFace] : "?");
                int tm = b->texMode;
                const char* tmn[] = { "Stretch", "Tile" };
                if (ImGui::Combo("Tex mode", &tm, tmn, 2))
                    build_.setBlockTexMode(selectedBlockId_, tm);
                if (b->texMode == 1) {
                    float sc = b->texScale;
                    if (ImGui::SliderFloat("UV scale", &sc, 0.05f, 8.0f, "%.2f"))
                        build_.setBlockTexScale(selectedBlockId_, sc);
                }
                if (ImGui::Button("Clear face texture"))
                    build_.clearBlockFaceTexture(selectedBlockId_);
            }
            if (ImGui::Button("Delete block (Del)")) {
                build_.removeBlock(selectedBlockId_);
                selectedBlockId_ = -1;
                selectedBlockFace_ = -1;
            }
            return;
        }
    }

    // --- Vertex selection (vertex tool) ---
    if (vertexEditor_.hasSelection()) {
        ImGui::TextDisabled("Vertices");
        ImGui::Separator();
        ImGui::Text("%d vertex%s selected",
                    vertexEditor_.selectionCount(),
                    vertexEditor_.selectionCount() == 1 ? "" : "es");
        if (ImGui::Button("Clear selection")) vertexEditor_.clearSelection();
        return;
    }

    ImGui::TextDisabled("(nothing selected)");
}

void App::drawVegetationContent() {
    ImGui::TextDisabled("Vegetation / Details");
    ImGui::Separator();

    // Import section.
    if (ImGui::Button("Import Model...")) {
        std::string path = openFileDialog("glTF / VRM", "*.gltf;*.glb;*.vrm",
                                          nativeWindow());
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
        if (ImGui::Selectable(p.name.c_str(), sel)) {
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
    brushCursor_.setShape(brush_.radius);   // no-op unless the radius changed

    ImGui::Separator();
    ImGui::TextWrapped("Left-drag: paint instances. Ctrl+left-drag: erase. "
                       "Density controls how many per stroke step.");
}

void App::drawBuildContent() {
    ImGui::TextDisabled("Build");
    ImGui::Separator();

    // Mode slots: Foundation / Wall / Texture (Z / X / C).
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float slotSz = 60.0f, gap = 6.0f;
    const char* modeNames[] = { "Foundation", "Wall", "Texture" };
    const char* modeKeys[]  = { "Z", "X", "C" };
    for (int i = 0; i < 3; ++i) {
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1 = ImVec2(p0.x + slotSz, p0.y + slotSz);
        bool active = ((int)build_.mode() == i);
        bool hover = ImGui::IsMouseHoveringRect(p0, p1);
        ImU32 bg = active ? IM_COL32(80, 100, 140, 255) :
                    hover  ? IM_COL32(60, 70, 90, 255) :
                             IM_COL32(40, 45, 55, 255);
        dl->AddRectFilled(p0, p1, bg, 6.0f);
        ImU32 col = active ? IM_COL32(255, 230, 110, 255) : IM_COL32(180, 180, 180, 255);
        if (i == 0) {
            // Foundation icon: solid filled square.
            float m = slotSz * 0.22f;
            dl->AddRectFilled(ImVec2(p0.x + m, p0.y + m),
                               ImVec2(p1.x - m, p1.y - m), col, 2.0f);
        } else if (i == 1) {
            // Wall icon: thin horizontal bar.
            float mx = slotSz * 0.18f;
            float my = slotSz * 0.40f;
            float mh = slotSz * 0.16f;
            dl->AddRectFilled(ImVec2(p0.x + mx, p0.y + my),
                              ImVec2(p1.x - mx, p0.y + my + mh), col, 2.0f);
        } else {
            // Texture icon: checker pattern.
            float m = slotSz * 0.22f;
            float s = (slotSz - 2 * m) * 0.5f;
            for (int cy = 0; cy < 2; ++cy)
                for (int cx = 0; cx < 2; ++cx)
                    if ((cx + cy) % 2 == 0)
                        dl->AddRectFilled(ImVec2(p0.x + m + cx * s, p0.y + m + cy * s),
                                          ImVec2(p0.x + m + (cx + 1) * s, p0.y + m + (cy + 1) * s), col);
        }
        ImGui::SetCursorScreenPos(p0);
        char lbl[16]; std::snprintf(lbl, sizeof(lbl), "##bmode%d", i);
        ImGui::InvisibleButton(lbl, ImVec2(slotSz, slotSz));
        if (hover) ImGui::SetTooltip("%s [%s]", modeNames[i], modeKeys[i]);
        if (ImGui::IsItemClicked()) build_.setMode((BuildSystem::Mode)i);
        ImGui::SameLine(0, gap);
    }
    ImGui::NewLine();
    ImGui::Text("Mode: %s  [%s]", modeNames[(int)build_.mode()],
                modeKeys[(int)build_.mode()]);
    ImGui::Separator();

    // --- Texture manager (shared library + active texture + UV scale) ---
    // Always visible so textures can be loaded/selected in any mode, but most
    // useful in ModeTexture (C) where clicking a face paints the active one.
    ImGui::TextDisabled("Texture manager");
    if (ImGui::Button("Load texture...")) {
        std::string p = openFileDialog("Image", "*.png;*.jpg;*.jpeg;*.tga;*.bmp", nativeWindow());
        if (!p.empty()) {
            int idx = build_.loadBlockTexture(p);
            if (idx >= 0) build_.setCurrentTexture(idx);
        }
    }
    int nTex = build_.blockTextureCount();
    int curTex = build_.currentTexture();
    if (nTex > 0) {
        for (int i = 0; i < nTex; ++i) {
            ImGui::PushID(i);
            GLuint tid = build_.blockTextureId(i);
            bool active = (curTex == i);
            // Highlight the active texture slot.
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            ImVec2 box1(p0.x + 44, p0.y + 36);
            if (active) dl->AddRect(p0, box1, IM_COL32(255, 230, 110, 255), 4.0f, 0, 2.0f);
            ImGui::Image((ImTextureID)(intptr_t)tid, ImVec2(32, 32));
            ImGui::SameLine();
            if (ImGui::Selectable(build_.blockTextureName(i).c_str(), active,
                                  ImGuiSelectableFlags_SpanAllColumns,
                                  ImVec2(0, 32)))
                build_.setCurrentTexture(i);
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) {
                build_.removeBlockTexture(i);
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
    } else {
        ImGui::TextDisabled("(no textures loaded)");
    }
    float dts = build_.defaultTexScale();
    int dtm = build_.defaultTexMode();
    const char* modeNames2[] = { "Stretch", "Tile" };
    if (ImGui::Combo("Texture mode", &dtm, modeNames2, 2))
        build_.setDefaultTexMode(dtm);
    if (dtm == 1) {
        if (ImGui::SliderFloat("UV scale (tile)", &dts, 0.05f, 8.0f, "%.2f"))
            build_.setDefaultTexScale(dts);
    } else {
        ImGui::TextDisabled("UV scale: n/a (Stretch)");
    }
    if (curTex >= 0)
        ImGui::Text("Active: %s", build_.blockTextureName(curTex).c_str());
    else
        ImGui::TextDisabled("(no active texture — load one to paint)");
    ImGui::Separator();

    // --- Mode-specific placement settings ---
    if (build_.mode() == BuildSystem::ModeFoundation) {
        float w = build_.blockWidth(), h = build_.blockHeight();
        if (ImGui::SliderFloat("Block width",  &w, 0.5f, 16.0f, "%.2f")) build_.setBlockSize(w, h);
        if (ImGui::SliderFloat("Block height", &h, 0.5f, 16.0f, "%.2f")) build_.setBlockSize(w, h);
        float sunk = build_.sunkDepth();
        if (ImGui::SliderFloat("Foundation sink", &sunk, 0.0f, 0.95f, "%.2f"))
            build_.setSunkDepth(sunk);
        glm::vec3 c = build_.color();
        float cf[3] = { c.r, c.g, c.b };
        if (ImGui::ColorEdit3("Block color", cf))
            build_.setColor(glm::vec3(cf[0], cf[1], cf[2]));
    } else if (build_.mode() == BuildSystem::ModeWall) {
        float w = build_.blockWidth(), h = build_.blockHeight();
        if (ImGui::SliderFloat("Block width",  &w, 0.5f, 16.0f, "%.2f")) build_.setBlockSize(w, h);
        if (ImGui::SliderFloat("Block height", &h, 0.5f, 16.0f, "%.2f")) build_.setBlockSize(w, h);
        float wt = build_.wallThickness();
        if (ImGui::SliderFloat("Wall thickness", &wt, 0.1f, 4.0f, "%.2f"))
            build_.setWallThickness(wt);
        const char* edgeNames[] = { "+X edge", "+Z edge", "-X edge", "-Z edge" };
        int we = build_.wallEdge();
        if (ImGui::Combo("Wall edge (R)", &we, edgeNames, 4))
            build_.setWallEdge(we);
        glm::vec3 c = build_.color();
        float cf[3] = { c.r, c.g, c.b };
        if (ImGui::ColorEdit3("Block color", cf))
            build_.setColor(glm::vec3(cf[0], cf[1], cf[2]));
    } else {
        // ModeTexture: only the shared settings apply (no block size/color).
    }

    if (build_.mode() != BuildSystem::ModeTexture) {
        float step = build_.gridStep();
        if (ImGui::SliderFloat("Grid step", &step, 0.25f, 8.0f, "%.2f"))
            build_.setGridStep(step);
    }

    ImGui::Separator();
    ImGui::Text("Placed blocks: %d", build_.count());
    ImGui::SameLine();
    if (ImGui::Button("Clear All")) {
        build_.clear();
        selectedBlockId_ = -1;
        selectedBlockFace_ = -1;
    }

    ImGui::Separator();
    ImGui::TextDisabled("Right-click a block to inspect it in the Inspector.");
    if (build_.mode() == BuildSystem::ModeFoundation) {
        ImGui::TextWrapped("Drag on terrain: foundation rectangle (sunk, same "
                           "level as neighbours). Click block side: extend "
                           "foundation. Ctrl+drag: erase. "
                           "Right-click: inspect block. Del: remove selected.");
    } else if (build_.mode() == BuildSystem::ModeWall) {
        ImGui::TextWrapped("Drag on block TOP along an edge: thin wall line "
                           "following that edge. R cycles the edge. "
                           "Ctrl+drag: erase. Right-click: inspect block. "
                           "Del: remove selected.");
    } else {
        ImGui::TextWrapped("Drag on a face to stretch-select a region: "
                           "horizontal faces give a rectangle, vertical faces "
                           "give a line. On release the active texture is "
                           "applied to every block in that region. "
                           "Ctrl+click: clear a face's texture. "
                           "Right-click: inspect block.");
    }
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

void App::drawNoiseContent() {
    ImGui::TextDisabled("Noise generator");
    ImGui::Separator();

    // --- Preview ---
    if (!noiseTex_.id()) {
        noiseTex_.create();
        glBindTexture(GL_TEXTURE_2D, noiseTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, noisePreviewSize_, noisePreviewSize_,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    if (noisePreviewDirty_) {
        std::vector<uint8_t> pix(noisePreviewSize_ * noisePreviewSize_ * 4);
        int perm[512];
        Noise::buildPerm(noiseParams_.seed, perm);
        // Sample the preview over a world-sized region so it matches what the
        // terrain will receive. Apply the same "cycles across the terrain"
        // frequency normalization as Terrain::generateNoise.
        float ws = terrain_.worldSize();
        Noise::Params np = noiseParams_;
        np.frequency = noiseParams_.frequency / ws;
        for (int y = 0; y < noisePreviewSize_; ++y) {
            for (int x = 0; x < noisePreviewSize_; ++x) {
                float wx = (float(x) / (noisePreviewSize_ - 1) - 0.5f) * ws;
                float wz = (float(y) / (noisePreviewSize_ - 1) - 0.5f) * ws;
                float n = Noise::sampleRawWithPerm(np, wx, wz, perm);
                uint8_t v = (uint8_t)std::clamp((n * 0.5f + 0.5f) * 255.0f, 0.0f, 255.0f);
                int i = (y * noisePreviewSize_ + x) * 4;
                pix[i] = v; pix[i + 1] = v; pix[i + 2] = v; pix[i + 3] = 255;
            }
        }
        glBindTexture(GL_TEXTURE_2D, noiseTex_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, noisePreviewSize_, noisePreviewSize_,
                        GL_RGBA, GL_UNSIGNED_BYTE, pix.data());
        noisePreviewDirty_ = false;
    }

    // Display preview centred, as large as the panel allows.
    float avail = ImGui::GetContentRegionAvail().x;
    float pv = std::min(avail, 220.0f);
    ImGui::Image((ImTextureID)(intptr_t)noiseTex_.id(), ImVec2(pv, pv));
    ImGui::SameLine();
    ImGui::BeginGroup();
    ImGui::Text("Preview");
    ImGui::TextDisabled("%dx%d", noisePreviewSize_, noisePreviewSize_);
    ImGui::EndGroup();

    ImGui::Spacing();

    // --- Realtime toggle ---
    bool changed = false;
    changed |= ImGui::Checkbox("Realtime generation", &realtimeNoise_);
    if (realtimeNoise_)
        ImGui::TextDisabled("(applies to terrain on every change)");

    ImGui::Separator();

    // --- Noise type & blend ---
    const char* typeNames[] = { "Perlin", "Simplex", "Value", "Worley", "Ridge" };
    int ti = (int)noiseParams_.type;
    if (ImGui::Combo("Noise type", &ti, typeNames, Noise::TypeCount)) {
        noiseParams_.type = (Noise::Type)ti; changed = true;
    }
    const char* blendNames[] = { "Replace", "Add", "Subtract", "Multiply", "Min", "Max" };
    int bi = (int)noiseParams_.blend;
    if (ImGui::Combo("Blend mode", &bi, blendNames, Noise::BlendCount)) {
        noiseParams_.blend = (Noise::BlendMode)bi; changed = true;
    }

    ImGui::Spacing();
    ImGui::TextDisabled("Shape");
    changed |= ImGui::SliderFloat("Amplitude",  &noiseParams_.amplitude,   0.1f,  60.0f, "%.1f");
    // Cycles across the whole terrain (1 = one broad hill pattern; 20+ =
    // fine detail). The generator normalizes this by the world size.
    changed |= ImGui::SliderFloat("Frequency",  &noiseParams_.frequency,   0.25f, 40.0f, "%.2f");
    changed |= ImGui::SliderFloat("Exponent",   &noiseParams_.exponent,    0.1f,   4.0f, "%.2f");
    changed |= ImGui::SliderFloat2("Offset",    &noiseParams_.offsetX,  -500.0f, 500.0f, "%.1f");

    ImGui::Spacing();
    ImGui::TextDisabled("Fractal (fBm)");
    int oct = noiseParams_.octaves;
    if (ImGui::SliderInt("Octaves", &oct, 1, 10)) { noiseParams_.octaves = oct; changed = true; }
    changed |= ImGui::SliderFloat("Persistence", &noiseParams_.persistence, 0.0f, 1.0f, "%.2f");
    changed |= ImGui::SliderFloat("Lacunarity",  &noiseParams_.lacunarity,  1.0f, 4.0f, "%.2f");

    ImGui::Spacing();
    ImGui::TextDisabled("Modifiers");
    if (ImGui::Checkbox("Invert",  &noiseParams_.invert))  changed = true;
    ImGui::SameLine();
    if (ImGui::Checkbox("Ridged", &noiseParams_.ridged)) changed = true;

    ImGui::Spacing();
    if (ImGui::SliderInt("Seed", &noiseParams_.seed, 1, 99999)) changed = true;
    ImGui::SameLine();
    if (ImGui::Button("Random")) {
        std::mt19937 rng((unsigned)std::chrono::steady_clock::now().time_since_epoch().count());
        noiseParams_.seed = (int)(rng() % 99999) + 1;
        changed = true;
    }

    // --- React to changes ---
    if (changed) {
        noisePreviewDirty_ = true;
        // Realtime regenerates from the CURRENT terrain; note that
        // non-idempotent blends (Add/Subtract/Multiply) accumulate with
        // every tweak — that is the documented behaviour of the toggle.
        if (realtimeNoise_)
            terrain_.generateNoise(noiseParams_);
    }

    ImGui::Spacing();
    ImGui::Separator();
    if (ImGui::Button("Generate", ImVec2(-1, 0)))
        terrain_.generateNoise(noiseParams_);
}

void App::drawLayersContent() {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float cell = 52.0f;
    const float gap2 = 6.0f;
    int nLay = terrain_.layerCount();

    ImGui::TextDisabled("Texture library (%d / %d)", nLay, Terrain::MAX_LAYERS);
    ImGui::Separator();

    // Library grid: click selects the paint layer; right-click removes.
    bool removed = false;
    for (int i = 0; i < nLay && !removed; ++i) {
        const auto& l = terrain_.layers()[i];
        ImGui::PushID(i);
        bool active = (brush_.textureLayer == i);
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImVec2 p1(p0.x + cell, p0.y + cell);
        ImU32 bg = ImGui::ColorConvertFloat4ToU32(
            ImGui::GetStyle().Colors[ImGuiCol_FrameBg]);
        dl->AddRectFilled(p0, p1, bg, 4.0f);
        if (l.albedo) {
            ImGui::SetCursorScreenPos(ImVec2(p0.x + 2, p0.y + 2));
            ImGui::Image((ImTextureID)(intptr_t)l.albedo,
                         ImVec2(cell - 4, cell - 4));
        } else {
            dl->AddRectFilled(ImVec2(p0.x + 4, p0.y + 4),
                              ImVec2(p1.x - 4, p1.y - 4),
                              IM_COL32(60, 60, 60, 255), 3.0f);
        }
        if (active)
            dl->AddRect(p0, p1, IM_COL32(255, 230, 110, 255), 4.0f, 0, 2.5f);
        char badge[6]; std::snprintf(badge, sizeof(badge), "%d", i);
        dl->AddText(ImVec2(p0.x + 3, p0.y + 1),
                    IM_COL32(255, 255, 255, 220), badge);
        ImGui::SetCursorScreenPos(p0);
        char lbl[16]; std::snprintf(lbl, sizeof(lbl), "##ly%d", i);
        ImGui::InvisibleButton(lbl, ImVec2(cell, cell));
        if (ImGui::IsItemClicked()) {
            brush_.textureLayer = i;
            brush_.type = Terrain::BrushParams::Texture;
            toolMode_ = ToolPaint;
        }
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Layer %d: %s  (right-click to remove)", i, l.name.c_str());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right) && nLay > 1) {
            history_.push(std::make_unique<LayerRemoveCommand>(
                terrain_, i, terrain_.layers()[i], terrain_.splatData()));
            terrain_.removeLayer(i);
            if (brush_.textureLayer >= terrain_.layerCount())
                brush_.textureLayer = terrain_.layerCount() - 1;
            removed = true;
        }
        ImGui::PopID();
        if (!removed && (i + 1) % 4 != 0) ImGui::SameLine(0, gap2);
    }

    if (nLay < Terrain::MAX_LAYERS) {
        if (ImGui::Button("Add texture...")) {
            std::string p = openFileDialog("Image", "*.png;*.jpg;*.jpeg;*.tga;*.bmp", nativeWindow());
            if (!p.empty()) {
                std::vector<uint8_t> splatBefore = terrain_.splatData();
                int idx = terrain_.addLayer(p);
                if (idx >= 0) {
                    history_.push(std::make_unique<LayerAddCommand>(
                        terrain_, idx, terrain_.layers()[idx], std::move(splatBefore)));
                    brush_.textureLayer = idx;
                }
            }
        }
    }

    ImGui::Separator();
    ImGui::TextDisabled("Active layer settings");
    int al = std::clamp(brush_.textureLayer, 0, terrain_.layerCount() - 1);
    if (al >= 0 && al < terrain_.layerCount()) {
        const auto& L = terrain_.layers()[al];
        ImGui::Text("%d: %s", al, L.name.c_str());
        if (L.albedo) {
            ImGui::SameLine();
            ImGui::Image((ImTextureID)(intptr_t)L.albedo, ImVec2(40, 40));
        }
        float ts = L.tileSize;
        if (ImGui::SliderFloat("Tile size", &ts, 0.5f, 64.0f, "%.1f"))
            terrain_.setLayerTileSize(al, ts);
        if (ImGui::Button("Replace albedo...")) {
            std::string p = openFileDialog("Image", "*.png;*.jpg;*.jpeg;*.tga;*.bmp", nativeWindow());
            if (!p.empty()) {
                auto cmd = std::make_unique<LayerTextureCommand>(terrain_, al, false);
                cmd->oldPix = terrain_.layers()[al].albedoPix;
                cmd->oldPath = terrain_.layers()[al].albedoPath;
                if (terrain_.loadLayerAlbedo(al, p)) {
                    cmd->newPix = terrain_.layers()[al].albedoPix;
                    cmd->newPath = terrain_.layers()[al].albedoPath;
                    history_.push(std::move(cmd));
                }
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load normal...")) {
            std::string p = openFileDialog("Image", "*.png;*.jpg;*.jpeg;*.tga;*.bmp", nativeWindow());
            if (!p.empty()) {
                auto cmd = std::make_unique<LayerTextureCommand>(terrain_, al, true);
                cmd->oldPix = terrain_.layers()[al].normalPix;
                cmd->oldPath = terrain_.layers()[al].normalPath;
                cmd->oldHasNormal = terrain_.layers()[al].hasNormal;
                if (terrain_.loadLayerNormal(al, p)) {
                    cmd->newPix = terrain_.layers()[al].normalPix;
                    cmd->newPath = terrain_.layers()[al].normalPath;
                    cmd->newHasNormal = terrain_.layers()[al].hasNormal;
                    history_.push(std::move(cmd));
                }
            }
        }
        char nm[64];
        std::snprintf(nm, sizeof(nm), "%s", L.name.c_str());
        if (ImGui::InputText("Name", nm, sizeof(nm)))
            terrain_.setLayerName(al, nm);
    }

    ImGui::Separator();
    if (ImGui::Button("Reset Splat")) {
        history_.push(std::make_unique<SplatResetCommand>(terrain_, terrain_.splatData()));
        terrain_.resetSplat();
    }
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
        std::string p = openFileDialog("Sky image", "*.hdr;*.png;*.jpg;*.jpeg;*.tga;*.bmp", nativeWindow());
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
    ImGui::Checkbox("Shadows", &showShadows_);
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
        // renderScene re-applies the viewport FBO size next frame.
    }
}

void App::drawHistoryContent() {
    ImGui::TextDisabled("History");
    ImGui::Separator();

    if (!history_.canUndo()) ImGui::BeginDisabled();
    if (ImGui::Button("Undo  (Ctrl+Z)")) undoEdit();
    if (!history_.canUndo()) ImGui::EndDisabled();
    ImGui::SameLine();
    if (!history_.canRedo()) ImGui::BeginDisabled();
    if (ImGui::Button("Redo  (Ctrl+Shift+Z)")) redoEdit();
    if (!history_.canRedo()) ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Clear")) history_.clear();

    ImGui::Text("Memory: %.1f / %.0f MB",
                history_.memoryUsed() / 1048576.0,
                history_.memoryLimit() / 1048576.0);
    ImGui::Separator();

    ImGui::BeginChild("historylist", ImVec2(0, 0), true);
    // Redo arm first (greyed) — these are the "future" edits.
    for (size_t i = 0; i < history_.redoCount(); ++i) {
        const Command* c = history_.redoAt(i);
        if (!c) continue;
        ImGui::PushID((int)i - 100000);
        ImGui::TextDisabled("%s", c->name());
        ImGui::PopID();
    }
    if (history_.canRedo()) {
        ImGui::Separator();
        ImGui::TextDisabled("-- current state --");
    }
    // Undo arm, most recent first; the top entry is the next to be undone.
    for (size_t i = 0; i < history_.undoCount(); ++i) {
        const Command* c = history_.undoAt(i);
        if (!c) continue;
        ImGui::PushID((int)i);
        if (i == 0) ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "%s", c->name());
        else        ImGui::Text("%s", c->name());
        ImGui::PopID();
    }
    if (!history_.canUndo() && !history_.canRedo())
        ImGui::TextDisabled("(empty — edits appear here)");
    ImGui::EndChild();
}

void App::drawFileContent() {
    ImGui::TextDisabled("File");
    ImGui::Separator();

    if (ImGui::Button("Save Scene...")) {
        std::string path = saveFileDialog("Scene", "*.scene", "scene", nativeWindow());
        if (!path.empty()) {
            // Fallback suffix check (the dialog already appends .scene via
            // lpstrDefExt, but only when the name has no extension at all).
            bool hasExt = false;
            if (path.size() >= 6) {
                std::string tail = path.substr(path.size() - 6);
                for (char& c : tail) c = (char)std::tolower((unsigned char)c);
                hasExt = (tail == ".scene");
            }
            if (!hasExt) path += ".scene";
            if (!saveScene(path))
                std::cerr << "Save failed: " << path << "\n";
        }
    }
    if (ImGui::Button("Load Scene...")) {
        std::string path = openFileDialog("Scene", "*.scene", nativeWindow());
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
                       "heights + splat, with props/blocks/details/cameras "
                       "embedded in JSON. "
                       "Asset paths are stored relative to the scene file.");
}

// --------------------------------------------------------------------------
// Scene cameras.

static bool sameCamera(const SceneCamera& a, const SceneCamera& b) {
    return a.name == b.name && a.tag == b.tag &&
           a.position == b.position && a.target == b.target &&
           a.fov == b.fov && a.nearPlane == b.nearPlane && a.farPlane == b.farPlane;
}

void App::drawCamerasContent() {
    ImGui::TextDisabled("Scene cameras");
    ImGui::Separator();

    if (ImGui::Button("Add from current view")) addCameraFromView();
    ImGui::SameLine();
    ImGui::Checkbox("Frustums", &showCamFrustums_);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Draw camera frustums in the viewport");

    if (cameraRig_.cameras().empty()) {
        ImGui::TextDisabled("(no cameras yet)");
        return;
    }
    ImGui::TextDisabled("[ / ]: cycle cameras; double-click: view through");

    for (const auto& c : cameraRig_.cameras()) {
        ImGui::PushID(c.id);
        char lbl[160];
        std::snprintf(lbl, sizeof(lbl), "%s%s  (#%d)", c.name.c_str(),
                      c.id == cameraRig_.activeId() ? " [active]" : "", c.id);
        if (ImGui::Selectable(lbl, c.id == selectedCameraId_,
                              ImGuiSelectableFlags_AllowDoubleClick))
            selectedCameraId_ = c.id;
        if (ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            activateSceneCamera(c.id);
        ImGui::PopID();
    }

    SceneCamera* sel = cameraRig_.findCamera(selectedCameraId_);
    if (!sel) {
        selectedCameraId_ = -1;   // stale selection (undo / scene load)
        return;
    }
    ImGui::Separator();

    // Undo capture for widget edits: snapshot on activation, push on
    // deactivation-after-edit. CameraEditCommand::merge coalesces drags.
    auto trackWidget = [&]() {
        if (ImGui::IsItemActivated() && !camEditActive_) {
            camEditActive_ = true;
            camEditId_     = sel->id;
            camEditBefore_ = *sel;
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && camEditActive_) {
            SceneCamera* p = cameraRig_.findCamera(camEditId_);
            if (p && !sameCamera(*p, camEditBefore_)) {
                history_.push(std::make_unique<CameraEditCommand>(
                    cameraRig_, camEditId_, camEditBefore_, *p));
                markCamPreviewsStale();
            }
            camEditActive_ = false;
        }
    };

    char nm[64];
    std::snprintf(nm, sizeof(nm), "%s", sel->name.c_str());
    if (ImGui::InputText("Name", nm, sizeof(nm))) sel->name = nm;
    trackWidget();
    char tg[64];
    std::snprintf(tg, sizeof(tg), "%s", sel->tag.c_str());
    if (ImGui::InputText("Tag", tg, sizeof(tg))) sel->tag = tg;
    trackWidget();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Free-form game metadata (saved in the scene)");

    ImGui::DragFloat3("Position", &sel->position[0], 0.1f);
    trackWidget();
    ImGui::DragFloat3("Target",   &sel->target[0],   0.1f);
    trackWidget();
    ImGui::SliderFloat("FOV", &sel->fov, 10.0f, 120.0f, "%.0f deg");
    trackWidget();
    ImGui::DragFloat("Near", &sel->nearPlane, 0.01f, 0.001f, 100.0f, "%.3f");
    trackWidget();
    ImGui::DragFloat("Far",  &sel->farPlane,  1.0f, 1.0f, 10000.0f, "%.0f");
    trackWidget();
    // Keep the planes sane no matter what the widgets allowed.
    sel->nearPlane = std::clamp(sel->nearPlane, 0.001f, sel->farPlane * 0.5f);
    sel->farPlane  = std::max(sel->farPlane, sel->nearPlane * 2.0f);

    if (ImGui::Button("Activate")) activateSceneCamera(sel->id);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("View through this camera (double-click works too)");
    ImGui::SameLine();
    bool isActive = (sel->id == cameraRig_.activeId());
    if (isActive) ImGui::BeginDisabled();
    if (ImGui::Button("Set active")) cameraRig_.setActive(sel->id);
    if (isActive) ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("The game's initial camera (saved in the scene)");
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        SceneCamera copy = *sel;   // copy BEFORE removeCamera invalidates sel
        int id = sel->id;
        history_.push(std::make_unique<CameraCommand>(
            cameraRig_, copy, false, "Delete Camera"));
        cameraRig_.removeCamera(id);
        selectedCameraId_ = -1;
        markCamPreviewsStale();
    }
}

// Camera TOOL controls (Tools window): add + options. The camera list and
// parameter editing live in the Cameras window (auto-opened by the tool).
void App::drawCameraToolContent() {
    if (ImGui::Button("Add from current view")) addCameraFromView();
    ImGui::SameLine();
    ImGui::Checkbox("Frustums", &showCamFrustums_);
    ImGui::Separator();
    ImGui::Text("Cameras: %d", (int)cameraRig_.cameras().size());
    const SceneCamera* sel = cameraRig_.findCamera(selectedCameraId_);
    ImGui::Text("Selected: %s", sel ? sel->name.c_str() : "(none)");
    ImGui::Separator();
    ImGui::TextWrapped("Cursor mode (no brush): left-click a camera frustum "
                       "to select it; click empty ground to deselect. "
                       "[ / ] cycles cameras; double-click in a list views "
                       "through the camera.");
    ImGui::TextWrapped("Edit name, tag, pose and FOV in the Cameras window.");
}

// --------------------------------------------------------------------------
// Spawn markers.

// Spawn TOOL controls (Tools window): add + hints. The marker list and field
// editing live in the Spawns window (auto-opened by the tool).
void App::drawSpawnToolContent() {
    if (ImGui::Button("Add at camera target")) {
        glm::vec3 t = camera_.target();
        t.y = terrain_.heightAtWorld(t.x, t.z);
        addSpawnAt(t);
    }
    ImGui::Separator();
    ImGui::Text("Spawns: %d", (int)spawns_.spawns().size());
    const SpawnPoint* sel = spawns_.findSpawn(selectedSpawnId_);
    ImGui::Text("Selected: %s", sel ? sel->name.c_str() : "(none)");
    ImGui::Separator();
    ImGui::TextWrapped("Cursor mode: click a marker to select, drag to move "
                       "it, Ctrl+click places a new marker. Fields edit in "
                       "the Spawns window; the logic graph opens via "
                       "Edit Logic.");
}

void App::drawSpawnsContent() {
    ImGui::TextDisabled("Character spawn markers");
    ImGui::Separator();

    if (ImGui::Button("Add at camera target")) {
        glm::vec3 t = camera_.target();
        t.y = terrain_.heightAtWorld(t.x, t.z);
        addSpawnAt(t);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Or Ctrl+click on terrain in the spawn tool");

    if (spawns_.spawns().empty()) {
        ImGui::TextDisabled("(no markers yet)");
        return;
    }

    for (const auto& s : spawns_.spawns()) {
        ImGui::PushID(s.id);
        char lbl[180];
        std::snprintf(lbl, sizeof(lbl), "%s%s  (#%d)", s.name.c_str(),
                      s.modelPath.empty() ? " [logic]" : "", s.id);
        if (ImGui::Selectable(lbl, s.id == selectedSpawnId_,
                              ImGuiSelectableFlags_AllowDoubleClick))
            selectedSpawnId_ = s.id;
        if (ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            showSpawnLogic_ = true;
        ImGui::PopID();
    }

    SpawnPoint* sel = spawns_.findSpawn(selectedSpawnId_);
    if (!sel) {
        selectedSpawnId_ = -1;   // stale selection (undo / scene load)
        return;
    }
    ImGui::Separator();

    // Undo capture for widget edits (SpawnEditCommand::merge coalesces).
    auto trackWidget = [&]() {
        if (ImGui::IsItemActivated() && !spawnEditActive_) {
            spawnEditActive_ = true;
            spawnEditId_     = sel->id;
            spawnEditBefore_ = SpawnEditCommand::capture(*sel);
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && spawnEditActive_) {
            SpawnPoint* p = spawns_.findSpawn(spawnEditId_);
            if (p) {
                SpawnEditCommand::Fields after = SpawnEditCommand::capture(*p);
                if (after.name != spawnEditBefore_.name ||
                    after.tag != spawnEditBefore_.tag ||
                    after.modelPath != spawnEditBefore_.modelPath ||
                    after.defaultAnim != spawnEditBefore_.defaultAnim ||
                    after.position != spawnEditBefore_.position ||
                    after.yaw != spawnEditBefore_.yaw ||
                    after.scale != spawnEditBefore_.scale) {
                    history_.push(std::make_unique<SpawnEditCommand>(
                        spawns_, spawnEditId_, spawnEditBefore_, after));
                }
            }
            spawnEditActive_ = false;
        }
    };

    char nm[64];
    std::snprintf(nm, sizeof(nm), "%s", sel->name.c_str());
    if (ImGui::InputText("Name", nm, sizeof(nm))) sel->name = nm;
    trackWidget();
    char tg[64];
    std::snprintf(tg, sizeof(tg), "%s", sel->tag.c_str());
    if (ImGui::InputText("Tag", tg, sizeof(tg))) sel->tag = tg;
    trackWidget();

    ImGui::DragFloat3("Position", &sel->position[0], 0.1f);
    trackWidget();
    ImGui::SliderAngle("Yaw", &sel->yaw, -180.0f, 180.0f);
    trackWidget();
    ImGui::SliderFloat("Scale", &sel->scale, 0.1f, 5.0f, "%.2f");
    trackWidget();

    // Character model (optional): empty = pure logic spawn.
    if (sel->modelPath.empty()) {
        ImGui::TextDisabled("Model: (none — logic-only spawn)");
    } else {
        ImGui::TextWrapped("Model: %s",
            std::filesystem::path(sel->modelPath).filename().string().c_str());
    }
    if (ImGui::Button("Load model...")) {
        SpawnEditCommand::Fields before = SpawnEditCommand::capture(*sel);
        std::string p = openFileDialog("glTF / VRM", "*.gltf;*.glb;*.vrm",
                                       nativeWindow());
        if (!p.empty()) {
            sel->modelPath = p;
            history_.push(std::make_unique<SpawnEditCommand>(
                spawns_, sel->id, before, SpawnEditCommand::capture(*sel)));
        }
    }
    if (!sel->modelPath.empty()) {
        ImGui::SameLine();
        if (ImGui::Button("Clear model")) {
            SpawnEditCommand::Fields before = SpawnEditCommand::capture(*sel);
            sel->modelPath.clear();
            history_.push(std::make_unique<SpawnEditCommand>(
                spawns_, sel->id, before, SpawnEditCommand::capture(*sel)));
        }
    }
    char da[64];
    std::snprintf(da, sizeof(da), "%s", sel->defaultAnim.c_str());
    if (ImGui::InputText("Default anim", da, sizeof(da))) sel->defaultAnim = da;
    trackWidget();
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Animation played at spawn (resolved by the game)");

    if (ImGui::Button("Edit Logic...")) showSpawnLogic_ = true;
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Condition/action node graph (double-click a "
                          "marker works too)");
    ImGui::SameLine();
    if (ImGui::Button("Delete")) {
        SpawnPoint copy = *sel;   // copy BEFORE removeSpawn invalidates sel
        int id = sel->id;
        history_.push(std::make_unique<SpawnCommand>(
            spawns_, copy, false, "Delete Spawn"));
        spawns_.removeSpawn(id);
        selectedSpawnId_ = -1;
    }
    ImGui::TextDisabled("Graph: %d node%s", (int)sel->nodes.size(),
                        sel->nodes.size() == 1 ? "" : "s");
}

void App::drawCameraViewContent() {
    ImGui::Checkbox("Live previews", &camPreviewsLive_);
    ImGui::SameLine();
    if (camPreviewsLive_) ImGui::BeginDisabled();
    if (ImGui::Button("Refresh")) markCamPreviewsStale();
    if (camPreviewsLive_) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled("(renders one camera per frame, round-robin)");

    const auto& cams = cameraRig_.cameras();
    if (cams.empty()) {
        ImGui::Spacing();
        ImGui::TextDisabled("No cameras yet. Add one in the Cameras panel "
                            "(\"Add from current view\").");
        return;
    }

    const float thumbW = 224.0f;
    const float thumbH = thumbW * 9.0f / 16.0f;
    float avail = ImGui::GetContentRegionAvail().x;
    int cols = std::max(1, (int)(avail / (thumbW + 12.0f)));
    if (!ImGui::BeginTable("camgrid", cols)) return;
    for (size_t i = 0; i < cams.size(); ++i) {
        const SceneCamera& c = cams[i];
        ImGui::TableNextColumn();
        ImGui::PushID(c.id);

        bool hasTex = i < camPreviews_.size() &&
                      camPreviews_[i].color.id() != 0 &&
                      !camPreviews_[i].stale;
        if (hasTex) {
            // GL textures are bottom-up — flip V.
            ImGui::Image((ImTextureID)(intptr_t)camPreviews_[i].color.id(),
                         ImVec2(thumbW, thumbH), ImVec2(0, 1), ImVec2(1, 0));
        } else {
            ImGui::Dummy(ImVec2(thumbW, thumbH));
        }
        ImVec2 r0 = ImGui::GetItemRectMin(), r1 = ImGui::GetItemRectMax();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImU32 border = (c.id == selectedCameraId_)  ? IM_COL32(255, 230, 110, 255) :
                       (c.id == cameraRig_.activeId()) ? IM_COL32(90, 220, 110, 255)
                                                      : IM_COL32(70, 70, 70, 255);
        dl->AddRect(r0, r1, border, 4.0f, 0, 2.0f);
        if (!hasTex)
            dl->AddText(ImVec2(r0.x + 8, (r0.y + r1.y) * 0.5f - 7),
                        IM_COL32(140, 140, 140, 255), "(rendering...)");

        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            selectedCameraId_ = c.id;
        if (ImGui::IsItemHovered() &&
            ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            activateSceneCamera(c.id);

        char cap[160];
        std::snprintf(cap, sizeof(cap), "%s%s", c.name.c_str(),
                      c.id == cameraRig_.activeId() ? " [active]" : "");
        ImGui::TextUnformatted(cap);
        if (!c.tag.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("[%s]", c.tag.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndTable();
}

// --------------------------------------------------------------------------
// Spawn Logic: custom lightweight node editor (no dependencies).
// Left column = selected node params, right = pannable canvas with nodes
// and true/false link pins. All edits go through SpawnGraphCommand (undo).

void App::drawSpawnLogicContent() {
    SpawnPoint* sp = spawns_.findSpawn(selectedSpawnId_);
    if (!sp) {
        ImGui::TextDisabled("Select a spawn marker first (Spawns panel or "
                            "the spawn tool).");
        return;
    }
    // A different marker got selected: drop node-editor transient state.
    static int s_lastSpawnId = -1;
    if (s_lastSpawnId != sp->id) {
        s_lastSpawnId = sp->id;
        nodeEdSelectedNode_ = -1;
        nodeEdLinkFrom_ = -1;
        nodeEdDragNode_ = -1;
        nodeEdScroll_ = glm::vec2(0.0f);
    }

    ImGui::Text("Logic: %s", sp->name.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Template: If/Else animation")) {
        SpawnGraphCommand::State before = SpawnGraphCommand::capture(*sp);
        const LogicNode* root = sp->findNode(sp->rootId);
        glm::vec2 base = root ? root->uiPos + glm::vec2(200.0f, 0.0f)
                              : glm::vec2(220.0f, 120.0f);
        int cid = sp->addNode(LogicNode::Cond, base);
        int aT  = sp->addNode(LogicNode::Act, base + glm::vec2(220.0f, -55.0f));
        int aF  = sp->addNode(LogicNode::Act, base + glm::vec2(220.0f, 55.0f));
        // Fetch by id after every addNode (vector may reallocate).
        if (LogicNode* c = sp->findNode(cid)) {
            c->cond.type = Condition::FlagEquals;
            c->cond.flagId = 1;
            c->cond.value = 1;
        }
        if (LogicNode* a = sp->findNode(aT)) {
            a->act.type = Action::SetAnimation;
            a->act.param = "start_emotion_happy";
        }
        if (LogicNode* a = sp->findNode(aF)) {
            a->act.type = Action::SetAnimation;
            a->act.param = "start_emotion_sad";
        }
        if (LogicNode* r = sp->findNode(sp->rootId)) r->nextTrue = cid;
        if (LogicNode* c = sp->findNode(cid)) {
            c->nextTrue = aT;
            c->nextFalse = aF;
        }
        pushSpawnGraphEdit(sp->id, "Template: If/Else", false, before);
    }
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("if flag1 == 1: start_emotion_happy  else: "
                          "start_emotion_sad");
    ImGui::SameLine();
    if (ImGui::Button("Reset view")) nodeEdScroll_ = glm::vec2(0.0f);
    ImGui::Separator();

    ImGui::BeginChild("##nodeparams", ImVec2(250.0f, 0.0f), true);
    nodeEdParamsPanel(*sp);
    ImGui::EndChild();
    ImGui::SameLine();
    ImGui::BeginChild("##nodecanvas", ImVec2(0.0f, 0.0f), true,
                      ImGuiWindowFlags_NoScrollbar |
                      ImGuiWindowFlags_NoScrollWithMouse);
    nodeEdCanvas(*sp);
    ImGui::EndChild();
}

void App::nodeEdParamsPanel(SpawnPoint& sp) {
    ImGui::TextDisabled("Selected node");
    ImGui::Separator();
    LogicNode* n = sp.findNode(nodeEdSelectedNode_);
    if (!n) {
        ImGui::TextWrapped("Click a node to edit it.");
        ImGui::Spacing();
        ImGui::TextWrapped("Right-click canvas: add condition/action. "
                           "Drag from a pin to link. Middle-drag pans. "
                           "Right-click a node: delete/unlink.");
        return;
    }

    // Undo capture for continuous widgets (text/sliders/drags): snapshot on
    // activation, push on deactivation-after-edit. Combos (single-shot
    // changes) push immediately with a manually captured before-state.
    auto track = [&]() {
        if (ImGui::IsItemActivated() && !nodeEdEditActive_) {
            nodeEdEditActive_ = true;
            nodeEdBefore_ = SpawnGraphCommand::capture(sp);
        }
        if (ImGui::IsItemDeactivatedAfterEdit() && nodeEdEditActive_) {
            pushSpawnGraphEdit(sp.id, "Edit Node", true, nodeEdBefore_);
            nodeEdEditActive_ = false;
        }
    };

    ImGui::Text("Node #%d", n->id);
    if (n->kind == LogicNode::Root) {
        ImGui::TextWrapped("Graph entry point. Link its output to a "
                           "condition or an action.");
        return;
    }

    if (n->kind == LogicNode::Cond) {
        const char* names[] = { "Always", "Flag ==", "Flag !=", "Flag >",
                                "Flag <", "Random %", "Player near" };
        int ct = (int)n->cond.type;
        SpawnGraphCommand::State beforeCombo = SpawnGraphCommand::capture(sp);
        if (ImGui::Combo("Condition", &ct, names, 7)) {
            n->cond.type = (Condition::Type)ct;
            pushSpawnGraphEdit(sp.id, "Edit Node", true, beforeCombo);
        }
        bool needFlag = n->cond.type == Condition::FlagEquals ||
                        n->cond.type == Condition::FlagNotEquals ||
                        n->cond.type == Condition::FlagGreater ||
                        n->cond.type == Condition::FlagLess;
        if (needFlag) {
            ImGui::InputInt("Flag id", &n->cond.flagId);
            track();
            ImGui::InputInt("Value", &n->cond.value);
            track();
        } else if (n->cond.type == Condition::RandomChance) {
            ImGui::SliderInt("Percent", &n->cond.value, 0, 100);
            track();
        } else if (n->cond.type == Condition::PlayerNear) {
            ImGui::SliderInt("Radius (m)", &n->cond.value, 1, 50);
            track();
        }
        ImGui::TextWrapped("true -> top (green) pin, false -> bottom (red).");
    } else {
        const char* names[] = { "Spawn", "Despawn", "Set animation",
                                "Camera focus", "Wait", "Set flag",
                                "Dialog line", "Play sound" };
        int at = (int)n->act.type;
        SpawnGraphCommand::State beforeCombo = SpawnGraphCommand::capture(sp);
        if (ImGui::Combo("Action", &at, names, 8)) {
            n->act.type = (Action::Type)at;
            pushSpawnGraphEdit(sp.id, "Edit Node", true, beforeCombo);
        }
        switch (n->act.type) {
            case Action::Spawn:
            case Action::Despawn:
                ImGui::DragFloat("Delay (s)", &n->act.floatParam, 0.05f,
                                 0.0f, 120.0f, "%.1f");
                track();
                break;
            case Action::SetAnimation: {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "%s", n->act.param.c_str());
                if (ImGui::InputText("Animation", buf, sizeof(buf)))
                    n->act.param = buf;
                track();
                break;
            }
            case Action::CameraFocus: {
                // -1 = this marker, otherwise a scene-camera id.
                const char* preview = "This marker";
                if (n->act.intParam >= 0) {
                    const SceneCamera* c = cameraRig_.findCamera(n->act.intParam);
                    preview = c ? c->name.c_str() : "(missing camera)";
                }
                SpawnGraphCommand::State beforeSel =
                    SpawnGraphCommand::capture(sp);
                if (ImGui::BeginCombo("Camera", preview)) {
                    if (ImGui::Selectable("This marker", n->act.intParam < 0)) {
                        n->act.intParam = -1;
                        pushSpawnGraphEdit(sp.id, "Edit Node", true, beforeSel);
                    }
                    for (const auto& c : cameraRig_.cameras()) {
                        char lbl[96];
                        std::snprintf(lbl, sizeof(lbl), "%s (#%d)",
                                      c.name.c_str(), c.id);
                        if (ImGui::Selectable(lbl, n->act.intParam == c.id)) {
                            n->act.intParam = c.id;
                            pushSpawnGraphEdit(sp.id, "Edit Node", true,
                                               beforeSel);
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::DragFloat("Blend (s)", &n->act.floatParam, 0.05f,
                                 0.0f, 30.0f, "%.1f");
                track();
                break;
            }
            case Action::Wait:
                ImGui::DragFloat("Seconds", &n->act.floatParam, 0.05f,
                                 0.0f, 300.0f, "%.1f");
                track();
                break;
            case Action::SetFlag:
                ImGui::InputInt("Flag id", &n->act.intParam);
                track();
                ImGui::InputInt("Value", &n->act.intParam2);
                track();
                break;
            case Action::DialogLine: {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "%s", n->act.param.c_str());
                if (ImGui::InputText("Dialog id", buf, sizeof(buf)))
                    n->act.param = buf;
                track();
                break;
            }
            case Action::PlaySound: {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "%s", n->act.param.c_str());
                if (ImGui::InputText("Sound", buf, sizeof(buf)))
                    n->act.param = buf;
                track();
                break;
            }
            default: break;
        }
        ImGui::TextWrapped("Actions chain through the output pin (executed "
                           "in order by the game).");
    }

    ImGui::Separator();
    if (ImGui::Button("Delete node")) {
        SpawnGraphCommand::State before = SpawnGraphCommand::capture(sp);
        int id = n->id;
        sp.removeNode(id);
        pushSpawnGraphEdit(sp.id, "Delete Node", false, before);
        if (nodeEdSelectedNode_ == id) nodeEdSelectedNode_ = -1;
    }
}

void App::nodeEdCanvas(SpawnPoint& sp) {
    const float NODE_W = 168.0f;
    const float ROOT_W = 120.0f;
    const float PIN_R  = 6.0f;
    const float TITLE_H = 20.0f;

    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 avail  = ImGui::GetContentRegionAvail();
    ImGui::InvisibleButton("##canvas", avail,
                           ImGuiButtonFlags_MouseButtonLeft |
                           ImGuiButtonFlags_MouseButtonRight |
                           ImGuiButtonFlags_MouseButtonMiddle);
    const bool hovered = ImGui::IsItemHovered();
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const glm::vec2 mCanvas(mouse.x - origin.x + nodeEdScroll_.x,
                            mouse.y - origin.y + nodeEdScroll_.y);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(origin, ImVec2(origin.x + avail.x, origin.y + avail.y),
                     true);

    auto nodeSize = [&](const LogicNode& n) {
        if (n.kind == LogicNode::Root) return ImVec2(ROOT_W, 46.0f);
        return ImVec2(NODE_W, n.kind == LogicNode::Cond ? 70.0f : 62.0f);
    };
    auto nodeP0 = [&](const LogicNode& n) {
        return ImVec2(origin.x + n.uiPos.x - nodeEdScroll_.x,
                      origin.y + n.uiPos.y - nodeEdScroll_.y);
    };
    auto inPin = [&](const LogicNode& n) {
        ImVec2 p0 = nodeP0(n);
        ImVec2 sz = nodeSize(n);
        return ImVec2(p0.x, p0.y + sz.y * 0.5f);
    };
    auto outPin = [&](const LogicNode& n, bool falsePin) {
        ImVec2 p0 = nodeP0(n);
        ImVec2 sz = nodeSize(n);
        if (n.kind == LogicNode::Cond)
            return ImVec2(p0.x + sz.x,
                          falsePin ? p0.y + sz.y - 14.0f : p0.y + 26.0f);
        return ImVec2(p0.x + sz.x, p0.y + sz.y * 0.5f);
    };
    auto dist2 = [](ImVec2 a, ImVec2 b) {
        float dx = a.x - b.x, dy = a.y - b.y;
        return dx * dx + dy * dy;
    };
    auto hitNode = [&](ImVec2 p) -> int {
        for (const auto& n : sp.nodes) {
            ImVec2 p0 = nodeP0(n);
            ImVec2 sz = nodeSize(n);
            if (p.x >= p0.x && p.x <= p0.x + sz.x &&
                p.y >= p0.y && p.y <= p0.y + sz.y) return n.id;
        }
        return -1;
    };

    // --- Interactions ---
    if (nodeEdLinkFrom_ >= 0) {
        // Link drag: release over an in-pin connects, elsewhere cancels.
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            float bestD = (PIN_R + 5.0f) * (PIN_R + 5.0f);
            int target = -1;
            for (const auto& n : sp.nodes) {
                if (n.kind == LogicNode::Root) continue;   // root has no input
                float d = dist2(mouse, inPin(n));
                if (d < bestD) { bestD = d; target = n.id; }
            }
            if (target >= 0 && target != nodeEdLinkFrom_ &&
                !spawnGraphReachable(sp, target, nodeEdLinkFrom_)) {
                SpawnGraphCommand::State before =
                    SpawnGraphCommand::capture(sp);
                if (LogicNode* src = sp.findNode(nodeEdLinkFrom_)) {
                    if (nodeEdLinkFalse_) src->nextFalse = target;
                    else                  src->nextTrue  = target;
                }
                pushSpawnGraphEdit(sp.id, "Link Nodes", false, before);
            }
            nodeEdLinkFrom_ = -1;
        }
    } else if (nodeEdDragNode_ >= 0) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            if (LogicNode* n = sp.findNode(nodeEdDragNode_)) {
                // Snap to an 8px grid.
                n->uiPos.x = std::round((mCanvas.x - nodeEdDragOff_.x) / 8.0f) * 8.0f;
                n->uiPos.y = std::round((mCanvas.y - nodeEdDragOff_.y) / 8.0f) * 8.0f;
            }
        } else {
            // End of drag: one undo entry if the node actually moved.
            bool moved = false;
            for (const auto& bn : nodeEdBefore_.nodes) {
                const LogicNode* cur = sp.findNode(bn.id);
                if (cur && cur->uiPos != bn.uiPos) { moved = true; break; }
            }
            if (moved)
                pushSpawnGraphEdit(sp.id, "Move Node", false, nodeEdBefore_);
            nodeEdDragNode_ = -1;
        }
    } else if (nodeEdPanning_) {
        if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
            nodeEdScroll_.x -= ImGui::GetIO().MouseDelta.x;
            nodeEdScroll_.y -= ImGui::GetIO().MouseDelta.y;
        } else {
            nodeEdPanning_ = false;
        }
    } else if (hovered) {
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
            nodeEdPanning_ = true;
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            // Priority: out-pin > node body > empty (deselect).
            int pinNode = -1;
            bool pinFalse = false;
            float bestD = (PIN_R + 5.0f) * (PIN_R + 5.0f);
            for (const auto& n : sp.nodes) {
                float d = dist2(mouse, outPin(n, false));
                if (d < bestD) { bestD = d; pinNode = n.id; pinFalse = false; }
                if (n.kind == LogicNode::Cond) {
                    d = dist2(mouse, outPin(n, true));
                    if (d < bestD) { bestD = d; pinNode = n.id; pinFalse = true; }
                }
            }
            if (pinNode >= 0) {
                nodeEdLinkFrom_ = pinNode;
                nodeEdLinkFalse_ = pinFalse;
            } else {
                int hit = hitNode(mouse);
                nodeEdSelectedNode_ = hit;
                if (hit >= 0) {
                    nodeEdDragNode_ = hit;
                    nodeEdBefore_ = SpawnGraphCommand::capture(sp);
                    const LogicNode* n = sp.findNode(hit);
                    nodeEdDragOff_ = mCanvas - n->uiPos;
                }
            }
        }
        if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            nodeEdCtxNode_ = hitNode(mouse);
            nodeEdCtxPos_ = mCanvas;
            ImGui::OpenPopup("##nodectx");
        }
    }

    // Context popup (add / delete / unlink).
    if (ImGui::BeginPopup("##nodectx")) {
        if (nodeEdCtxNode_ >= 0) {
            LogicNode* n = sp.findNode(nodeEdCtxNode_);
            if (n && n->kind != LogicNode::Root) {
                if (ImGui::MenuItem("Delete node")) {
                    SpawnGraphCommand::State before =
                        SpawnGraphCommand::capture(sp);
                    int id = n->id;
                    sp.removeNode(id);
                    pushSpawnGraphEdit(sp.id, "Delete Node", false, before);
                    if (nodeEdSelectedNode_ == id) nodeEdSelectedNode_ = -1;
                }
                if (ImGui::MenuItem("Unlink outputs")) {
                    SpawnGraphCommand::State before =
                        SpawnGraphCommand::capture(sp);
                    n->nextTrue = -1;
                    n->nextFalse = -1;
                    pushSpawnGraphEdit(sp.id, "Unlink", false, before);
                }
            } else {
                ImGui::TextDisabled("Root node (fixed)");
            }
        } else {
            if (ImGui::MenuItem("Add condition")) {
                SpawnGraphCommand::State before =
                    SpawnGraphCommand::capture(sp);
                nodeEdSelectedNode_ =
                    sp.addNode(LogicNode::Cond, nodeEdCtxPos_);
                pushSpawnGraphEdit(sp.id, "Add Node", false, before);
            }
            ImGui::Separator();
            for (int t = 0; t <= (int)Action::PlaySound; ++t) {
                char lbl[64];
                std::snprintf(lbl, sizeof(lbl), "Add: %s", actTypeName(t));
                if (ImGui::MenuItem(lbl)) {
                    SpawnGraphCommand::State before =
                        SpawnGraphCommand::capture(sp);
                    int nid = sp.addNode(LogicNode::Act, nodeEdCtxPos_);
                    if (LogicNode* a = sp.findNode(nid))
                        a->act.type = (Action::Type)t;
                    nodeEdSelectedNode_ = nid;
                    pushSpawnGraphEdit(sp.id, "Add Node", false, before);
                }
            }
        }
        ImGui::EndPopup();
    }

    // --- Drawing ---
    // Grid dots.
    const float grid = 32.0f;
    ImU32 gridCol = IM_COL32(255, 255, 255, 13);
    float sx = std::fmod(nodeEdScroll_.x, grid);
    float sy = std::fmod(nodeEdScroll_.y, grid);
    for (float x = origin.x - sx; x < origin.x + avail.x; x += grid)
        for (float y = origin.y - sy; y < origin.y + avail.y; y += grid)
            dl->AddCircleFilled(ImVec2(x, y), 1.0f, gridCol);

    // Links (below nodes).
    auto bezier = [&](ImVec2 a, ImVec2 b, ImU32 col) {
        float dx = std::max(40.0f, std::abs(b.x - a.x) * 0.5f);
        dl->AddBezierCubic(a, ImVec2(a.x + dx, a.y),
                           ImVec2(b.x - dx, b.y), b, col, 2.5f);
    };
    for (const auto& n : sp.nodes) {
        if (n.nextTrue >= 0) {
            if (const LogicNode* t = sp.findNode(n.nextTrue)) {
                ImU32 col = n.kind == LogicNode::Cond
                                ? IM_COL32(90, 220, 110, 255)
                                : IM_COL32(185, 185, 195, 230);
                bezier(outPin(n, false), inPin(*t), col);
            }
        }
        if (n.kind == LogicNode::Cond && n.nextFalse >= 0) {
            if (const LogicNode* t = sp.findNode(n.nextFalse))
                bezier(outPin(n, true), inPin(*t), IM_COL32(230, 100, 90, 255));
        }
    }
    // In-progress link drag.
    if (nodeEdLinkFrom_ >= 0) {
        if (const LogicNode* src = sp.findNode(nodeEdLinkFrom_)) {
            ImU32 col = nodeEdLinkFalse_ ? IM_COL32(230, 100, 90, 255)
                                         : IM_COL32(90, 220, 110, 255);
            bezier(outPin(*src, nodeEdLinkFalse_), mouse, col);
        }
    }

    // Nodes.
    for (const auto& n : sp.nodes) {
        ImVec2 p0 = nodeP0(n);
        ImVec2 sz = nodeSize(n);
        ImVec2 p1(p0.x + sz.x, p0.y + sz.y);
        ImU32 titleCol = n.kind == LogicNode::Root      ? IM_COL32(88, 88, 102, 255) :
                         n.kind == LogicNode::Cond ? IM_COL32(185, 122, 48, 255)
                                                        : IM_COL32(58, 108, 185, 255);
        dl->AddRectFilled(p0, p1, IM_COL32(37, 39, 47, 255), 6.0f);
        dl->AddRectFilled(p0, ImVec2(p1.x, p0.y + TITLE_H), titleCol, 6.0f,
                          ImDrawFlags_RoundCornersTop);
        dl->AddRect(p0, p1,
                    n.id == nodeEdSelectedNode_ ? IM_COL32(255, 230, 110, 255)
                                                : IM_COL32(18, 18, 22, 255),
                    6.0f, 0, n.id == nodeEdSelectedNode_ ? 2.0f : 1.0f);
        const char* title = n.kind == LogicNode::Root      ? "ROOT" :
                            n.kind == LogicNode::Cond ? "IF" : "DO";
        dl->AddText(ImVec2(p0.x + 7, p0.y + 3), IM_COL32(255, 255, 255, 255),
                    title);

        std::string line1, line2;
        if (n.kind == LogicNode::Root) {
            line1 = "scene start";
        } else if (n.kind == LogicNode::Cond) {
            line1 = condTypeName(n.cond.type);
            line2 = condSummary(n.cond);
        } else {
            line1 = actTypeName(n.act.type);
            line2 = actSummary(n.act);
        }
        dl->AddText(ImVec2(p0.x + 8, p0.y + TITLE_H + 7),
                    IM_COL32(222, 222, 228, 255), line1.c_str());
        if (!line2.empty())
            dl->AddText(ImVec2(p0.x + 8, p0.y + TITLE_H + 23),
                        IM_COL32(158, 163, 175, 255), line2.c_str());

        // Pins.
        if (n.kind != LogicNode::Root) {
            dl->AddCircleFilled(inPin(n), PIN_R, IM_COL32(205, 205, 215, 255));
            dl->AddCircle(inPin(n), PIN_R, IM_COL32(18, 18, 22, 255), 0, 1.5f);
        }
        if (n.kind == LogicNode::Cond) {
            ImVec2 ot = outPin(n, false);
            ImVec2 of = outPin(n, true);
            dl->AddCircleFilled(ot, PIN_R, IM_COL32(90, 220, 110, 255));
            dl->AddCircleFilled(of, PIN_R, IM_COL32(230, 100, 90, 255));
            dl->AddText(ImVec2(ot.x - 15, ot.y - 7), IM_COL32(90, 220, 110, 255), "T");
            dl->AddText(ImVec2(of.x - 15, of.y - 7), IM_COL32(230, 100, 90, 255), "F");
        } else {
            dl->AddCircleFilled(outPin(n, false), PIN_R,
                                IM_COL32(205, 205, 215, 255));
        }
    }

    dl->PopClipRect();
}

