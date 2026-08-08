// ============================================================================
// SimulationEngine - environment, plant state bridge, and growth time controller
// 第11周：向光性与向地性，环境资源状态传递
// ============================================================================
#include "Engine/SimulationEngine.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <QDebug>
#include <QJsonArray>

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
    growthData_.capture(plantModel_);

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
    growthData_.clear();
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
    captureGrowthFrameIfNeeded();
}
void SimulationEngine::setGrowthSpeed(float speed) {
    growthClock_.setSpeed(speed);
    // Confirm remote speed changes immediately instead of waiting for the next
    // realtime tick (which may never arrive while the simulation is paused).
    emit growthUpdated(buildReport(growthClock_.timeline().sample(growthClock_.timeline().currentAge())));
}
void SimulationEngine::stepGrowth(float deltaYears) { growthClock_.stepOnce(deltaYears); }

void SimulationEngine::seekGrowth(float age) {
    const GrowthDataFrame* frame = growthData_.nearest(std::max(0.0f, age));
    if (!frame) {
        growthClock_.reset(std::max(0.0f, age));
        return;
    }
    growthClock_.pause();
    PlantModel restored;
    QString error;
    if (!PlantModel::fromJson(frame->plantState, &restored, &error)) {
        emit growthLogMessage(QStringLiteral("Replay restore failed: %1").arg(error));
        return;
    }
    restoringRecordedFrame_ = true;
    plantModel_ = std::move(restored);
    dynamicBranching_.reset();
    growthClock_.reset(frame->age);
    environment_.time = frame->age;
    rebuildMetaballField();
    // Timeline scrubbing must stay responsive; use a coarser preview surface.
    // The next live tick rebuilds the normal-resolution surface (0.18m grid).
    rebuildPlantSurface(0.24f);
    restoringRecordedFrame_ = false;
    emit plantSurfaceUpdated(plantSurface_);
    emit growthUpdated(buildReport(growthClock_.timeline().sample(frame->age)));
    emit growthLogMessage(QStringLiteral("Replay seek -> %1y").arg(frame->age, 0, 'f', 2));
}

void SimulationEngine::jumpToGrowthStage(const QString& stage) {
    const QString normalized = stage.trimmed().toLower();
    const GrowthAxisThresholds thresholds = growthClock_.timeline().thresholds();
    float targetAge = 0.0f;
    if (normalized == QStringLiteral("seed") || normalized == QStringLiteral("seedling")) {
        targetAge = 0.0f;
    } else if (normalized == QStringLiteral("sprout") || normalized == QStringLiteral("vegetative")) {
        targetAge = thresholds.seedlingEndYear;
    } else if (normalized == QStringLiteral("growing")) {
        targetAge = thresholds.vegetativeEndYear;
    } else if (normalized == QStringLiteral("mature")) {
        // A representative point inside the mature stage rather than its start.
        targetAge = thresholds.vegetativeEndYear +
                    (thresholds.matureEndYear - thresholds.vegetativeEndYear) / 3.0f;
    } else if (normalized == QStringLiteral("aging") || normalized == QStringLiteral("completed")) {
        targetAge = thresholds.matureEndYear;
    } else {
        return;
    }
    seekGrowth(targetAge);
}

void SimulationEngine::requestGrowthData() {
    // The browser needs the full skeleton snapshots to keep the viewport in
    // lock-step with the metrics timeline. File downloads can still use the
    // lightweight metrics-only archive.
    QJsonObject data = growthData_.toJson();
    const GrowthAxisThresholds thresholds = growthClock_.timeline().thresholds();
    data.insert("keyStages", QJsonArray{
        QJsonObject{{"key", "seedling"}, {"label", "Seedling"}, {"age", 0.0}},
        QJsonObject{{"key", "vegetative"}, {"label", "Vegetative"}, {"age", static_cast<double>(thresholds.seedlingEndYear)}},
        QJsonObject{{"key", "mature"}, {"label", "Mature"}, {"age", static_cast<double>(thresholds.vegetativeEndYear)}},
        QJsonObject{{"key", "completed"}, {"label", "Completed"}, {"age", static_cast<double>(thresholds.matureEndYear)}}
    });
    emit growthDataAvailable(data);
}

// ============================================================================
// 内部槽
// ============================================================================
void SimulationEngine::onGrowthTickProduced(const GrowthSample& sample) {
    // seekGrowth() has already restored an exact PlantModel snapshot. Do not
    // apply the analytical growth scale again or the replayed geometry drifts.
    if (restoringRecordedFrame_) {
        environment_.time = sample.age;
        emit growthUpdated(buildReport(sample));
        return;
    }
    const float previousAge = plantModel_.age;
    if (sample.age + 1.0e-4f < previousAge && !initialPlantSnapshot_.isEmpty()) {
        PlantModel restored;
        QString error;
        if (PlantModel::fromJson(initialPlantSnapshot_, &restored, &error)) {
            plantModel_ = std::move(restored);
            dynamicBranching_.reset();
            growthEvents_.clear();
            keyframes_.clear();
            growthData_.truncateAfter(0.0f);
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
    if (!growthData_.empty() && sample.age + 1.0e-4f < growthData_.frames().back().age) {
        // Continuing from a replayed frame starts a new simulation branch.
        growthData_.truncateAfter(previousAge + 1.0e-4f);
    }
    captureGrowthFrameIfNeeded();
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

bool SimulationEngine::saveGrowthData(const QString& filePath, QString* error) const {
    return growthData_.saveJson(filePath, error);
}

bool SimulationEngine::saveGrowthMetricsCsv(const QString& filePath, QString* error) const {
    return growthData_.saveCsv(filePath, error);
}

void SimulationEngine::captureGrowthFrameIfNeeded() {
    if (restoringRecordedFrame_) return;
    if (!growthData_.empty()) {
        const GrowthDataFrame* latest = growthData_.at(growthData_.size() - 1);
        if (latest && std::abs(latest->age - plantModel_.age) < 1.0e-4f) return;
    }
    growthData_.capture(plantModel_);
}

void SimulationEngine::onGrowthClockLog(const QString& message) {
    qInfo().noquote() << QStringLiteral("[Growth] %1").arg(message);
    emit growthLogMessage(message);
}

void SimulationEngine::rebuildMetaballField() {
    metaballField_.rebuildFromPlant(plantModel_, metaballSettings_);
}

void SimulationEngine::rebuildPlantSurface(float requestedSpacing) {
    const ScalarFieldGrid grid = metaballField_.sampleGrid(requestedSpacing, 100000);
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
    report.metrics = GrowthDataRecorder::measure(plantModel_);
    report.recordedFrameCount = static_cast<int>(growthData_.size());
    report.recordedEndAge = growthData_.empty() ? 0.0f : growthData_.frames().back().age;
    report.plantState = plantModel_.toJson();
    return report;
}
