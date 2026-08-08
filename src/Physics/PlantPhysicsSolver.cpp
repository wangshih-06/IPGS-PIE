// ============================================================================
// PlantPhysicsSolver - PBD mass points and structural plant constraints
// ============================================================================
#include "Physics/PlantPhysicsSolver.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include "Plant/PlantModel.h"

namespace {
constexpr float kEpsilon = 1.0e-6f;
constexpr float kMinimumRestLength = 1.0e-4f;
constexpr float kPi = 3.14159265358979323846f;

float clamp01(float value) {
    return std::max(0.0f, std::min(1.0f, value));
}

float clampCosine(float value) {
    return std::max(-1.0f, std::min(1.0f, value));
}

void setError(QString* error, const QString& message) {
    if (error) *error = message;
}

float edgeLength(const PlantNode& parent, const PlantNode& child) {
    const float geometricLength = (child.position - parent.position).norm();
    if (geometricLength > kMinimumRestLength) return geometricLength;
    return std::max(kMinimumRestLength, child.length);
}

float includedAngle(const Vec3& first, const Vec3& second) {
    const float firstLength = first.norm();
    const float secondLength = second.norm();
    if (firstLength < kEpsilon || secondLength < kEpsilon) return 0.0f;
    return std::acos(clampCosine(first.dot(second) / (firstLength * secondLength)));
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
    settings_.defaultBendingStiffness = clamp01(settings_.defaultBendingStiffness);
    settings_.defaultBranchAngleStiffness = clamp01(settings_.defaultBranchAngleStiffness);
    if (!settings_.gravity.allFinite()) settings_.gravity = Vec3::Zero();
}

void PlantPhysicsSolver::clear() {
    massPoints_.clear();
    lengthConstraints_.clear();
    bendingConstraints_.clear();
    branchAngleConstraints_.clear();
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

    std::function<void(const PlantNode*)> addConstraints = [&](const PlantNode* parent) {
        if (!parent) return;
        const auto parentIt = nodeToParticle_.find(parent->id);
        if (parentIt == nodeToParticle_.end()) return;

        std::vector<int> childIndices;
        childIndices.reserve(parent->children.size());
        for (const auto& childOwner : parent->children) {
            const PlantNode* child = childOwner.get();
            const auto childIt = nodeToParticle_.find(child->id);
            if (childIt == nodeToParticle_.end()) continue;

            PlantLengthConstraint length;
            length.parentIndex = parentIt->second;
            length.childIndex = childIt->second;
            length.restLength = edgeLength(*parent, *child);
            length.stiffness = branchStiffnessForChild(model, child->id);
            lengthConstraints_.push_back(length);
            childIndices.push_back(childIt->second);

            if (parent->parent) {
                const auto baseIt = nodeToParticle_.find(parent->parent->id);
                if (baseIt != nodeToParticle_.end()) {
                    PlantBendingConstraint bend;
                    bend.baseIndex = baseIt->second;
                    bend.jointIndex = parentIt->second;
                    bend.tipIndex = childIt->second;
                    bend.restChordLength = std::max(
                        kMinimumRestLength,
                        (child->position - parent->parent->position).norm());
                    bend.stiffness = settings_.defaultBendingStiffness;
                    bendingConstraints_.push_back(bend);
                }
            }
            addConstraints(child);
        }

        for (std::size_t first = 0; first < childIndices.size(); ++first) {
            for (std::size_t second = first + 1; second < childIndices.size(); ++second) {
                PlantBranchAngleConstraint angle;
                angle.parentIndex = parentIt->second;
                angle.firstChildIndex = childIndices[first];
                angle.secondChildIndex = childIndices[second];
                const Vec3 firstDirection = massPoints_[angle.firstChildIndex].position -
                                            massPoints_[angle.parentIndex].position;
                const Vec3 secondDirection = massPoints_[angle.secondChildIndex].position -
                                             massPoints_[angle.parentIndex].position;
                angle.restAngleRadians = includedAngle(firstDirection, secondDirection);
                angle.stiffness = settings_.defaultBranchAngleStiffness;
                branchAngleConstraints_.push_back(angle);
            }
        }
    };
    addConstraints(root);

    updateStatistics();
    if (massPoints_.empty()) {
        setError(error, QStringLiteral("Plant physics model did not produce any mass points."));
        return false;
    }
    return true;
}

bool PlantPhysicsSolver::synchronizeRestConfiguration(const PlantModel& model, QString* error) {
    if (massPoints_.empty() || massPoints_.size() != model.nodeCount()) {
        return rebuildFromPlant(model, error);
    }

    for (PlantMassPoint& point : massPoints_) {
        const PlantNode* node = model.findNode(point.nodeId);
        if (!node || (point.fixed != (node->parent == nullptr))) {
            return rebuildFromPlant(model, error);
        }
        const float segmentLength = node->parent ? edgeLength(*node->parent, *node) : 0.02f;
        point.mass = nodeMass(node->radius, segmentLength);
        point.inverseMass = point.fixed ? 0.0f : 1.0f / point.mass;
        if (point.fixed) {
            point.position = node->position;
            point.predictedPosition = node->position;
            point.velocity = Vec3::Zero();
        }
    }

    for (PlantLengthConstraint& constraint : lengthConstraints_) {
        if (constraint.parentIndex < 0 || constraint.childIndex < 0 ||
            constraint.parentIndex >= static_cast<int>(massPoints_.size()) ||
            constraint.childIndex >= static_cast<int>(massPoints_.size())) {
            return rebuildFromPlant(model, error);
        }
        const PlantNode* parent = model.findNode(massPoints_[constraint.parentIndex].nodeId);
        const PlantNode* child = model.findNode(massPoints_[constraint.childIndex].nodeId);
        if (!parent || !child || child->parent != parent) return rebuildFromPlant(model, error);
        constraint.restLength = edgeLength(*parent, *child);
        constraint.stiffness = branchStiffnessForChild(model, child->id);
    }

    for (PlantBendingConstraint& constraint : bendingConstraints_) {
        if (constraint.baseIndex < 0 || constraint.tipIndex < 0 ||
            constraint.baseIndex >= static_cast<int>(massPoints_.size()) ||
            constraint.tipIndex >= static_cast<int>(massPoints_.size())) return rebuildFromPlant(model, error);
        const PlantNode* base = model.findNode(massPoints_[constraint.baseIndex].nodeId);
        const PlantNode* tip = model.findNode(massPoints_[constraint.tipIndex].nodeId);
        if (!base || !tip) return rebuildFromPlant(model, error);
        constraint.restChordLength = std::max(kMinimumRestLength, (tip->position - base->position).norm());
    }

    for (PlantBranchAngleConstraint& constraint : branchAngleConstraints_) {
        if (constraint.parentIndex < 0 || constraint.firstChildIndex < 0 || constraint.secondChildIndex < 0 ||
            constraint.parentIndex >= static_cast<int>(massPoints_.size()) ||
            constraint.firstChildIndex >= static_cast<int>(massPoints_.size()) ||
            constraint.secondChildIndex >= static_cast<int>(massPoints_.size())) return rebuildFromPlant(model, error);
        const PlantNode* parent = model.findNode(massPoints_[constraint.parentIndex].nodeId);
        const PlantNode* first = model.findNode(massPoints_[constraint.firstChildIndex].nodeId);
        const PlantNode* second = model.findNode(massPoints_[constraint.secondChildIndex].nodeId);
        if (!parent || !first || !second || first->parent != parent || second->parent != parent) {
            return rebuildFromPlant(model, error);
        }
        constraint.restAngleRadians = includedAngle(first->position - parent->position,
                                                    second->position - parent->position);
    }
    updateStatistics();
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

void PlantPhysicsSolver::projectBendingConstraint(const PlantBendingConstraint& constraint) {
    if (constraint.baseIndex < 0 || constraint.tipIndex < 0 ||
        constraint.baseIndex >= static_cast<int>(massPoints_.size()) ||
        constraint.tipIndex >= static_cast<int>(massPoints_.size())) return;

    PlantMassPoint& base = massPoints_[constraint.baseIndex];
    PlantMassPoint& tip = massPoints_[constraint.tipIndex];
    const Vec3 delta = tip.predictedPosition - base.predictedPosition;
    const float chordLength = delta.norm();
    if (!std::isfinite(chordLength) || chordLength < kEpsilon) return;

    const float inverseMassSum = base.inverseMass + tip.inverseMass;
    if (inverseMassSum < kEpsilon) return;

    const float error = chordLength - constraint.restChordLength;
    const Vec3 correction = delta * (constraint.stiffness * error / (chordLength * inverseMassSum));
    base.predictedPosition += correction * base.inverseMass;
    tip.predictedPosition -= correction * tip.inverseMass;
}

void PlantPhysicsSolver::projectBranchAngleConstraint(const PlantBranchAngleConstraint& constraint) {
    if (constraint.parentIndex < 0 || constraint.firstChildIndex < 0 || constraint.secondChildIndex < 0 ||
        constraint.parentIndex >= static_cast<int>(massPoints_.size()) ||
        constraint.firstChildIndex >= static_cast<int>(massPoints_.size()) ||
        constraint.secondChildIndex >= static_cast<int>(massPoints_.size())) return;

    PlantMassPoint& parent = massPoints_[constraint.parentIndex];
    PlantMassPoint& firstChild = massPoints_[constraint.firstChildIndex];
    PlantMassPoint& secondChild = massPoints_[constraint.secondChildIndex];
    const Vec3 firstVector = firstChild.predictedPosition - parent.predictedPosition;
    const Vec3 secondVector = secondChild.predictedPosition - parent.predictedPosition;
    const float firstLength = firstVector.norm();
    const float secondLength = secondVector.norm();
    if (!std::isfinite(firstLength) || !std::isfinite(secondLength) ||
        firstLength < kEpsilon || secondLength < kEpsilon) return;

    const Vec3 firstDirection = firstVector / firstLength;
    const Vec3 secondDirection = secondVector / secondLength;
    const float cosine = clampCosine(firstDirection.dot(secondDirection));
    const float restCosine = std::cos(constraint.restAngleRadians);
    const float value = cosine - restCosine;

    // Gradients of dot(normalize(a), normalize(b)) for the two branch arms.
    const Vec3 firstGradient = (secondDirection - cosine * firstDirection) / firstLength;
    const Vec3 secondGradient = (firstDirection - cosine * secondDirection) / secondLength;
    const Vec3 parentGradient = -firstGradient - secondGradient;
    const float denominator = parent.inverseMass * parentGradient.squaredNorm() +
                              firstChild.inverseMass * firstGradient.squaredNorm() +
                              secondChild.inverseMass * secondGradient.squaredNorm();
    if (!std::isfinite(denominator) || denominator < kEpsilon) return;

    const float multiplier = constraint.stiffness * value / denominator;
    parent.predictedPosition -= parent.inverseMass * multiplier * parentGradient;
    firstChild.predictedPosition -= firstChild.inverseMass * multiplier * firstGradient;
    secondChild.predictedPosition -= secondChild.inverseMass * multiplier * secondGradient;
}

void PlantPhysicsSolver::projectConstraints(int iterations) {
    const int count = iterations < 0 ? settings_.solverIterations : std::max(1, iterations);
    for (int iteration = 0; iteration < count; ++iteration) {
        for (const PlantLengthConstraint& constraint : lengthConstraints_) projectLengthConstraint(constraint);
        for (const PlantBendingConstraint& constraint : bendingConstraints_) projectBendingConstraint(constraint);
        for (const PlantBranchAngleConstraint& constraint : branchAngleConstraints_) {
            projectBranchAngleConstraint(constraint);
        }
        // Angle and bending projections can slightly perturb branch lengths.
        // Finish each PBD iteration with a distance pass to keep the skeleton
        // edges tight, including after the final iteration.
        for (const PlantLengthConstraint& constraint : lengthConstraints_) projectLengthConstraint(constraint);
    }
    statistics_.iterationsUsed = count;
    updateStatistics();
}

void PlantPhysicsSolver::projectLengthConstraints(int iterations) {
    const int count = iterations < 0 ? settings_.solverIterations : std::max(1, iterations);
    for (int iteration = 0; iteration < count; ++iteration) {
        for (const PlantLengthConstraint& constraint : lengthConstraints_) projectLengthConstraint(constraint);
    }
    statistics_.iterationsUsed = count;
    updateStatistics();
}

void PlantPhysicsSolver::step(float deltaSeconds, const Vec3& externalAcceleration) {
    if (massPoints_.empty()) return;
    const Vec3 acceleration = settings_.gravity +
                              (externalAcceleration.allFinite() ? externalAcceleration : Vec3::Zero());
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
            point.velocity += acceleration * subStep;
            point.predictedPosition = point.position + point.velocity * subStep;
        }

        projectConstraints();

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
    statistics_.bendingConstraintCount = static_cast<int>(bendingConstraints_.size());
    statistics_.branchAngleConstraintCount = static_cast<int>(branchAngleConstraints_.size());

    statistics_.maxLengthError = 0.0f;
    statistics_.meanLengthError = 0.0f;
    statistics_.maxBendingError = 0.0f;
    statistics_.meanBendingError = 0.0f;
    statistics_.maxBranchAngleErrorRadians = 0.0f;
    statistics_.meanBranchAngleErrorRadians = 0.0f;

    float lengthTotal = 0.0f;
    int lengthCount = 0;
    for (const PlantLengthConstraint& constraint : lengthConstraints_) {
        if (constraint.parentIndex < 0 || constraint.childIndex < 0 ||
            constraint.parentIndex >= static_cast<int>(massPoints_.size()) ||
            constraint.childIndex >= static_cast<int>(massPoints_.size())) continue;
        const float currentLength = (massPoints_[constraint.childIndex].position -
                                     massPoints_[constraint.parentIndex].position).norm();
        const float error = std::abs(currentLength - constraint.restLength);
        statistics_.maxLengthError = std::max(statistics_.maxLengthError, error);
        lengthTotal += error;
        ++lengthCount;
    }
    statistics_.meanLengthError = lengthCount > 0 ? lengthTotal / static_cast<float>(lengthCount) : 0.0f;

    float bendTotal = 0.0f;
    int bendCount = 0;
    for (const PlantBendingConstraint& constraint : bendingConstraints_) {
        if (constraint.baseIndex < 0 || constraint.tipIndex < 0 ||
            constraint.baseIndex >= static_cast<int>(massPoints_.size()) ||
            constraint.tipIndex >= static_cast<int>(massPoints_.size())) continue;
        const float chord = (massPoints_[constraint.tipIndex].position -
                             massPoints_[constraint.baseIndex].position).norm();
        const float error = std::abs(chord - constraint.restChordLength);
        statistics_.maxBendingError = std::max(statistics_.maxBendingError, error);
        bendTotal += error;
        ++bendCount;
    }
    statistics_.meanBendingError = bendCount > 0 ? bendTotal / static_cast<float>(bendCount) : 0.0f;

    float angleTotal = 0.0f;
    int angleCount = 0;
    for (const PlantBranchAngleConstraint& constraint : branchAngleConstraints_) {
        if (constraint.parentIndex < 0 || constraint.firstChildIndex < 0 || constraint.secondChildIndex < 0 ||
            constraint.parentIndex >= static_cast<int>(massPoints_.size()) ||
            constraint.firstChildIndex >= static_cast<int>(massPoints_.size()) ||
            constraint.secondChildIndex >= static_cast<int>(massPoints_.size())) continue;
        const Vec3 first = massPoints_[constraint.firstChildIndex].position -
                           massPoints_[constraint.parentIndex].position;
        const Vec3 second = massPoints_[constraint.secondChildIndex].position -
                            massPoints_[constraint.parentIndex].position;
        const float error = std::abs(includedAngle(first, second) - constraint.restAngleRadians);
        statistics_.maxBranchAngleErrorRadians = std::max(statistics_.maxBranchAngleErrorRadians, error);
        angleTotal += error;
        ++angleCount;
    }
    statistics_.meanBranchAngleErrorRadians =
        angleCount > 0 ? angleTotal / static_cast<float>(angleCount) : 0.0f;
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

    snapshot.bendingConstraints.reserve(bendingConstraints_.size());
    for (const PlantBendingConstraint& constraint : bendingConstraints_) {
        if (constraint.baseIndex < 0 || constraint.jointIndex < 0 || constraint.tipIndex < 0 ||
            constraint.baseIndex >= static_cast<int>(massPoints_.size()) ||
            constraint.jointIndex >= static_cast<int>(massPoints_.size()) ||
            constraint.tipIndex >= static_cast<int>(massPoints_.size())) continue;
        const Vec3& base = massPoints_[constraint.baseIndex].position;
        const Vec3& joint = massPoints_[constraint.jointIndex].position;
        const Vec3& tip = massPoints_[constraint.tipIndex].position;
        const float rest = std::max(kMinimumRestLength, constraint.restChordLength);
        snapshot.bendingConstraints.push_back({
            base, joint, tip, std::abs((tip - base).norm() - constraint.restChordLength) / rest});
    }

    snapshot.branchAngleConstraints.reserve(branchAngleConstraints_.size());
    for (const PlantBranchAngleConstraint& constraint : branchAngleConstraints_) {
        if (constraint.parentIndex < 0 || constraint.firstChildIndex < 0 || constraint.secondChildIndex < 0 ||
            constraint.parentIndex >= static_cast<int>(massPoints_.size()) ||
            constraint.firstChildIndex >= static_cast<int>(massPoints_.size()) ||
            constraint.secondChildIndex >= static_cast<int>(massPoints_.size())) continue;
        const Vec3& parent = massPoints_[constraint.parentIndex].position;
        const Vec3& first = massPoints_[constraint.firstChildIndex].position;
        const Vec3& second = massPoints_[constraint.secondChildIndex].position;
        snapshot.branchAngleConstraints.push_back({
            parent, first, second,
            std::abs(includedAngle(first - parent, second - parent) - constraint.restAngleRadians)});
    }
    return snapshot;
}
