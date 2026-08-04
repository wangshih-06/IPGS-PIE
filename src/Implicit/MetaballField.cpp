#include "Implicit/MetaballField.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>

#include "Algorithm/PlantNode.h"
#include "Plant/PlantModel.h"

namespace {
float positiveRadius(float value, float minimumValue) {
    return std::max(std::max(0.000001f, minimumValue), value);
}

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float segmentParameter(const Vec3& point, const Vec3& start, const Vec3& end) {
    const Vec3 segment = end - start;
    const float squaredLength = segment.squaredNorm();
    if (squaredLength <= 1.0e-12f) {
        return 0.0f;
    }
    return clamp01((point - start).dot(segment) / squaredLength);
}

std::size_t safeProduct(const Eigen::Vector3i& dimensions) {
    if ((dimensions.array() <= 0).any()) {
        return 0;
    }
    return static_cast<std::size_t>(dimensions.x()) *
           static_cast<std::size_t>(dimensions.y()) *
           static_cast<std::size_t>(dimensions.z());
}
}

bool BoundingBox3::isValid() const {
    return minimum.allFinite() && maximum.allFinite() &&
           (minimum.array() <= maximum.array()).all();
}

Vec3 BoundingBox3::size() const {
    if (!isValid()) {
        return Vec3::Zero();
    }
    return maximum - minimum;
}

Vec3 BoundingBox3::center() const {
    if (!isValid()) {
        return Vec3::Zero();
    }
    return (minimum + maximum) * 0.5f;
}

void BoundingBox3::reset() {
    minimum = Vec3::Constant(std::numeric_limits<float>::infinity());
    maximum = Vec3::Constant(-std::numeric_limits<float>::infinity());
}

void BoundingBox3::expand(const Vec3& point) {
    if (!point.allFinite()) {
        return;
    }
    minimum = minimum.cwiseMin(point);
    maximum = maximum.cwiseMax(point);
}

void BoundingBox3::expand(const Vec3& point, float radius) {
    const float safeRadius = std::max(0.0f, radius);
    expand(point - Vec3::Constant(safeRadius));
    expand(point + Vec3::Constant(safeRadius));
}

void BoundingBox3::expand(const BoundingBox3& other) {
    if (!other.isValid()) {
        return;
    }
    expand(other.minimum);
    expand(other.maximum);
}

BoundingBox3 BoundingBox3::padded(float amount) const {
    if (!isValid()) {
        return {};
    }
    const float padding = std::max(0.0f, amount);
    BoundingBox3 result = *this;
    result.minimum.array() -= padding;
    result.maximum.array() += padding;
    return result;
}

bool ScalarFieldGrid::isValid() const {
    return bounds.isValid() && sampleCount() > 0 && values.size() == sampleCount();
}

std::size_t ScalarFieldGrid::sampleCount() const {
    return safeProduct(dimensions);
}

std::size_t ScalarFieldGrid::linearIndex(int x, int y, int z) const {
    return static_cast<std::size_t>(x) +
           static_cast<std::size_t>(dimensions.x()) *
               (static_cast<std::size_t>(y) +
                static_cast<std::size_t>(dimensions.y()) * static_cast<std::size_t>(z));
}

float ScalarFieldGrid::value(int x, int y, int z) const {
    if (x < 0 || y < 0 || z < 0 ||
        x >= dimensions.x() || y >= dimensions.y() || z >= dimensions.z()) {
        return 0.0f;
    }
    return values[linearIndex(x, y, z)];
}

Vec3 ScalarFieldGrid::position(int x, int y, int z) const {
    return bounds.minimum + spacing.cwiseProduct(Vec3(static_cast<float>(x),
                                                       static_cast<float>(y),
                                                       static_cast<float>(z)));
}

void MetaballField::clear() {
    nodeSources_.clear();
    segmentSources_.clear();
}

void MetaballField::setIsoThreshold(float threshold) {
    isoThreshold_ = std::max(0.000001f, threshold);
    settings_.isoThreshold = isoThreshold_;
}

float MetaballField::isoThreshold() const {
    return isoThreshold_;
}

void MetaballField::addNodeSource(const MetaballNodeSource& source) {
    MetaballNodeSource safe = source;
    safe.influenceRadius = positiveRadius(source.influenceRadius,
                                          settings_.minimumSourceRadius);
    safe.weight = std::max(0.0f, source.weight);
    if (safe.center.allFinite() && safe.weight > 0.0f) {
        nodeSources_.push_back(safe);
    }
}

void MetaballField::addSegmentSource(const MetaballSegmentSource& source) {
    MetaballSegmentSource safe = source;
    safe.startInfluenceRadius = positiveRadius(source.startInfluenceRadius,
                                               settings_.minimumSourceRadius);
    safe.endInfluenceRadius = positiveRadius(source.endInfluenceRadius,
                                             settings_.minimumSourceRadius);
    safe.weight = std::max(0.0f, source.weight);
    if (safe.start.allFinite() && safe.end.allFinite() && safe.weight > 0.0f) {
        segmentSources_.push_back(safe);
    }
}

void MetaballField::removeSourcesOwnedBy(int nodeId) {
    nodeSources_.erase(
        std::remove_if(nodeSources_.begin(), nodeSources_.end(),
                       [nodeId](const MetaballNodeSource& source) {
                           return source.ownerNodeId == nodeId;
                       }),
        nodeSources_.end());
    segmentSources_.erase(
        std::remove_if(segmentSources_.begin(), segmentSources_.end(),
                       [nodeId](const MetaballSegmentSource& source) {
                           return source.parentNodeId == nodeId ||
                                  source.childNodeId == nodeId;
                       }),
        segmentSources_.end());
}

void MetaballField::rebuildFromPlant(const PlantModel& model,
                                     const MetaballFieldSettings& settings) {
    rebuildFromRoot(model.rootNode(), settings);
}

void MetaballField::rebuildFromRoot(const PlantNode* root,
                                    const MetaballFieldSettings& settings) {
    clear();
    settings_ = settings;
    settings_.influenceScale = std::max(0.01f, settings_.influenceScale);
    settings_.segmentWeight = std::max(0.0f, settings_.segmentWeight);
    settings_.jointSmoothness = clamp01(settings_.jointSmoothness);
    settings_.minimumSourceRadius = std::max(0.000001f, settings_.minimumSourceRadius);
    settings_.boundsPadding = std::max(0.0f, settings_.boundsPadding);
    setIsoThreshold(settings.isoThreshold);

    if (!root) {
        return;
    }

    const float smoothness = settings_.jointSmoothness;
    std::function<void(const PlantNode*)> visit = [&](const PlantNode* node) {
        if (!node || !node->active) {
            return;
        }

        const bool isJunction = node->children.size() > 1;
        const bool isEndpoint = node->parent == nullptr || node->children.empty();
        float nodeWeight = 0.0f;
        if (isJunction) {
            nodeWeight = 0.9f * smoothness;
        } else if (isEndpoint) {
            nodeWeight = 0.15f + 0.35f * smoothness;
        } else {
            nodeWeight = 0.2f * smoothness;
        }

        if (nodeWeight > 0.0f) {
            const float radiusScale = settings_.influenceScale *
                                      (1.0f + (isJunction ? 0.4f : 0.15f) * smoothness);
            addNodeSource({node->position,
                           positiveRadius(node->radius * radiusScale,
                                          settings_.minimumSourceRadius),
                           nodeWeight,
                           node->id,
                           isJunction});
        }

        for (const auto& childOwner : node->children) {
            const PlantNode* child = childOwner.get();
            if (!child || !child->active) {
                continue;
            }
            addSegmentSource({node->position,
                              child->position,
                              positiveRadius(node->radius * settings_.influenceScale,
                                             settings_.minimumSourceRadius),
                              positiveRadius(child->radius * settings_.influenceScale,
                                             settings_.minimumSourceRadius),
                              settings_.segmentWeight,
                              node->id,
                              child->id});
            visit(child);
        }
    };
    visit(root);
}

float MetaballField::evaluate(const Vec3& point) const {
    if (!point.allFinite()) {
        return 0.0f;
    }

    float value = 0.0f;
    for (const MetaballNodeSource& source : nodeSources_) {
        value += compactKernel((point - source.center).squaredNorm(),
                               source.influenceRadius,
                               source.weight);
    }

    for (const MetaballSegmentSource& source : segmentSources_) {
        const float t = segmentParameter(point, source.start, source.end);
        const Vec3 closestPoint = source.start + (source.end - source.start) * t;
        const float radius = source.startInfluenceRadius +
                             (source.endInfluenceRadius - source.startInfluenceRadius) * t;
        value += compactKernel((point - closestPoint).squaredNorm(),
                               radius,
                               source.weight);
    }
    return value;
}

float MetaballField::evaluateSigned(const Vec3& point) const {
    return evaluate(point) - isoThreshold_;
}

Vec3 MetaballField::gradient(const Vec3& point, float epsilon) const {
    const float step = std::max(0.000001f, epsilon);
    const Vec3 dx(step, 0.0f, 0.0f);
    const Vec3 dy(0.0f, step, 0.0f);
    const Vec3 dz(0.0f, 0.0f, step);
    return Vec3((evaluate(point + dx) - evaluate(point - dx)) / (2.0f * step),
                (evaluate(point + dy) - evaluate(point - dy)) / (2.0f * step),
                (evaluate(point + dz) - evaluate(point - dz)) / (2.0f * step));
}

BoundingBox3 MetaballField::bounds(float extraPadding) const {
    BoundingBox3 result;
    for (const MetaballNodeSource& source : nodeSources_) {
        result.expand(source.center, source.influenceRadius);
    }
    for (const MetaballSegmentSource& source : segmentSources_) {
        result.expand(source.start, source.startInfluenceRadius);
        result.expand(source.end, source.endInfluenceRadius);
    }

    if (!result.isValid()) {
        result.expand(Vec3::Constant(-0.5f));
        result.expand(Vec3::Constant(0.5f));
    }
    return result.padded(settings_.boundsPadding + std::max(0.0f, extraPadding));
}

ScalarFieldGrid MetaballField::sampleGrid(float requestedSpacing,
                                          std::size_t maximumSampleCount) const {
    ScalarFieldGrid grid;
    grid.bounds = bounds();
    grid.thresholdUsed = isoThreshold_;
    grid.requestedSpacing = std::max(0.0001f, requestedSpacing);
    maximumSampleCount = std::max<std::size_t>(8, maximumSampleCount);

    const Vec3 extent = grid.bounds.size().cwiseMax(Vec3::Constant(grid.requestedSpacing));
    float effectiveSpacing = grid.requestedSpacing;

    auto dimensionsForSpacing = [&](float spacing) {
        Eigen::Vector3i dimensions;
        for (int axis = 0; axis < 3; ++axis) {
            dimensions[axis] = std::max(2,
                static_cast<int>(std::ceil(extent[axis] / spacing)) + 1);
        }
        return dimensions;
    };

    grid.dimensions = dimensionsForSpacing(effectiveSpacing);
    for (int attempt = 0;
         attempt < 8 && safeProduct(grid.dimensions) > maximumSampleCount;
         ++attempt) {
        const double ratio = static_cast<double>(safeProduct(grid.dimensions)) /
                             static_cast<double>(maximumSampleCount);
        effectiveSpacing *= static_cast<float>(std::cbrt(ratio) * 1.01);
        grid.dimensions = dimensionsForSpacing(effectiveSpacing);
        grid.spacingAdjusted = true;
    }

    while (safeProduct(grid.dimensions) > maximumSampleCount) {
        effectiveSpacing *= 1.05f;
        grid.dimensions = dimensionsForSpacing(effectiveSpacing);
        grid.spacingAdjusted = true;
    }

    for (int axis = 0; axis < 3; ++axis) {
        grid.spacing[axis] = extent[axis] /
                             static_cast<float>(std::max(1, grid.dimensions[axis] - 1));
    }

    const std::size_t count = grid.sampleCount();
    grid.values.resize(count);
    grid.minimumValue = std::numeric_limits<float>::infinity();
    grid.maximumValue = -std::numeric_limits<float>::infinity();
    double sum = 0.0;

    for (int z = 0; z < grid.dimensions.z(); ++z) {
        for (int y = 0; y < grid.dimensions.y(); ++y) {
            for (int x = 0; x < grid.dimensions.x(); ++x) {
                const float sample = evaluate(grid.position(x, y, z));
                grid.values[grid.linearIndex(x, y, z)] = sample;
                grid.minimumValue = std::min(grid.minimumValue, sample);
                grid.maximumValue = std::max(grid.maximumValue, sample);
                sum += sample;
                if (sample >= isoThreshold_) {
                    ++grid.samplesAtOrAboveThreshold;
                }
            }
        }
    }

    if (count == 0) {
        grid.minimumValue = grid.maximumValue = 0.0f;
        grid.meanValue = 0.0;
    } else {
        grid.meanValue = sum / static_cast<double>(count);
    }
    return grid;
}

const std::vector<MetaballNodeSource>& MetaballField::nodeSources() const {
    return nodeSources_;
}

const std::vector<MetaballSegmentSource>& MetaballField::segmentSources() const {
    return segmentSources_;
}

const MetaballFieldSettings& MetaballField::settings() const {
    return settings_;
}

float MetaballField::compactKernel(float squaredDistance,
                                   float influenceRadius,
                                   float weight) {
    const float radius = std::max(0.000001f, influenceRadius);
    const float radiusSquared = radius * radius;
    if (squaredDistance >= radiusSquared || weight <= 0.0f) {
        return 0.0f;
    }
    const float normalized = 1.0f - squaredDistance / radiusSquared;
    return weight * normalized * normalized;
}


