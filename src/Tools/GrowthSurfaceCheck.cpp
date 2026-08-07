// ============================================================================
// GrowthSurfaceCheck - 第9周：枝干伸长动画数值验证
// ----------------------------------------------------------------------------
//   同一棵植物在 age=0（幼苗）与 age=30（成熟）两个时刻分别提取表面网格，
//   对比顶点 / 三角形数量与包围盒尺寸，证明"枝干伸长"在表面网格层面成立。
// ============================================================================
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QTextStream>

#include <algorithm>
#include <cmath>

#include "Algorithm/PlantSkeletonPresets.h"
#include "Algorithm/TurtleInterpreter.h"
#include "Engine/GrowthTimeline.h"
#include "Geometry/MarchingCubes.h"
#include "Implicit/MetaballField.h"
#include "Plant/PlantModel.h"

namespace {

bool buildPlant(const QString& presetName, PlantModel* model, QString* error) {
    PlantSkeletonPreset preset;
    if (!PlantSkeletonPresets::fromName(presetName, &preset)) {
        *error = QStringLiteral("Unknown preset: %1").arg(presetName);
        return false;
    }
    const int iterations = std::min(4, preset.iterations);
    const LSystemGenerationResult generated =
        preset.system.generateDetailed(preset.system.axiom, iterations, 20260804u);
    if (generated.truncated) {
        *error = QStringLiteral("L-System output was truncated.");
        return false;
    }
    TurtleInterpretationResult interpreted =
        TurtleInterpreter().interpretDetailed(generated.sequence, preset.turtleRule);
    if (!interpreted.root) {
        *error = QStringLiteral("Turtle interpretation failed.");
        return false;
    }
    model->id = 901;
    model->name = QStringLiteral("Growth Surface Check");
    model->species = preset.key;
    model->setRootNode(std::move(interpreted.root));
    return true;
}

struct MeshMeasure {
    float age = 0.0f;
    std::size_t vertices = 0;
    std::size_t triangles = 0;
    Vec3 min = Vec3::Zero();
    Vec3 max = Vec3::Zero();
    float height = 0.0f;
    float crownWidth = 0.0f;
};

MeshMeasure measureAt(PlantModel& plant, float age, const MetaballFieldSettings& settings) {
    // applyGrowthSample 基于基线 × scale 重写几何，重复调用无累积漂移
    GrowthTimeline timeline;
    plant.applyGrowthSample(timeline.sample(age));

    MetaballField field;
    field.rebuildFromPlant(plant, settings);
    const ScalarFieldGrid grid = field.sampleGrid(0.06f);
    const SurfaceMesh mesh = MarchingCubes::extract(grid, field.isoThreshold());

    MeshMeasure m;
    m.age = age;
    m.vertices = mesh.positions.size();
    m.triangles = mesh.indices.size() / 3;
    m.min = mesh.stats.bounds.minimum;
    m.max = mesh.stats.bounds.maximum;
    m.height = mesh.stats.bounds.maximum.y() - mesh.stats.bounds.minimum.y();
    m.crownWidth = std::max(mesh.stats.bounds.maximum.x() - mesh.stats.bounds.minimum.x(),
                            mesh.stats.bounds.maximum.z() - mesh.stats.bounds.minimum.z());
    return m;
}

QString vec(const Vec3& v) {
    return QStringLiteral("(%1, %2, %3)")
        .arg(v.x(), 0, 'f', 2).arg(v.y(), 0, 'f', 2).arg(v.z(), 0, 'f', 2);
}
}  // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("GrowthSurfaceCheck"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Week 9: verify the branch-elongation animation numerically by comparing "
        "the extracted surface mesh at age=0 vs age=30."));
    parser.addHelpOption();
    QCommandLineOption presetOption({QStringLiteral("p"), QStringLiteral("preset")},
        QStringLiteral("Plant preset (cherry|pine|willow|shrub)."),
        QStringLiteral("name"), QStringLiteral("cherry"));
    parser.addOption(presetOption);
    parser.process(app);

    QTextStream out(stdout);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
#endif

    PlantModel plant;
    QString error;
    if (!buildPlant(parser.value(presetOption), &plant, &error)) {
        out << "[GrowthSurfaceCheck] Build plant failed: " << error << Qt::endl;
        return 1;
    }
    if (!plant.validate(&error)) {
        out << "[GrowthSurfaceCheck] Validation failed: " << error << Qt::endl;
        return 2;
    }

    MetaballFieldSettings settings;
    settings.isoThreshold = 0.5f;
    settings.influenceScale = 2.0f;
    settings.jointSmoothness = 0.65f;

    const MeshMeasure seedling = measureAt(plant, 0.0f, settings);
    const MeshMeasure mature = measureAt(plant, 30.0f, settings);

    out << QStringLiteral("[GrowthSurfaceCheck] Preset %1  nodes=%2 branches=%3 leaves=%4")
               .arg(parser.value(presetOption))
               .arg(static_cast<qulonglong>(plant.nodeCount()))
               .arg(static_cast<qulonglong>(plant.branches().size()))
               .arg(static_cast<qulonglong>(plant.leaves().size()))
        << Qt::endl;
    out << QStringLiteral("  age=0 (seedling): vertices=%1 triangles=%2 height=%3 crownWidth=%4 bounds=%5")
               .arg(static_cast<qulonglong>(seedling.vertices))
               .arg(static_cast<qulonglong>(seedling.triangles))
               .arg(seedling.height, 0, 'f', 2)
               .arg(seedling.crownWidth, 0, 'f', 2)
               .arg(vec(seedling.min) + QStringLiteral("..") + vec(seedling.max))
        << Qt::endl;
    out << QStringLiteral("  age=30 (mature):  vertices=%1 triangles=%2 height=%3 crownWidth=%4 bounds=%5")
               .arg(static_cast<qulonglong>(mature.vertices))
               .arg(static_cast<qulonglong>(mature.triangles))
               .arg(mature.height, 0, 'f', 2)
               .arg(mature.crownWidth, 0, 'f', 2)
               .arg(vec(mature.min) + QStringLiteral("..") + vec(mature.max))
        << Qt::endl;

    const float heightGain = mature.height / std::max(1.0e-4f, seedling.height);
    const float vertexGain = static_cast<float>(mature.vertices) /
                             std::max(1u, static_cast<unsigned>(seedling.vertices));
    out << QStringLiteral("  elongation: height x%1, vertices x%2 -> BRANCH ELONGATION %3")
               .arg(heightGain, 0, 'f', 2)
               .arg(vertexGain, 0, 'f', 2)
               .arg((mature.height > seedling.height * 1.5f && mature.vertices > seedling.vertices)
                        ? QStringLiteral("CONFIRMED")
                        : QStringLiteral("NOT ENOUGH"))
        << Qt::endl;
    return (mature.height > seedling.height * 1.5f && mature.vertices > seedling.vertices) ? 0 : 3;
}
