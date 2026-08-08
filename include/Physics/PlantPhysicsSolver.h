// ============================================================================
// PlantPhysicsSolver - mass-point / position-based dynamics representation
// Week 13: plant mass points, PBD structural constraints and debug data
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

// A three-node chain is stabilized by preserving the rest chord distance from
// its base to its tip. With the two segment lengths fixed, this preserves the
// original bend without introducing a fragile angular singularity.
struct PlantBendingConstraint {
    int baseIndex = -1;
    int jointIndex = -1;
    int tipIndex = -1;
    float restChordLength = 0.0f;
    float stiffness = 0.72f;
};

// A pair of child branches keeps its included angle about a shared parent.
struct PlantBranchAngleConstraint {
    int parentIndex = -1;
    int firstChildIndex = -1;
    int secondChildIndex = -1;
    float restAngleRadians = 0.0f;
    float stiffness = 0.84f;
};

struct PlantPhysicsSettings {
    // Solver configuration. Stiffness values are normalized to [0, 1].
    int solverIterations = 12;
    float damping = 0.985f;
    float maximumTimeStep = 1.0f / 60.0f;
    float massDensity = 42.0f;
    float minimumMass = 0.025f;
    float defaultLengthStiffness = 0.985f;
    float defaultBendingStiffness = 0.72f;
    float defaultBranchAngleStiffness = 0.84f;
    Vec3 gravity = Vec3(0.0f, -3.2f, 0.0f);
};

struct PlantPhysicsStatistics {
    int particleCount = 0;
    int fixedParticleCount = 0;
    int lengthConstraintCount = 0;
    int bendingConstraintCount = 0;
    int branchAngleConstraintCount = 0;
    float maxLengthError = 0.0f;
    float meanLengthError = 0.0f;
    float maxBendingError = 0.0f;
    float meanBendingError = 0.0f;
    float maxBranchAngleErrorRadians = 0.0f;
    float meanBranchAngleErrorRadians = 0.0f;
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

struct PlantPhysicsDebugBend {
    Vec3 base = Vec3::Zero();
    Vec3 joint = Vec3::Zero();
    Vec3 tip = Vec3::Zero();
    float normalizedError = 0.0f;
};

struct PlantPhysicsDebugAngle {
    Vec3 parent = Vec3::Zero();
    Vec3 firstChild = Vec3::Zero();
    Vec3 secondChild = Vec3::Zero();
    float angleErrorRadians = 0.0f;
};

// Lightweight renderer-facing data. It deliberately contains only resolved
// positions and errors, so debug drawing never mutates the solver state.
struct PlantPhysicsDebugSnapshot {
    std::vector<PlantPhysicsDebugPoint> points;
    std::vector<PlantPhysicsDebugSegment> lengthSegments;
    std::vector<PlantPhysicsDebugBend> bendingConstraints;
    std::vector<PlantPhysicsDebugAngle> branchAngleConstraints;
    PlantPhysicsStatistics statistics;
};

class PlantPhysicsSolver {
public:
    explicit PlantPhysicsSolver(const PlantPhysicsSettings& settings = PlantPhysicsSettings());

    void setSettings(const PlantPhysicsSettings& settings);
    const PlantPhysicsSettings& settings() const { return settings_; }

    // Builds mass points and structural constraints from the skeleton. The
    // model remains unchanged until applyToPlant() is called.
    bool rebuildFromPlant(const PlantModel& model, QString* error = nullptr);
    // Updates growing rest lengths, masses and the fixed-root anchor without
    // discarding velocities. A topology change automatically rebuilds.
    bool synchronizeRestConfiguration(const PlantModel& model, QString* error = nullptr);
    void clear();
    bool empty() const { return massPoints_.empty(); }

    // Integrates external acceleration then projects all PBD constraints.
    // Passing a non-positive delta only performs a constraint projection.
    void step(float deltaSeconds, const Vec3& externalAcceleration = Vec3::Zero());
    void projectConstraints(int iterations = -1);
    void projectLengthConstraints(int iterations = -1);
    void resetDynamics();

    // Writes solved positions, segment directions and segment lengths back to
    // the PlantModel. The root remains anchored because its inverse mass is 0.
    bool applyToPlant(PlantModel* model, QString* error = nullptr) const;

    bool setParticlePosition(int nodeId, const Vec3& position, bool clearVelocity = true);
    int particleIndexForNode(int nodeId) const;

    const std::vector<PlantMassPoint>& massPoints() const { return massPoints_; }
    const std::vector<PlantLengthConstraint>& lengthConstraints() const { return lengthConstraints_; }
    const std::vector<PlantBendingConstraint>& bendingConstraints() const { return bendingConstraints_; }
    const std::vector<PlantBranchAngleConstraint>& branchAngleConstraints() const { return branchAngleConstraints_; }
    const PlantPhysicsStatistics& statistics() const { return statistics_; }
    PlantPhysicsDebugSnapshot debugSnapshot() const;

private:
    float nodeMass(float radius, float segmentLength) const;
    float branchStiffnessForChild(const PlantModel& model, int childNodeId) const;
    void updateStatistics();
    void projectLengthConstraint(const PlantLengthConstraint& constraint);
    void projectBendingConstraint(const PlantBendingConstraint& constraint);
    void projectBranchAngleConstraint(const PlantBranchAngleConstraint& constraint);

    PlantPhysicsSettings settings_;
    std::vector<PlantMassPoint> massPoints_;
    std::vector<PlantLengthConstraint> lengthConstraints_;
    std::vector<PlantBendingConstraint> bendingConstraints_;
    std::vector<PlantBranchAngleConstraint> branchAngleConstraints_;
    std::unordered_map<int, int> nodeToParticle_;
    PlantPhysicsStatistics statistics_;
};
