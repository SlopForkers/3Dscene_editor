#pragma once
#include <glm/glm.hpp>

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
};

struct PropTool final : ITool {
    bool handleInput(App& app, float dt, const ImGuiIO& io, bool overUI, bool typing) override;
    void cancelDrag() override;
    void drawPanelContent(App& app) override;
};

struct VertexTool final : ITool {
    bool handleInput(App& app, float dt, const ImGuiIO& io, bool overUI, bool typing) override;
    void cancelDrag() override;
    void drawPanelContent(App& app) override;
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
