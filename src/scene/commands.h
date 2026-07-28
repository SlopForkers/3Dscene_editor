#pragma once
#include "history.h"
#include "prop.h"
#include "build.h"
#include "detail.h"
#include "terrain.h"
#include "scene_camera.h"
#include <glm/glm.hpp>
#include <array>
#include <string>
#include <unordered_map>
#include <vector>

// ---------------------------------------------------------------------------
// Concrete undoable commands. Tools mutate the scene first, then push these
// with the before/after data already captured.
// ---------------------------------------------------------------------------

// Sparse heightfield edit (brush stroke, vertex drag, flatten). Captures only
// touched cells; one instance per stroke, so no merge() needed.
class TerrainHeightsCommand : public Command {
public:
    TerrainHeightsCommand(Terrain& t, DetailSystem& d, BuildSystem& b,
                          const char* name)
        : terrain_(&t), details_(&d), build_(&b), name_(name) {}

    // Capture one cell's height BEFORE/AFTER it changes. Before-values are
    // recorded once per cell per stroke; after-values track the latest.
    void captureBefore(int ix, int iz);
    void captureAfter(int ix, int iz);
    // Capture a whole rect (flatten, vertex drags).
    void captureRectBefore(int x0, int z0, int x1, int z1);
    void captureRectAfter(int x0, int z0, int x1, int z1);

    // False when nothing actually changed (identical before/after).
    bool hasChanges() const;

    void redo() override;
    void undo() override;
    size_t memoryBytes() const override;
    const char* name() const override { return name_; }

private:
    void apply(const std::unordered_map<int, float>& vals);

    Terrain* terrain_;
    DetailSystem* details_;
    BuildSystem* build_;
    const char* name_;
    std::unordered_map<int, float> oldH_, newH_;
    // Union rect of captured cells (empty while rx1_ < rx0_).
    int rx0_ = INT_MAX, rz0_ = INT_MAX, rx1_ = -1, rz1_ = -1;
};

// Sparse splat (texture paint) edit. One instance per stroke.
class TerrainSplatCommand : public Command {
public:
    explicit TerrainSplatCommand(Terrain& t) : terrain_(&t) {}

    void captureBefore(int ix, int iz);
    void captureAfter(int ix, int iz);
    bool hasChanges() const;

    void redo() override;
    void undo() override;
    size_t memoryBytes() const override;
    const char* name() const override { return "Paint Texture"; }

private:
    using Weights = std::array<uint8_t, 16>;   // 4 splat maps x 4 channels
    void apply(const std::unordered_map<int, Weights>& vals);

    Terrain* terrain_;
    std::unordered_map<int, Weights> oldW_, newW_;
};

// Prop placement / removal (one command per prop).
class PropCommand : public Command {
public:
    PropCommand(PropManager& pm, const Prop& p, bool addedByUser,
                const char* name)
        : props_(&pm), prop_(p), added_(addedByUser), name_(name) {}

    void redo() override {
        if (added_) props_->addPropWithId(prop_);
        else props_->removeProp(prop_.id);
    }
    void undo() override {
        if (added_) props_->removeProp(prop_.id);
        else props_->addPropWithId(prop_);
    }
    size_t memoryBytes() const override { return sizeof(*this) + prop_.displayName.size(); }
    const char* name() const override { return name_; }

private:
    PropManager* props_;
    Prop prop_;
    bool added_;
    const char* name_;
};

// Prop transform change (gizmo drag or panel sliders). Merges consecutive
// edits of the same prop so slider drags don't flood the stack.
class PropTransformCommand : public Command {
public:
    PropTransformCommand(PropManager& pm, int id,
                         const glm::vec3& oldPos, const glm::vec3& oldRot, const glm::vec3& oldScale,
                         const glm::vec3& newPos, const glm::vec3& newRot, const glm::vec3& newScale)
        : props_(&pm), id_(id), oldPos_(oldPos), oldRot_(oldRot), oldScale_(oldScale),
          newPos_(newPos), newRot_(newRot), newScale_(newScale) {}

    void redo() override;
    void undo() override;
    bool merge(const Command& next) override;
    size_t memoryBytes() const override { return sizeof(*this); }
    const char* name() const override { return "Transform Prop"; }

private:
    void apply(const glm::vec3& pos, const glm::vec3& rot, const glm::vec3& scale);

    PropManager* props_;
    int id_;
    glm::vec3 oldPos_, oldRot_, oldScale_;
    glm::vec3 newPos_, newRot_, newScale_;
};

// Block placement / erasure, single or batched (fillRect / eraseRect /
// fillWallLine). Stores full block values so ids survive undo/redo cycles.
class BlocksCommand : public Command {
public:
    BlocksCommand(BuildSystem& bs, std::vector<BuildSystem::Block> blocks,
                  bool addedByUser, const char* name)
        : build_(&bs), blocks_(std::move(blocks)), added_(addedByUser), name_(name) {}

    void redo() override {
        if (added_) { for (const auto& b : blocks_) build_->placeBlockWithId(b); }
        else        { for (const auto& b : blocks_) build_->removeBlock(b.id); }
    }
    void undo() override {
        if (added_) { for (const auto& b : blocks_) build_->removeBlock(b.id); }
        else        { for (const auto& b : blocks_) build_->placeBlockWithId(b); }
    }
    size_t memoryBytes() const override {
        return sizeof(*this) + blocks_.size() * sizeof(BuildSystem::Block);
    }
    const char* name() const override { return name_; }

private:
    BuildSystem* build_;
    std::vector<BuildSystem::Block> blocks_;
    bool added_;
    const char* name_;
};

// Per-block face texture changes (paint, clear, rect/line batch).
class BlockTextureCommand : public Command {
public:
    struct Entry {
        int blockId;
        int oldTexIdx, oldTexFace; float oldTexScale; int oldTexMode;
        int newTexIdx, newTexFace; float newTexScale; int newTexMode;
    };

    BlockTextureCommand(BuildSystem& bs, std::vector<Entry> entries)
        : build_(&bs), entries_(std::move(entries)) {}

    void redo() override;
    void undo() override;
    size_t memoryBytes() const override {
        return sizeof(*this) + entries_.size() * sizeof(Entry);
    }
    const char* name() const override { return "Texture Block"; }

private:
    void applySide(int texIdx, int texFace, float texScale, int texMode, int blockId);

    BuildSystem* build_;
    std::vector<Entry> entries_;
};

// Vegetation paint stroke: instances added and/or removed. Relies on LIFO
// stack discipline: undo pops what the stroke appended, then re-appends what
// it erased; redo reverses. Instance order has no semantic meaning.
class DetailPaintCommand : public Command {
public:
    DetailPaintCommand(DetailSystem& ds) : details_(&ds) {}

    std::vector<DetailSystem::Instance> added;
    std::vector<DetailSystem::Instance> removed;

    void redo() override;
    void undo() override;
    size_t memoryBytes() const override {
        return sizeof(*this) +
               (added.size() + removed.size()) * sizeof(DetailSystem::Instance);
    }
    const char* name() const override { return "Paint Vegetation"; }

private:
    DetailSystem* details_;
};

// Layer added (redo re-inserts; undo removes and restores the splat map).
class LayerAddCommand : public Command {
public:
    LayerAddCommand(Terrain& t, int index, Terrain::Layer layer,
                    std::vector<uint8_t> splatBefore)
        : terrain_(&t), index_(index), layer_(std::move(layer)),
          splatBefore_(std::move(splatBefore)) {}

    void redo() override;
    void undo() override;
    size_t memoryBytes() const override;
    const char* name() const override { return "Add Layer"; }

private:
    Terrain* terrain_;
    int index_;
    Terrain::Layer layer_;
    std::vector<uint8_t> splatBefore_;
};

// Layer removed (undo re-inserts and restores the splat map).
class LayerRemoveCommand : public Command {
public:
    LayerRemoveCommand(Terrain& t, int index, Terrain::Layer layer,
                       std::vector<uint8_t> splatBefore)
        : terrain_(&t), index_(index), layer_(std::move(layer)),
          splatBefore_(std::move(splatBefore)) {}

    void redo() override;
    void undo() override;
    size_t memoryBytes() const override;
    const char* name() const override { return "Remove Layer"; }

private:
    Terrain* terrain_;
    int index_;
    Terrain::Layer layer_;
    std::vector<uint8_t> splatBefore_;
};

// Layer albedo/normal texture replaced.
class LayerTextureCommand : public Command {
public:
    LayerTextureCommand(Terrain& t, int index, bool isNormal)
        : terrain_(&t), index_(index), isNormal_(isNormal) {}

    std::vector<uint8_t> oldPix, newPix;
    std::string oldPath, newPath;
    bool oldHasNormal = false, newHasNormal = false;   // normal maps only

    void redo() override;
    void undo() override;
    size_t memoryBytes() const override {
        return sizeof(*this) + oldPix.size() + newPix.size();
    }
    const char* name() const override { return "Layer Texture"; }

private:
    void apply(const std::vector<uint8_t>& pix, const std::string& path, bool hasNormal);

    Terrain* terrain_;
    int index_;
    bool isNormal_;
};

// Scene camera added / removed. Stores the full camera so ids survive
// undo/redo; also remembers whether it was the active (initial) camera.
class CameraCommand : public Command {
public:
    CameraCommand(CameraRig& rig, const SceneCamera& c, bool addedByUser,
                  const char* name)
        : rig_(&rig), cam_(c), added_(addedByUser),
          wasActive_(rig.activeId() == c.id), name_(name) {}

    void redo() override {
        if (added_) restore();
        else rig_->removeCamera(cam_.id);
    }
    void undo() override {
        if (added_) rig_->removeCamera(cam_.id);
        else restore();
    }
    size_t memoryBytes() const override {
        return sizeof(*this) + cam_.name.size() + cam_.tag.size();
    }
    const char* name() const override { return name_; }

private:
    void restore() {
        rig_->addCameraWithId(cam_);
        if (wasActive_) rig_->setActive(cam_.id);
    }

    CameraRig* rig_;
    SceneCamera cam_;
    bool added_;
    bool wasActive_;
    const char* name_;
};

// Scene camera metadata/transform edit (panel widgets). Merges consecutive
// edits of the same camera so slider drags don't flood the stack.
class CameraEditCommand : public Command {
public:
    CameraEditCommand(CameraRig& rig, int id,
                      const SceneCamera& before, const SceneCamera& after)
        : rig_(&rig), id_(id), before_(before), after_(after) {}

    void redo() override { apply(after_); }
    void undo() override { apply(before_); }
    bool merge(const Command& next) override;
    size_t memoryBytes() const override {
        return sizeof(*this) + before_.name.size() + before_.tag.size() +
               after_.name.size() + after_.tag.size();
    }
    const char* name() const override { return "Edit Camera"; }

private:
    void apply(const SceneCamera& v) {
        SceneCamera* c = rig_->findCamera(id_);
        if (!c) return;
        int id = c->id;
        *c = v;
        c->id = id;   // id is the command's key — never let it drift
    }

    CameraRig* rig_;
    int id_;
    SceneCamera before_, after_;
};

// Splat reset (clear to layer 0).
class SplatResetCommand : public Command {
public:
    SplatResetCommand(Terrain& t, std::vector<uint8_t> before)
        : terrain_(&t), before_(std::move(before)) {}

    void redo() override;
    void undo() override;
    size_t memoryBytes() const override { return sizeof(*this) + before_.size(); }
    const char* name() const override { return "Reset Splat"; }

private:
    Terrain* terrain_;
    std::vector<uint8_t> before_;
};
