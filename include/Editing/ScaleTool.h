#pragma once

#include <QString>

#include "Plant/PlantModel.h"

struct ScaleParams {
    Vec3 scale = Vec3::Ones();
    float minimumRadius = 0.005f;
    float minimumLength = 0.01f;
    bool scaleLeaves = true;
};

class ScaleTool {
public:
    // Scales the selected node and every descendant around the selected node's
    // current position. A non-uniform scale updates directions by the same
    // axis transform and derives branch length from its directional stretch.
    static bool apply(PlantModel& model,
                      int nodeId,
                      const ScaleParams& params,
                      QString* error = nullptr);
};
