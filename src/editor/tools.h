#pragma once
#include <glm/glm.hpp>
#include <memory>
#include "commands.h"

struct ImGuiIO;
class App;

struct ITool {
    virtual ~ITool() = default;
    virtual bool handleInput(App& app, float dt, const ImGuiIO& io, bool overUI, bool typing) = 0;
    virtual void cancelDrag() {}
    virtual void drawPanelContent(App& app) { (void)app; }
};

struct TerrainTool final : ITool {
    bool handleInput(App& app, float dt, const ImGuiIO& io, bool overUI, bool typing) override;
    void cancelDrag() override;
    void drawPanelContent(App& app) override;

    bool painting_ = false;

private:
    void beginStroke(App& app);
    void endStroke(App& app);

    // In-progress stroke capture (one command per drag, pushed on release).
    std::unique_ptr<TerrainHeightsCommand> heightsCmd_;
    std::unique_ptr<TerrainSplatCommand>   splatCmd_;
    std::unique_ptr<DetailPaintCommand>    detailCmd_;
};

struct PropTool final : ITool {
    bool handleInput(App& app, float dt, const ImGuiIO& io, bool overUI, bool typing) override;
    void cancelDrag() override;
    void drawPanelContent(App& app) override;

private:
    // Gizmo drag tracking for undo (transform captured at drag start).
    bool gizmoWasDragging_ = false;
    int  dragPropId_ = -1;
    glm::vec3 dragStartPos_   = glm::vec3(0.0f);
    glm::vec3 dragStartRot_   = glm::vec3(0.0f);
    glm::vec3 dragStartScale_ = glm::vec3(1.0f);
};

struct VertexTool final : ITool {
    bool handleInput(App& app, float dt, const ImGuiIO& io, bool overUI, bool typing) override;
    void cancelDrag() override;
    void drawPanelContent(App& app) override;

private:
    bool wasDragging_ = false;
    std::unique_ptr<TerrainHeightsCommand> cmd_;
};

struct BuildTool final : ITool {
    bool handleInput(App& app, float dt, const ImGuiIO& io, bool overUI, bool typing) override;
    void cancelDrag() override;
    void drawPanelContent(App& app) override;

    bool dragging() const { return buildDragging_; }

    bool  buildDragging_ = false;
    bool  buildDragErase_ = false;
    glm::vec2 buildDragStart_ = glm::vec2(0.0f);
    bool  buildDragOnBlocks_ = false;
    float buildDragBaseY_ = 0.0f;
    float buildDragFixed_ = 0.0f;
    bool  buildDragAlongX_ = false;
    int   buildTexFace_ = -1;
    bool  buildTexLine_ = false;
    int   buildTexPressBlock_ = -1;
    int   buildTexPressFace_ = -1;
    double buildTexPressMX_ = 0.0;
    double buildTexPressMY_ = 0.0;
};
