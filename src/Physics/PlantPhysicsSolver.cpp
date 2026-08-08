// ============================================================================
// PlantPhysicsSolver - PBD mass points and parent-child length constraints
// ============================================================================
#include "Physics/PlantPhysicsSolver.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <limits>

#include "Plant/PlantModel.h"

namespace {
constexpr float kEpsilon = 1.0e-6f;
constexpr float kMinimumRestLength = 1.0e-4f;
constexpr float kPi = 3.14159265358979323846f;

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

void setError(QString* error, const QString& message) {
    if (error) *error = message;
}

float edgeLength(const PlantNode& parent, const PlantNode& child) {
    const float geometricLength = (child.position - parent.position).norm();
    if (geometricLength > kMinimumRestLength) return geometricLength;
    return std::max(kMinimumRestLength, child.length);
}
}

PlantPhysicsSolver::PlantPhysicsSolver(const PlantPhysicsSettings& settings) {
    setSettings(settings);
}

void PlantPhysicsSolver::setSettings(const PlantPhysicsSettings& settings) {
    settings_ = settings;
    settings_.solverIterations = std::max(1, settings_.solverIterations);
    settings_.damping = clamp01(settings_.damping);
    settings_.maximumTimeStep = std::max(1.0e-4f, settings_.maximumTimeStep);
    settings_.massDensity = std::max(0.0f, settings_.massDensity);
    settings_.minimumMass = std::max(kEpsilon, settings_.minimumMass);
    settings_.defaultLengthStiffness = clamp01(settings_.defaultLengthStiffness);
    if (!settings_.gravity.allFinite()) settings_.gravity = Vec3::Zero();
}

void PlantPhysicsSolver::clear() {
    massPoints_.clear();
    lengthConstraints_.clear();
    nodeToParticle_.clear();
    statistics_ = PlantPhysicsStatistics{};
}

float PlantPhysicsSolver::nodeMass(float radius, float segmentLength) const {
    const float safeRadius = std::max(0.002f, radius);
    const float safeLength = std::max(0.02f, segmentLength);
    const float cylindricalVolume = kPi * safeRadius * safeRadius * safeLength;
    return std::max(settings_.minimumMass, cylindricalVolume * settings_.massDensity);
}

float PlantPhysicsSolver::branchStiffnessForChild(const PlantModel& model, int childNodeId) const {
    for (const Branch& branch : model.branches()) {
        if (branch.childNodeId == childNodeId) return clamp01(branch.stiffness);
    }
    return settings_.defaultLengthStiffness;
}

bool PlantPhysicsSolver::rebuildFromPlant(const PlantModel& model, QString* error) {
    clear();
    const PlantNode* root = model.rootNode();
    if (!root) {
        setError(error, QStringLiteral("Cannot build physics model without a plant root node."));
        return false;
    }

    std::function<void(const PlantNode*)> addNode = [&](const PlantNode* node) {
        if (!node) return;
        const float segmentLength = node->parent ? edgeLength(*node->parent, *node) : 0.02f;
        PlantMassPoint point;
        point.nodeId = node->id;
        point.parentNodeId = node->parentId;
        point.position = node->position;
        point.predictedPosition = node->position;
        point.mass = nodeMass(node->radius, segmentLength);
        point.fixed = node->parent == nullptr;
        point.inverseMass = point.fixed ? 0.0f : 1.0f / point.mass;
        nodeToParticle_.emplace(point.nodeId, static_cast<int>(massPoints_.size()));
        massPoints_.push_back(point);
        for (const auto& child : node->children) addNode(child.get());
    };
    addNode(root);

    std::function<void(const PlantNode*)> addEdges = [&](const PlantNode* parent) {
        if (!parent) return;
        const auto parentIt = nodeToParticle_.find(parent->id);
        if (parentIt == nodeToParticle_.end()) return;
        for (const auto& childOwner : parent->children) {
            const PlantNode* child = childOwner.get();
            const auto childIt = nodeToParticle_.find(child->id);
            if (childIt == nodeToParticle_.end()) continue;
            PlantLengthConstraint constraint;
            constraint.parentIndex = parentIt->second;
            constraint.childIndex = childIt->second;
            constraint.restLength = edgeLength(*parent, *child);
            constraint.stiffness = branchStiffnessForChild(model, child->id);
            lengthConstraints_.push_back(constraint);
            addEdges(child);
        }
    };
    addEdges(root);

    updateStatistics();
    if (massPoints_.empty()) {
        setError(error, QStringLiteral("Plant physics model did not produce any mass points."));
        return false;
    }
    return true;
}

void PlantPhysicsSolver::resetDynamics() {
    for (PlantMassPoint& point : massPoints_) {
        point.predictedPosition = point.position;
        point.velocity = Vec3::Zero();
    }
    updateStatistics();
}

int PlantPhysicsSolver::particleIndexForNode(int nodeId) const {
    const auto it = nodeToParticle_.find(nodeId);
    return it == nodeToParticle_.end() ? -1 : it->second;
}

bool PlantPhysicsSolver::setParticlePosition(int nodeId, const Vec3& position, bool clearVelocity) {
    const int index = particleIndexForNode(nodeId);
    if (index < 0 || index >= static_cast<int>(massPoints_.size()) || !position.allFinite()) return false;
    PlantMassPoint& point = massPoints_[index];
    point.position = position;
    point.predictedPosition = position;
    if (clearVelocity) point.velocity = Vec3::Zero();
    return true;
}

void PlantPhysicsSolver::projectLengthConstraint(const PlantLengthConstraint& constraint) {
    if (constraint.parentIndex < 0 || constraint.childIndex < 0 ||
        constraint.parentIndex >= static_cast<int>(massPoints_.size()) ||
        constraint.childIndex >= static_cast<int>(massPoints_.size())) return;

    PlantMassPoint& parent = massPoints_[constraint.parentIndex];
    PlantMassPoint& child = massPoints_[constraint.childIndex];
    Vec3 delta = child.predictedPosition - parent.predictedPosition;
    const float currentLength = delta.norm();
    if (!std::isfinite(currentLength) || currentLength < kEpsilon) return;

    const float inverseMassSum = parent.inverseMass + child.inverseMass;
    if (inverseMassSum < kEpsilon) return;

    const float error = currentLength - constraint.restLength;
    const Vec3 correction = delta * (constraint.stiffness * error / (currentLength * inverseMassSum));
    parent.predictedPosition += correction * parent.inverseMass;
    child.predictedPosition -= correction * child.inverseMass;
}

void PlantPhysicsSolver::projectLengthConstraints(int iterations) {
    const int count = iterations < 0 ? settings_.solverIterations : std::max(1, iterations);
    for (int iteration = 0; iteration < count; ++iteration) {
        for (const PlantLengthConstraint& constraint : lengthConstraints_) {
            projectLengthConstraint(constraint);
        }
    }
    statistics_.iterationsUsed = count;
    updateStatistics();
}

void PlantPhysicsSolver::step(float deltaSeconds, const Vec3& externalAcceleration) {
    if (massPoints_.empty()) return;
    const Vec3 acceleration = settings_.gravity + (externalAcceleration.allFinite() ? externalAcceleration : Vec3::Zero());
    const float requested = std::max(0.0f, deltaSeconds);
    const int subSteps = requested > kEpsilon
                             ? std::max(1, static_cast<int>(std::ceil(requested / settings_.maximumTimeStep)))
                             : 1;
    const float subStep = requested > kEpsilon ? requested / static_cast<float>(subSteps) : 0.0f;

    for (int stepIndex = 0; stepIndex < subSteps; ++stepIndex) {
        for (PlantMassPoint& point : massPoints_) {
            if (point.fixed) {
                point.predictedPosition = point.position;
                point.velocity = Vec3::Zero();
                continue;
            }
            if (subStep > 0.0f) point.velocity += acceleration * subStep;
            point.predictedPosition = point.position + point.velocity * subStep;
        }

        projectLengthConstraints();

        if (subStep > 0.0f) {
            for (PlantMassPoint& point : massPoints_) {
                if (point.fixed) continue;
                point.velocity = (point.predictedPosition - point.position) / subStep;
                point.velocity *= settings_.damping;
                point.position = point.predictedPosition;
            }
        } else {
            for (PlantMassPoint& point : massPoints_) point.position = point.predictedPosition;
        }
    }
    updateStatistics();
}

bool PlantPhysicsSolver::applyToPlant(PlantModel* model, QString* error) const {
    if (!model) {
        setError(error, QStringLiteral("Cannot apply physics to a null PlantModel."));
        return false;
    }
    if (massPoints_.empty()) return true;

    for (const PlantMassPoint& point : massPoints_) {
        PlantNode* node = model->findNode(point.nodeId);
        if (!node) {
            setError(error, QStringLiteral("Plant node %1 no longer exists.").arg(point.nodeId));
            return false;
        }
        if (!point.position.allFinite()) {
            setError(error, QStringLiteral("Physics particle %1 has a non-finite position.").arg(point.nodeId));
            return false;
        }
        node->position = point.position;
    }

    for (const PlantMassPoint& point : massPoints_) {
        PlantNode* node = model->findNode(point.nodeId);
        if (!node || !node->parent) continue;
        const Vec3 segment = node->position - node->parent->position;
        const float segmentLength = segment.norm();
        if (segmentLength > kEpsilon) {
            node->direction = segment / segmentLength;
            node->length = segmentLength;
        }
    }
    return true;
}

void PlantPhysicsSolver::updateStatistics() {
    statistics_.particleCount = static_cast<int>(massPoints_.size());
    statistics_.fixedParticleCount = static_cast<int>(std::count_if(
        massPoints_.begin(), massPoints_.end(), [](const PlantMassPoint& point) { return point.fixed; }));
    statistics_.lengthConstraintCount = static_cast<int>(lengthConstraints_.size());
    statistics_.maxLengthError = 0.0f;
    float totalError = 0.0f;
    int counted = 0;
    for (const PlantLengthConstraint& constraint : lengthConstraints_) {
        if (constraint.parentIndex < 0 || constraint.childIndex < 0 ||
            constraint.parentIndex >= static_cast<int>(massPoints_.size()) ||
            constraint.childIndex >= static_cast<int>(massPoints_.size())) continue;
        const float currentLength = (massPoints_[constraint.childIndex].position -
                                     massPoints_[constraint.parentIndex].position).norm();
        const float error = std::abs(currentLength - constraint.restLength);
        statistics_.maxLengthError = std::max(statistics_.maxLengthError, error);
        totalError += error;
        ++counted;
    }
    statistics_.meanLengthError = counted > 0 ? totalError / static_cast<float>(counted) : 0.0f;
}

PlantPhysicsDebugSnapshot PlantPhysicsSolver::debugSnapshot() const {
    PlantPhysicsDebugSnapshot snapshot;
    snapshot.statistics = statistics_;
    snapshot.points.reserve(massPoints_.size());
    for (const PlantMassPoint& point : massPoints_) {
        snapshot.points.push_back({point.nodeId, point.position,
                                   std::max(0.012f, std::cbrt(point.mass) * 0.024f), point.fixed});
    }
    snapshot.lengthSegments.reserve(lengthConstraints_.size());
    for (const PlantLengthConstraint& constraint : lengthConstraints_) {
        if (constraint.parentIndex < 0 || constraint.childIndex < 0 ||
            constraint.parentIndex >= static_cast<int>(massPoints_.size()) ||
            constraint.childIndex >= static_cast<int>(massPoints_.size())) continue;
        const Vec3& start = massPoints_[constraint.parentIndex].position;
        const Vec3& end = massPoints_[constraint.childIndex].position;
        const float rest = std::max(kMinimumRestLength, constraint.restLength);
        snapshot.lengthSegments.push_back({start, end, std::abs((end - start).norm() - constraint.restLength) / rest});
    }
    return snapshot;
}