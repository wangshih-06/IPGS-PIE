// ============================================================================
// PlantModelExportDemo - 第8周：完整植物静态模型导出管线
// ----------------------------------------------------------------------------
// L-System 预设植物 -> MetaballField -> MarchingCubes -> 拉普拉斯平滑
// （连接处凹陷增强）-> LOD0/1/2 -> 叶片生成 -> 多材质 OBJ+MTL 导出
// -> 彩色软渲染预览 PNG -> 导出摘要 JSON。
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
#include "Geometry/LeafGenerator.h"
#include "Geometry/MarchingCubes.h"
#include "Geometry/MeshExporter.h"
#include "Geometry/MeshProcessing.h"
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

    model->id = 800;
    model->name = QStringLiteral("Export %1 Plant").arg(preset->displayName);
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

// 枝干树皮 + 4 色叶片调色板（程序化颜色，随叶片索引轮换）。
std::vector<ObjMaterial> buildMaterials() {
    std::vector<ObjMaterial> materials;
    ObjMaterial bark;
    bark.name = QStringLiteral("bark");
    bark.diffuse = Vec3(0.45f, 0.30f, 0.20f);
    bark.specular = Vec3(0.05f, 0.04f, 0.03f);
    bark.shininess = 8.0f;
    materials.push_back(bark);

    const Vec3 palette[4] = {
        Vec3(0.30f, 0.52f, 0.26f),
        Vec3(0.40f, 0.60f, 0.29f),
        Vec3(0.26f, 0.45f, 0.23f),
        Vec3(0.48f, 0.64f, 0.32f)
    };
    for (int i = 0; i < 4; ++i) {
        ObjMaterial leaf;
        leaf.name = QStringLiteral("leaf_%1").arg(i);
        leaf.diffuse = palette[i];
        leaf.specular = Vec3(0.10f, 0.12f, 0.08f);
        leaf.shininess = 24.0f;
        leaf.doubleSided = true;
        materials.push_back(leaf);
    }
    return materials;
}

QJsonObject meshStatsJson(const SurfaceMeshStats& stats) {
    return QJsonObject{
        {QStringLiteral("vertexCount"), static_cast<qint64>(stats.vertexCount)},
        {QStringLiteral("triangleCount"), static_cast<qint64>(stats.triangleCount)},
        {QStringLiteral("manifoldEdgeCount"), static_cast<qint64>(stats.manifoldEdgeCount)},
        {QStringLiteral("boundaryEdgeCount"), static_cast<qint64>(stats.boundaryEdgeCount)},
        {QStringLiteral("watertight"), stats.watertight},
        {QStringLiteral("signedVolume"), stats.signedVolume},
        {QStringLiteral("surfaceArea"), stats.surfaceArea},
        {QStringLiteral("bounds"), QJsonObject{
             {QStringLiteral("minimum"), vectorToJson(stats.bounds.minimum)},
             {QStringLiteral("maximum"), vectorToJson(stats.bounds.maximum)}}}
    };
}

// 彩色软渲染预览：枝干单面光照，叶片双面光照（法向量背向相机时翻转）。
QImage renderPreview(const SurfaceMesh& branchMesh,
                     const SurfaceMesh& leafMesh,
                     const std::vector<std::uint16_t>& leafMaterials,
                     const std::vector<ObjMaterial>& materials,
                     int width,
                     int height,
                     float azimuthDegrees,
                     float elevationDegrees) {
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(QColor(10, 15, 20));

    BoundingBox3 bounds = branchMesh.stats.bounds;
    bounds.expand(leafMesh.stats.bounds);
    if (!bounds.isValid()) {
        return image;
    }
    const Vec3 center = bounds.center();
    const float extent = std::max(0.001f, bounds.size().maxCoeff());
    constexpr float kPi = 3.14159265358979323846f;
    const float azimuth = azimuthDegrees * kPi / 180.0f;
    const float elevation = elevationDegrees * kPi / 180.0f;
    const float ca = std::cos(azimuth), sa = std::sin(azimuth);
    const float ce = std::cos(elevation), se = std::sin(elevation);

    auto rotate = [&](const Vec3& v) {
        const Vec3 d = v - center;
        const float x1 = d.x() * ca + d.z() * sa;
        const float z1 = -d.x() * sa + d.z() * ca;
        return Vec3(x1, d.y() * ce - z1 * se, d.y() * se + z1 * ce);
    };

    struct Triangle {
        QPointF points[3];
        float depth;
        QColor color;
    };
    std::vector<Triangle> triangles;
    triangles.reserve(branchMesh.stats.triangleCount + leafMesh.stats.triangleCount);

    const float scale = 0.80f * std::min(width, height) / extent;
    const float cameraDistance = 3.0f * extent;
    const Vec3 lightDirection = Vec3(-0.45f, 0.75f, 0.5f).normalized();

    auto appendMesh = [&](const SurfaceMesh& mesh,
                          const std::vector<std::uint16_t>* faceMaterials,
                          int uniformMaterial,
                          bool twoSided) {
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            Triangle triangle;
            float depthSum = 0.0f;
            bool behind = false;
            for (int k = 0; k < 3; ++k) {
                const Vec3 rotated = rotate(mesh.positions[mesh.indices[i + k]]);
                const float perspective = cameraDistance / (cameraDistance - rotated.z());
                if (perspective <= 0.0f) {
                    behind = true;
                    break;
                }
                triangle.points[k] =
                    QPointF(width * 0.5 + rotated.x() * scale * perspective,
                            height * 0.55 - rotated.y() * scale * perspective);
                depthSum += rotated.z();
            }
            if (behind) {
                continue;
            }
            const Vec3& a = mesh.positions[mesh.indices[i]];
            const Vec3& b = mesh.positions[mesh.indices[i + 1]];
            const Vec3& c = mesh.positions[mesh.indices[i + 2]];
            Vec3 normal = (b - a).cross(c - a);
            if (normal.squaredNorm() < 1e-20f) {
                continue;
            }
            normal.normalize();
            const Vec3 rotatedNormal = rotate(normal + center).normalized();
            Vec3 viewNormal = rotatedNormal;
            if (twoSided && viewNormal.z() < 0.0f) {
                viewNormal = -viewNormal;
            }
            const float diffuse = std::max(0.0f, viewNormal.dot(lightDirection));
            const float brightness = 0.30f + 0.70f * diffuse;

            int materialIndex = uniformMaterial;
            if (faceMaterials && i / 3 < faceMaterials->size()) {
                materialIndex = (*faceMaterials)[i / 3];
            }
            materialIndex = std::max(0, std::min(static_cast<int>(materials.size()) - 1,
                                                 materialIndex));
            const Vec3& base = materials[materialIndex].diffuse;
            triangle.color = QColor::fromRgbF(std::min(1.0f, base.x() * brightness),
                                              std::min(1.0f, base.y() * brightness),
                                              std::min(1.0f, base.z() * brightness));
            triangle.depth = depthSum / 3.0f;
            triangles.push_back(triangle);
        }
    };
    appendMesh(branchMesh, nullptr, 0, false);
    appendMesh(leafMesh, leafMaterials.empty() ? nullptr : &leafMaterials, 1, true);

    std::sort(triangles.begin(), triangles.end(),
              [](const Triangle& a, const Triangle& b) { return a.depth < b.depth; });

    QPainter painter(&image);
    painter.setPen(Qt::NoPen);
    for (const Triangle& triangle : triangles) {
        painter.setBrush(triangle.color);
        painter.drawPolygon(triangle.points, 3);
    }
    painter.setPen(QColor(170, 190, 210));
    painter.drawText(12, height - 14,
                     QStringLiteral("branchTris=%1  leafTris=%2  materials=%3")
                         .arg(branchMesh.stats.triangleCount)
                         .arg(leafMesh.stats.triangleCount)
                         .arg(materials.size()));
    painter.end();
    return image;
}

} // namespace

int main(int argc, char* argv[]) {
    QGuiApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("PlantModelExportDemo"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        QStringLiteral("Optimize the plant branch mesh, generate leaves, export LOD OBJ models."));
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
    QCommandLineOption spacingOption(QStringLiteral("spacing"),
                                     QStringLiteral("Scalar field sampling spacing."),
                                     QStringLiteral("value"), QStringLiteral("0.05"));
    QCommandLineOption smoothOption(QStringLiteral("smooth"),
                                    QStringLiteral("Laplacian smoothing iterations."),
                                    QStringLiteral("count"), QStringLiteral("8"));
    QCommandLineOption junctionOption(QStringLiteral("junction-passes"),
                                      QStringLiteral("Extra smoothing passes at branch junctions."),
                                      QStringLiteral("count"), QStringLiteral("6"));
    QCommandLineOption lod1Option(QStringLiteral("lod1"),
                                  QStringLiteral("LOD1 clustering cell size."),
                                  QStringLiteral("size"), QStringLiteral("0.075"));
    QCommandLineOption lod2Option(QStringLiteral("lod2"),
                                  QStringLiteral("LOD2 clustering cell size."),
                                  QStringLiteral("size"), QStringLiteral("0.12"));
    QCommandLineOption outputOption({QStringLiteral("o"), QStringLiteral("output")},
                                    QStringLiteral("Output OBJ path (LOD0); LOD1/LOD2 share the stem."),
                                    QStringLiteral("file"),
                                    QStringLiteral("examples/plant_cherry_lod0.obj"));
    QCommandLineOption summaryOption(QStringLiteral("summary"),
                                     QStringLiteral("Export summary JSON path."),
                                     QStringLiteral("file"));
    QCommandLineOption previewOption(QStringLiteral("preview"),
                                     QStringLiteral("Preview PNG output path."),
                                     QStringLiteral("file"));
    QCommandLineOption noPreviewOption(QStringLiteral("no-preview"),
                                       QStringLiteral("Skip software preview rendering."));

    for (const QCommandLineOption* option :
         {&presetOption, &iterationsOption, &seedOption, &thresholdOption, &spacingOption,
          &smoothOption, &junctionOption, &lod1Option, &lod2Option, &outputOption,
          &summaryOption, &previewOption, &noPreviewOption}) {
        parser.addOption(*option);
    }
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
    const float spacing = parser.value(spacingOption).toFloat(&valid);
    if (!valid || spacing <= 0.0f) {
        err << "Spacing must be positive." << Qt::endl;
        return 5;
    }
    const int smoothIterations = parser.value(smoothOption).toInt(&valid);
    if (!valid || smoothIterations < 0) {
        err << "Smoothing iterations must be non-negative." << Qt::endl;
        return 6;
    }
    const int junctionPasses = parser.value(junctionOption).toInt(&valid);
    if (!valid || junctionPasses < 0) {
        err << "Junction passes must be non-negative." << Qt::endl;
        return 7;
    }
    const float lod1Cell = parser.value(lod1Option).toFloat(&valid);
    const float lod2Cell = parser.value(lod2Option).toFloat(&valid);
    if (!valid || lod1Cell <= 0.0f || lod2Cell <= 0.0f || lod2Cell < lod1Cell) {
        err << "LOD cell sizes must satisfy 0 < lod1 <= lod2." << Qt::endl;
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

    // 1) 隐式场 + Marching Cubes 提取原始枝干网格。
    MetaballFieldSettings fieldSettings;
    fieldSettings.isoThreshold = threshold;
    fieldSettings.jointSmoothness = 0.65f;
    fieldSettings.influenceScale = 2.0f;
    fieldSettings.boundsPadding = spacing;
    MetaballField field;
    field.rebuildFromPlant(model, fieldSettings);
    const ScalarFieldGrid grid = field.sampleGrid(spacing);
    SurfaceMesh lod0 = MarchingCubes::extract(grid, field.isoThreshold());
    if (!lod0.isValid()) {
        err << "Marching Cubes extraction produced no triangles." << Qt::endl;
        return 10;
    }
    const SurfaceMeshStats rawStats = lod0.stats;

    // 2) 拉普拉斯平滑 + 连接处凹陷增强处理。
    LaplacianSmoothingSettings smoothSettings;
    smoothSettings.iterations = smoothIterations;
    smoothSettings.junctionExtraIterations = junctionPasses;
    std::vector<JunctionRegion> junctions;
    for (const MetaballNodeSource& source : field.nodeSources()) {
        if (source.junction) {
            junctions.push_back(JunctionRegion{source.center, source.influenceRadius});
        }
    }
    MeshProcessing::laplacianSmooth(lod0, smoothSettings, junctions);
    if (!MarchingCubes::validate(lod0, &error)) {
        err << "Smoothed mesh validation failed: " << error << Qt::endl;
        return 11;
    }

    // 3) 顶点聚类简化生成 LOD1 / LOD2。
    SurfaceMesh lod1 = MeshProcessing::simplifyVertexClustering(lod0, lod1Cell);
    SurfaceMesh lod2 = MeshProcessing::simplifyVertexClustering(lod0, lod2Cell);

    // 4) 生成叶片并绑定到骨架节点。
    LeafGenerationSettings leafSettings;
    leafSettings.seed = seed;
    const GeneratedLeaves leaves = LeafGenerator::generate(model, leafSettings);
    // 叶片调色板索引整体偏移 1（材质 0 是树皮），得到全局材质索引。
    std::vector<std::uint16_t> leafFaceMaterials = leaves.faceMaterials;
    for (std::uint16_t& material : leafFaceMaterials) {
        material = static_cast<std::uint16_t>(material + 1);
    }

    // 5) 多材质 OBJ + MTL 导出（LOD0/1/2 三个细节层次）。
    const std::vector<ObjMaterial> materials = buildMaterials();
    const QFileInfo objInfo(parser.value(outputOption));
    QDir().mkpath(objInfo.absolutePath());
    const QString baseName = objInfo.completeBaseName();
    const QString baseStem = baseName.endsWith(QStringLiteral("_lod0"))
        ? baseName.left(baseName.size() - 5) : baseName;
    const QStringList lodSuffixes{QStringLiteral("lod0"),
                                  QStringLiteral("lod1"),
                                  QStringLiteral("lod2")};
    const SurfaceMesh* lodMeshes[3] = {&lod0, &lod1, &lod2};
    QStringList exportedFiles;
    for (int lod = 0; lod < 3; ++lod) {
        const QString lodPath = objInfo.absolutePath() + QStringLiteral("/") +
                                baseStem + QStringLiteral("_") + lodSuffixes[lod] +
                                QStringLiteral(".obj");
        const std::vector<ObjMeshGroup> groups{
            ObjMeshGroup{lodMeshes[lod], QStringLiteral("branches"), 0, nullptr},
            ObjMeshGroup{&leaves.mesh, QStringLiteral("leaves"), 1, &leafFaceMaterials}
        };
        if (!MeshExporter::saveObj(lodPath, materials, groups, &error)) {
            err << error << Qt::endl;
            return 12;
        }
        exportedFiles << lodPath;
    }

    // 6) 摘要 JSON 与彩色预览。
    const QString summaryPath = parser.isSet(summaryOption)
        ? QFileInfo(parser.value(summaryOption)).absoluteFilePath()
        : objInfo.absolutePath() + QStringLiteral("/") + baseStem +
              QStringLiteral("_export.json");
    const QJsonObject summary{
        {QStringLiteral("format"), QStringLiteral("PlantSim Plant Model Export Summary")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("preset"), preset.key},
        {QStringLiteral("iterations"), iterations},
        {QStringLiteral("seed"), static_cast<qint64>(seed)},
        {QStringLiteral("plantNodeCount"), static_cast<qint64>(model.nodeCount())},
        {QStringLiteral("isoLevel"), field.isoThreshold()},
        {QStringLiteral("smoothing"), QJsonObject{
             {QStringLiteral("iterations"), smoothSettings.iterations},
             {QStringLiteral("lambda"), smoothSettings.lambda},
             {QStringLiteral("taubinCompensation"), smoothSettings.taubinCompensation},
             {QStringLiteral("mu"), smoothSettings.mu},
             {QStringLiteral("junctionRegionCount"),
              static_cast<qint64>(junctions.size())},
             {QStringLiteral("junctionExtraIterations"),
              smoothSettings.junctionExtraIterations}}},
        {QStringLiteral("rawBranchMesh"), meshStatsJson(rawStats)},
        {QStringLiteral("lod"), QJsonArray{
             QJsonObject{{QStringLiteral("level"), 0},
                         {QStringLiteral("cellSize"), 0.0},
                         {QStringLiteral("mesh"), meshStatsJson(lod0.stats)}},
             QJsonObject{{QStringLiteral("level"), 1},
                         {QStringLiteral("cellSize"), lod1Cell},
                         {QStringLiteral("mesh"), meshStatsJson(lod1.stats)}},
             QJsonObject{{QStringLiteral("level"), 2},
                         {QStringLiteral("cellSize"), lod2Cell},
                         {QStringLiteral("mesh"), meshStatsJson(lod2.stats)}}}},
        {QStringLiteral("leaves"), QJsonObject{
             {QStringLiteral("leafCount"), leaves.leafCount},
             {QStringLiteral("boundNodeCount"), leaves.boundNodeCount},
             {QStringLiteral("triangleCount"),
              static_cast<qint64>(leaves.mesh.stats.triangleCount)},
             {QStringLiteral("lengthSegments"), leafSettings.lengthSegments},
             {QStringLiteral("paletteSize"), 4}}},
        {QStringLiteral("materials"), QJsonArray{
             QStringLiteral("bark"), QStringLiteral("leaf_0"), QStringLiteral("leaf_1"),
             QStringLiteral("leaf_2"), QStringLiteral("leaf_3")}},
        {QStringLiteral("files"), QJsonArray::fromStringList(exportedFiles)}
    };
    {
        QFile summaryFile(summaryPath);
        if (!summaryFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            err << "Cannot write summary: " << summaryPath << Qt::endl;
            return 13;
        }
        summaryFile.write(QJsonDocument(summary).toJson(QJsonDocument::Indented));
    }

    QString previewPath;
    if (!parser.isSet(noPreviewOption)) {
        previewPath = parser.isSet(previewOption)
            ? QFileInfo(parser.value(previewOption)).absoluteFilePath()
            : objInfo.absolutePath() + QStringLiteral("/") + baseStem +
                  QStringLiteral("_preview.png");
        const QImage preview = renderPreview(lod0, leaves.mesh, leafFaceMaterials,
                                             materials, 960, 960, -35.0f, 18.0f);
        if (!preview.save(previewPath)) {
            err << "Cannot save preview: " << previewPath << Qt::endl;
            return 14;
        }
    }

    out << "Plant model exported" << Qt::endl
        << "  preset=" << preset.key << ", iterations=" << iterations
        << ", seed=" << seed << Qt::endl
        << "  raw: vertices=" << static_cast<qulonglong>(rawStats.vertexCount)
        << ", triangles=" << static_cast<qulonglong>(rawStats.triangleCount) << Qt::endl
        << "  lod0(smoothed): vertices=" << static_cast<qulonglong>(lod0.stats.vertexCount)
        << ", triangles=" << static_cast<qulonglong>(lod0.stats.triangleCount)
        << ", watertight=" << (lod0.stats.watertight ? "yes" : "no")
        << ", volume=" << lod0.stats.signedVolume << Qt::endl
        << "  lod1: vertices=" << static_cast<qulonglong>(lod1.stats.vertexCount)
        << ", triangles=" << static_cast<qulonglong>(lod1.stats.triangleCount) << Qt::endl
        << "  lod2: vertices=" << static_cast<qulonglong>(lod2.stats.vertexCount)
        << ", triangles=" << static_cast<qulonglong>(lod2.stats.triangleCount) << Qt::endl
        << "  leaves=" << leaves.leafCount
        << ", boundNodes=" << leaves.boundNodeCount
        << ", leafTriangles=" << static_cast<qulonglong>(leaves.mesh.stats.triangleCount)
        << Qt::endl
        << "  junctionRegions=" << static_cast<qulonglong>(junctions.size()) << Qt::endl
        << "  files=" << exportedFiles.join(QStringLiteral(", ")) << Qt::endl
        << "  summary=" << summaryPath << Qt::endl;
    if (!previewPath.isEmpty()) {
        out << "  preview=" << previewPath << Qt::endl;
    }
    return 0;
}
