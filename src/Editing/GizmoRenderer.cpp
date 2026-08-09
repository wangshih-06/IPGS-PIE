#include "Editing/GizmoRenderer.h"

#include <algorithm>

namespace {
void extendBounds(const PlantNode* node, Vec3* minimum, Vec3* maximum) {
    if (!node || !minimum || !maximum) return;
    *minimum = minimum->cwiseMin(node->position);
    *maximum = maximum->cwiseMax(node->position);
    for (const auto& child : node->children) extendBounds(child.get(), minimum, maximum);
}

void addAxes(GizmoRenderData* data, const Vec3& origin, float length) {
    if (!data) return;
    data->primitives.push_back({GizmoPrimitiveType::Axis, Vec3(0.95f, 0.16f, 0.14f), origin, origin + Vec3::UnitX() * length});
    data->primitives.push_back({GizmoPrimitiveType::Axis, Vec3(0.20f, 0.90f, 0.28f), origin, origin, origin, origin, length});
    data->primitives.back().end = origin + Vec3::UnitY() * length;
    data->primitives.push_back({GizmoPrimitiveType::Axis, Vec3(0.20f, 0.45f, 1.0f), origin, origin + Vec3::UnitZ() * length});
}

Vec3 colorFor(const EditPickResult& pick, bool hovered) {
    if (hovered) return SelectionManager::hoverColor();
    return pick.wholePlant ? SelectionManager::wholePlantSelectionColor()
                           : SelectionManager::nodeSelectionColor();
}

void appendPickGizmo(GizmoRenderData* data,
                     const PlantModel& model,
                     const EditPickResult& pick,
                     bool hovered) {
    if (!data || !pick.hit) return;
    const PlantNode* node = model.findNode(pick.nodeId);
    if (!node) return;

    const Vec3 color = colorFor(pick, hovered);
    const float radius = std::max(0.06f, node->radius * 1.7f);
    data->primitives.push_back({GizmoPrimitiveType::Sphere, color, node->position, {}, {}, {}, radius});

    if (!hovered) addAxes(data, node->position, radius * 3.0f);
    if (!pick.wholePlant) return;

    Vec3 minimum = node->position;
    Vec3 maximum = node->position;
    extendBounds(model.rootNode(), &minimum, &maximum);
    for (const Leaf& leaf : model.leaves()) {
        minimum = minimum.cwiseMin(leaf.position);
        maximum = maximum.cwiseMax(leaf.position);
    }
    const Vec3 padding = Vec3::Constant(radius);
    data->primitives.push_back({GizmoPrimitiveType::BoundingBox, color, {}, {}, minimum - padding, maximum + padding});
}
} // namespace

GizmoRenderData GizmoRenderer::build(const PlantModel& model, const SelectionManager& selection) {
    GizmoRenderData data;
    appendPickGizmo(&data, model, selection.hovered(), true);
    appendPickGizmo(&data, model, selection.selected(), false);
    return data;
}
