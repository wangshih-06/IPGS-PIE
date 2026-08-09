// ============================================================================
// WindField - coherent 3D Perlin-noise wind implementation
// ============================================================================
#include "Engine/WindField.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <random>

namespace {
constexpr float kEpsilon = 1.0e-6f;

float clampNonNegative(float value) {
    return std::max(0.0f, value);
}
}

PerlinWindField::PerlinWindField(const WindFieldSettings& settings) {
    setSettings(settings);
}

void PerlinWindField::setSettings(const WindFieldSettings& settings) {
    settings_ = settings;
    settings_.spatialFrequency = std::max(0.001f, settings_.spatialFrequency);
    settings_.temporalFrequency = std::max(0.0f, settings_.temporalFrequency);
    settings_.octaves = std::clamp(settings_.octaves, 1, 6);
    settings_.persistence = std::clamp(settings_.persistence, 0.05f, 0.95f);
    settings_.lateralStrength = clampNonNegative(settings_.lateralStrength);
    settings_.verticalStrength = clampNonNegative(settings_.verticalStrength);
    if (!settings_.prevailingDirection.allFinite() ||
        settings_.prevailingDirection.squaredNorm() < kEpsilon) {
        settings_.prevailingDirection = Vec3(1.0f, 0.0f, 0.0f);
    }
    settings_.prevailingDirection.normalize();
    rebuildPermutation();
}

void PerlinWindField::rebuildPermutation() {
    std::array<int, 256> base{};
    std::iota(base.begin(), base.end(), 0);
    std::mt19937 generator(settings_.seed);
    std::shuffle(base.begin(), base.end(), generator);
    for (int index = 0; index < 512; ++index) permutation_[index] = base[index & 255];
}

float PerlinWindField::fade(float value) {
    return value * value * value * (value * (value * 6.0f - 15.0f) + 10.0f);
}

float PerlinWindField::lerp(float first, float second, float amount) {
    return first + amount * (second - first);
}

float PerlinWindField::gradient(int hash, float x, float y, float z) {
    const int selector = hash & 15;
    const float first = selector < 8 ? x : y;
    const float second = selector < 4 ? y : (selector == 12 || selector == 14 ? x : z);
    return ((selector & 1) == 0 ? first : -first) + ((selector & 2) == 0 ? second : -second);
}

float PerlinWindField::noise(float x, float y, float z) const {
    const int cellX = static_cast<int>(std::floor(x)) & 255;
    const int cellY = static_cast<int>(std::floor(y)) & 255;
    const int cellZ = static_cast<int>(std::floor(z)) & 255;
    x -= std::floor(x);
    y -= std::floor(y);
    z -= std::floor(z);
    const float u = fade(x);
    const float v = fade(y);
    const float w = fade(z);

    const int a = permutation_[cellX] + cellY;
    const int aa = permutation_[a] + cellZ;
    const int ab = permutation_[a + 1] + cellZ;
    const int b = permutation_[cellX + 1] + cellY;
    const int ba = permutation_[b] + cellZ;
    const int bb = permutation_[b + 1] + cellZ;
    return lerp(
        lerp(lerp(gradient(permutation_[aa], x, y, z),
                  gradient(permutation_[ba], x - 1.0f, y, z), u),
             lerp(gradient(permutation_[ab], x, y - 1.0f, z),
                  gradient(permutation_[bb], x - 1.0f, y - 1.0f, z), u), v),
        lerp(lerp(gradient(permutation_[aa + 1], x, y, z - 1.0f),
                  gradient(permutation_[ba + 1], x - 1.0f, y, z - 1.0f), u),
             lerp(gradient(permutation_[ab + 1], x, y - 1.0f, z - 1.0f),
                  gradient(permutation_[bb + 1], x - 1.0f, y - 1.0f, z - 1.0f), u), v), w);
}

float PerlinWindField::fractalNoise(const Vec3& position) const {
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float sum = 0.0f;
    float normalizer = 0.0f;
    for (int octave = 0; octave < settings_.octaves; ++octave) {
        sum += amplitude * noise(position.x() * frequency, position.y() * frequency, position.z() * frequency);
        normalizer += amplitude;
        amplitude *= settings_.persistence;
        frequency *= 2.0f;
    }
    return normalizer > kEpsilon ? sum / normalizer : 0.0f;
}

Vec3 PerlinWindField::sample(const Vec3& position, float time, float intensity) const {
    if (!position.allFinite() || !std::isfinite(time)) return Vec3::Zero();
    const float scale = clampNonNegative(intensity);
    if (scale < kEpsilon) return Vec3::Zero();

    const Vec3 p = position * settings_.spatialFrequency;
    const float t = time * settings_.temporalFrequency;
    // Three decorrelated channels make a divergence-like, gusty vector field
    // instead of the synchronized sine/cosine motion used previously.
    const float xGust = fractalNoise(p + Vec3(17.31f + t, 4.17f, 9.73f));
    const float yGust = fractalNoise(p + Vec3(2.61f, 31.57f + t * 0.71f, 14.29f));
    const float zGust = fractalNoise(p + Vec3(11.83f, 7.41f, 23.19f + t * 0.89f));
    const Vec3 gust(xGust, yGust * settings_.verticalStrength, zGust);
    return scale * (settings_.prevailingDirection * settings_.lateralStrength +
                    Vec3(gust.x() * settings_.lateralStrength,
                         gust.y(),
                         gust.z() * settings_.lateralStrength));
}
