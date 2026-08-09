#pragma once

#include <QString>

#include "Plant/PlantModel.h"

struct BendParams {
    float angleRadians = 0.0f;
    Vec3 bendAxis = Vec3::UnitZ();
    float falloff = 1.0f;
    float stiffness = 1.0f;
    float maximumAngleRadians = 1.309f; // 75 degrees
};

struct BendCurve {
    Vec3 start = Vec3::Zero();
    Vec3 control1 = Vec3::Zero();
    Vec3 control2 = Vec3::Zero();
    Vec3 end = Vec3::Zero();

    Vec3 sample(float u) const;
    Vec3 tangent(float u) const;
};

class BendTool {
public:
    // Bends the edge from the selected node's parent to the selected node and
    // rotates its complete subtree as one rigid transform. This keeps all
    // parent/child links attached while supplying a Bezier preview curve.
    static bool apply(PlantModel& model,
                      int nodeId,
                      const BendParams& params,
                      QString* error = nullptr);
    static BendCurve previewCurve(const PlantModel& model,
                                  int nodeId,
                                  const BendParams& params,
                                  QString* error = nullptr);
};
