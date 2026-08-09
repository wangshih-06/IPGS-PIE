#include <cstdlib>
#include <iostream>

#include <QCoreApplication>
#include <QJsonDocument>

#include "Editing/ScaleTool.h"
#include "Engine/SimulationEngine.h"

namespace {
void require(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "PlantEditEngineRegression failure: " << message << '\n';
    std::exit(1);
}

QByteArray compactPlantJson(const PlantModel& model) {
    return QJsonDocument(model.toJson()).toJson(QJsonDocument::Compact);
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    SimulationEngine engine;
    const int rootId = engine.plantModel().rootNodeId();
    require(rootId >= 0, "engine fixture must contain a root node");

    const QByteArray initialPlant = compactPlantJson(engine.plantModel());
    const quint64 initialMeshVersion = engine.meshVersion();
    const quint64 initialEditRevision = engine.editRevision();

    QString error;
    ScaleParams invalidScale;
    invalidScale.scale = Vec3(0.0f, 1.0f, 1.0f);
    require(!engine.applyScaleEdit(rootId, invalidScale, false, &error),
            "invalid scale factors must be rejected");
    require(!error.isEmpty(), "invalid edit should explain the rejection");
    require(engine.meshVersion() == initialMeshVersion &&
            engine.editRevision() == initialEditRevision,
            "rejected edits must not rebuild the mesh or increment the revision");
    require(compactPlantJson(engine.plantModel()) == initialPlant,
            "rejected edits must not alter the plant model");

    ScaleParams scale;
    scale.scale = Vec3::Constant(1.025f);
    engine.beginEdit();
    require(engine.applyScaleEdit(rootId, scale, true, &error),
            "preview scale edit should be accepted");
    require(engine.meshVersion() == initialMeshVersion + 1 &&
            engine.editRevision() == initialEditRevision + 1,
            "preview edit must rebuild exactly one preview mesh and revision");
    const QByteArray editedPlant = compactPlantJson(engine.plantModel());
    require(editedPlant != initialPlant, "accepted edit must change the plant state");

    require(engine.commitEdit(), "committing a preview edit should record undo history");
    require(engine.canUndo(), "committed edit should enable undo");
    require(engine.meshVersion() == initialMeshVersion + 2 &&
            engine.editRevision() == initialEditRevision + 2,
            "commit must rebuild the committed mesh and increment the revision");

    require(engine.undoLastEdit(&error), "undo should restore the pre-edit snapshot");
    require(compactPlantJson(engine.plantModel()) == initialPlant,
            "undo must restore the exact initial plant JSON");
    require(engine.meshVersion() == initialMeshVersion + 3 &&
            engine.editRevision() == initialEditRevision + 3,
            "undo must rebuild one authoritative mesh and revision");

    require(engine.applyScaleEdit(rootId, scale, false, &error),
            "one-shot scale edit should be accepted");
    require(compactPlantJson(engine.plantModel()) != initialPlant,
            "one-shot edit must change the plant before reset");

    const quint64 meshBeforeReset = engine.meshVersion();
    const quint64 revisionBeforeReset = engine.editRevision();
    engine.resetPlant();
    require(compactPlantJson(engine.plantModel()) == initialPlant,
            "reset must restore the constructor snapshot");
    require(!engine.canUndo(), "reset must clear edit history");
    require(engine.meshVersion() == meshBeforeReset + 1 &&
            engine.editRevision() == revisionBeforeReset + 1,
            "reset must rebuild once and advance the edit revision");

    std::cout << "Plant edit engine regression checks passed. meshVersion="
              << engine.meshVersion() << ", revision=" << engine.editRevision() << '\n';
    return 0;
}
