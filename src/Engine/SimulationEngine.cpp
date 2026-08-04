// ============================================================================
// SimulationEngine implementation
// ============================================================================
#include "Engine/SimulationEngine.h"

#include <QDebug>

#include "Algorithm/TurtleInterpreter.h"

SimulationEngine::SimulationEngine(QObject* parent)
    : QObject(parent) {
    lSystem_ = LSystem::treePreset();
    plantProgram_ = lSystem_.generate(3);

    PlantRule rule;
    rule.angleDegrees = 23.0f;
    rule.length = 0.42f;
    rule.radius = 0.16f;
    rule.lengthScale = 0.9f;
    rule.radiusScale = 0.7f;

    plantModel_.id = 1;
    plantModel_.name = QStringLiteral("L-System Tree");
    plantModel_.species = QStringLiteral("Procedural Demo");
    plantModel_.age = 1.0f;
    plantModel_.lifeStage = PlantLifeStage::Vegetative;
    plantModel_.growthState = PlantGrowthState::Active;
    plantModel_.setRootNode(TurtleInterpreter().interpret(plantProgram_, rule));

    MetaballFieldSettings fieldSettings;
    fieldSettings.isoThreshold = 0.5f;
    fieldSettings.influenceScale = 2.0f;
    fieldSettings.jointSmoothness = 0.65f;
    metaballField_.rebuildFromPlant(plantModel_, fieldSettings);

    qInfo().noquote() << QStringLiteral("L-System ready: iterations=3, symbols=%1, nodes=%2, max generation=%3")
                             .arg(plantProgram_.size())
                             .arg(static_cast<qulonglong>(plantNodeCount()))
                             .arg(plantRoot() ? plantRoot()->maxGeneration() : 0);
    qInfo().noquote() << QStringLiteral("Metaball field ready: node sources=%1, segment sources=%2, threshold=%3")
                             .arg(static_cast<qulonglong>(metaballField_.nodeSources().size()))
                             .arg(static_cast<qulonglong>(metaballField_.segmentSources().size()))
                             .arg(metaballField_.isoThreshold(), 0, 'f', 2);
}

const EnvironmentParams& SimulationEngine::environment() const {
    return environment_;
}

const PlantModel& SimulationEngine::plantModel() const {
    return plantModel_;
}

const PlantNode* SimulationEngine::plantRoot() const {
    return plantModel_.rootNode();
}

const QString& SimulationEngine::plantProgram() const {
    return plantProgram_;
}

std::size_t SimulationEngine::plantNodeCount() const {
    return plantModel_.nodeCount();
}

const MetaballField& SimulationEngine::metaballField() const {
    return metaballField_;
}

void SimulationEngine::setLightIntensity(float intensity) {
    environment_.lightIntensity = qBound(0.0f, intensity, 1.0f);
    qInfo().noquote() << QStringLiteral("Light = %1").arg(environment_.lightIntensity, 0, 'f', 1);
    qInfo().noquote() << QStringLiteral("Environment Updated");
    emit logMessage(QStringLiteral("Environment Updated"));
    emit environmentUpdated(environment_.lightIntensity);
}

