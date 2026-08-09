#pragma once

#include "Editing/EditTypes.h"
#include "Plant/PlantModel.h"

struct RayPickerSettings {
    float nodeRadius = 0.12f;
    float leafRadius = 0.10f;
};

class RayPicker {
public:
    // Converts pixel coordinates (with a top-left origin) into a normalized
    // world-space ray by unprojecting both clip-space depth endpoints.
    static EditRay screenToWorldRay(float mouseX,
                                    float mouseY,
                                    float viewportWidth,
                                    float viewportHeight,
                                    const Mat4& viewMatrix,
                                    const Mat4& projectionMatrix);

    static bool intersectSphere(const EditRay& ray,
                                const Vec3& center,
                                float radius,
                                float* distance = nullptr,
                                Vec3* hitPoint = nullptr);

    static EditPickResult pick(const PlantModel& model,
                               const EditRay& ray,
                               EditPickMode mode = EditPickMode::Node,
                               const RayPickerSettings& settings = {});
};
