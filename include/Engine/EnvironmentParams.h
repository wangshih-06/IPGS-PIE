// ============================================================================
// EnvironmentParams - shared environment state for the simulation and renderer
// ============================================================================
#pragma once

#include "Common/MathTypes.h"

struct EnvironmentParams {
    Vec3 lightDirection = Vec3(-0.35f, 0.85f, 0.45f).normalized();
    float lightIntensity = 0.8f;
    float moisture = 0.72f;
    float nutrition = 0.64f;
    float temperature = 22.0f;
    float windIntensity = 0.28f;
    float time = 0.0f;
};
