#include <cmath>
#include <cstdlib>
#include <iostream>

#include "Editing/GizmoRenderer.h"
#include "Editing/RayPicker.h"

namespace {
constexpr float kTolerance = 1.0e-4f;

void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "PlantEditDemo failure: " << message << '\n';
    std::exit(1);
}

bool close(float lhs, float rhs, float tolerance = kTolerance) {
    return std::abs(lhs - rhs) <= tolerance;
}
} // namespace

int main() {
    PlantModel model;
    PlantNode* root = model.createRootNode(Vec3(0.0f, 0.0f, 0.0f), Vec3::UnitY(), 0.15f);
    PlantNode* node1 = model.addNode(root->id, Vec3(0.0f, 1.0f, 0.0f), Vec3::UnitY(), 0.10f, 1.0f);
    PlantNode* node2 = model.addNode(root->id, Vec3(0.7f, 0.8f, 0.0f), Vec3::UnitX(), 0.08f, 1.1f);
    PlantNode* node3 = model.addNode(node1->id, Vec3(0.0f, 2.0f, 0.0f), Vec3::UnitY(), 0.08f, 1.0f);
    model.addLeaf(node2->id, Vec3(0.7f, 0.8f, 0.2f), Vec3::UnitZ(), Vec2(0.25f, 0.1f));

    const EditRay nodeRay {Vec3(0.0f, 1.0f, 4.0f), -Vec3::UnitZ()};
    const EditPickResult nodePick = RayPicker::pick(model, nodeRay);
    require(nodePick.hit, "ray should hit a skeleton node");
    require(nodePick.object == EditPickObject::Node, "nearest object should be the node");
    require(nodePick.nodeId == node1->id, "ray should select the nearest requested node");
    require(close(nodePick.distance, 3.88f), "node intersection distance should be exact");

    const EditRay leafRay {Vec3(0.7f, 0.8f, 4.0f), -Vec3::UnitZ()};
    const EditPickResult leafPick = RayPicker::pick(model, leafRay);
    require(leafPick.hit && leafPick.object == EditPickObject::Leaf, "ray should hit leaf center");
    require(leafPick.nodeId == node2->id, "leaf pick should report owning node");

    const EditPickResult wholePick = RayPicker::pick(model, nodeRay, EditPickMode::WholePlant);
    require(wholePick.hit && wholePick.wholePlant, "whole-plant mode should still report a hit");
    require(wholePick.nodeId == root->id, "whole-plant mode should select root node");

    SelectionManager selection;
    require(selection.updateHover(nodePick), "hover should accept a new pick");
    require(selection.selectHovered(), "selected state should copy hovered pick");
    require(selection.hasSelection() && selection.selected().nodeId == node1->id,
            "selection should preserve picked node");
    const GizmoRenderData nodeGizmo = GizmoRenderer::build(model, selection);
    require(nodeGizmo.primitives.size() >= 5, "node selection should draw hover sphere, selected sphere and axes");
    selection.setPickMode(EditPickMode::WholePlant);
    require(selection.select(wholePick), "whole-plant result should replace node selection");
    const GizmoRenderData wholeGizmo = GizmoRenderer::build(model, selection);
    bool hasBounds = false;
    for (const GizmoPrimitive& primitive : wholeGizmo.primitives) {
        hasBounds = hasBounds || primitive.type == GizmoPrimitiveType::BoundingBox;
    }
    require(hasBounds, "whole-plant selection should include a bounding box");

    const EditRay centerRay = RayPicker::screenToWorldRay(50.0f, 50.0f, 100.0f, 100.0f,
                                                            Mat4::Identity(), Mat4::Identity());
    require(centerRay.direction.isApprox(Vec3::UnitZ(), kTolerance), "screen ray direction should be normalized");

    std::cout << "PlantEditDemo ray-picking checks passed. Nodes: " << model.nodeCount()
              << ", node3=" << node3->id << '\n';
    return 0;
}
