#pragma once

#include "Engine/EnvironmentParams.h"
#include <vector>

class Light {
public:
    Light() = default;

    void setEnvironment(const EnvironmentParams& environment) {
        direction = environment.calculateEffectiveLightDirection(Vec3::Zero());
        intensity = environment.lightIntensity;
        lightSources = environment.lightSources;
    }

    Vec3 direction = Vec3(-0.35f, 0.85f, 0.45f).normalized();
    Vec3 color = Vec3(1.0f, 0.95f, 0.82f);
    float intensity = 0.8f;

    std::vector<LightSource> lightSources;
};
