#pragma once

#include "Engine/EnvironmentParams.h"

class Light {
public:
    Light() = default;

    void setEnvironment(const EnvironmentParams& environment) {
        direction = environment.lightDirection.normalized();
        intensity = environment.lightIntensity;
    }

    Vec3 direction = Vec3(-0.35f, 0.85f, 0.45f).normalized();
    Vec3 color = Vec3(1.0f, 0.95f, 0.82f);
    float intensity = 0.8f;
};
