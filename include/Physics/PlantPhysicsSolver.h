// ============================================================================
// PlantPhysicsSolver - mass-point / position-based dynamics representation
// 第13周：植物质量点模型、固定根节点与枝干长度约束
// ============================================================================
#pragma once

#include <unordered_map>
#include <vector>

#include <QString>

#include "Common/MathTypes.h"

class PlantModel;

// One PlantNode becomes one mass point. The root point is fixed by default so
// the above-ground skeleton keeps a stable attachment to the soil.
struct PlantMassPoint {
    int nodeId = -1;
    int parentNodeId = -1;
    Vec3 position = Vec3::Zero();
    Vec3 predictedPosition = Vec3::Zero();
    Vec3 velocity = Vec3::Zero();
    float mass = 0.05f;
    float inverseMass = 20.0f;
    bool fixed = false;
};

// A skeleton parent-child edge is represented as a PBD distance constraint.
struct PlantLengthConstraint {
    int parentIndex = -1;
    int childIndex = -1;
    float restLength = 0.0f;
    float stiffness = 0.98f;
};

struct PlantPhysicsSettings {
    // Solver configuration. Stiffness values are normalized to [0, 1].
    int solverIterations = 12;
    float damping = 0.985f;
    float maximumTimeStep = 1.0f / 60.0f;
    float massDensity = 42.0f;
    float minimumMass = 0.025f;
    float defaultLengthStiffness = 0.985f;
    Vec3 gravity = Vec3(0.0f, -3.2f, 0.0f);
};

struct PlantPhysicsStatistics {
    int particleCount = 0;
    int fixedParticleCount = 0;
    int lengthConstraintCount = 0;
    float maxLengthError = 0.0f;
    float meanLengthError = 0.0f;
    int iterationsUsed = 0;
};

struct PlantPhysicsDebugPoint {
    int nodeId = -1;
    Vec3 position = Vec3::Zero();
    float radius = 0.025f;
    bool fixed = false;
};

struct PlantPhysicsDebugSegment {
    Vec3 start = Vec3::Zero();
    Vec3 end = Vec3::Zero();
    float normalizedError = 0.0f;
};

// Lightweight renderer-facing data. It deliberately contains only resolved
// positions and errors, so debug drawing never mutates the solver state.
struct PlantPhysicsDebugSnapshot {
    std::vector<PlantPhysicsDebugPoint> points;
    std::vector<PlantPhysicsDebugSegment> lengthSegments;
    PlantPhysicsStatistics statistics;
};

class PlantPhysicsSolver {
public:
    explicit PlantPhysicsSolver(const PlantPhysicsSettings& settings = PlantPhysicsSettings());

    void setSettings(const PlantPhysicsSettings& settings);
    const PlantPhysicsSettings& settings() const { return settings_; }

    // Builds one mass point per skeleton node and one length constraint per
    // parent-child branch edge. The model remains unchanged until applyToPlant.
    bool rebuildFromPlant(const PlantModel& model, QString* error = nullptr);
    void clear();
    bool empty() const { return massPoints_.empty(); }

    // Integrates external acceleration then projects all PBD length constraints.
    // Passing a non-positive delta only performs a constraint projection.
    void step(float deltaSeconds, const Vec3& externalAcceleration = Vec3::Zero());
    void projectLengthConstraints(int iterations = -1);
    void resetDynamics();

    // Writes solved positions, segment directions and segment lengths back to
    // the PlantModel. The root remains anchored because its inverse mass is 0.
    bool applyToPlant(PlantModel* model, QString* error = nullptr) const;

    bool setParticlePosition(int nodeId, const Vec3& position, bool clearVelocity = true);
    int particleIndexForNode(int nodeId) const;

    const std::vector<PlantMassPoint>& massPoints() const { return massPoints_; }
    const std::vector<PlantLengthConstraint>& lengthConstraints() const { return lengthConstraints_; }
    const PlantPhysicsStatistics& statistics() const { return statistics_; }
    PlantPhysicsDebugSnapshot debugSnapshot() const;

private:
    float nodeMass(float radius, float segmentLength) const;
    float branchStiffnessForChild(const PlantModel& model, int childNodeId) const;
    void updateStatistics();
    void projectLengthConstraint(const PlantLengthConstraint& constraint);

    PlantPhysicsSettings settings_;
    std::vector<PlantMassPoint> massPoints_;
    std::vector<PlantLengthConstraint> lengthConstraints_;
    std::unordered_map<int, int> nodeToParticle_;
    PlantPhysicsStatistics statistics_;
};