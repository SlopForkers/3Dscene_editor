#include "tools.h"
#include "app.h"
#include "input.h"
#include "commands.h"
#include "imgui.h"
#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <algorithm>
#include <array>
#include <unordered_map>

// ============================================================================
// CameraTool — scene camera picking (cursor mode, no brush)
// ============================================================================

bool CameraTool::handleInput(App& app, float /*dt*/, const ImGuiIO& /*io*/,
                             bool overUI, bool /*typing*/) {
    if (g_input.mousePressed(Input::Left) && !overUI) {
        glm::vec3 ro, rd;
        app.cursorRay(ro, rd);
        int id = app.pickSceneCamera(ro, rd);
        app.selectedCameraId_ = id;   // -1 on empty click = deselect
        if (id >= 0) app.showCameras_ = true;   // surface the params panel
        return id >= 0;
    }
    return false;
}

void CameraTool::drawPanelContent(App& app) {
    app.drawCameraToolContent();
}

// ============================================================================
// SpawnTool — spawn marker picking / moving / placement (cursor mode)
// ============================================================================

void SpawnTool::cancelDrag() {
    moving_ = false;
    moveId_ = -1;
}

void SpawnTool::drawPanelContent(App& app) {
    app.drawSpawnToolContent();
}

bool SpawnTool::handleInput(App& app, float /*dt*/, const ImGuiIO& /*io*/,
                            bool overUI, bool /*typing*/) {
    bool ctrl = g_input.keyDown(GLFW_KEY_LEFT_CONTROL) ||
                g_input.keyDown(GLFW_KEY_RIGHT_CONTROL);

    if (g_input.mousePressed(Input::Left) && !overUI) {
        glm::vec3 ro, rd;
        app.cursorRay(ro, rd);
        if (ctrl) {
            // Ctrl+click places a new marker at the terrain hit.
            glm::vec3 hit;
            if (app.terrain_.raycast(ro, rd, hit)) app.addSpawnAt(hit);
            return true;
        }
        int id = app.pickSpawn(ro, rd);
        app.selectedSpawnId_ = id;   // -1 on empty click = deselect
        if (id >= 0) {
            app.showSpawns_ = true;
            SpawnPoint* sp = app.spawns_.findSpawn(id);
            if (sp) {
                // Begin a move drag; capture fields for the undo command.
                moving_ = true;
                moveId_ = id;
                moveBefore_ = SpawnEditCommand::capture(*sp);
            }
        }
        return id >= 0;
    }

    if (moving_ && g_input.mouseDown(Input::Left)) {
        SpawnPoint* sp = app.spawns_.findSpawn(moveId_);
        if (sp) {
            glm::vec3 ro, rd;
            app.cursorRay(ro, rd);
            glm::vec3 hit;
            if (app.terrain_.raycast(ro, rd, hit)) sp->position = hit;
        }
    }
    if (g_input.mouseReleased(Input::Left) && moving_) {
        SpawnPoint* sp = app.spawns_.findSpawn(moveId_);
        if (sp) {
            SpawnEditCommand::Fields after = SpawnEditCommand::capture(*sp);
            if (after.position != moveBefore_.position) {
                app.history_.push(std::make_unique<SpawnEditCommand>(
                    app.spawns_, moveId_, moveBefore_, after));
            }
        }
        moving_ = false;
        moveId_ = -1;
    }
    return moving_;
}

// ============================================================================
// TerrainTool — brush sculpt + vegetation painting
// ============================================================================

void TerrainTool::cancelDrag() {
    painting_ = false;
    heightsCmd_.reset();
    splatCmd_.reset();
    detailCmd_.reset();
}

void TerrainTool::drawPanelContent(App& app) {
    app.drawBrushContent();
}

void TerrainTool::beginStroke(App& app) {
    if (app.brush_.type == Terrain::BrushParams::Vegetation)
        detailCmd_ = std::make_unique<DetailPaintCommand>(app.details_);
    else if (app.brush_.type == Terrain::BrushParams::Texture)
        splatCmd_ = std::make_unique<TerrainSplatCommand>(app.terrain_);
    else
        heightsCmd_ = std::make_unique<TerrainHeightsCommand>(
            app.terrain_, app.details_, app.build_, "Sculpt Terrain");
}

void TerrainTool::endStroke(App& app) {
    if (heightsCmd_) {
        if (heightsCmd_->hasChanges()) app.history_.push(std::move(heightsCmd_));
        else heightsCmd_.reset();
    }
    if (splatCmd_) {
        if (splatCmd_->hasChanges()) app.history_.push(std::move(splatCmd_));
        else splatCmd_.reset();
    }
    if (detailCmd_) {
        if (!detailCmd_->added.empty() || !detailCmd_->removed.empty())
            app.history_.push(std::move(detailCmd_));
        else detailCmd_.reset();
    }
}

bool TerrainTool::handleInput(App& app, float dt, const ImGuiIO& /*io*/, bool overUI, bool /*typing*/) {
    // NB: no io.WantCaptureMouse early-out here — with docking the whole
    // window is a dockspace, so WantCaptureMouse is almost always true. The
    // overUI flag (viewport hover aware) is the authoritative gate.
    if (g_input.mousePressed(Input::Left)) {
        painting_ = !overUI;
        if (painting_) beginStroke(app);
    }
    if (g_input.mouseReleased(Input::Left)) {
        if (painting_) endStroke(app);
        painting_ = false;
    }
    if (!painting_) return false;

    glm::vec3 origin, dir;
    app.cursorRay(origin, dir);
    glm::vec3 hit;
    if (!app.terrain_.raycast(origin, dir, hit)) return false;

    if (app.brush_.type == Terrain::BrushParams::Vegetation) {
        bool erase = g_input.keyDown(GLFW_KEY_LEFT_CONTROL) ||
                     g_input.keyDown(GLFW_KEY_RIGHT_CONTROL);
        float s = std::clamp(app.brush_.strength, 0.05f, 2.0f);
        float density = app.continuousStroke_ ? s * dt * 60.0f : s;
        if (detailCmd_ && erase) {
            // Capture instances about to be erased (within the brush disc).
            float r2 = app.brush_.radius * app.brush_.radius;
            for (const auto& inst : app.details_.instances()) {
                float dx = inst.position.x - hit.x;
                float dz = inst.position.z - hit.z;
                if (dx * dx + dz * dz < r2) detailCmd_->removed.push_back(inst);
            }
        }
        size_t base = app.details_.instances().size();
        app.details_.paint(app.terrain_, hit, app.brush_.radius, density, erase);
        if (detailCmd_ && !erase) {
            size_t now = app.details_.instances().size();
            for (size_t i = base; i < now; ++i)
                detailCmd_->added.push_back(app.details_.instances()[i]);
        }
    } else {
        int x0, z0, x1, z1;
        app.terrain_.brushFootprint(hit, app.brush_.radius, x0, z0, x1, z1);
        if (heightsCmd_) heightsCmd_->captureRectBefore(x0, z0, x1, z1);
        if (splatCmd_) {
            for (int iz = z0; iz <= z1; ++iz)
                for (int ix = x0; ix <= x1; ++ix)
                    splatCmd_->captureBefore(ix, iz);
        }
        float amount = app.continuousStroke_ ? app.brush_.strength * dt * 60.0f
                                             : app.brush_.strength;
        Terrain::BrushParams step = app.brush_;
        step.strength = amount;
        bool changed = app.terrain_.applyBrush(step, hit);
        if (changed) {
            if (heightsCmd_) heightsCmd_->captureRectAfter(x0, z0, x1, z1);
            if (splatCmd_) {
                for (int iz = z0; iz <= z1; ++iz)
                    for (int ix = x0; ix <= x1; ++ix)
                        splatCmd_->captureAfter(ix, iz);
            }
            app.details_.reproject(app.terrain_, hit, app.brush_.radius * 1.5f);
            app.build_.reproject(app.terrain_, hit, app.brush_.radius * 1.5f);
        }
    }
    return true;
}

// ============================================================================
// PropTool — prop picking + gizmo
// ============================================================================

void PropTool::cancelDrag() { /* gizmo cancel handled externally */ }

void PropTool::drawPanelContent(App& app) {
    app.drawPropToolContent();
}

bool PropTool::handleInput(App& app, float dt, const ImGuiIO& /*io*/, bool overUI, bool /*typing*/) {
    (void)dt;
    Prop* sel = app.props_.selected();

    bool gizmoConsumed = false;
    if (sel) {
        Gizmo::Transform cur{ sel->position, sel->rotationEuler, sel->scale };
        Gizmo::Transform next;
        if (app.gizmo_.handleInput(app.camera_, sel->position, cur, next, overUI)) {
            gizmoConsumed = true;
            if (app.gizmo_.dragging()) {
                if (!gizmoWasDragging_) {
                    // Drag start: snapshot the transform for undo.
                    dragPropId_      = sel->id;
                    dragStartPos_    = sel->position;
                    dragStartRot_    = sel->rotationEuler;
                    dragStartScale_  = sel->scale;
                }
                sel->position       = next.position;
                sel->rotationEuler  = next.rotationEuler;
                sel->scale          = next.scale;
            }
        }
    }

    // Drag end: push the transform command (if anything actually moved).
    if (gizmoWasDragging_ && !app.gizmo_.dragging()) {
        Prop* p = app.props_.findProp(dragPropId_);
        if (p && (p->position != dragStartPos_ ||
                  p->rotationEuler != dragStartRot_ ||
                  p->scale != dragStartScale_)) {
            app.history_.push(std::make_unique<PropTransformCommand>(
                app.props_, dragPropId_,
                dragStartPos_, dragStartRot_, dragStartScale_,
                p->position, p->rotationEuler, p->scale));
        }
        dragPropId_ = -1;
    }
    gizmoWasDragging_ = app.gizmo_.dragging();

    if (!gizmoConsumed && g_input.mousePressed(Input::Left) && !overUI) {
        glm::vec3 origin, dir;
        app.cursorRay(origin, dir);
        int picked = app.props_.pick(origin, dir);
        if (picked >= 0)
            app.props_.select(picked);
        else
            app.props_.select(-1);
    }
    return gizmoConsumed;
}

// ============================================================================
// VertexTool — vertex-level terrain editing
// ============================================================================

void VertexTool::cancelDrag() {
    // VertexEditor manages its own drag state (App cancels it on switch).
    cmd_.reset();
    wasDragging_ = false;
}

void VertexTool::drawPanelContent(App& app) {
    app.drawVertexContent();
}

bool VertexTool::handleInput(App& app, float dt, const ImGuiIO& io, bool overUI, bool /*typing*/) {
    (void)dt;
    if (!app.wireframe_) {
        if (g_input.mousePressed(Input::Left) && !overUI)
            app.wireframe_ = true;
        return false;
    }

    bool wasDragging = app.vertexEditor_.dragging();
    app.vertexEditor_.handleInput(app.camera_, app.terrain_, app.brush_.radius,
                                  app.brush_.falloff, io, overUI);
    bool nowDragging = app.vertexEditor_.dragging();

    // Drag start: capture the affected box (dragBox persists after release,
    // so reading it on the release frame is still valid).
    if (nowDragging && !wasDragging) {
        cmd_ = std::make_unique<TerrainHeightsCommand>(
            app.terrain_, app.details_, app.build_, "Vertex Edit");
        int x0, z0, x1, z1;
        app.vertexEditor_.dragBox(x0, z0, x1, z1);
        cmd_->captureRectBefore(x0, z0, x1, z1);
    }
    // Drag end: capture and push.
    if (!nowDragging && wasDragging && cmd_) {
        int x0, z0, x1, z1;
        app.vertexEditor_.dragBox(x0, z0, x1, z1);
        cmd_->captureRectAfter(x0, z0, x1, z1);
        if (cmd_->hasChanges()) app.history_.push(std::move(cmd_));
        else cmd_.reset();
    }
    wasDragging_ = nowDragging;

    if (nowDragging || wasDragging) {
        glm::vec3 c = app.vertexEditor_.selectionCenter();
        app.details_.reproject(app.terrain_, c, app.brush_.radius * 1.5f);
        app.build_.reproject(app.terrain_, c, app.brush_.radius * 1.5f);
    }
    return nowDragging;
}

// ============================================================================
// BuildTool — snap-based block building
// ============================================================================

void BuildTool::cancelDrag() {
    buildDragging_ = false;
    buildDragOnBlocks_ = false;
    buildTexFace_ = -1;
}

void BuildTool::drawPanelContent(App& app) {
    app.drawBuildContent();
}

namespace {

// Per-block face-texture state, keyed by block id.
struct TexState { int ti, tf, tm; float ts; };
using TexStateMap = std::unordered_map<int, TexState>;

TexStateMap snapshotTexState(BuildSystem& build) {
    TexStateMap m;
    for (const auto& b : build.blocks())
        m[b.id] = { b.textureIdx, b.textureFace, b.texMode, b.texScale };
    return m;
}

// Push a BlockTextureCommand for every block whose texture state differs
// from the snapshot. No-op when nothing changed.
void pushTexDiff(App& app, const TexStateMap& before) {
    std::vector<BlockTextureCommand::Entry> entries;
    for (const auto& b : app.build_.blocks()) {
        auto it = before.find(b.id);
        if (it == before.end()) continue;
        const TexState& o = it->second;
        if (o.ti == b.textureIdx && o.tf == b.textureFace &&
            o.ts == b.texScale && o.tm == b.texMode) continue;
        BlockTextureCommand::Entry e;
        e.blockId = b.id;
        e.oldTexIdx = o.ti;  e.oldTexFace = o.tf;  e.oldTexScale = o.ts;  e.oldTexMode = o.tm;
        e.newTexIdx = b.textureIdx; e.newTexFace = b.textureFace;
        e.newTexScale = b.texScale; e.newTexMode = b.texMode;
        entries.push_back(e);
    }
    if (!entries.empty())
        app.history_.push(std::make_unique<BlockTextureCommand>(app.build_, std::move(entries)));
}

// Collect blocks added since `baseSize` (they append at the tail).
std::vector<BuildSystem::Block> tailBlocks(BuildSystem& build, size_t baseSize) {
    std::vector<BuildSystem::Block> out;
    const auto& all = build.blocks();
    for (size_t i = baseSize; i < all.size(); ++i) out.push_back(all[i]);
    return out;
}

} // namespace

bool BuildTool::handleInput(App& app, float dt, const ImGuiIO& /*io*/, bool overUI, bool typing) {
    (void)dt;
    glm::vec3 origin, dir;
    app.cursorRay(origin, dir);

    bool ctrl = g_input.keyDown(GLFW_KEY_LEFT_CONTROL) ||
                g_input.keyDown(GLFW_KEY_RIGHT_CONTROL);
    float gs = app.build_.gridStep();
    BuildSystem::Mode bmode = app.build_.mode();

    if (!typing && g_input.keyPressed(GLFW_KEY_Z)) app.build_.setMode(BuildSystem::ModeFoundation);
    if (!typing && g_input.keyPressed(GLFW_KEY_X)) app.build_.setMode(BuildSystem::ModeWall);
    if (!typing && g_input.keyPressed(GLFW_KEY_C)) app.build_.setMode(BuildSystem::ModeTexture);
    if (!typing && bmode == BuildSystem::ModeWall && g_input.keyPressed(GLFW_KEY_R))
        app.build_.rotateWallEdge();

    // Ghost preview (only when not dragging).
    if (!buildDragging_) {
        app.hasGhost_ = false;
        if (!overUI) {
            glm::vec3 hp, hn;
            int id = app.build_.pick(origin, dir, hp, hn);
            if (id >= 0) {
                const BuildSystem::Block* b = app.build_.findBlock(id);
                if (b) {
                    if (bmode == BuildSystem::ModeTexture) {
                        app.selectedBlockId_ = id;
                        app.selectedBlockFace_ = BuildSystem::faceFromNormal(hn);
                    } else if (bmode == BuildSystem::ModeWall && hn.y > 0.5f) {
                        float fixed; bool alongX;
                        app.build_.wallLineParamsFor(*b, fixed, alongX);
                        app.ghostSize_ = alongX
                            ? glm::vec3(app.build_.blockWidth(), app.build_.blockHeight(), app.build_.wallThickness())
                            : glm::vec3(app.build_.wallThickness(), app.build_.blockHeight(), app.build_.blockWidth());
                        float ex = alongX ? b->position.x : fixed;
                        float ez = alongX ? fixed : b->position.z;
                        if (alongX) ex = std::round(ex / gs) * gs;
                        else        ez = std::round(ez / gs) * gs;
                        app.ghostCenter_ = glm::vec3(ex, b->max().y + app.build_.blockHeight() * 0.5f, ez);
                        app.ghostType_ = BuildSystem::Wall;
                        app.hasGhost_ = true;
                    } else if (bmode == BuildSystem::ModeFoundation && hn.y <= 0.5f) {
                        glm::vec3 gc, gsz;
                        BuildSystem::BlockType gt;
                        if (app.build_.computePlacement(app.terrain_, origin, dir, gc, gsz, gt)) {
                            app.ghostCenter_ = gc; app.ghostSize_ = gsz; app.ghostType_ = gt;
                            app.hasGhost_ = true;
                        }
                    }
                }
            } else if (bmode == BuildSystem::ModeFoundation) {
                glm::vec3 tHit;
                if (app.terrain_.raycast(origin, dir, tHit)) {
                    float gx = std::round(tHit.x / gs) * gs;
                    float gz = std::round(tHit.z / gs) * gs;
                    app.ghostSize_ = glm::vec3(app.build_.blockWidth(), app.build_.blockHeight(), app.build_.blockWidth());
                    float th = app.terrain_.heightAtWorld(gx, gz);
                    float topY = th + app.build_.blockHeight() * (1.0f - app.build_.sunkDepth());
                    app.ghostCenter_ = glm::vec3(gx, topY - app.build_.blockHeight() * 0.5f, gz);
                    app.ghostType_ = BuildSystem::Foundation;
                    app.hasGhost_ = true;
                }
            }
        }
    } else {
        app.hasGhost_ = false;
    }

    // Press: start drag or single-place.
    if (g_input.mousePressed(Input::Left) && !overUI) {
        glm::vec3 hp, hn;
        int id = app.build_.pick(origin, dir, hp, hn);
        buildDragErase_ = ctrl;
        if (id >= 0) {
            const BuildSystem::Block* b = app.build_.findBlock(id);
            if (ctrl) {
                if (bmode == BuildSystem::ModeTexture) {
                    auto before = snapshotTexState(app.build_);
                    app.build_.clearBlockFaceTexture(id);
                    pushTexDiff(app, before);
                    if (app.selectedBlockId_ == id) app.selectedBlockFace_ = -1;
                } else if (b) {
                    app.history_.push(std::make_unique<BlocksCommand>(
                        app.build_, std::vector<BuildSystem::Block>{*b},
                        false /*removed*/, "Remove Block"));
                    app.build_.removeBlock(id);
                    if (app.selectedBlockId_ == id) { app.selectedBlockId_ = -1; app.selectedBlockFace_ = -1; }
                }
            } else if (b) {
                if (bmode == BuildSystem::ModeTexture) {
                    int face = BuildSystem::faceFromNormal(hn);
                    bool horizontal = (face == BuildSystem::FacePY || face == BuildSystem::FaceNY);
                    buildTexFace_ = face;
                    buildTexLine_ = !horizontal;
                    buildTexPressBlock_ = id;
                    buildTexPressFace_ = face;
                    buildTexPressMX_ = g_input.mouseX();
                    buildTexPressMY_ = g_input.mouseY();
                    buildDragging_ = true;
                    buildDragOnBlocks_ = !horizontal;
                    if (horizontal) {
                        buildDragStart_ = glm::vec2(std::round(hp.x / gs) * gs,
                                                     std::round(hp.z / gs) * gs);
                    } else {
                        buildDragAlongX_ = (face == BuildSystem::FacePZ || face == BuildSystem::FaceNZ);
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
                    app.selectedBlockId_ = id;
                    app.selectedBlockFace_ = face;
                } else if (bmode == BuildSystem::ModeWall && hn.y > 0.5f) {
                    float fixed; bool alongX;
                    app.build_.wallLineParamsFor(*b, fixed, alongX);
                    buildDragging_ = true;
                    buildDragOnBlocks_ = true;
                    buildDragBaseY_ = b->max().y;
                    buildDragAlongX_ = alongX;
                    buildDragFixed_ = fixed;
                    buildDragStart_ = alongX
                        ? glm::vec2(std::round(hp.x / gs) * gs, fixed)
                        : glm::vec2(fixed, std::round(hp.z / gs) * gs);
                } else if (bmode == BuildSystem::ModeFoundation && hn.y <= 0.5f) {
                    glm::vec3 gc, gsz;
                    BuildSystem::BlockType gt;
                    if (app.build_.computePlacement(app.terrain_, origin, dir, gc, gsz, gt)) {
                        int nid = app.build_.placeBlock(gc, gsz, gt, app.build_.color());
                        if (const BuildSystem::Block* nb = app.build_.findBlock(nid))
                            app.history_.push(std::make_unique<BlocksCommand>(
                                app.build_, std::vector<BuildSystem::Block>{*nb},
                                true /*added*/, "Place Block"));
                        app.selectedBlockId_ = nid;
                    }
                }
            }
        } else if (bmode == BuildSystem::ModeFoundation ||
                   (ctrl && bmode != BuildSystem::ModeTexture)) {
            glm::vec3 tHit;
            if (app.terrain_.raycast(origin, dir, tHit)) {
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
            if (app.terrain_.raycast(origin, dir, tHit)) {
                float gx = std::round(tHit.x / gs) * gs;
                float gz = std::round(tHit.z / gs) * gs;
                if (bmode == BuildSystem::ModeTexture) {
                    float pdx = (float)(g_input.mouseX() - buildTexPressMX_);
                    float pdy = (float)(g_input.mouseY() - buildTexPressMY_);
                    bool moved = (pdx * pdx + pdy * pdy) > 25.0f;
                    auto before = snapshotTexState(app.build_);
                    if (!moved) {
                        if (buildTexPressBlock_ >= 0 && buildTexPressFace_ >= 0)
                            app.build_.paintCurrentTexture(buildTexPressBlock_, buildTexPressFace_);
                    } else if (buildTexLine_) {
                        float startC = buildDragAlongX_ ? buildDragStart_.x : buildDragStart_.y;
                        float curC   = buildDragAlongX_ ? gx : gz;
                        app.build_.applyTextureToLine(startC, curC, buildDragFixed_,
                                                       buildDragAlongX_, buildTexFace_);
                    } else {
                        app.build_.applyTextureToRect(buildDragStart_.x, buildDragStart_.y,
                                                       gx, gz, buildTexFace_);
                    }
                    pushTexDiff(app, before);
                } else if (buildDragErase_) {
                    std::vector<BuildSystem::Block> removed;
                    int n = app.build_.eraseRect(buildDragStart_.x, buildDragStart_.y, gx, gz, &removed);
                    if (n > 0) {
                        app.history_.push(std::make_unique<BlocksCommand>(
                            app.build_, std::move(removed), false, "Erase Blocks"));
                        app.selectedBlockId_ = -1; app.selectedBlockFace_ = -1;
                    }
                } else if (buildDragOnBlocks_) {
                    size_t base = app.build_.blocks().size();
                    float startC = buildDragAlongX_ ? buildDragStart_.x : buildDragStart_.y;
                    float curC = buildDragAlongX_ ? gx : gz;
                    int n = app.build_.fillWallLine(startC, curC, buildDragFixed_,
                                                     buildDragBaseY_, buildDragAlongX_);
                    if (n > 0)
                        app.history_.push(std::make_unique<BlocksCommand>(
                            app.build_, tailBlocks(app.build_, base), true, "Fill Walls"));
                } else {
                    size_t base = app.build_.blocks().size();
                    int n = app.build_.fillRect(app.terrain_, buildDragStart_.x, buildDragStart_.y,
                                                gx, gz, BuildSystem::Foundation);
                    if (n > 0)
                        app.history_.push(std::make_unique<BlocksCommand>(
                            app.build_, tailBlocks(app.build_, base), true, "Fill Blocks"));
                }
            }
        }
        buildDragging_ = false;
        buildDragOnBlocks_ = false;
        buildTexFace_ = -1;
    }

    if (!typing && g_input.keyPressed(GLFW_KEY_DELETE) && app.selectedBlockId_ >= 0) {
        if (const BuildSystem::Block* b = app.build_.findBlock(app.selectedBlockId_)) {
            app.history_.push(std::make_unique<BlocksCommand>(
                app.build_, std::vector<BuildSystem::Block>{*b}, false, "Remove Block"));
        }
        app.build_.removeBlock(app.selectedBlockId_);
        app.selectedBlockId_ = -1;
        app.selectedBlockFace_ = -1;
    }

    if (g_input.mousePressed(Input::Right) && !overUI) {
        glm::vec3 hp, hn;
        int id = app.build_.pick(origin, dir, hp, hn);
        if (id >= 0) {
            app.selectedBlockId_ = id;
            app.selectedBlockFace_ = BuildSystem::faceFromNormal(hn);
        } else {
            app.selectedBlockId_ = -1;
            app.selectedBlockFace_ = -1;
        }
    }
    return buildDragging_ || (bmode != BuildSystem::ModeTexture && ctrl);
}
