#pragma once

#include <optional>

#include <QString>

#include "Plant/PlantModel.h"

struct NodeParameterUpdate {
    // Absolute horizontal heading in degrees, constrained to [-180, 180].
    std::optional<float> angleDegrees;
    std::optional<float> length;
    std::optional<float> radius;
    // Applies to leaves directly attached to this node, constrained to [0, 1].
    std::optional<float> leafDensity;
    std::optional<float> age;
    // Maps to PlantNode::generation; structural depth remains topology-derived.
    std::optional<int> growthDepth;
};

class NodeParameterEditor {
public:
    static bool apply(PlantModel& model,
                      int nodeId,
                      const NodeParameterUpdate& update,
                      QString* error = nullptr);
};
