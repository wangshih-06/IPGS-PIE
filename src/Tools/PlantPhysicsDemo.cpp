// ============================================================================
// PlantPhysicsDemo - Week 13 PBD structural constraint regression check
// ============================================================================
#include <cmath>
#include <vector>

#include <QCoreApplication>
#include <QDebug>

#include "Physics/PlantPhysicsSolver.h"
#include "Plant/PlantModel.h"

namespace {
float includedAngle(const Vec3& first, const Vec3& second) {
    const float firstLength = first.norm();
    const float secondLength = second.norm();
    if (firstLength < 1.0e-6f || secondLength < 1.0e-6f) return 0.0f;
    const float cosine = std::max(-1.0f, std::min(1.0f, first.dot(second) / (firstLength * secondLength)));
    return std::acos(cosine);
}

bool hasValidLengths(const PlantPhysicsSolver& solver, float tolerance) {
    const auto& points = solver.massPoints();
    for (const PlantLengthConstraint& constraint : solver.lengthConstraints()) {
        const float current = (points[constraint.childIndex].position -
                               points[constraint.parentIndex].position).norm();
        if (std::abs(current - constraint.restLength) > tolerance) return false;
    }
    return true;
}
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    // The test tree has a bent trunk and two branches, providing all Week 13
    // constraint families: length, bending and sibling branch-angle.
    PlantModel plant;
    PlantNode* root = plant.createRootNode(Vec3::Zero(), Vec3::UnitY(), 0.18f);
    PlantNode* trunk = root ? plant.addNode(root->id, Vec3(0.10f, 0.75f, 0.0f),
                                            Vec3(0.13f, 0.99f, 0.0f), 0.12f, 0.76f)
                            : nullptr;
    PlantNode* junction = trunk ? plant.addNode(trunk->id, Vec3(0.25f, 1.40f, 0.0f),
                                                 Vec3(0.22f, 0.98f, 0.0f), 0.09f, 0.67f)
                                : nullptr;
    PlantNode* left = junction ? plant.addNode(junction->id, Vec3(-0.48f, 1.95f, 0.20f),
                                                Vec3(-0.78f, 0.59f, 0.21f), 0.06f, 0.94f)
                               : nullptr;
    PlantNode* right = junction ? plant.addNode(junction->id, Vec3(0.80f, 1.84f, -0.23f),
                                                 Vec3(0.82f, 0.52f, -0.24f), 0.06f, 0.86f)
                                : nullptr;
    if (!root || !trunk || !junction || !left || !right) return 1;

    PlantPhysicsSettings settings;
    settings.gravity = Vec3::Zero();
    settings.solverIterations = 768;
    settings.defaultLengthStiffness = 1.0f;
    settings.defaultBendingStiffness = 1.0f;
    settings.defaultBranchAngleStiffness = 1.0f;
    PlantPhysicsSolver solver(settings);
    QString error;
    if (!solver.rebuildFromPlant(plant, &error)) {
        qCritical().noquote() << error;
        return 2;
    }

    const PlantPhysicsStatistics initial = solver.statistics();
    if (solver.massPoints().size() != plant.nodeCount() ||
        initial.lengthConstraintCount != static_cast<int>(plant.nodeCount()) - 1 ||
        initial.bendingConstraintCount != 3 || initial.branchAngleConstraintCount != 1 ||
        !solver.massPoints().front().fixed || solver.massPoints().front().inverseMass != 0.0f) {
        qCritical().noquote() << "FAILED: invalid physics topology or fixed root invariant.";
        return 3;
    }

    const Vec3 rootAnchor = solver.massPoints().front().position;
    if (!solver.setParticlePosition(left->id, Vec3(0.18f, 2.35f, 0.72f)) ||
        !solver.setParticlePosition(right->id, Vec3(1.37f, 1.16f, -0.62f))) {
        qCritical().noquote() << "FAILED: cannot perturb physics particles.";
        return 4;
    }
    solver.step(0.0f);

    const PlantPhysicsStatistics result = solver.statistics();
    if (!hasValidLengths(solver, 2.0e-3f) || result.maxBendingError > 2.5e-3f ||
        result.maxBranchAngleErrorRadians > 4.0e-3f) {
        qCritical().noquote() << QStringLiteral(
            "FAILED: structural constraints did not converge: length=%1 bend=%2 angle=%3")
            .arg(result.maxLengthError, 0, 'g', 4)
            .arg(result.maxBendingError, 0, 'g', 4)
            .arg(result.maxBranchAngleErrorRadians, 0, 'g', 4);
        return 5;
    }
    if ((solver.massPoints().front().position - rootAnchor).norm() > 1.0e-7f) {
        qCritical().noquote() << "FAILED: fixed root moved.";
        return 6;
    }

    const auto& angle = solver.branchAngleConstraints().front();
    const auto& points = solver.massPoints();
    const float solvedAngle = includedAngle(
        points[angle.firstChildIndex].position - points[angle.parentIndex].position,
        points[angle.secondChildIndex].position - points[angle.parentIndex].position);
    if (std::abs(solvedAngle - angle.restAngleRadians) > 4.0e-3f) {
        qCritical().noquote() << "FAILED: branch angle did not recover its rest state.";
        return 7;
    }

    if (!solver.applyToPlant(&plant, &error) || !plant.validate(&error)) {
        qCritical().noquote() << "FAILED: physics application invalidated plant:" << error;
        return 8;
    }

    const PlantPhysicsDebugSnapshot debug = solver.debugSnapshot();
    if (debug.points.size() != plant.nodeCount() ||
        debug.lengthSegments.size() != plant.nodeCount() - 1 ||
        debug.bendingConstraints.size() != static_cast<std::size_t>(initial.bendingConstraintCount) ||
        debug.branchAngleConstraints.size() != static_cast<std::size_t>(initial.branchAngleConstraintCount)) {
        qCritical().noquote() << "FAILED: debug snapshot is incomplete.";
        return 9;
    }

    // Regression for high-fanout junctions: six sibling arms would formerly
    // allocate 15 all-pairs angle constraints. The sparse ring needs only six.
    PlantModel fanPlant;
    PlantNode* fanRoot = fanPlant.createRootNode(Vec3::Zero(), Vec3::UnitY(), 0.18f);
    std::vector<PlantNode*> fanChildren;
    for (int child = 0; fanRoot && child < 6; ++child) {
        const float radians = 2.0f * 3.14159265358979323846f * static_cast<float>(child) / 6.0f;
        const Vec3 direction = Vec3(std::cos(radians), 0.65f, std::sin(radians)).normalized();
        fanChildren.push_back(fanPlant.addNode(fanRoot->id, direction, direction, 0.055f, 0.9f));
    }
    if (!fanRoot || fanChildren.size() != 6 ||
        std::any_of(fanChildren.begin(), fanChildren.end(), [](const PlantNode* node) { return node == nullptr; })) {
        qCritical().noquote() << "FAILED: cannot create high-fanout regression plant.";
        return 10;
    }
    PlantPhysicsSolver fanSolver(settings);
    if (!fanSolver.rebuildFromPlant(fanPlant, &error) ||
        fanSolver.statistics().branchAngleConstraintCount != 6 ||
        fanSolver.statistics().branchAngleConstraintCount >= 15) {
        qCritical().noquote() << "FAILED: high-fanout angle constraints were not sparsified.";
        return 11;
    }

    qInfo().noquote() << QStringLiteral(
        "Plant physics constraints passed: particles=%1 length=%2 bend=%3 angle=%4 maxErrors=(%5, %6, %7)")
        .arg(result.particleCount)
        .arg(result.lengthConstraintCount)
        .arg(result.bendingConstraintCount)
        .arg(result.branchAngleConstraintCount)
        .arg(result.maxLengthError, 0, 'g', 4)
        .arg(result.maxBendingError, 0, 'g', 4)
        .arg(result.maxBranchAngleErrorRadians, 0, 'g', 4);
    return 0;
}
