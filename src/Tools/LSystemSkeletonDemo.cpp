#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QTextStream>

#include "Algorithm/PlantSkeletonPresets.h"
#include "Plant/PlantModel.h"

namespace {
QString outputPathFor(const QString& outputValue,
                      bool generatingAll,
                      const QString& presetKey) {
    if (generatingAll) {
        return QDir(outputValue).filePath(QStringLiteral("lsystem_%1.json").arg(presetKey));
    }

    const QFileInfo info(outputValue);
    if (info.suffix().compare(QStringLiteral("json"), Qt::CaseInsensitive) == 0) {
        return info.filePath();
    }
    return QDir(outputValue).filePath(QStringLiteral("lsystem_%1.json").arg(presetKey));
}

int generateOne(const PlantSkeletonPreset& sourcePreset,
                int iterationsOverride,
                quint32 seed,
                const QString& outputPath,
                bool showSequence,
                bool showTree,
                QTextStream& out,
                QTextStream& err) {
    PlantSkeletonPreset preset = sourcePreset;
    const int iterations = iterationsOverride >= 0 ? iterationsOverride : preset.iterations;
    preset.system.setRandomSeed(seed);
    preset.turtleRule.randomSeed = seed;

    const LSystemGenerationResult generation =
        preset.system.generateDetailed(preset.system.axiom, iterations, seed);
    const LSystemGenerationResult repeated =
        preset.system.generateDetailed(preset.system.axiom, iterations, seed);
    if (generation.sequence != repeated.sequence) {
        err << preset.key << ": fixed-seed reproducibility check failed." << Qt::endl;
        return 10;
    }

    TurtleInterpreter interpreter;
    TurtleInterpretationResult interpreted =
        interpreter.interpretDetailed(generation.sequence, preset.turtleRule);
    if (!interpreted.root) {
        err << preset.key << ": turtle interpreter returned an empty skeleton." << Qt::endl;
        return 11;
    }
    if (interpreted.stats.unmatchedClosingBrackets != 0 ||
        interpreted.stats.unclosedBranches != 0) {
        err << preset.key << ": unbalanced turtle stack: unmatchedClosing="
            << interpreted.stats.unmatchedClosingBrackets
            << ", unclosed=" << interpreted.stats.unclosedBranches << Qt::endl;
        return 12;
    }

    PlantModel model;
    model.id = 100;
    model.name = QStringLiteral("L-System %1 Skeleton").arg(preset.displayName);
    model.species = preset.key;
    model.age = 1.0f;
    model.lifeStage = PlantLifeStage::Vegetative;
    model.growthState = PlantGrowthState::Active;
    model.setRootNode(std::move(interpreted.root));

    QString validationError;
    if (!model.validate(&validationError)) {
        err << preset.key << ": skeleton validation failed: "
            << validationError << Qt::endl;
        return 13;
    }

    const QFileInfo outputInfo(outputPath);
    if (!QDir().mkpath(outputInfo.absolutePath())) {
        err << preset.key << ": cannot create output directory: "
            << outputInfo.absolutePath() << Qt::endl;
        return 14;
    }

    QString error;
    if (!model.saveJson(outputInfo.absoluteFilePath(), &error)) {
        err << preset.key << ": JSON save failed: " << error << Qt::endl;
        return 15;
    }

    PlantModel restored;
    if (!PlantModel::loadJson(outputInfo.absoluteFilePath(), &restored, &error)) {
        err << preset.key << ": JSON round-trip load failed: " << error << Qt::endl;
        return 16;
    }
    if (restored.nodeCount() != model.nodeCount() ||
        restored.branches().size() != model.branches().size()) {
        err << preset.key << ": JSON round-trip changed skeleton counts." << Qt::endl;
        return 17;
    }

    const TurtleInterpretationStats& stats = interpreted.stats;
    out << "[" << preset.key << "] " << preset.displayName << Qt::endl
        << "  " << preset.description << Qt::endl
        << "  iterations=" << iterations
        << ", seed=" << seed
        << ", symbols=" << generation.sequence.size()
        << ", completedIterations=" << generation.completedIterations
        << ", truncated=" << (generation.truncated ? "yes" : "no") << Qt::endl
        << "  segments=" << stats.segmentCount
        << ", nodes=" << static_cast<qulonglong>(model.nodeCount())
        << ", branches=" << static_cast<qulonglong>(model.branches().size())
        << ", rotations=" << stats.rotationCount
        << ", maxStackDepth=" << stats.maximumStackDepth
        << ", maxTreeDepth=" << (model.rootNode() ? model.rootNode()->maxDepth() : 0)
        << Qt::endl
        << "  JSON=" << outputInfo.absoluteFilePath() << Qt::endl
        << "  fixed-seed reproducibility=passed, JSON round-trip=passed" << Qt::endl;

    if (showSequence) {
        out << "  sequence=" << generation.sequence << Qt::endl;
    }
    if (showTree) {
        out << Qt::endl << restored.toTreeString() << Qt::endl;
    }
    out << Qt::endl;
    return 0;
}
}

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("LSystemSkeletonDemo"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Generate 3D plant skeleton JSON with a stochastic L-System and turtle interpreter."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption presetOption(
        {QStringLiteral("p"), QStringLiteral("preset")},
        QStringLiteral("Plant preset: pine, willow, cherry, shrub, or all."),
        QStringLiteral("name"),
        QStringLiteral("pine"));
    QCommandLineOption iterationsOption(
        {QStringLiteral("i"), QStringLiteral("iterations")},
        QStringLiteral("Override the preset iteration count."),
        QStringLiteral("count"));
    QCommandLineOption seedOption(
        {QStringLiteral("s"), QStringLiteral("seed")},
        QStringLiteral("Random seed for rewriting and turtle variation."),
        QStringLiteral("number"),
        QStringLiteral("20260804"));
    QCommandLineOption outputOption(
        {QStringLiteral("o"), QStringLiteral("output")},
        QStringLiteral("Output JSON file for one preset, or directory for --preset all."),
        QStringLiteral("path"),
        QStringLiteral("examples"));
    QCommandLineOption sequenceOption(
        QStringLiteral("show-sequence"),
        QStringLiteral("Print the complete generated L-System sequence."));
    QCommandLineOption treeOption(
        QStringLiteral("show-tree"),
        QStringLiteral("Print the complete parent-child skeleton tree."));

    parser.addOption(presetOption);
    parser.addOption(iterationsOption);
    parser.addOption(seedOption);
    parser.addOption(outputOption);
    parser.addOption(sequenceOption);
    parser.addOption(treeOption);
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
    err.setCodec("UTF-8");
#endif

    bool seedOk = false;
    const quint32 seed = parser.value(seedOption).toUInt(&seedOk);
    if (!seedOk) {
        err << "Invalid seed: " << parser.value(seedOption) << Qt::endl;
        return 2;
    }

    int iterationsOverride = -1;
    if (parser.isSet(iterationsOption)) {
        bool iterationsOk = false;
        iterationsOverride = parser.value(iterationsOption).toInt(&iterationsOk);
        if (!iterationsOk || iterationsOverride < 0) {
            err << "Iterations must be a non-negative integer." << Qt::endl;
            return 3;
        }
    }

    const QString requestedPreset = parser.value(presetOption).trimmed().toLower();
    const bool generatingAll = requestedPreset == QStringLiteral("all");
    QStringList presetNames;
    if (generatingAll) {
        presetNames = PlantSkeletonPresets::names();
    } else {
        presetNames.append(requestedPreset);
    }

    for (const QString& presetName : presetNames) {
        PlantSkeletonPreset preset;
        if (!PlantSkeletonPresets::fromName(presetName, &preset)) {
            err << "Unknown preset: " << presetName
                << ". Available: " << PlantSkeletonPresets::names().join(QStringLiteral(", "))
                << ", all" << Qt::endl;
            return 4;
        }

        const QString outputPath = outputPathFor(parser.value(outputOption),
                                                 generatingAll,
                                                 preset.key);
        const int status = generateOne(preset,
                                       iterationsOverride,
                                       seed,
                                       outputPath,
                                       parser.isSet(sequenceOption),
                                       parser.isSet(treeOption),
                                       out,
                                       err);
        if (status != 0) {
            return status;
        }
    }

    return 0;
}


