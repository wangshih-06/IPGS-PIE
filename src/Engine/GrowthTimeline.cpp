// ============================================================================
// GrowthTimeline - 时间轴实现
// ============================================================================
#include "Engine/GrowthTimeline.h"

#include <algorithm>
#include <cmath>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>

namespace {
constexpr int kSchemaVersion = 1;

QString playbackModeToString(GrowthPlaybackMode mode) {
    switch (mode) {
    case GrowthPlaybackMode::Realtime: return QStringLiteral("realtime");
    case GrowthPlaybackMode::Step:     return QStringLiteral("step");
    case GrowthPlaybackMode::Completed: return QStringLiteral("completed");
    case GrowthPlaybackMode::Paused:
    default: return QStringLiteral("paused");
    }
}

GrowthPlaybackMode playbackModeFromString(const QString& value) {
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("realtime"))   return GrowthPlaybackMode::Realtime;
    if (normalized == QStringLiteral("step"))       return GrowthPlaybackMode::Step;
    if (normalized == QStringLiteral("completed"))  return GrowthPlaybackMode::Completed;
    return GrowthPlaybackMode::Paused;
}

float clamp(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

float logistic(float age, float rate, float midpoint, float asymptote, float minimum) {
    // asymptote / (1 + exp(-rate * (age - midpoint)))
    const double exponent = -static_cast<double>(rate) *
                            (static_cast<double>(age) - static_cast<double>(midpoint));
    const double scale = static_cast<double>(asymptote) / (1.0 + std::exp(exponent));
    return std::max(minimum, static_cast<float>(scale));
}

void setError(QString* error, const QString& message) {
    if (error) *error = message;
}
}

QString toString(GrowthPlaybackMode mode) { return playbackModeToString(mode); }
GrowthPlaybackMode growthPlaybackModeFromString(const QString& value) { return playbackModeFromString(value); }

// ============================================================================
// 构造
// ============================================================================
GrowthTimeline::GrowthTimeline() = default;

GrowthTimeline::GrowthTimeline(const GrowthAxisThresholds& thresholds,
                               const GrowthShapeParams& shape,
                               float initialYears)
    : thresholds_(thresholds), shape_(shape),
      currentAge_(std::max(0.0f, initialYears)) {
    recomputeModeByAge();
}

// ============================================================================
// sample() - 纯函数；任何相同 age 必然返回完全相同结果
// ============================================================================
GrowthSample GrowthTimeline::sample(float ageYears) const {
    GrowthSample s;
    s.age = std::max(0.0f, ageYears);
    s.lifeStage = lifeStageAt(s.age);

    s.lengthScale = logistic(s.age, shape_.lengthLogisticRate, shape_.lengthMidpoint,
                             shape_.lengthAsymptoteRatio, shape_.minimumScale);

    const float radiusBase = logistic(s.age, shape_.radiusLogisticRate, shape_.radiusMidpoint,
                                      shape_.radiusAsymptoteRatio, shape_.minimumScale);
    const float secondary = std::max(0.0f, s.age - thresholds_.vegetativeEndYear) *
                            shape_.secondaryGrowthRate;
    s.radiusScale = std::min(radiusBase + secondary, shape_.radiusAsymptoteRatio + 0.5f);

    const float leafFactor = logistic(s.age, shape_.leafLogisticRate, shape_.leafMidpoint,
                                      shape_.leafAsymptoteRatio, shape_.minimumScale);
    s.leafScale = Vec2(leafFactor, leafFactor);
    return s;
}

// ============================================================================
// advance() - 推进 currentAge_，返回新样本
// ============================================================================
GrowthSample GrowthTimeline::advance(float deltaYears) {
    if (deltaYears <= 0.0f) return sample(currentAge_);
    currentAge_ += deltaYears;
    if (currentAge_ >= thresholds_.matureEndYear) {
        currentAge_ = thresholds_.matureEndYear;
        recomputeModeByAge();
    }
    return sample(currentAge_);
}

// ============================================================================
// 阶段 / 状态映射
// ============================================================================
PlantLifeStage GrowthTimeline::lifeStageAt(float ageYears) const {
    if (ageYears < thresholds_.seedlingEndYear)  return PlantLifeStage::Seedling;
    if (ageYears < thresholds_.vegetativeEndYear) return PlantLifeStage::Vegetative;
    return PlantLifeStage::Mature;
}

PlantGrowthState GrowthTimeline::growthStateAt(float ageYears) const {
    if (ageYears >= thresholds_.matureEndYear) return PlantGrowthState::Completed;
    if (mode_ == GrowthPlaybackMode::Paused)   return PlantGrowthState::Paused;
    return PlantGrowthState::Active;
}

// ============================================================================
// 状态变更
// ============================================================================
void GrowthTimeline::reset(float initialYears) {
    currentAge_ = std::max(0.0f, initialYears);
    recomputeModeByAge();
}

void GrowthTimeline::setSpeed(float speed) {
    speed_ = clamp(speed, kMinSpeed, kMaxSpeed);
}

void GrowthTimeline::setMode(GrowthPlaybackMode m) {
    mode_ = m;
    if (m == GrowthPlaybackMode::Completed) {
        currentAge_ = thresholds_.matureEndYear;
    }
}

void GrowthTimeline::setSecondsPerYear(float seconds) {
    secondsPerYear_ = std::max(0.001f, seconds);
}

void GrowthTimeline::setThresholds(const GrowthAxisThresholds& t) {
    thresholds_ = t;
    if (currentAge_ >= thresholds_.matureEndYear) {
        currentAge_ = thresholds_.matureEndYear;
    }
    recomputeModeByAge();
}

void GrowthTimeline::setShape(const GrowthShapeParams& s) {
    shape_ = s;
}

void GrowthTimeline::recomputeModeByAge() {
    if (mode_ == GrowthPlaybackMode::Completed) return; // 终态保持
    if (currentAge_ >= thresholds_.matureEndYear) {
        mode_ = GrowthPlaybackMode::Completed;
    } else if (mode_ == GrowthPlaybackMode::Completed) {
        // 如果 age 被 reset 回未完成区间，恢复到 Paused（让用户重新 start）
        mode_ = GrowthPlaybackMode::Paused;
    }
}

// ============================================================================
// 描述
// ============================================================================
QString GrowthTimeline::describeMode() const {
    switch (mode_) {
    case GrowthPlaybackMode::Realtime:  return QStringLiteral("Running");
    case GrowthPlaybackMode::Step:      return QStringLiteral("Stepping");
    case GrowthPlaybackMode::Completed: return QStringLiteral("Completed");
    case GrowthPlaybackMode::Paused:
    default: return QStringLiteral("Paused");
    }
}

QString GrowthTimeline::describeLifeStage(PlantLifeStage stage) const {
    switch (stage) {
    case PlantLifeStage::Seed:        return QStringLiteral("Seed");
    case PlantLifeStage::Seedling:    return QStringLiteral("Seedling");
    case PlantLifeStage::Vegetative:  return QStringLiteral("Vegetative");
    case PlantLifeStage::Mature:      return QStringLiteral("Mature");
    case PlantLifeStage::Flowering:   return QStringLiteral("Flowering");
    case PlantLifeStage::Dormant:     return QStringLiteral("Dormant");
    case PlantLifeStage::Senescent:   return QStringLiteral("Senescent");
    }
    return QStringLiteral("Unknown");
}

// ============================================================================
// JSON 持久化（plantsim.growth_timeline, version 1）
// ============================================================================
QJsonObject GrowthTimeline::toJson() const {
    QJsonObject thresholds{
        {QStringLiteral("seedlingEndYear"),   static_cast<double>(thresholds_.seedlingEndYear)},
        {QStringLiteral("vegetativeEndYear"), static_cast<double>(thresholds_.vegetativeEndYear)},
        {QStringLiteral("matureEndYear"),     static_cast<double>(thresholds_.matureEndYear)}
    };
    QJsonObject shape{
        {QStringLiteral("lengthLogisticRate"),    static_cast<double>(shape_.lengthLogisticRate)},
        {QStringLiteral("lengthMidpoint"),        static_cast<double>(shape_.lengthMidpoint)},
        {QStringLiteral("lengthAsymptoteRatio"),  static_cast<double>(shape_.lengthAsymptoteRatio)},
        {QStringLiteral("radiusLogisticRate"),    static_cast<double>(shape_.radiusLogisticRate)},
        {QStringLiteral("radiusMidpoint"),        static_cast<double>(shape_.radiusMidpoint)},
        {QStringLiteral("radiusAsymptoteRatio"),  static_cast<double>(shape_.radiusAsymptoteRatio)},
        {QStringLiteral("secondaryGrowthRate"),   static_cast<double>(shape_.secondaryGrowthRate)},
        {QStringLiteral("leafLogisticRate"),      static_cast<double>(shape_.leafLogisticRate)},
        {QStringLiteral("leafMidpoint"),          static_cast<double>(shape_.leafMidpoint)},
        {QStringLiteral("leafAsymptoteRatio"),    static_cast<double>(shape_.leafAsymptoteRatio)},
        {QStringLiteral("minimumScale"),          static_cast<double>(shape_.minimumScale)}
    };
    QJsonObject speeds{
        {QStringLiteral("current"),        static_cast<double>(speed_)},
        {QStringLiteral("min"),            static_cast<double>(kMinSpeed)},
        {QStringLiteral("max"),            static_cast<double>(kMaxSpeed)},
        {QStringLiteral("secondsPerYear"), static_cast<double>(secondsPerYear_)}
    };
    QJsonObject state{
        {QStringLiteral("age"),           static_cast<double>(currentAge_)},
        {QStringLiteral("lifeStage"),     toString(lifeStageAt(currentAge_))},
        {QStringLiteral("playbackMode"),  playbackModeToString(mode_)},
        {QStringLiteral("growthState"),   toString(growthStateAt(currentAge_))}
    };
    return QJsonObject{
        {QStringLiteral("schema"),     QStringLiteral("plantsim.growth_timeline")},
        {QStringLiteral("version"),    kSchemaVersion},
        {QStringLiteral("preset"),     QStringLiteral("default")},
        {QStringLiteral("speeds"),     speeds},
        {QStringLiteral("thresholds"), thresholds},
        {QStringLiteral("shape"),      shape},
        {QStringLiteral("state"),      state}
    };
}

bool GrowthTimeline::fromJson(const QJsonObject& json, GrowthTimeline* output, QString* error) {
    if (!output) { setError(error, QStringLiteral("Output GrowthTimeline pointer is null.")); return false; }
    if (json.value(QStringLiteral("schema")).toString() != QStringLiteral("plantsim.growth_timeline")) {
        setError(error, QStringLiteral("Unsupported or missing JSON schema."));
        return false;
    }
    if (json.value(QStringLiteral("version")).toInt(-1) != kSchemaVersion) {
        setError(error, QStringLiteral("Unsupported growth_timeline schema version."));
        return false;
    }

    GrowthTimeline timeline;
    const QJsonObject thresholdsJson = json.value(QStringLiteral("thresholds")).toObject();
    timeline.thresholds_.seedlingEndYear   = static_cast<float>(
        thresholdsJson.value(QStringLiteral("seedlingEndYear")).toDouble(0.5));
    timeline.thresholds_.vegetativeEndYear = static_cast<float>(
        thresholdsJson.value(QStringLiteral("vegetativeEndYear")).toDouble(3.0));
    timeline.thresholds_.matureEndYear     = static_cast<float>(
        thresholdsJson.value(QStringLiteral("matureEndYear")).toDouble(30.0));

    const QJsonObject shapeJson = json.value(QStringLiteral("shape")).toObject();
    timeline.shape_.lengthLogisticRate    = static_cast<float>(
        shapeJson.value(QStringLiteral("lengthLogisticRate")).toDouble(1.6));
    timeline.shape_.lengthMidpoint        = static_cast<float>(
        shapeJson.value(QStringLiteral("lengthMidpoint")).toDouble(1.5));
    timeline.shape_.lengthAsymptoteRatio  = static_cast<float>(
        shapeJson.value(QStringLiteral("lengthAsymptoteRatio")).toDouble(1.0));
    timeline.shape_.radiusLogisticRate    = static_cast<float>(
        shapeJson.value(QStringLiteral("radiusLogisticRate")).toDouble(0.9));
    timeline.shape_.radiusMidpoint        = static_cast<float>(
        shapeJson.value(QStringLiteral("radiusMidpoint")).toDouble(0.7));
    timeline.shape_.radiusAsymptoteRatio  = static_cast<float>(
        shapeJson.value(QStringLiteral("radiusAsymptoteRatio")).toDouble(1.45));
    timeline.shape_.secondaryGrowthRate   = static_cast<float>(
        shapeJson.value(QStringLiteral("secondaryGrowthRate")).toDouble(0.03));
    timeline.shape_.leafLogisticRate      = static_cast<float>(
        shapeJson.value(QStringLiteral("leafLogisticRate")).toDouble(1.8));
    timeline.shape_.leafMidpoint          = static_cast<float>(
        shapeJson.value(QStringLiteral("leafMidpoint")).toDouble(1.0));
    timeline.shape_.leafAsymptoteRatio    = static_cast<float>(
        shapeJson.value(QStringLiteral("leafAsymptoteRatio")).toDouble(1.15));
    timeline.shape_.minimumScale          = static_cast<float>(
        shapeJson.value(QStringLiteral("minimumScale")).toDouble(0.0));

    const QJsonObject speedsJson = json.value(QStringLiteral("speeds")).toObject();
    timeline.speed_          = static_cast<float>(speedsJson.value(QStringLiteral("current")).toDouble(1.0));
    timeline.secondsPerYear_ = static_cast<float>(
        speedsJson.value(QStringLiteral("secondsPerYear")).toDouble(2.0));
    timeline.speed_ = clamp(timeline.speed_, kMinSpeed, kMaxSpeed);

    const QJsonObject stateJson = json.value(QStringLiteral("state")).toObject();
    timeline.currentAge_ = std::max(0.0f, static_cast<float>(
        stateJson.value(QStringLiteral("age")).toDouble(0.0)));
    const QString modeStr = stateJson.value(QStringLiteral("playbackMode")).toString();
    timeline.mode_ = playbackModeFromString(modeStr);

    timeline.recomputeModeByAge();
    *output = std::move(timeline);
    return true;
}

bool GrowthTimeline::saveJson(const QString& filePath, QString* error) const {
    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(error, QStringLiteral("Cannot open %1 for writing: %2").arg(filePath, file.errorString()));
        return false;
    }
    const QByteArray bytes = QJsonDocument(toJson()).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        setError(error, QStringLiteral("Failed to write %1: %2").arg(filePath, file.errorString()));
        return false;
    }
    if (!file.commit()) {
        setError(error, QStringLiteral("Failed to commit %1: %2").arg(filePath, file.errorString()));
        return false;
    }
    return true;
}

bool GrowthTimeline::loadJson(const QString& filePath, GrowthTimeline* output, QString* error) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Cannot open %1 for reading: %2").arg(filePath, file.errorString()));
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject()) {
        setError(error, QStringLiteral("Invalid JSON in %1: %2").arg(filePath, parseError.errorString()));
        return false;
    }
    return fromJson(doc.object(), output, error);
}