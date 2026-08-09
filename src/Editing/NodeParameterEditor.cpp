#include "Editing/NodeParameterEditor.h"

#include <cmath>
#include <unordered_set>

namespace {
constexpr float kEpsilon = 1.0e-6f;
constexpr float kPi = 3.14159265359f;

void setError(QString* error, const QString& message) {
    if (error) *error = message;
}

void collectSubtree(PlantNode* node, std::unordered_set<int>* nodeIds) {
    if (!node || !nodeIds) return;
    nodeIds->insert(node->id);
    for (const auto& child : node->children) collectSubtree(child.get(), nodeIds);
}

void transformSubtree(PlantNode* node,
                      const Vec3& pivot,
                      const Quat& rotation) {
    if (!node) return;
    node->position = pivot + rotation * (node->position - pivot);
    node->direction = (rotation * node->direction).normalized();
    for (const auto& child : node->children) transformSubtree(child.get(), pivot, rotation);
}

void translateSubtree(PlantNode* node, const Vec3& delta) {
    if (!node) return;
    node->position += delta;
    for (const auto& child : node->children) translateSubtree(child.get(), delta);
}

void transformLeaves(std::vector<Leaf>& leaves,
                     const std::unordered_set<int>& nodeIds,
                     const Vec3& pivot,
                     const Quat& rotation) {
    for (Leaf& leaf : leaves) {
        if (nodeIds.find(leaf.parentNodeId) == nodeIds.end()) continue;
        leaf.position = pivot + rotation * (leaf.position - pivot);
        leaf.direction = (rotation * leaf.direction).normalized();
    }
}

void translateLeaves(std::vector<Leaf>& leaves,
                     const std::unordered_set<int>& nodeIds,
                     const Vec3& delta) {
    for (Leaf& leaf : leaves) {
        if (nodeIds.find(leaf.parentNodeId) != nodeIds.end()) leaf.position += delta;
    }
}

bool validateUpdate(const NodeParameterUpdate& update, QString* error) {
    if (update.angleDegrees && (!std::isfinite(*update.angleDegrees) ||
                                *update.angleDegrees < -180.0f || *update.angleDegrees > 180.0f)) {
        setError(error, QStringLiteral("Node angle must be within [-180, 180] degrees."));
        return false;
    }
    if (update.length && (!std::isfinite(*update.length) || *update.length <= 0.0f)) {
        setError(error, QStringLiteral("Branch length must be finite and positive."));
        return false;
    }
    if (update.radius && (!std::isfinite(*update.radius) || *update.radius <= 0.0f)) {
        setError(error, QStringLiteral("Branch radius must be finite and positive."));
        return false;
    }
    if (update.leafDensity && (!std::isfinite(*update.leafDensity) ||
                                *update.leafDensity < 0.0f || *update.leafDensity > 1.0f)) {
        setError(error, QStringLiteral("Leaf density must be within [0, 1]."));
        return false;
    }
    if (update.age && (!std::isfinite(*update.age) || *update.age < 0.0f)) {
        setError(error, QStringLiteral("Node age must be finite and non-negative."));
        return false;
    }
    if (update.growthDepth && *update.growthDepth < 0) {
        setError(error, QStringLiteral("Growth depth must be non-negative."));
        return false;
    }
    return true;
}
} // namespace

bool NodeParameterEditor::apply(PlantModel& model,
                                int nodeId,
                                const NodeParameterUpdate& update,
                                QString* error) {
    PlantNode* node = model.findNode(nodeId);
    if (!node) {
        setError(error, QStringLiteral("Cannot edit an unknown node."));
        return false;
    }
    if (!validateUpdate(update, error)) return false;

    std::unordered_set<int> subtreeNodeIds;
    collectSubtree(node, &subtreeNodeIds);
    std::vector<Leaf>& leaves = model.mutableLeaves();

    if (update.angleDegrees) {
        const float currentAngle = std::atan2(node->direction.z(), node->direction.x()) * 180.0f / kPi;
        const float deltaRadians = (*update.angleDegrees - currentAngle) * kPi / 180.0f;
        const Vec3 pivot = node->parent ? node->parent->position : node->position;
        const Quat rotation(Eigen::AngleAxisf(deltaRadians, Vec3::UnitY()));
        transformSubtree(node, pivot, rotation);
        transformLeaves(leaves, subtreeNodeIds, pivot, rotation);
    }

    if (update.length) {
        node->length = *update.length;
        if (node->parent) {
            const Vec3 newPosition = node->parent->position + node->direction * node->length;
            const Vec3 delta = newPosition - node->position;
            translateSubtree(node, delta);
            translateLeaves(leaves, subtreeNodeIds, delta);
        }
    }
    if (update.radius) node->radius = *update.radius;
    if (update.age) node->age = *update.age;
    if (update.growthDepth) node->generation = *update.growthDepth;
    if (update.leafDensity) {
        for (Leaf& leaf : leaves) {
            if (leaf.parentNodeId != node->id) continue;
            const float previous = std::max(leaf.growthProgress, kEpsilon);
            leaf.size *= *update.leafDensity / previous;
            leaf.growthProgress = *update.leafDensity;
            leaf.active = *update.leafDensity > 0.0f;
        }
    }

    model.syncBranchStates();
    return model.validate(error);
}
