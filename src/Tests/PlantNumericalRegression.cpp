#include <QCoreApplication>
#include <QDebug>
#include <QString>

#include <cmath>
#include <vector>

#include "Engine/GrowthDataRecorder.h"
#include "Geometry/MarchingCubes.h"
#include "Implicit/MetaballField.h"
#include "Plant/PlantModel.h"

namespace {
constexpr float kTolerance = 1.0e-5f;

bool require(bool condition, const QString& message) {
    if (!condition) qCritical().noquote() << QStringLiteral("FAILED:") << message;
    return condition;
}

float linearReference(const MetaballField& field, const Vec3& point) {
    float value = 0.0f;
    for (const MetaballNodeSource& source : field.nodeSources()) {
        value += MetaballField::compactKernel((point - source.center).squaredNorm(),
                                              source.influenceRadius, source.weight);
    }
    return value;
}

bool testMetaballAndMarchingCubes() {
    MetaballField field;
    field.setIsoThreshold(0.5f);
    // More sources than a leaf bucket hold, forcing the exact spatial index to
    // be exercised instead of merely taking the small-field fast path.
    for (int x = -3; x <= 3; ++x) {
        for (int y = -2; y <= 2; ++y) {
            for (int z = -3; z <= 3; ++z) {
                MetaballNodeSource source;
                source.center = Vec3(static_cast<float>(x) * 0.38f,
                                     static_cast<float>(y) * 0.38f,
                                     static_cast<float>(z) * 0.38f);
                source.influenceRadius = 0.31f;
                source.weight = 0.72f + 0.01f * static_cast<float>((x + y + z + 8) % 5);
                field.addNodeSource(source);
            }
        }
    }

    const std::vector<Vec3> probePoints{
        Vec3::Zero(), Vec3(0.17f, -0.21f, 0.28f), Vec3(-0.93f, 0.66f, -0.47f), Vec3(3.0f, 2.0f, 1.0f)
    };
    for (const Vec3& point : probePoints) {
        const float accelerated = field.evaluate(point);
        const float reference = linearReference(field, point);
        if (!require(std::abs(accelerated - reference) <= kTolerance,
                     QStringLiteral("Metaball spatial-index result differs from linear reference."))) {
            return false;
        }
    }

    // A single compact source yields a deterministic closed iso-surface.
    MetaballField sphereField;
    sphereField.setIsoThreshold(0.5f);
    sphereField.addNodeSource(MetaballNodeSource{Vec3::Zero(), 1.0f, 1.0f});
    const ScalarFieldGrid grid = sphereField.sampleGrid(0.12f, 100000);
    if (!require(grid.isValid() && grid.sampleCount() > 1000 &&
                 grid.minimumValue <= 0.0f && grid.maximumValue >= sphereField.isoThreshold(),
                 QStringLiteral("Metaball grid sampling did not produce a valid signed range."))) {
        return false;
    }

    const SurfaceMesh mesh = MarchingCubes::extract(grid, sphereField.isoThreshold());
    QString meshError;
    return require(mesh.isValid() && mesh.stats.triangleCount > 0 &&
                   mesh.stats.surfaceArea > 0.0 && MarchingCubes::validate(mesh, &meshError),
                   QStringLiteral("Marching Cubes regression failed: %1").arg(meshError));
}

bool testGrowthRecorder() {
    PlantModel plant;
    PlantNode* root = plant.createRootNode(Vec3::Zero(), Vec3::UnitY(), 0.16f);
    PlantNode* child = root ? plant.addNode(root->id, Vec3(0.0f, 0.9f, 0.0f),
                                             Vec3::UnitY(), 0.08f, 0.9f) : nullptr;
    if (!require(root != nullptr && child != nullptr, QStringLiteral("Cannot construct recorder test plant."))) {
        return false;
    }
    plant.addLeaf(child->id, Vec3(0.2f, 1.2f, 0.0f), Vec3::UnitX(), Vec2(0.3f, 0.12f));

    GrowthDataRecorder recorder;
    recorder.setSnapshotInterval(2);
    for (int frame = 0; frame < 5; ++frame) {
        plant.age = static_cast<float>(frame);
        recorder.capture(plant, frame == 3);
    }
    if (!require(recorder.size() == 5 && recorder.snapshotCount() == 4,
                 QStringLiteral("Recorder did not preserve expected metric/checkpoint cadence."))) {
        return false;
    }
    if (!require(recorder.at(1) && !recorder.at(1)->hasPlantState() &&
                 recorder.at(3) && recorder.at(3)->hasPlantState(),
                 QStringLiteral("Recorder checkpoint frame policy is incorrect."))) {
        return false;
    }
    const PlantGrowthMetrics* cached = recorder.cachedMetrics(4.0f);
    if (!require(cached && cached->branchCount == 1 && cached->leafCount == 1 &&
                 recorder.cachedMetrics(3.5f) == nullptr,
                 QStringLiteral("Recorder metric cache did not honor the frame age."))) {
        return false;
    }
    const GrowthDataFrame* checkpoint = recorder.nearestSnapshot(3.2f);
    if (!require(checkpoint && checkpoint->step == 3,
                 QStringLiteral("Recorder nearest checkpoint lookup is incorrect."))) {
        return false;
    }
    const QJsonArray metricFrames = recorder.metricsToJson().value("frames").toArray();
    if (!require(metricFrames.size() == 5 && !metricFrames.at(0).toObject().contains("plantState"),
                 QStringLiteral("Lightweight recorder export contains skeleton snapshots."))) {
        return false;
    }
    recorder.truncateAfter(2.0f);
    return require(recorder.size() == 3 && recorder.snapshotCount() == 2,
                   QStringLiteral("Recorder truncation failed to rebuild checkpoints."));
}
}  // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    if (!testMetaballAndMarchingCubes()) return 1;
    if (!testGrowthRecorder()) return 2;
    qInfo() << "Plant numerical regression suite passed.";
    return 0;
}

