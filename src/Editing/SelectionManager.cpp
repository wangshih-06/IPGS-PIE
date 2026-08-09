#include "Editing/SelectionManager.h"

#include <cmath>

namespace {
constexpr float kPositionTolerance = 1.0e-5f;
}

void SelectionManager::setPickMode(EditPickMode mode) {
    pickMode_ = mode;
    if (hovered_.hit) hovered_.wholePlant = mode == EditPickMode::WholePlant;
    if (selected_.hit) selected_.wholePlant = mode == EditPickMode::WholePlant;
}

EditPickMode SelectionManager::pickMode() const {
    return pickMode_;
}

bool SelectionManager::updateHover(const EditPickResult& pick) {
    if (equivalent(hovered_, pick)) return false;
    hovered_ = pick;
    return true;
}

bool SelectionManager::select(const EditPickResult& pick) {
    if (equivalent(selected_, pick)) return false;
    selected_ = pick;
    return true;
}

bool SelectionManager::selectHovered() {
    return select(hovered_);
}

bool SelectionManager::clearHover() {
    return updateHover({});
}

bool SelectionManager::clearSelection() {
    return select({});
}

const EditPickResult& SelectionManager::hovered() const {
    return hovered_;
}

const EditPickResult& SelectionManager::selected() const {
    return selected_;
}

bool SelectionManager::hasHover() const {
    return hovered_.hit;
}

bool SelectionManager::hasSelection() const {
    return selected_.hit;
}

Vec3 SelectionManager::hoverColor() {
    return Vec3(1.0f, 0.82f, 0.05f);
}

Vec3 SelectionManager::nodeSelectionColor() {
    return Vec3(0.05f, 0.88f, 0.88f);
}

Vec3 SelectionManager::wholePlantSelectionColor() {
    return Vec3(1.0f, 0.45f, 0.08f);
}

bool SelectionManager::equivalent(const EditPickResult& lhs, const EditPickResult& rhs) {
    return lhs.hit == rhs.hit &&
           lhs.wholePlant == rhs.wholePlant &&
           lhs.plantId == rhs.plantId &&
           lhs.nodeId == rhs.nodeId &&
           lhs.leafId == rhs.leafId &&
           lhs.object == rhs.object &&
           std::abs(lhs.distance - rhs.distance) < kPositionTolerance &&
           lhs.hitPoint.isApprox(rhs.hitPoint, kPositionTolerance);
}
