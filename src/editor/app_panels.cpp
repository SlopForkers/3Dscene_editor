// Left-rail panel contents: one draw*Content() per category (Brush, Vertex,
// Props, Vegetation, Build, Terrain, Noise, Layers, Env, View, File).
#include "app.h"
#include "ui_common.h"
#include "model.h"
#include "commands.h"
#include "file_dialog.h"
#include <imgui.h>
#include <algorithm>
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

void App::drawPropsContent() {
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
    }
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
    ImGui::TextDisabled("Selected block");
    const char* faceNames[] = { "+X", "-X", "+Y", "-Y", "+Z", "-Z" };
    if (selectedBlockId_ >= 0) {
        const BuildSystem::Block* b = build_.findBlock(selectedBlockId_);
        if (b) {
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
        }
    } else {
        ImGui::TextDisabled("(none — right-click a block to inspect)");
    }

    ImGui::Separator();
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
        camera_.setViewport(fbWidth_, fbHeight_);
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
                       "heights + splat, with props/blocks/details embedded in JSON. "
                       "Asset paths are stored relative to the scene file.");
}

