#include "Editing/BendTool.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace {
constexpr float kEpsilon = 1.0e-6f;

void setError(QString* error, const QString& message) {
    if (error) *error = message;
}

bool validParams(const BendParams& params) {
    return std::isfinite(params.angleRadians) && params.bendAxis.allFinite() &&
           std::isfinite(params.falloff) && std::isfinite(params.stiffness) &&
           std::isfinite(params.maximumAngleRadians) && params.bendAxis.squaredNorm() > kEpsilon &&
           params.falloff >= 0.0f && params.stiffness >= 0.0f && params.maximumAngleRadians > 0.0f;
}

float effectiveAngle(const BendParams& params) {
    const float maximum = std::abs(params.maximumAngleRadians);
    return std::clamp(params.angleRadians, -maximum, maximum) * std::clamp(params.stiffness, 0.0f, 1.0f);
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

Vec3 BendCurve::sample(float u) const {
    const float t = std::clamp(u, 0.0f, 1.0f);
    const float inverse = 1.0f - t;
    return inverse * inverse * inverse * start +
           3.0f * inverse * inverse * t * control1 +
           3.0f * inverse * t * t * control2 +
           t * t * t * end;
}

Vec3 BendCurve::tangent(float u) const {
    const float t = std::clamp(u, 0.0f, 1.0f);
    const float inverse = 1.0f - t;
    const Vec3 derivative = 3.0f * inverse * inverse * (control1 - start) +
                            6.0f * inverse * t * (control2 - control1) +
                            3.0f * t * t * (end - control2);
    return derivative.squaredNorm() > kEpsilon ? derivative.normalized() : Vec3::UnitY();
}

BendCurve BendTool::previewCurve(const PlantModel& model,
                                 int nodeId,
                                 const BendParams& params,
                                 QString* error) {
    const PlantNode* node = model.findNode(nodeId);
    if (!node || !node->parent) {
        setError(error, QStringLiteral("Bending requires a selected non-root node."));
        return {};
    }
    if (!validParams(params)) {
        setError(error, QStringLiteral("Bend parameters are invalid."));
        return {};
    }

    const Vec3 start = node->parent->position;
    const Vec3 originalOffset = node->position - start;
    const Vec3 axis = params.bendAxis.normalized();
    const Quat rotation(Eigen::AngleAxisf(effectiveAngle(params), axis));
    const Vec3 end = start + rotation * originalOffset;
    const float bendStrength = std::sin(std::abs(effectiveAngle(params))) *
                               std::clamp(params.falloff, 0.0f, 2.0f) * originalOffset.norm() * 0.35f;
    Vec3 lateral = axis.cross(originalOffset);
    if (lateral.squaredNorm() < kEpsilon) lateral = axis.unitOrthogonal();
    lateral.normalize();

    BendCurve curve;
    curve.start = start;
    curve.end = end;
    curve.control1 = start + originalOffset / 3.0f + lateral * bendStrength;
    curve.control2 = end - (rotation * originalOffset) / 3.0f + lateral * bendStrength;
    return curve;
}

bool BendTool::apply(PlantModel& model,
                     int nodeId,
                     const BendParams& params,
                     QString* error) {
    PlantNode* node = model.findNode(nodeId);
    if (!node || !node->parent) {
        setError(error, QStringLiteral("Bending requires a selected non-root node."));
        return false;
    }
    if (!validParams(params)) {
        setError(error, QStringLiteral("Bend parameters are invalid."));
        return false;
    }

    const Vec3 pivot = node->parent->position;
    const Quat rotation(Eigen::AngleAxisf(effectiveAngle(params), params.bendAxis.normalized()));
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
