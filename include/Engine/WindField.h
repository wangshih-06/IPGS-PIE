// ============================================================================
// WindField - deterministic 3D Perlin-noise wind acceleration for plant PBD
// ============================================================================
#pragma once

#include <array>
#include <cstdint>

#include "Common/MathTypes.h"

struct WindFieldSettings {
    std::uint32_t seed = 0x5EED1234u;
    float spatialFrequency = 0.55f;
    float temporalFrequency = 0.85f;
    int octaves = 3;
    float persistence = 0.52f;
    float lateralStrength = 1.8f;
    float verticalStrength = 0.22f;
    Vec3 prevailingDirection = Vec3(0.75f, 0.0f, 0.45f);
};

// A coherent, deterministic wind field. Nearby points receive similar gusts,
// while different branches still experience position-dependent motion.
class PerlinWindField {
public:
    explicit PerlinWindField(const WindFieldSettings& settings = WindFieldSettings());

    void setSettings(const WindFieldSettings& settings);
    const WindFieldSettings& settings() const { return settings_; }

    // Returns acceleration at position/time. intensity is normally sourced
    // from EnvironmentParams::windIntensity and is clamped to non-negative.
    Vec3 sample(const Vec3& position, float time, float intensity = 1.0f) const;

private:
    static float fade(float value);
    static float lerp(float first, float second, float amount);
    static float gradient(int hash, float x, float y, float z);
    float noise(float x, float y, float z) const;
    float fractalNoise(const Vec3& position) const;
    void rebuildPermutation();

    WindFieldSettings settings_;
    std::array<int, 512> permutation_{};
};
