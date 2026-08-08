// ============================================================================
// PlantPhysicsDemo - 第13周质量点与PBD长度约束回归检查
// ============================================================================
#include <cmath>

#include <QCoreApplication>
#include <QDebug>

#include "Physics/PlantPhysicsSolver.h"
#include "Plant/PlantModel.h"

namespace {
float segmentLength(const PlantMassPoint& a, const PlantMassPoint& b) {
    return (b.position - a.position).norm();
}
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    PlantModel plant;
    PlantNode* root = plant.createRootNode(Vec3::Zero(), Vec3::UnitY(), 0.18f);
    if (!root) return 1;
    PlantNode* trunk = plant.addNode(root->id, Vec3(0.0f, 0.8f, 0.0f), Vec3::UnitY(), 0.12f, 0.8f);
    PlantNode* tip = trunk ? plant.addNode(trunk->id, Vec3(0.0f, 1.5f, 0.0f), Vec3::UnitY(), 0.07f, 0.7f) : nullptr;
    if (!trunk || !tip) return 2;

    PlantPhysicsSettings settings;
    settings.gravity = Vec3::Zero();
    settings.solverIterations = 36;
    settings.defaultLengthStiffness = 1.0f;
    PlantPhysicsSolver solver(settings);
    QString error;
    if (!solver.rebuildFromPlant(plant, &error)) {
        qCritical().noquote() << error;
        return 3;
    }

    if (solver.massPoints().size() != plant.nodeCount() ||
        solver.lengthConstraints().size() != plant.nodeCount() - 1 ||
        !solver.massPoints().front().fixed || solver.massPoints().front().inverseMass != 0.0f) {
        qCritical().noquote() << "FAILED: node-to-particle conversion or fixed root invariant.";
        return 4;
    }

    const Vec3 rootAnchor = solver.massPoints().front().position;
    if (!solver.setParticlePosition(tip->id, Vec3(0.72f, 1.92f, -0.31f))) return 5;
    solver.step(0.0f);

    const auto& points = solver.massPoints();
    for (const PlantLengthConstraint& constraint : solver.lengthConstraints()) {
        const float errorLength = std::abs(segmentLength(points[constraint.parentIndex], points[constraint.childIndex]) - constraint.restLength);
        if (errorLength > 2.0e-3f) {
            qCritical().noquote() << "FAILED: length constraint residual" << errorLength;
            return 6;
        }
    }
    if ((solver.massPoints().front().position - rootAnchor).norm() > 1.0e-7f) {
        qCritical().noquote() << "FAILED: fixed root moved.";
        return 7;
    }

    if (!solver.applyToPlant(&plant, &error) || !plant.validate(&error)) {
        qCritical().noquote() << "FAILED: physics application invalidated plant:" << error;
        return 8;
    }

    const PlantPhysicsDebugSnapshot debug = solver.debugSnapshot();
    if (debug.points.size() != plant.nodeCount() || debug.lengthSegments.size() != plant.nodeCount() - 1) {
        qCritical().noquote() << "FAILED: debug snapshot is incomplete.";
        return 9;
    }

    qInfo().noquote() << QStringLiteral("Plant physics length check passed: particles=%1 constraints=%2 maxError=%3")
                             .arg(debug.statistics.particleCount)
                             .arg(debug.statistics.lengthConstraintCount)
                             .arg(debug.statistics.maxLengthError, 0, 'g', 4);
    return 0;
}