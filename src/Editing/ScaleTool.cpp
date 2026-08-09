#include "Editing/ScaleTool.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace {
constexpr float kEpsilon = 1.0e-6f;

void setError(QString* error, const QString& message) {
    if (error) *error = message;
}

float geometricMeanScale(const Vec3& scale) {
    return std::cbrt(scale.x() * scale.y() * scale.z());
}

void transformSubtree(PlantNode* node,
                      const Vec3& pivot,
                      const ScaleParams& params,
                      std::unordered_set<int>* nodeIds) {
    if (!node || !nodeIds) return;
    nodeIds->insert(node->id);

    const Vec3 transformedDirection = params.scale.cwiseProduct(node->direction);
    const float directionStretch = transformedDirection.norm();
    if (node->parent) node->position = pivot + params.scale.cwiseProduct(node->position - pivot);
    if (directionStretch > kEpsilon) node->direction = transformedDirection / directionStretch;
    node->length = std::max(params.minimumLength, node->length * std::max(directionStretch, kEpsilon));
    node->radius = std::max(params.minimumRadius, node->radius * geometricMeanScale(params.scale));

    for (const auto& child : node->children) {
        transformSubtree(child.get(), pivot, params, nodeIds);
    }
}
} // namespace

bool ScaleTool::apply(PlantModel& model,
                      int nodeId,
                      const ScaleParams& params,
                      QString* error) {
    PlantNode* node = model.findNode(nodeId);
    if (!node) {
        setError(error, QStringLiteral("Cannot scale an unknown node."));
        return false;
    }
    if (!params.scale.allFinite() || params.scale.x() <= 0.0f ||
        params.scale.y() <= 0.0f || params.scale.z() <= 0.0f ||
        !std::isfinite(params.minimumRadius) || !std::isfinite(params.minimumLength) ||
        params.minimumRadius <= 0.0f || params.minimumLength <= 0.0f) {
        setError(error, QStringLiteral("Scale factors and minimum dimensions must be finite and positive."));
        return false;
    }

    const Vec3 pivot = node->position;
    std::unordered_set<int> scaledNodeIds;
    transformSubtree(node, pivot, params, &scaledNodeIds);

    if (params.scaleLeaves) {
        const float leafScale = geometricMeanScale(params.scale);
        for (Leaf& leaf : model.mutableLeaves()) {
            if (scaledNodeIds.find(leaf.parentNodeId) == scaledNodeIds.end()) continue;
            leaf.position = pivot + params.scale.cwiseProduct(leaf.position - pivot);
            const Vec3 direction = params.scale.cwiseProduct(leaf.direction);
            if (direction.squaredNorm() > kEpsilon) leaf.direction = direction.normalized();
            leaf.size = (leaf.size * leafScale).cwiseMax(Vec2::Constant(params.minimumRadius));
        }
    }

    model.syncBranchStates();
    return model.validate(error);
}
