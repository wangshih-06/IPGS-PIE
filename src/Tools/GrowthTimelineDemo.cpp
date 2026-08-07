// ============================================================================
// GrowthTimelineDemo - 第9周：生长时间轴 CLI 演示
// ----------------------------------------------------------------------------
//   读取 L-System 预设 → 构建 PlantModel → 用 GrowthTimeline 按步推进
//   → 每步 applyGrowthSample 改写 PlantModel 的 length/radius/leaf.size
//   → 写 plantsim.growth_timeline JSON（含 samples 表 + plant 终态）
//   → 可选地把最终 PlantModel 另存为 plant_skeleton.json 用于回归
// ============================================================================
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTextStream>

#include <algorithm>
#include <cmath>

#include "Algorithm/PlantSkeletonPresets.h"
#include "Algorithm/TurtleInterpreter.h"
#include "Engine/GrowthClock.h"
#include "Engine/GrowthStateReport.h"
#include "Engine/GrowthTimeline.h"
#include "Plant/PlantModel.h"
#include "Plant/PlantTypes.h"

namespace {

constexpr int kSchemaVersion = 1;

bool preparePlant(const QString& presetName,
                  int iterationsOverride,
                  quint32 seed,
                  PlantSkeletonPreset* preset,
                  int* iterations,
                  PlantModel* model,
                  QString* error) {
    if (!preset || !model || !PlantSkeletonPresets::fromName(presetName, preset)) {
        if (error) *error = QStringLiteral("Unknown preset: %1").arg(presetName);
        return false;
    }
    *iterations = iterationsOverride >= 0
                      ? iterationsOverride
                      : std::min(4, preset->iterations);
    preset->turtleRule.randomSeed = seed;
    const LSystemGenerationResult generated =
        preset->system.generateDetailed(preset->system.axiom, *iterations, seed);
    if (generated.truncated) {
        if (error) *error = QStringLiteral("L-System output was truncated before all iterations completed.");
        return false;
    }
    TurtleInterpretationResult interpreted =
        TurtleInterpreter().interpretDetailed(generated.sequence, preset->turtleRule);
    if (!interpreted.root || interpreted.stats.unmatchedClosingBrackets != 0 ||
        interpreted.stats.unclosedBranches != 0) {
        if (error) *error = QStringLiteral("Turtle interpretation failed or produced an unbalanced stack.");
        return false;
    }

    model->id = 900;
    model->name = QStringLiteral("Growth Timeline %1").arg(preset->displayName);
    model->species = preset->key;
    model->age = 0.0f;
    model->lifeStage = PlantLifeStage::Seedling;
    model->growthState = PlantGrowthState::Paused;
    model->setRootNode(std::move(interpreted.root));

    QString validationError;
    if (!model->validate(&validationError)) {
        if (error) *error = QStringLiteral("Plant skeleton validation failed: %1").arg(validationError);
        return false;
    }
    return true;
}

QJsonObject buildTimelineJson(const QString& presetKey,
                              const GrowthTimeline& timeline,
                              const QJsonArray& samples,
                              const PlantModel& finalModel) {
    const GrowthSample last = timeline.sample(timeline.currentAge());
    QJsonObject speeds{
        {QStringLiteral("current"),        static_cast<double>(timeline.speed())},
        {QStringLiteral("min"),            static_cast<double>(GrowthTimeline::kMinSpeed)},
        {QStringLiteral("max"),            static_cast<double>(GrowthTimeline::kMaxSpeed)},
        {QStringLiteral("secondsPerYear"), static_cast<double>(timeline.secondsPerYear())}
    };
    QJsonObject thresholds{
        {QStringLiteral("seedlingEndYear"),
         static_cast<double>(timeline.thresholds().seedlingEndYear)},
        {QStringLiteral("vegetativeEndYear"),
         static_cast<double>(timeline.thresholds().vegetativeEndYear)},
        {QStringLiteral("matureEndYear"),
         static_cast<double>(timeline.thresholds().matureEndYear)}
    };
    const GrowthShapeParams shape = timeline.shape();
    QJsonObject shapeJson{
        {QStringLiteral("lengthLogisticRate"),
         static_cast<double>(shape.lengthLogisticRate)},
        {QStringLiteral("lengthMidpoint"),
         static_cast<double>(shape.lengthMidpoint)},
        {QStringLiteral("lengthAsymptoteRatio"),
         static_cast<double>(shape.lengthAsymptoteRatio)},
        {QStringLiteral("radiusLogisticRate"),
         static_cast<double>(shape.radiusLogisticRate)},
        {QStringLiteral("radiusMidpoint"),
         static_cast<double>(shape.radiusMidpoint)},
        {QStringLiteral("radiusAsymptoteRatio"),
         static_cast<double>(shape.radiusAsymptoteRatio)},
        {QStringLiteral("secondaryGrowthRate"),
         static_cast<double>(shape.secondaryGrowthRate)},
        {QStringLiteral("leafLogisticRate"),
         static_cast<double>(shape.leafLogisticRate)},
        {QStringLiteral("leafMidpoint"),
         static_cast<double>(shape.leafMidpoint)},
        {QStringLiteral("leafAsymptoteRatio"),
         static_cast<double>(shape.leafAsymptoteRatio)},
        {QStringLiteral("minimumScale"),
         static_cast<double>(shape.minimumScale)}
    };
    QJsonObject state{
        {QStringLiteral("age"),          static_cast<double>(timeline.currentAge())},
        {QStringLiteral("lifeStage"),    toString(last.lifeStage)},
        {QStringLiteral("playbackMode"), toString(timeline.mode())},
        {QStringLiteral("growthState"),  toString(finalModel.growthState)}
    };
    QJsonObject plant{
        {QStringLiteral("age"),          static_cast<double>(finalModel.age)},
        {QStringLiteral("lifeStage"),    toString(finalModel.lifeStage)},
        {QStringLiteral("growthState"),  toString(finalModel.growthState)},
        {QStringLiteral("nodeCount"),
         static_cast<int>(finalModel.nodeCount())},
        {QStringLiteral("branchCount"),
         static_cast<int>(finalModel.branches().size())},
        {QStringLiteral("leafCount"),
         static_cast<int>(finalModel.leaves().size())}
    };
    return QJsonObject{
        {QStringLiteral("schema"),     QStringLiteral("plantsim.growth_timeline")},
        {QStringLiteral("version"),    kSchemaVersion},
        {QStringLiteral("preset"),     presetKey},
        {QStringLiteral("speeds"),     speeds},
        {QStringLiteral("thresholds"), thresholds},
        {QStringLiteral("shape"),      shapeJson},
        {QStringLiteral("state"),      state},
        {QStringLiteral("samples"),    samples},
        {QStringLiteral("plant"),      plant}
    };
}

bool writeJsonFile(const QString& path, const QJsonObject& obj, QString* error) {
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("Cannot open %1 for writing: %2")
                                  .arg(path, file.errorString());
        return false;
    }
    const QByteArray bytes = QJsonDocument(obj).toJson(QJsonDocument::Indented);
    if (file.write(bytes) != bytes.size()) {
        if (error) *error = QStringLiteral("Failed to write %1: %2")
                                  .arg(path, file.errorString());
        return false;
    }
    if (!file.commit()) {
        if (error) *error = QStringLiteral("Failed to commit %1: %2")
                                  .arg(path, file.errorString());
        return false;
    }
    return true;
}

QString describePreset(const PlantSkeletonPreset& preset, int iterations, quint32 seed) {
    return QStringLiteral("%1 (iterations=%2, seed=%3)")
        .arg(preset.displayName).arg(iterations).arg(seed);
}

QString stageToChinese(PlantLifeStage stage) {
    switch (stage) {
    case PlantLifeStage::Seed:        return QStringLiteral("种子期");
    case PlantLifeStage::Seedling:    return QStringLiteral("幼苗期");
    case PlantLifeStage::Vegetative:  return QStringLiteral("生长期");
    case PlantLifeStage::Mature:      return QStringLiteral("成熟期");
    case PlantLifeStage::Flowering:   return QStringLiteral("开花期");
    case PlantLifeStage::Dormant:     return QStringLiteral("休眠期");
    case PlantLifeStage::Senescent:   return QStringLiteral("衰老期");
    }
    return QStringLiteral("未知");
}
}

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("GrowthTimelineDemo"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "PlantSim Growth Timeline demo (week 9): drive a plant model through "
        "its growth timeline using either deterministic step mode or real-time."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption presetOption({QStringLiteral("p"), QStringLiteral("preset")},
        QStringLiteral("Plant preset (cherry|pine|willow|shrub)."), QStringLiteral("name"),
        QStringLiteral("cherry"));
    QCommandLineOption iterationsOption({QStringLiteral("i"), QStringLiteral("iterations")},
        QStringLiteral("L-System iteration override (-1 = preset default)."),
        QStringLiteral("count"), QStringLiteral("-1"));
    QCommandLineOption seedOption({QStringLiteral("s"), QStringLiteral("seed")},
        QStringLiteral("Random seed."), QStringLiteral("number"), QStringLiteral("20260804"));
    QCommandLineOption modeOption({QStringLiteral("m"), QStringLiteral("mode")},
        QStringLiteral("Playback mode (step|realtime|paused)."),
        QStringLiteral("name"), QStringLiteral("step"));
    QCommandLineOption startAgeOption({QStringLiteral("start-age")},
        QStringLiteral("Initial plant age in years."), QStringLiteral("years"),
        QStringLiteral("0"));
    QCommandLineOption durationOption({QStringLiteral("d"), QStringLiteral("duration")},
        QStringLiteral("Total simulated years to advance through."),
        QStringLiteral("years"), QStringLiteral("5"));
    QCommandLineOption stepOption({QStringLiteral("step")},
        QStringLiteral("Step size in years (used in step mode)."),
        QStringLiteral("years"), QStringLiteral("0.05"));
    QCommandLineOption speedOption({QStringLiteral("speed")},
        QStringLiteral("Growth speed multiplier (0.1 .. 8.0)."),
        QStringLiteral("x"), QStringLiteral("1.0"));
    QCommandLineOption timelineOption({QStringLiteral("t"), QStringLiteral("timeline")},
        QStringLiteral("Optional timeline config JSON to override built-in defaults."),
        QStringLiteral("file"));
    QCommandLineOption outputOption({QStringLiteral("o"), QStringLiteral("output")},
        QStringLiteral("Timeline JSON output path."),
        QStringLiteral("file"), QStringLiteral("examples/growth_timeline_cherry.json"));
    QCommandLineOption plantExportOption({QStringLiteral("plant-export")},
        QStringLiteral("Also write the final PlantModel as a plant_skeleton.json."),
        QStringLiteral("file"));
    QCommandLineOption reportOption({QStringLiteral("report")},
        QStringLiteral("Also write the final GrowthStateReport as JSON."),
        QStringLiteral("file"));
    QCommandLineOption quietOption({QStringLiteral("quiet")},
        QStringLiteral("Suppress per-step stdout output."));
    QCommandLineOption noMetaOption({QStringLiteral("no-meta")},
        QStringLiteral("Skip loading built-in lifecycle metadata."));

    for (QCommandLineOption* option : {&presetOption, &iterationsOption, &seedOption, &modeOption,
                                       &startAgeOption, &durationOption, &stepOption,
                                       &speedOption, &timelineOption, &outputOption,
                                       &plantExportOption, &reportOption, &quietOption,
                                       &noMetaOption}) {
        parser.addOption(*option);
    }
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
    err.setCodec("UTF-8");
#endif

    // ------------------------------------------------------------------
    // 1. 准备 PlantModel（基于 L-System 预设）
    // ------------------------------------------------------------------
    PlantSkeletonPreset preset;
    int iterations = -1;
    PlantModel model;
    QString error;
    if (!preparePlant(parser.value(presetOption),
                      parser.value(iterationsOption).toInt(),
                      parser.value(seedOption).toUInt(),
                      &preset, &iterations, &model, &error)) {
        err << "[GrowthTimelineDemo] Prepare plant failed: " << error << Qt::endl;
        return 1;
    }
    out << "[GrowthTimelineDemo] Prepared plant " << describePreset(preset, iterations, parser.value(seedOption).toUInt())
        << "  nodes=" << model.nodeCount()
        << " branches=" << model.branches().size()
        << " leaves=" << model.leaves().size()
        << Qt::endl;

    // ------------------------------------------------------------------
    // 2. 准备 GrowthTimeline（可选从 --timeline JSON 覆盖）
    // ------------------------------------------------------------------
    GrowthTimeline timeline;
    if (parser.isSet(timelineOption)) {
        if (!GrowthTimeline::loadJson(parser.value(timelineOption), &timeline, &error)) {
            err << "[GrowthTimelineDemo] Failed to load timeline: " << error << Qt::endl;
            return 2;
        }
    }
    timeline.setSpeed(parser.value(speedOption).toFloat());

    const QString modeName = parser.value(modeOption).trimmed().toLower();
    GrowthPlaybackMode mode = GrowthPlaybackMode::Step;
    if (modeName == QLatin1String("realtime")) mode = GrowthPlaybackMode::Realtime;
    else if (modeName == QLatin1String("paused")) mode = GrowthPlaybackMode::Paused;
    timeline.setMode(mode);

    const float startAge = parser.value(startAgeOption).toFloat();
    const float duration = parser.value(durationOption).toFloat();
    const float stepSize = parser.value(stepOption).toFloat();
    if (stepSize <= 0.0f || duration <= 0.0f) {
        err << "[GrowthTimelineDemo] --step and --duration must be positive." << Qt::endl;
        return 3;
    }
    timeline.reset(startAge);
    model.applyGrowthSample(timeline.sample(startAge));

    // ------------------------------------------------------------------
    // 3. 推进时间轴，累积采样
    // ------------------------------------------------------------------
    QJsonArray samples;
    const bool quiet = parser.isSet(quietOption);
    auto recordSample = [&](float age) {
        const GrowthSample s = timeline.sample(age);
        QJsonObject entry{
            {QStringLiteral("age"),             static_cast<double>(s.age)},
            {QStringLiteral("lengthScale"),     static_cast<double>(s.lengthScale)},
            {QStringLiteral("radiusScale"),     static_cast<double>(s.radiusScale)},
            {QStringLiteral("leafScaleLength"), static_cast<double>(s.leafScale.x())},
            {QStringLiteral("leafScaleWidth"),  static_cast<double>(s.leafScale.y())},
            {QStringLiteral("lifeStage"),       toString(s.lifeStage)}
        };
        samples.append(entry);
        return s;
    };
    recordSample(startAge);

    const int totalSteps = static_cast<int>(std::ceil(duration / stepSize));
    for (int i = 1; i <= totalSteps; ++i) {
        const float targetAge = std::min(startAge + duration, startAge + i * stepSize);
        const float delta = targetAge - timeline.currentAge();
        if (delta <= 0.0f) break;
        timeline.advance(delta);
        const GrowthSample s = timeline.sample(timeline.currentAge());
        model.applyGrowthSample(s);
        recordSample(timeline.currentAge());
        if (!quiet) {
            out << QStringLiteral("  age=%1 stage=%2 L=%3 R=%4 leaf=(%5,%6)")
                       .arg(s.age, 6, 'f', 2)
                       .arg(stageToChinese(s.lifeStage))
                       .arg(s.lengthScale, 7, 'f', 3)
                       .arg(s.radiusScale, 7, 'f', 3)
                       .arg(s.leafScale.x(), 7, 'f', 3)
                       .arg(s.leafScale.y(), 7, 'f', 3)
                << Qt::endl;
        }
        if (timeline.isCompleted()) break;
    }

    // ------------------------------------------------------------------
    // 4. 输出 JSON
    // ------------------------------------------------------------------
    QJsonObject root = buildTimelineJson(preset.key, timeline, samples, model);
    const QString outputPath = parser.value(outputOption);
    if (!writeJsonFile(outputPath, root, &error)) {
        err << "[GrowthTimelineDemo] Write failed: " << error << Qt::endl;
        return 4;
    }
    out << "[GrowthTimelineDemo] Timeline JSON: " << QFileInfo(outputPath).absoluteFilePath() << Qt::endl;

    if (parser.isSet(plantExportOption)) {
        QString writeError;
        if (!model.saveJson(parser.value(plantExportOption), &writeError)) {
            err << "[GrowthTimelineDemo] PlantModel save failed: " << writeError << Qt::endl;
            return 5;
        }
        out << "[GrowthTimelineDemo] PlantModel JSON: "
            << QFileInfo(parser.value(plantExportOption)).absoluteFilePath() << Qt::endl;
    }
    if (parser.isSet(reportOption)) {
        const GrowthSample last = timeline.sample(timeline.currentAge());
        GrowthStateReport report;
        report.age         = last.age;
        report.lifeStage   = last.lifeStage;
        report.growthState = model.growthState;
        report.lengthScale = last.lengthScale;
        report.radiusScale = last.radiusScale;
        report.leafScale   = last.leafScale;
        report.speed       = timeline.speed();
        report.mode        = static_cast<int>(timeline.mode());
        report.nodeCount   = static_cast<int>(model.nodeCount());
        report.branchCount = static_cast<int>(model.branches().size());
        report.leafCount   = static_cast<int>(model.leaves().size());
        QJsonObject reportJson{
            {QStringLiteral("age"),         static_cast<double>(report.age)},
            {QStringLiteral("lifeStage"),   toString(report.lifeStage)},
            {QStringLiteral("growthState"), toString(report.growthState)},
            {QStringLiteral("lengthScale"), static_cast<double>(report.lengthScale)},
            {QStringLiteral("radiusScale"), static_cast<double>(report.radiusScale)},
            {QStringLiteral("leafScale"), QJsonArray{report.leafScale.x(), report.leafScale.y()}},
            {QStringLiteral("speed"),       static_cast<double>(report.speed)},
            {QStringLiteral("mode"),        report.mode},
            {QStringLiteral("nodeCount"),   report.nodeCount},
            {QStringLiteral("branchCount"), report.branchCount},
            {QStringLiteral("leafCount"),   report.leafCount}
        };
        if (!writeJsonFile(parser.value(reportOption), reportJson, &error)) {
            err << "[GrowthTimelineDemo] Report write failed: " << error << Qt::endl;
            return 6;
        }
        out << "[GrowthTimelineDemo] Final report JSON: "
            << QFileInfo(parser.value(reportOption)).absoluteFilePath() << Qt::endl;
    }

    out << "[GrowthTimelineDemo] Done  finalAge=" << timeline.currentAge()
        << " stage=" << stageToChinese(model.lifeStage)
        << Qt::endl;
    return 0;
}