#pragma once

#include <cstddef>
#include <limits>
#include <vector>

#include <Eigen/Core>

#include "Common/MathTypes.h"

class PlantModel;
class PlantNode;

struct BoundingBox3 {
    Vec3 minimum = Vec3::Constant(std::numeric_limits<float>::infinity());
    Vec3 maximum = Vec3::Constant(-std::numeric_limits<float>::infinity());

    bool isValid() const;
    Vec3 size() const;
    Vec3 center() const;
    void reset();
    void expand(const Vec3& point);
    void expand(const Vec3& point, float radius);
    void expand(const BoundingBox3& other);
    BoundingBox3 padded(float amount) const;
};

enum class MetaballSourceType {
    PlantNode,
    BranchSegment
};

struct MetaballNodeSource {
    Vec3 center = Vec3::Zero();
    float influenceRadius = 0.1f;
    float weight = 1.0f;
    int ownerNodeId = -1;
    bool junction = false;
};

struct MetaballSegmentSource {
    Vec3 start = Vec3::Zero();
    Vec3 end = Vec3::UnitY();
    float startInfluenceRadius = 0.1f;
    float endInfluenceRadius = 0.1f;
    float weight = 1.0f;
    int parentNodeId = -1;
    int childNodeId = -1;
};

struct MetaballFieldSettings {
    float isoThreshold = 0.5f;
    float influenceScale = 2.0f;
    float segmentWeight = 1.0f;
    float jointSmoothness = 0.65f;
    float minimumSourceRadius = 0.005f;
    float boundsPadding = 0.1f;
};

struct ScalarFieldGrid {
    BoundingBox3 bounds;
    Eigen::Vector3i dimensions = Eigen::Vector3i::Zero();
    Vec3 spacing = Vec3::Zero();
    std::vector<float> values;
    float minimumValue = 0.0f;
    float maximumValue = 0.0f;
    double meanValue = 0.0;
    std::size_t samplesAtOrAboveThreshold = 0;
    float thresholdUsed = 0.5f;
    float requestedSpacing = 0.1f;
    bool spacingAdjusted = false;

    bool isValid() const;
    std::size_t sampleCount() const;
    std::size_t linearIndex(int x, int y, int z) const;
    float value(int x, int y, int z) const;
    Vec3 position(int x, int y, int z) const;
};

class MetaballField {
public:
    void clear();

    void setIsoThreshold(float threshold);
    float isoThreshold() const;

    void addNodeSource(const MetaballNodeSource& source);
    void addSegmentSource(const MetaballSegmentSource& source);
    void removeSourcesOwnedBy(int nodeId);

    void rebuildFromPlant(const PlantModel& model,
                          const MetaballFieldSettings& settings = {});
    void rebuildFromRoot(const PlantNode* root,
                         const MetaballFieldSettings& settings = {});

    float evaluate(const Vec3& point) const;
    float evaluateSigned(const Vec3& point) const;
    Vec3 gradient(const Vec3& point, float epsilon = 0.001f) const;

    BoundingBox3 bounds(float extraPadding = 0.0f) const;
    ScalarFieldGrid sampleGrid(float requestedSpacing,
                               std::size_t maximumSampleCount = 2000000) const;

    const std::vector<MetaballNodeSource>& nodeSources() const;
    const std::vector<MetaballSegmentSource>& segmentSources() const;
    const MetaballFieldSettings& settings() const;

    static float compactKernel(float squaredDistance,
                               float influenceRadius,
                               float weight);

private:
    std::vector<MetaballNodeSource> nodeSources_;
    std::vector<MetaballSegmentSource> segmentSources_;
    MetaballFieldSettings settings_;
    float isoThreshold_ = 0.5f;
};
