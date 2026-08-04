// ============================================================================
// MarchingCubesDemo - 第7周：从 Metaball 标量场提取枝干三角网格
// ----------------------------------------------------------------------------
// 流程：L-System 预设植物 -> MetaballField -> ScalarFieldGrid
//       -> MarchingCubes -> SurfaceMesh
// 输出：OBJ 网格、前端用网格 JSON、统计摘要、软渲染光照预览 PNG。
// ============================================================================
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <vector>

#include "Algorithm/PlantSkeletonPresets.h"
#include "Geometry/MarchingCubes.h"
#include "Implicit/MetaballField.h"
#include "Plant/PlantModel.h"

namespace {

QJsonArray vectorToJson(const Vec3& value) {
    return QJsonArray{value.x(), value.y(), value.z()};
}

bool preparePlant(const QString& presetName,
                  int iterationsOverride,
                  quint32 seed,
                  PlantSkeletonPreset* preset,
                  int* iterations,
                  PlantModel* model,
                  QString* error) {
    if (!preset || !model || !PlantSkeletonPresets::fromName(presetName, preset)) {
        if (error) {
            *error = QStringLiteral("Unknown preset: %1").arg(presetName);
        }
        return false;
    }

    *iterations = iterationsOverride >= 0 ? iterationsOverride
                                          : std::min(4, preset->iterations);
    preset->turtleRule.randomSeed = seed;
    const LSystemGenerationResult generated = preset->system.generateDetailed(
        preset->system.axiom, *iterations, seed);
    if (generated.truncated) {
        if (error) {
            *error = QStringLiteral("L-System output was truncated before all iterations completed.");
        }
        return false;
    }

    TurtleInterpretationResult interpreted = TurtleInterpreter().interpretDetailed(
        generated.sequence, preset->turtleRule);
    if (!interpreted.root || interpreted.stats.unmatchedClosingBrackets != 0 ||
        interpreted.stats.unclosedBranches != 0) {
        if (error) {
            *error = QStringLiteral("Turtle interpretation failed or produced an unbalanced stack.");
        }
        return false;
    }

    model->id = 700;
    model->name = QStringLiteral("Marching Cubes %1 Plant").arg(preset->displayName);
    model->species = preset->key;
    model->age = 1.0f;
    model->lifeStage = PlantLifeStage::Vegetative;
    model->growthState = PlantGrowthState::Active;
    model->setRootNode(std::move(interpreted.root));

    QString validationError;
    if (!model->validate(&validationError)) {
        if (error) {
            *error = QStringLiteral("Plant skeleton validation failed: %1").arg(validationError);
        }
        return false;
    }
    return true;
}

bool saveObj(const QString& path, const SurfaceMesh& mesh, QString* error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("Cannot write OBJ: %1").arg(path);
        }
        return false;
    }
    QTextStream stream(&file);
    stream.setRealNumberNotation(QTextStream::FixedNotation);
    stream.setRealNumberPrecision(6);
    stream << "# PlantSim Marching Cubes mesh\n";
    stream << "# vertices=" << mesh.positions.size()
           << " triangles=" << mesh.stats.triangleCount << '\n';
    for (const Vec3& p : mesh.positions) {
        stream << "v " << p.x() << ' ' << p.y() << ' ' << p.z() << '\n';
    }
    for (const Vec3& n : mesh.normals) {
        stream << "vn " << n.x() << ' ' << n.y() << ' ' << n.z() << '\n';
    }
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        // OBJ 索引从 1 开始。
        stream << "f " << (mesh.indices[i] + 1) << "//" << (mesh.indices[i] + 1) << ' '
               << (mesh.indices[i + 1] + 1) << "//" << (mesh.indices[i + 1] + 1) << ' '
               << (mesh.indices[i + 2] + 1) << "//" << (mesh.indices[i + 2] + 1) << '\n';
    }
    return true;
}

void writeFloatArray(QByteArray* target, const std::vector<Vec3>& values) {
    QTextStream stream(target, QIODevice::WriteOnly | QIODevice::Append);
    stream.setRealNumberNotation(QTextStream::FixedNotation);
    stream.setRealNumberPrecision(5);
    stream << '[';
    bool first = true;
    for (const Vec3& v : values) {
        for (int axis = 0; axis < 3; ++axis) {
            if (!first) {
                stream << ',';
            }
            stream << v[axis];
            first = false;
        }
    }
    stream << ']';
}

void writeIndexArray(QByteArray* target, const std::vector<std::uint32_t>& values) {
    QTextStream stream(target, QIODevice::WriteOnly | QIODevice::Append);
    stream << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i > 0) {
            stream << ',';
        }
        stream << values[i];
    }
    stream << ']';
}

bool saveMeshJson(const QString& path,
                  const SurfaceMesh& mesh,
                  const QJsonObject& meta,
                  QString* error) {
    QByteArray json;
    json.reserve(static_cast<int>(mesh.positions.size() * 42 + mesh.indices.size() * 7));
    json += "{\n  \"format\": \"PlantSim Marching Cubes Mesh\",\n  \"version\": 1,\n  \"meta\": ";
    json += QJsonDocument(meta).toJson(QJsonDocument::Indented).replace('\n', "\n  ");
    json += ",\n  \"positions\": ";
    writeFloatArray(&json, mesh.positions);
    json += ",\n  \"normals\": ";
    writeFloatArray(&json, mesh.normals);
    json += ",\n  \"indices\": ";
    writeIndexArray(&json, mesh.indices);
    json += "\n}\n";

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) {
            *error = QStringLiteral("Cannot write mesh JSON: %1").arg(path);
        }
        return false;
    }
    file.write(json);
    return true;
}

// 软渲染预览：画家算法 + 顶点法向量 Lambert/镜面光照，验证法向量与光照效果。
QImage renderPreview(const SurfaceMesh& mesh,
                     int width,
                     int height,
                     float azimuthDegrees,
                     float elevationDegrees) {
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(QColor(10, 15, 20));
    if (!mesh.isValid()) {
        return image;
    }

    const Vec3 center = mesh.stats.bounds.center();
    const float extent = std::max(0.001f, mesh.stats.bounds.size().maxCoeff());
    constexpr float kPi = 3.14159265358979323846f;
    const float azimuth = azimuthDegrees * kPi / 180.0f;
    const float elevation = elevationDegrees * kPi / 180.0f;
    const float ca = std::cos(azimuth), sa = std::sin(azimuth);
    const float ce = std::cos(elevation), se = std::sin(elevation);

    auto rotate = [&](const Vec3& v) {
        const Vec3 d = v - center;
        const float x1 = d.x() * ca + d.z() * sa;
        const float z1 = -d.x() * sa + d.z() * ca;
        const float y2 = d.y() * ce - z1 * se;
        const float z2 = d.y() * se + z1 * ce;
        return Vec3(x1, y2, z2);
    };
    auto rotateNormal = [&](const Vec3& n) {
        const float x1 = n.x() * ca + n.z() * sa;
        const float z1 = -n.x() * sa + n.z() * ca;
        const float y2 = n.y() * ce - z1 * se;
        const float z2 = n.y() * se + z1 * ce;
        return Vec3(x1, y2, z2).normalized();
    };

    struct ProjectedTriangle {
        QPointF points[3];
        float depth;
        QColor color;
    };
    std::vector<ProjectedTriangle> triangles;
    triangles.reserve(mesh.stats.triangleCount);

    const float scale = 0.82f * std::min(width, height) / extent;
    const float cameraDistance = 3.0f * extent;
    const Vec3 lightDirection = Vec3(-0.45f, 0.75f, 0.5f).normalized();
    const Vec3 baseColor(0.56f, 0.37f, 0.25f);

    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        ProjectedTriangle triangle;
        Vec3 normalSum = Vec3::Zero();
        float depthSum = 0.0f;
        bool behind = false;
        for (int k = 0; k < 3; ++k) {
            const std::uint32_t index = mesh.indices[i + k];
            const Vec3 rotated = rotate(mesh.positions[index]);
            const float perspective = cameraDistance / (cameraDistance - rotated.z());
            if (perspective <= 0.0f) {
                behind = true;
                break;
            }
            triangle.points[k] = QPointF(width * 0.5 + rotated.x() * scale * perspective,
                                         height * 0.55 - rotated.y() * scale * perspective);
            normalSum += rotateNormal(mesh.normals[index]);
            depthSum += rotated.z();
        }
        if (behind) {
            continue;
        }
        const Vec3 normal = normalSum.normalized();
        const float diffuse = std::max(0.0f, normal.dot(lightDirection));
        const Vec3 halfway = (lightDirection + Vec3::UnitZ()).normalized();
        const float specular = std::pow(std::max(0.0f, normal.dot(halfway)), 24.0f);
        const float brightness = 0.34f + 0.72f * diffuse;
        triangle.color = QColor::fromRgbF(
            std::min(1.0f, baseColor.x() * brightness + 0.25f * specular),
            std::min(1.0f, baseColor.y() * brightness + 0.22f * specular),
            std::min(1.0f, baseColor.z() * brightness + 0.18f * specular));
        triangle.depth = depthSum / 3.0f;
        triangles.push_back(triangle);
    }

    // 画家算法：先画远处三角形。
    std::sort(triangles.begin(), triangles.end(),
              [](const ProjectedTriangle& a, const ProjectedTriangle& b) {
                  return a.depth < b.depth;
              });

    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing, false);
    for (const ProjectedTriangle& triangle : triangles) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(triangle.color);
        painter.drawPolygon(triangle.points, 3);
    }
    painter.setPen(QColor(170, 190, 210));
    painter.drawText(12, height - 14,
                     QStringLiteral("vertices=%1  triangles=%2  watertight=%3")
                         .arg(mesh.stats.vertexCount)
                         .arg(mesh.stats.triangleCount)
                         .arg(mesh.stats.watertight ? QStringLiteral("yes")
                                                    : QStringLiteral("no")));
    painter.end();
    return image;
}

QJsonObject buildMeta(const QString& preset,
                      int iterations,
                      quint32 seed,
                      const PlantModel& model,
                      const MetaballField& field,
                      const ScalarFieldGrid& grid,
                      const SurfaceMesh& mesh) {
    const SurfaceMeshStats& stats = mesh.stats;
    return QJsonObject{
        {QStringLiteral("preset"), preset},
        {QStringLiteral("iterations"), iterations},
        {QStringLiteral("seed"), static_cast<qint64>(seed)},
        {QStringLiteral("plantNodeCount"), static_cast<qint64>(model.nodeCount())},
        {QStringLiteral("isoLevel"), field.isoThreshold()},
        {QStringLiteral("jointSmoothness"), field.settings().jointSmoothness},
        {QStringLiteral("grid"), QJsonObject{
             {QStringLiteral("dimensions"), QJsonArray{grid.dimensions.x(),
                                                        grid.dimensions.y(),
                                                        grid.dimensions.z()}},
             {QStringLiteral("spacing"), vectorToJson(grid.spacing)},
             {QStringLiteral("sampleCount"), static_cast<qint64>(grid.sampleCount())}}},
        {QStringLiteral("mesh"), QJsonObject{
             {QStringLiteral("voxelCount"), static_cast<qint64>(stats.voxelCount)},
             {QStringLiteral("activeVoxelCount"), static_cast<qint64>(stats.activeVoxelCount)},
             {QStringLiteral("insideVoxelCount"), static_cast<qint64>(stats.insideVoxelCount)},
             {QStringLiteral("cubeLocalVertexCount"),
              static_cast<qint64>(stats.cubeLocalVertexCount)},
             {QStringLiteral("vertexCount"), static_cast<qint64>(stats.vertexCount)},
             {QStringLiteral("triangleCount"), static_cast<qint64>(stats.triangleCount)},
             {QStringLiteral("manifoldEdgeCount"), static_cast<qint64>(stats.manifoldEdgeCount)},
             {QStringLiteral("boundaryEdgeCount"), static_cast<qint64>(stats.boundaryEdgeCount)},
             {QStringLiteral("watertight"), stats.watertight},
             {QStringLiteral("orientationFlipped"), stats.orientationFlipped},
             {QStringLiteral("signedVolume"), stats.signedVolume},
             {QStringLiteral("surfaceArea"), stats.surfaceArea},
             {QStringLiteral("bounds"), QJsonObject{
                  {QStringLiteral("minimum"), vectorToJson(stats.bounds.minimum)},
                  {QStringLiteral("maximum"), vectorToJson(stats.bounds.maximum)}}}}}
    };
}

} // namespace

int main(int argc, char* argv[]) {
    // 预览渲染用到字体绘制，需要 QGuiApplication（offscreen 平台亦可）。
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("MarchingCubesDemo"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Extract a triangle mesh from the plant Metaball scalar field."));
    parser.addHelpOption();
    parser.addVersionOption();

    QCommandLineOption presetOption({QStringLiteral("p"), QStringLiteral("preset")},
                                    QStringLiteral("Plant preset: pine, willow, cherry, shrub."),
                                    QStringLiteral("name"), QStringLiteral("cherry"));
    QCommandLineOption iterationsOption({QStringLiteral("i"), QStringLiteral("iterations")},
                                        QStringLiteral("L-System iterations (default: at most 4)."),
                                        QStringLiteral("count"));
    QCommandLineOption seedOption({QStringLiteral("s"), QStringLiteral("seed")},
                                  QStringLiteral("Random seed."),
                                  QStringLiteral("number"), QStringLiteral("20260804"));
    QCommandLineOption thresholdOption({QStringLiteral("t"), QStringLiteral("threshold")},
                                       QStringLiteral("Iso-surface threshold."),
                                       QStringLiteral("value"), QStringLiteral("0.5"));
    QCommandLineOption smoothnessOption(QStringLiteral("smoothness"),
                                        QStringLiteral("Joint smoothness in [0, 1]."),
                                        QStringLiteral("value"), QStringLiteral("0.65"));
    QCommandLineOption spacingOption(QStringLiteral("spacing"),
                                     QStringLiteral("Requested sampling spacing."),
                                     QStringLiteral("value"), QStringLiteral("0.05"));
    QCommandLineOption objOption({QStringLiteral("o"), QStringLiteral("obj")},
                                 QStringLiteral("OBJ mesh output path."),
                                 QStringLiteral("file"),
                                 QStringLiteral("examples/marching_cubes_cherry.obj"));
    QCommandLineOption meshJsonOption(QStringLiteral("mesh-json"),
                                      QStringLiteral("Frontend mesh JSON output path."),
                                      QStringLiteral("file"));
    QCommandLineOption summaryOption(QStringLiteral("summary"),
                                     QStringLiteral("Statistics summary JSON output path."),
                                     QStringLiteral("file"));
    QCommandLineOption previewOption(QStringLiteral("preview"),
                                     QStringLiteral("Shaded preview PNG output path."),
                                     QStringLiteral("file"));
    QCommandLineOption azimuthOption(QStringLiteral("azimuth"),
                                     QStringLiteral("Preview camera azimuth in degrees."),
                                     QStringLiteral("degrees"), QStringLiteral("-35"));
    QCommandLineOption elevationOption(QStringLiteral("elevation"),
                                       QStringLiteral("Preview camera elevation in degrees."),
                                       QStringLiteral("degrees"), QStringLiteral("18"));
    QCommandLineOption noPreviewOption(QStringLiteral("no-preview"),
                                       QStringLiteral("Skip software preview rendering."));

    parser.addOption(presetOption);
    parser.addOption(iterationsOption);
    parser.addOption(seedOption);
    parser.addOption(thresholdOption);
    parser.addOption(smoothnessOption);
    parser.addOption(spacingOption);
    parser.addOption(objOption);
    parser.addOption(meshJsonOption);
    parser.addOption(summaryOption);
    parser.addOption(previewOption);
    parser.addOption(azimuthOption);
    parser.addOption(elevationOption);
    parser.addOption(noPreviewOption);
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
    err.setCodec("UTF-8");
#endif

    bool valid = false;
    const quint32 seed = parser.value(seedOption).toUInt(&valid);
    if (!valid) {
        err << "Invalid seed." << Qt::endl;
        return 2;
    }

    int iterations = -1;
    if (parser.isSet(iterationsOption)) {
        iterations = parser.value(iterationsOption).toInt(&valid);
        if (!valid || iterations < 0) {
            err << "Iterations must be a non-negative integer." << Qt::endl;
            return 3;
        }
    }

    const float threshold = parser.value(thresholdOption).toFloat(&valid);
    if (!valid || threshold <= 0.0f) {
        err << "Threshold must be positive." << Qt::endl;
        return 4;
    }
    const float smoothness = parser.value(smoothnessOption).toFloat(&valid);
    if (!valid || smoothness < 0.0f || smoothness > 1.0f) {
        err << "Smoothness must be in [0, 1]." << Qt::endl;
        return 5;
    }
    const float spacing = parser.value(spacingOption).toFloat(&valid);
    if (!valid || spacing <= 0.0f) {
        err << "Spacing must be positive." << Qt::endl;
        return 6;
    }
    const float azimuth = parser.value(azimuthOption).toFloat(&valid);
    if (!valid) {
        err << "Invalid azimuth." << Qt::endl;
        return 7;
    }
    const float elevation = parser.value(elevationOption).toFloat(&valid);
    if (!valid) {
        err << "Invalid elevation." << Qt::endl;
        return 8;
    }

    PlantSkeletonPreset preset;
    PlantModel model;
    QString error;
    if (!preparePlant(parser.value(presetOption), iterations, seed,
                      &preset, &iterations, &model, &error)) {
        err << error << Qt::endl;
        return 9;
    }

    MetaballFieldSettings settings;
    settings.isoThreshold = threshold;
    settings.jointSmoothness = smoothness;
    settings.influenceScale = 2.0f;
    settings.segmentWeight = 1.0f;
    settings.boundsPadding = spacing;

    MetaballField field;
    field.rebuildFromPlant(model, settings);
    const ScalarFieldGrid grid = field.sampleGrid(spacing);
    if (!grid.isValid()) {
        err << "Scalar field grid generation failed." << Qt::endl;
        return 10;
    }

    const SurfaceMesh mesh = MarchingCubes::extract(grid, field.isoThreshold());
    if (!mesh.isValid()) {
        err << "Marching Cubes extraction produced no triangles." << Qt::endl;
        return 11;
    }
    if (!MarchingCubes::validate(mesh, &error)) {
        err << "Mesh validation failed: " << error << Qt::endl;
        return 12;
    }

    const QJsonObject meta = buildMeta(preset.key, iterations, seed, model, field, grid, mesh);

    const QFileInfo objInfo(parser.value(objOption));
    QDir().mkpath(objInfo.absolutePath());
    if (!saveObj(objInfo.absoluteFilePath(), mesh, &error)) {
        err << error << Qt::endl;
        return 13;
    }

    const QString meshJsonPath = parser.isSet(meshJsonOption)
        ? QFileInfo(parser.value(meshJsonOption)).absoluteFilePath()
        : QDir(objInfo.absolutePath()).filePath(objInfo.completeBaseName() + QStringLiteral("_mesh.json"));
    const QString summaryPath = parser.isSet(summaryOption)
        ? QFileInfo(parser.value(summaryOption)).absoluteFilePath()
        : QDir(objInfo.absolutePath()).filePath(objInfo.completeBaseName() + QStringLiteral("_summary.json"));
    QDir().mkpath(QFileInfo(meshJsonPath).absolutePath());
    QDir().mkpath(QFileInfo(summaryPath).absolutePath());

    if (!saveMeshJson(meshJsonPath, mesh, meta, &error)) {
        err << error << Qt::endl;
        return 14;
    }
    {
        QFile summaryFile(summaryPath);
        if (!summaryFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            err << "Cannot write summary: " << summaryPath << Qt::endl;
            return 15;
        }
        QJsonObject summary = meta;
        summary.insert(QStringLiteral("format"),
                       QStringLiteral("PlantSim Marching Cubes Mesh Summary"));
        summary.insert(QStringLiteral("version"), 1);
        summaryFile.write(QJsonDocument(summary).toJson(QJsonDocument::Indented));
    }

    QString previewPath;
    if (!parser.isSet(noPreviewOption)) {
        previewPath = parser.isSet(previewOption)
            ? QFileInfo(parser.value(previewOption)).absoluteFilePath()
            : QDir(objInfo.absolutePath()).filePath(objInfo.completeBaseName() + QStringLiteral("_preview.png"));
        QDir().mkpath(QFileInfo(previewPath).absolutePath());
        const QImage preview = renderPreview(mesh, 960, 960, azimuth, elevation);
        if (!preview.save(previewPath)) {
            err << "Cannot save preview: " << previewPath << Qt::endl;
            return 16;
        }
    }

    const SurfaceMeshStats& stats = mesh.stats;
    out << "Marching Cubes mesh extracted" << Qt::endl
        << "  preset=" << preset.key << ", iterations=" << iterations
        << ", seed=" << seed << Qt::endl
        << "  grid=" << grid.dimensions.x() << "x" << grid.dimensions.y() << "x"
        << grid.dimensions.z() << ", isoLevel=" << field.isoThreshold() << Qt::endl
        << "  voxels=" << static_cast<qulonglong>(stats.voxelCount)
        << ", active=" << static_cast<qulonglong>(stats.activeVoxelCount)
        << ", inside=" << static_cast<qulonglong>(stats.insideVoxelCount) << Qt::endl
        << "  vertices=" << static_cast<qulonglong>(stats.vertexCount)
        << " (cube-local " << static_cast<qulonglong>(stats.cubeLocalVertexCount)
        << ", dedup ratio "
        << (stats.cubeLocalVertexCount
                ? static_cast<double>(stats.cubeLocalVertexCount) /
                      static_cast<double>(stats.vertexCount)
                : 0.0)
        << "x)" << Qt::endl
        << "  triangles=" << static_cast<qulonglong>(stats.triangleCount)
        << ", manifoldEdges=" << static_cast<qulonglong>(stats.manifoldEdgeCount)
        << ", boundaryEdges=" << static_cast<qulonglong>(stats.boundaryEdgeCount) << Qt::endl
        << "  watertight=" << (stats.watertight ? "yes" : "no")
        << ", orientationFlipped=" << (stats.orientationFlipped ? "yes" : "no")
        << ", signedVolume=" << stats.signedVolume
        << ", surfaceArea=" << stats.surfaceArea << Qt::endl
        << "  obj=" << objInfo.absoluteFilePath() << Qt::endl
        << "  meshJson=" << meshJsonPath << Qt::endl
        << "  summary=" << summaryPath << Qt::endl;
    if (!previewPath.isEmpty()) {
        out << "  preview=" << previewPath << Qt::endl;
    }
    return 0;
}
