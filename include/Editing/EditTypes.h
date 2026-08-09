#pragma once

#include <limits>

#include "Common/MathTypes.h"

// Indicates which class of plant data an edit changed. Consumers use this to
// decide whether a lightweight preview or a full geometry rebuild is required.
enum class EditDirtyFlag {
    None,
    TransformChanged,
    ParameterChanged,
    TopologyChanged
};

// Interaction types shared by picking, selection and editing tools.
enum class EditPickMode {
    Node,
    WholePlant
};

enum class EditPickObject {
    None,
    Node,
    Leaf
};

struct EditRay {
    Vec3 origin = Vec3::Zero();
    Vec3 direction = Vec3::UnitZ();
};

struct EditPickResult {
    bool hit = false;
    bool wholePlant = false;
    int plantId = -1;
    int nodeId = -1;
    int leafId = -1;
    EditPickObject object = EditPickObject::None;
    float distance = std::numeric_limits<float>::infinity();
    Vec3 hitPoint = Vec3::Zero();
    Vec3 axis = Vec3::UnitY();
};
