// ============================================================================
// SimulationEngine - environment, plant state bridge, and growth time controller
// 第11周：向光性与向地性，环境资源状态传递
// ============================================================================
#include "Engine/SimulationEngine.h"

#include <algorithm>
#include <utility>

#include <QDebug>

#include "Algorithm/TurtleInterpreter.h"

SimulationEngine::SimulationEngine(QObject* parent)
    : QObject(parent) {
    growthClock_.setParent(this);

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
    plantModel_.age = 0.0f;
    plantModel_.lifeStage = PlantLifeStage::Seedling;
    plantModel_.growthState = PlantGrowthState::Paused;
    plantModel_.setRootNode(TurtleInterpreter().interpret(plantProgram_, rule));

    MetaballFieldSettings fieldSettings;
    fieldSettings.isoThreshold = 0.5f;
    fieldSettings.influenceScale = 2.0f;
    fieldSettings.jointSmoothness = 0.65f;
    metaballSettings_ = fieldSettings;
    metaballField_.rebuildFromPlant(plantModel_, fieldSettings);
    initialPlantSnapshot_ = plantModel_.toJson();

    connect(&growthClock_, &GrowthClock::tickProduced,
            this, &SimulationEngine::onGrowthTickProduced);
    connect(&growthClock_, &GrowthClock::logMessage,
            this, &SimulationEngine::onGrowthClockLog);
    connect(&growthClock_, &GrowthClock::modeChanged,
            this, [this](int) {
                emit growthLogMessage(QStringLiteral("Mode -> %1")
                    .arg(growthClock_.timeline().describeMode()));
            });

    qInfo().noquote() << QStringLiteral("L-System ready: iterations=3, symbols=%1, nodes=%2, max generation=%3")
                             .arg(plantProgram_.size())
                             .arg(static_cast<qulonglong>(plantNodeCount()))
                             .arg(plantRoot() ? plantRoot()->maxGeneration() : 0);
    qInfo().noquote() << QStringLiteral("Metaball field ready: node sources=%1, segment sources=%2, threshold=%3")
                             .arg(static_cast<qulonglong>(metaballField_.nodeSources().size()))
                             .arg(static_cast<qulonglong>(metaballField_.segmentSources().size()))
                             .arg(metaballField_.isoThreshold(), 0, 'f', 2);
    qInfo().noquote() << QStringLiteral("Growth timeline ready: thresholds=[%1, %2, %3], speed=%4x")
                             .arg(growthClock_.timeline().thresholds().seedlingEndYear, 0, 'f', 1)
                             .arg(growthClock_.timeline().thresholds().vegetativeEndYear, 0, 'f', 1)
                             .arg(growthClock_.timeline().thresholds().matureEndYear, 0, 'f', 1)
                             .arg(growthClock_.timeline().speed(), 0, 'f', 1);

    rebuildPlantSurface();
}

// ============================================================================
// 访问器
// ============================================================================
const EnvironmentParams& SimulationEngine::environment() const { return environment_; }
EnvironmentParams& SimulationEngine::mutableEnvironment() { return environment_; }
const PlantModel& SimulationEngine::plantModel() const { return plantModel_; }
const PlantNode* SimulationEngine::plantRoot() const { return plantModel_.rootNode(); }
const QString& SimulationEngine::plantProgram() const { return plantProgram_; }
std::size_t SimulationEngine::plantNodeCount() const { return plantModel_.nodeCount(); }
const MetaballField& SimulationEngine::metaballField() const { return metaballField_; }

// ============================================================================
// 第11周：环境光照与向性槽
// ============================================================================
void SimulationEngine::setLightIntensity(float intensity) {
    environment_.lightIntensity = qBound(0.0f, intensity, 1.0f);
    if (!environment_.lightSources.empty()) {
        environment_.lightSources[0].intensity = environment_.lightIntensity;
    }
    qInfo().noquote() << QStringLiteral("Light intensity = %1").arg(environment_.lightIntensity, 0, 'f', 2);
    emit logMessage(QStringLiteral("Environment Updated"));
    emit environmentUpdated(environment_.lightIntensity);
}

void SimulationEngine::setPhototropismWeight(float weight) {
    environment_.phototropismWeight = qBound(0.0f, weight, 1.0f);
    qInfo().noquote() << QStringLiteral("Phototropism weight = %1").arg(environment_.phototropismWeight, 0, 'f', 2);
    emit tropismUpdated(environment_.phototropismWeight, environment_.gravitropismWeight);
}

void SimulationEngine::setGravitropismWeight(float weight) {
    environment_.gravitropismWeight = qBound(0.0f, weight, 1.0f);
    qInfo().noquote() << QStringLiteral("Gravitropism weight = %1").arg(environment_.gravitropismWeight, 0, 'f', 2);
    emit tropismUpdated(environment_.phototropismWeight, environment_.gravitropismWeight);
}

void SimulationEngine::setLightSourcePosition(int lightId, float x, float y, float z) {
    for (auto& light : environment_.lightSources) {
        if (light.id == lightId) {
            light.position = Vec3(x, y, z);
            if (light.type == LightType::Directional) {
                light.direction = (-light.position).normalized();
            }
            qInfo().noquote() << QStringLiteral("Light #%1 position set to (%2, %3, %4)")
                                     .arg(lightId).arg(x, 0, 'f', 1).arg(y, 0, 'f', 1).arg(z, 0, 'f', 1);
            emit environmentUpdated(environment_.lightIntensity);
            break;
        }
    }
}

// ============================================================================
// 生长时间轴 slots
// ============================================================================
void SimulationEngine::startGrowth()  { growthClock_.start(); }
void SimulationEngine::pauseGrowth()  { growthClock_.pause(); }
void SimulationEngine::resumeGrowth() { growthClock_.resume(); }
void SimulationEngine::resetGrowth(float initialYears) {
    dynamicBranching_.reset();
    growthEvents_.clear();
    keyframes_.clear();
    nextAutoKeyframeAge_ = std::max(1.0f, initialYears + 1.0f);
    PlantModel restored;
    QString error;
    if (!initialPlantSnapshot_.isEmpty() && PlantModel::fromJson(initialPlantSnapshot_, &restored, &error)) {
        plantModel_ = std::move(restored);
    }
    growthClock_.reset(initialYears);
    rebuildMetaballField();
    rebuildPlantSurface();
    emit plantSurfaceUpdated(plantSurface_);
}
void SimulationEngine::setGrowthSpeed(float speed) { growthClock_.setSpeed(speed); }
void SimulationEngine::stepGrowth(float deltaYears) { growthClock_.stepOnce(deltaYears); }

// ============================================================================
// 内部槽
// ============================================================================
void SimulationEngine::onGrowthTickProduced(const GrowthSample& sample) {
    const float previousAge = plantModel_.age;
    if (sample.age + 1.0e-4f < previousAge && !initialPlantSnapshot_.isEmpty()) {
        PlantModel restored;
        QString error;
        if (PlantModel::fromJson(initialPlantSnapshot_, &restored, &error)) {
            plantModel_ = std::move(restored);
            dynamicBranching_.reset();
            growthEvents_.clear();
            keyframes_.clear();
            nextAutoKeyframeAge_ = 1.0f;
        } else {
            qWarning().noquote() << error;
        }
    }
    const float deltaYears = sample.age - plantModel_.age;
    if (deltaYears > 0.0f) {
        plantModel_.advanceAge(deltaYears);
        const std::size_t eventCountBefore = growthEvents_.size();
        dynamicBranching_.update(plantModel_, sample.age, deltaYears, resourceState(), &growthEvents_);
        for (const GrowthEvent& event : growthEvents_.since(eventCountBefore)) {
            emit growthEventAdded(toString(event.type), event.age, event.nodeId, event.leafId);
        }
    }
    plantModel_.applyGrowthSample(sample);
    environment_.time = sample.age;
    if (sample.age != previousAge) {
        rebuildMetaballField();
        rebuildPlantSurface();
        emit plantSurfaceUpdated(plantSurface_);
    }
    while (sample.age >= nextAutoKeyframeAge_) {
        captureGrowthKeyframe(QStringLiteral("auto_%1y").arg(nextAutoKeyframeAge_, 0, 'f', 2));
        nextAutoKeyframeAge_ += 1.0f;
    }
    emit growthUpdated(buildReport(sample));
}

void SimulationEngine::captureGrowthKeyframe(const QString& label) {
    const GrowthKeyframe& keyframe = keyframes_.capture(plantModel_, plantModel_.age, label, static_cast<int>(growthEvents_.size()));
    growthEvents_.record(plantModel_.age, GrowthEvent::Type::KeyframeCaptured, -1, -1, -1, keyframe.label);
    emit growthKeyframeCaptured(keyframe.id, keyframe.age);
}

bool SimulationEngine::saveGrowthKeyframes(const QString& filePath, QString* error) const {
    return keyframes_.saveJson(filePath, error);
}

void SimulationEngine::onGrowthClockLog(const QString& message) {
    qInfo().noquote() << QStringLiteral("[Growth] %1").arg(message);
    emit growthLogMessage(message);
}

void SimulationEngine::rebuildMetaballField() {
    metaballField_.rebuildFromPlant(plantModel_, metaballSettings_);
}

void SimulationEngine::rebuildPlantSurface() {
    const ScalarFieldGrid grid = metaballField_.sampleGrid(0.06f);
    plantSurface_ = MarchingCubes::extract(grid, metaballField_.isoThreshold());
}

void SimulationEngine::collectAllNodePositions(const PlantNode* node, std::vector<Vec3>* positions) const {
    if (!node || !positions) return;
    positions->push_back(node->position);
    for (const auto& child : node->children) {
        collectAllNodePositions(child.get(), positions);
    }
}

GrowthResourceState SimulationEngine::resourceState() const {
    GrowthResourceState state;
    state.light = environment_.lightIntensity;
    state.moisture = environment_.moisture;
    state.nutrition = environment_.nutrition;
    state.temperature = environment_.temperature;
    state.wind = environment_.windIntensity;
    state.environment = environment_;
    collectAllNodePositions(plantModel_.rootNode(), &state.allNodePositions);
    return state;
}

GrowthStateReport SimulationEngine::buildReport(const GrowthSample& sample) const {
    GrowthStateReport report;
    report.age         = sample.age;
    report.lifeStage   = sample.lifeStage;
    report.growthState = plantModel_.growthState;
    report.lengthScale = sample.lengthScale;
    report.radiusScale = sample.radiusScale;
    report.leafScale   = sample.leafScale;
    report.speed       = growthClock_.timeline().speed();
    report.mode        = static_cast<int>(growthClock_.timeline().mode());
    report.nodeCount   = static_cast<int>(plantModel_.nodeCount());
    report.branchCount = static_cast<int>(plantModel_.branches().size());
    report.leafCount   = static_cast<int>(plantModel_.leaves().size());
    return report;
}
