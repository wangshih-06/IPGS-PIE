#pragma once

#include <vector>

#include "Editing/SelectionManager.h"
#include "Plant/PlantModel.h"

enum class GizmoPrimitiveType {
    Sphere,
    Axis,
    BoundingBox
};

struct GizmoPrimitive {
    GizmoPrimitiveType type = GizmoPrimitiveType::Sphere;
    Vec3 color = Vec3::Ones();
    Vec3 start = Vec3::Zero();
    Vec3 end = Vec3::Zero();
    Vec3 boundsMin = Vec3::Zero();
    Vec3 boundsMax = Vec3::Zero();
    float radius = 0.04f;
};

struct GizmoRenderData {
    std::vector<GizmoPrimitive> primitives;

    bool empty() const { return primitives.empty(); }
};

class GizmoRenderer {
public:
    static GizmoRenderData build(const PlantModel& model, const SelectionManager& selection);
};
