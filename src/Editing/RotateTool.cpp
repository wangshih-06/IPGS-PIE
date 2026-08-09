#include "Editing/RotateTool.h"

#include <cmath>
#include <unordered_set>

namespace {
constexpr float kEpsilon = 1.0e-6f;

void setError(QString* error, const QString& message) {
    if (error) *error = message;
}

void rotateSubtree(PlantNode* node,
                   const Vec3& pivot,
                   const Quat& rotation,
                   std::unordered_set<int>* nodeIds) {
    if (!node || !nodeIds) return;
    nodeIds->insert(node->id);
    node->position = pivot + rotation * (node->position - pivot);
    node->direction = (rotation * node->direction).normalized();
    for (const auto& child : node->children) rotateSubtree(child.get(), pivot, rotation, nodeIds);
}
} // namespace

bool RotateTool::apply(PlantModel& model,
                       int nodeId,
                       const Vec3& pivot,
                       const Vec3& axis,
                       float angleRadians,
                       QString* error) {
    PlantNode* node = model.findNode(nodeId);
    if (!node) {
        setError(error, QStringLiteral("Cannot rotate an unknown node."));
        return false;
    }
    if (!pivot.allFinite() || !axis.allFinite() || axis.squaredNorm() < kEpsilon ||
        !std::isfinite(angleRadians)) {
        setError(error, QStringLiteral("Rotation pivot, axis and angle must be finite."));
        return false;
    }

    const Quat rotation(Eigen::AngleAxisf(angleRadians, axis.normalized()));
    std::unordered_set<int> affectedNodes;
    rotateSubtree(node, pivot, rotation, &affectedNodes);
    for (Leaf& leaf : model.mutableLeaves()) {
        if (affectedNodes.find(leaf.parentNodeId) == affectedNodes.end()) continue;
        leaf.position = pivot + rotation * (leaf.position - pivot);
        leaf.direction = (rotation * leaf.direction).normalized();
    }

    model.syncBranchStates();
    return model.validate(error);
}
