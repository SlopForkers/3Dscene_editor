#include "tools.h"
#include "app.h"
#include "input.h"
#include "imgui.h"
#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>
#include <algorithm>

// ============================================================================
// TerrainTool — brush sculpt + vegetation painting
// ============================================================================

void TerrainTool::cancelDrag() { painting_ = false; }

void TerrainTool::drawPanelContent(App& app) {
    app.drawBrushContent();
}

bool TerrainTool::handleInput(App& app, float dt, const ImGuiIO& io, bool overUI, bool /*typing*/) {
    if (io.WantCaptureMouse) return false;

    if (g_input.mousePressed(Input::Left)) {
        painting_ = !overUI;
    }
    if (g_input.mouseReleased(Input::Left)) {
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
        app.details_.paint(app.terrain_, hit, app.brush_.radius, density, erase);
    } else {
        float amount = app.continuousStroke_ ? app.brush_.strength * dt * 60.0f
                                             : app.brush_.strength;
        Terrain::BrushParams step = app.brush_;
        step.strength = amount;
        bool changed = app.terrain_.applyBrush(step, hit);
        if (changed) {
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
    app.drawPropsContent();
}

bool PropTool::handleInput(App& app, float dt, const ImGuiIO& io, bool overUI, bool /*typing*/) {
    (void)dt;
    Prop* sel = app.props_.selected();

    bool gizmoConsumed = false;
    if (sel) {
        Gizmo::Transform cur{ sel->position, sel->rotationEuler, sel->scale };
        Gizmo::Transform next;
        if (app.gizmo_.handleInput(app.camera_, sel->position, cur, next, io)) {
            gizmoConsumed = true;
            if (app.gizmo_.dragging()) {
                sel->position       = next.position;
                sel->rotationEuler  = next.rotationEuler;
                sel->scale          = next.scale;
            }
        }
    }

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
    // VertexEditor manages its own drag state.
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
    if (app.vertexEditor_.dragging() || wasDragging) {
        glm::vec3 c = app.vertexEditor_.selectionCenter();
        app.details_.reproject(app.terrain_, c, app.brush_.radius * 1.5f);
        app.build_.reproject(app.terrain_, c, app.brush_.radius * 1.5f);
    }
    return app.vertexEditor_.dragging();
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
                    app.build_.clearBlockFaceTexture(id);
                    if (app.selectedBlockId_ == id) app.selectedBlockFace_ = -1;
                } else {
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
                } else if (buildDragErase_) {
                    int n = app.build_.eraseRect(buildDragStart_.x, buildDragStart_.y, gx, gz);
                    if (n > 0) { app.selectedBlockId_ = -1; app.selectedBlockFace_ = -1; }
                } else if (buildDragOnBlocks_) {
                    float startC = buildDragAlongX_ ? buildDragStart_.x : buildDragStart_.y;
                    float curC = buildDragAlongX_ ? gx : gz;
                    app.build_.fillWallLine(startC, curC, buildDragFixed_,
                                             buildDragBaseY_, buildDragAlongX_);
                } else {
                    app.build_.fillRect(app.terrain_, buildDragStart_.x, buildDragStart_.y,
                                        gx, gz, BuildSystem::Foundation);
                }
            }
        }
        buildDragging_ = false;
        buildDragOnBlocks_ = false;
        buildTexFace_ = -1;
    }

    if (!typing && g_input.keyPressed(GLFW_KEY_DELETE) && app.selectedBlockId_ >= 0) {
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
