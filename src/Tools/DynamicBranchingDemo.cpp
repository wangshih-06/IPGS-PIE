#include <QCoreApplication>
#include <QDebug>

#include "Engine/DynamicBranchingSystem.h"
#include "Engine/GrowthKeyframeStore.h"

namespace {
bool hasEvent(const GrowthEventManager& events, GrowthEvent::Type type) {
    for (const GrowthEvent& event : events.events()) {
        if (event.type == type) return true;
    }
    return false;
}
}

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    PlantModel plant;
    PlantNode* root = plant.createRootNode(Vec3::Zero(), Vec3::UnitY(), 0.18f, 0.0f);
    if (!root) return 1;
    root->length = 1.0f;
    plant.captureBaselines();

    DynamicBranchingSettings settings;
    settings.branchStartAge = 0.5f;
    settings.branchInterval = 0.5f;
    settings.branchProbability = 1.0f;
    settings.resourceExponent = 0.01f;
    settings.leafSproutAge = 0.5f;
    settings.leafProbability = 1.0f;
    settings.maxDepth = 2;
    settings.maxTotalBranches = 4;
    settings.smoothingYears = 2.0f;
    settings.healthDecayRate = 0.8f;

    DynamicBranchingSystem system(settings);
    GrowthEventManager events;
    GrowthResourceState resources;
    resources.light = 1.0f;
    resources.moisture = 1.0f;
    resources.nutrition = 1.0f;
    resources.temperature = 22.0f;
    resources.wind = 0.0f;

    plant.advanceAge(0.5f);
    system.update(plant, plant.age, 0.5f, resources, &events);
    GrowthSample sample;
    sample.age = plant.age;
    sample.lengthScale = 1.0f;
    sample.radiusScale = 1.0f;
    sample.leafScale = Vec2::Ones();
    plant.applyGrowthSample(sample);

    if (plant.branches().empty() || plant.leaves().empty() ||
        !hasEvent(events, GrowthEvent::Type::BranchCreated) ||
        !hasEvent(events, GrowthEvent::Type::LeafSprouted)) {
        return 2;
    }
    const PlantNode* firstBranch = plant.rootNode()->children.front().get();
    if (!firstBranch || firstBranch->growthProgress >= 1.0f ||
        plant.leaves().front().growthProgress >= 1.0f) {
        return 3;
    }

    GrowthKeyframeStore keyframes;
    keyframes.capture(plant, plant.age, QStringLiteral("healthy"), static_cast<int>(events.size()));
    PlantModel restored;
    QString error;
    if (!keyframes.restore(0, &restored, &error)) {
        qWarning().noquote() << error;
        return 4;
    }
    if (restored.nodeCount() != plant.nodeCount() || restored.leaves().size() != plant.leaves().size()) {
        return 5;
    }

    GrowthResourceState drought;
    drought.light = 0.0f;
    drought.moisture = 0.0f;
    drought.nutrition = 0.0f;
    for (int step = 0; step < 40; ++step) {
        plant.advanceAge(0.5f);
        system.update(plant, plant.age, 0.5f, drought, &events);
    }
    if (!hasEvent(events, GrowthEvent::Type::BranchDied)) return 6;

    qInfo().noquote() << QStringLiteral("Dynamic branching check passed: branches=%1 leaves=%2 events=%3")
                             .arg(static_cast<qulonglong>(plant.branches().size()))
                             .arg(static_cast<qulonglong>(plant.leaves().size()))
                             .arg(static_cast<qulonglong>(events.size()));
    return 0;
}
