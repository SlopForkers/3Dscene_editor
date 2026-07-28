#include "commands.h"
#include "terrain.h"
#include <algorithm>
#include <climits>

// ============================================================================
// TerrainHeightsCommand
// ============================================================================

void TerrainHeightsCommand::captureBefore(int ix, int iz) {
    int key = iz * terrain_->gridX() + ix;
    if (oldH_.find(key) == oldH_.end())
        oldH_[key] = terrain_->getHeight(ix, iz);
    rx0_ = std::min(rx0_, ix);
    rz0_ = std::min(rz0_, iz);
    rx1_ = std::max(rx1_, ix);
    rz1_ = std::max(rz1_, iz);
}

void TerrainHeightsCommand::captureAfter(int ix, int iz) {
    int key = iz * terrain_->gridX() + ix;
    if (oldH_.find(key) != oldH_.end())
        newH_[key] = terrain_->getHeight(ix, iz);
}

void TerrainHeightsCommand::captureRectBefore(int x0, int z0, int x1, int z1) {
    for (int iz = z0; iz <= z1; ++iz)
        for (int ix = x0; ix <= x1; ++ix)
            captureBefore(ix, iz);
}

void TerrainHeightsCommand::captureRectAfter(int x0, int z0, int x1, int z1) {
    for (int iz = z0; iz <= z1; ++iz)
        for (int ix = x0; ix <= x1; ++ix)
            captureAfter(ix, iz);
}

bool TerrainHeightsCommand::hasChanges() const {
    if (rx1_ < rx0_ || rz1_ < rz0_) return false;
    return oldH_ != newH_;
}

void TerrainHeightsCommand::apply(const std::unordered_map<int, float>& vals) {
    int gx = terrain_->gridX();
    for (const auto& [key, h] : vals)
        terrain_->setHeightRaw(key % gx, key / gx, h);
    terrain_->heightsChanged(rx0_, rz0_, rx1_, rz1_);

    // Details and foundation blocks ride the heightfield — reproject the
    // union area so they follow the restored heights.
    float wx0 = terrain_->worldXAt(rx0_), wx1 = terrain_->worldXAt(rx1_);
    float wz0 = terrain_->worldZAt(rz0_), wz1 = terrain_->worldZAt(rz1_);
    glm::vec3 center((wx0 + wx1) * 0.5f, 0.0f, (wz0 + wz1) * 0.5f);
    float radius = std::max(wx1 - wx0, wz1 - wz0) * 0.5f +
                   terrain_->worldSize() / float(gx - 1);
    details_->reproject(*terrain_, center, radius);
    build_->reproject(*terrain_, center, radius);
}

void TerrainHeightsCommand::undo() { apply(oldH_); }
void TerrainHeightsCommand::redo() { apply(newH_); }

size_t TerrainHeightsCommand::memoryBytes() const {
    // ~48 bytes per unordered_map node is the usual libstdc++ overhead.
    return sizeof(*this) + (oldH_.size() + newH_.size()) * (sizeof(float) + sizeof(int) + 48);
}

// ============================================================================
// TerrainSplatCommand
// ============================================================================

void TerrainSplatCommand::captureBefore(int ix, int iz) {
    int key = iz * terrain_->gridX() + ix;
    if (oldW_.find(key) != oldW_.end()) return;
    Weights w;
    const auto& splat = terrain_->splatData();
    size_t texels = (size_t)terrain_->gridX() * terrain_->gridZ();
    for (int m = 0; m < 4; ++m)
        for (int c = 0; c < 4; ++c)
            w[m * 4 + c] = splat[(size_t)m * texels * 4 + (size_t)key * 4 + c];
    oldW_[key] = w;
}

void TerrainSplatCommand::captureAfter(int ix, int iz) {
    int key = iz * terrain_->gridX() + ix;
    if (oldW_.find(key) == oldW_.end()) return;
    Weights w;
    const auto& splat = terrain_->splatData();
    size_t texels = (size_t)terrain_->gridX() * terrain_->gridZ();
    for (int m = 0; m < 4; ++m)
        for (int c = 0; c < 4; ++c)
            w[m * 4 + c] = splat[(size_t)m * texels * 4 + (size_t)key * 4 + c];
    newW_[key] = w;
}

bool TerrainSplatCommand::hasChanges() const {
    if (oldW_.empty()) return false;
    return oldW_ != newW_;
}

void TerrainSplatCommand::apply(const std::unordered_map<int, Weights>& vals) {
    // Patch a full copy of the splat and upload — splat_ is planar with no
    // partial write API, and a 256^2x16 patch is ~1 MB, cheap for an edit.
    std::vector<uint8_t> splat = terrain_->splatData();
    size_t texels = (size_t)terrain_->gridX() * terrain_->gridZ();
    for (const auto& [key, w] : vals) {
        for (int m = 0; m < 4; ++m)
            for (int c = 0; c < 4; ++c)
                splat[(size_t)m * texels * 4 + (size_t)key * 4 + c] = w[m * 4 + c];
    }
    terrain_->setSplat(splat);
}

void TerrainSplatCommand::undo() { apply(oldW_); }
void TerrainSplatCommand::redo() { apply(newW_); }

size_t TerrainSplatCommand::memoryBytes() const {
    return sizeof(*this) + (oldW_.size() + newW_.size()) * (sizeof(Weights) + sizeof(int) + 48);
}

// ============================================================================
// PropTransformCommand
// ============================================================================

void PropTransformCommand::apply(const glm::vec3& pos, const glm::vec3& rot,
                                 const glm::vec3& scale) {
    Prop* p = props_->findProp(id_);
    if (!p) return;
    p->position = pos;
    p->rotationEuler = rot;
    p->scale = scale;
}

void PropTransformCommand::redo() { apply(newPos_, newRot_, newScale_); }
void PropTransformCommand::undo() { apply(oldPos_, oldRot_, oldScale_); }

bool PropTransformCommand::merge(const Command& next) {
    auto* n = dynamic_cast<const PropTransformCommand*>(&next);
    if (!n || n->id_ != id_) return false;
    // Keep our old*, take the newer new*.
    newPos_ = n->newPos_;
    newRot_ = n->newRot_;
    newScale_ = n->newScale_;
    return true;
}

// ============================================================================
// CameraEditCommand
// ============================================================================

bool CameraEditCommand::merge(const Command& next) {
    auto* n = dynamic_cast<const CameraEditCommand*>(&next);
    if (!n || n->id_ != id_) return false;
    // Keep our before_, take the newer after_.
    after_ = n->after_;
    return true;
}

// ============================================================================
// SpawnEditCommand / SpawnGraphCommand
// ============================================================================

bool SpawnEditCommand::merge(const Command& next) {
    auto* n = dynamic_cast<const SpawnEditCommand*>(&next);
    if (!n || n->id_ != id_) return false;
    after_ = n->after_;
    return true;
}

bool SpawnGraphCommand::merge(const Command& next) {
    auto* n = dynamic_cast<const SpawnGraphCommand*>(&next);
    if (!n || n->id_ != id_) return false;
    // Only param-widget edits coalesce (both sides marked mergeable);
    // structural ops must stay one-undo-per-op.
    if (!mergeable_ || !n->mergeable_) return false;
    after_ = n->after_;
    return true;
}

// ============================================================================
// BlockTextureCommand
// ============================================================================

void BlockTextureCommand::applySide(int texIdx, int texFace, float texScale,
                                    int texMode, int blockId) {
    if (!build_->findBlockMutable(blockId)) return;
    if (texIdx < 0 || texFace < 0) {
        build_->clearBlockFaceTexture(blockId);
    } else {
        build_->setBlockFaceTexture(blockId, texIdx, texFace);
        build_->setBlockTexScale(blockId, texScale);
        build_->setBlockTexMode(blockId, texMode);
    }
}

void BlockTextureCommand::redo() {
    for (const auto& e : entries_)
        applySide(e.newTexIdx, e.newTexFace, e.newTexScale, e.newTexMode, e.blockId);
}

void BlockTextureCommand::undo() {
    for (const auto& e : entries_)
        applySide(e.oldTexIdx, e.oldTexFace, e.oldTexScale, e.oldTexMode, e.blockId);
}

// ============================================================================
// DetailPaintCommand
// ============================================================================

void DetailPaintCommand::undo() {
    // Pop what the stroke appended, re-append what it erased.
    size_t n = details_->instances().size();
    if (added.size() <= n)
        details_->truncateInstances(n - added.size());
    for (const auto& inst : removed)
        details_->addInstance(inst);
}

void DetailPaintCommand::redo() {
    size_t n = details_->instances().size();
    if (removed.size() <= n)
        details_->truncateInstances(n - removed.size());
    for (const auto& inst : added)
        details_->addInstance(inst);
}

// ============================================================================
// Layer commands
// ============================================================================

void LayerAddCommand::redo() {
    terrain_->insertLayer(index_, layer_);
}

void LayerAddCommand::undo() {
    terrain_->removeLayer(index_);
    // removeLayer remaps splat channels — restore the pre-add splat.
    terrain_->setSplat(splatBefore_);
}

size_t LayerAddCommand::memoryBytes() const {
    return sizeof(*this) + splatBefore_.size() +
           layer_.albedoPix.size() + layer_.normalPix.size();
}

void LayerRemoveCommand::redo() {
    terrain_->removeLayer(index_);
}

void LayerRemoveCommand::undo() {
    terrain_->insertLayer(index_, layer_);
    // insertLayer does not touch the splat — restore the pre-remove map.
    terrain_->setSplat(splatBefore_);
}

size_t LayerRemoveCommand::memoryBytes() const {
    return sizeof(*this) + splatBefore_.size() +
           layer_.albedoPix.size() + layer_.normalPix.size();
}

void LayerTextureCommand::apply(const std::vector<uint8_t>& pix,
                                const std::string& path, bool hasNormal) {
    if (isNormal_)
        terrain_->setLayerNormalPixels(index_, pix, path, hasNormal);
    else
        terrain_->setLayerAlbedoPixels(index_, pix, path);
}

void LayerTextureCommand::redo() { apply(newPix, newPath, newHasNormal); }
void LayerTextureCommand::undo() { apply(oldPix, oldPath, oldHasNormal); }

// ============================================================================
// SplatResetCommand
// ============================================================================

void SplatResetCommand::redo() { terrain_->resetSplat(); }
void SplatResetCommand::undo() { terrain_->setSplat(before_); }
