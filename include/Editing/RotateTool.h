#pragma once

#include <QString>

#include "Plant/PlantModel.h"

class RotateTool {
public:
    // Rotates the selected node and all of its descendants around an explicit
    // node/branch pivot. Quaternion rotation keeps the transform gimbal-lock free.
    static bool apply(PlantModel& model,
                      int nodeId,
                      const Vec3& pivot,
                      const Vec3& axis,
                      float angleRadians,
                      QString* error = nullptr);
};
