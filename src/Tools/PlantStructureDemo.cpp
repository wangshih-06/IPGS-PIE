#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QTextStream>

#include "Plant/PlantModel.h"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("PlantStructureDemo"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.0"));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Plant skeleton tree and JSON serialization demo"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption loadOption({QStringLiteral("l"), QStringLiteral("load")},
                                  QStringLiteral("Load and display an existing skeleton JSON file."),
                                  QStringLiteral("file"));
    QCommandLineOption outputOption({QStringLiteral("o"), QStringLiteral("output")},
                                    QStringLiteral("Write the generated demo skeleton to this file."),
                                    QStringLiteral("file"), QStringLiteral("plant_skeleton.json"));
    parser.addOption(loadOption);
    parser.addOption(outputOption);
    parser.process(app);

    QTextStream out(stdout);
    QTextStream err(stderr);
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
    out.setCodec("UTF-8");
    err.setCodec("UTF-8");
#endif

    PlantModel model;
    QString error;
    if (parser.isSet(loadOption)) {
        const QString inputPath = parser.value(loadOption);
        if (!PlantModel::loadJson(inputPath, &model, &error)) {
            err << "Load failed: " << error << Qt::endl;
            return 1;
        }
        out << "Loaded: " << QFileInfo(inputPath).absoluteFilePath() << Qt::endl;
    } else {
        model = PlantModel::createDemoTree();
        const QString outputPath = parser.value(outputOption);
        if (!model.saveJson(outputPath, &error)) {
            err << "Save failed: " << error << Qt::endl;
            return 2;
        }

        // Read the file back immediately so the demo also verifies round-trip
        // deserialization and parent-child reconstruction.
        PlantModel restored;
        if (!PlantModel::loadJson(outputPath, &restored, &error)) {
            err << "Round-trip load failed: " << error << Qt::endl;
            return 3;
        }
        if (restored.nodeCount() != model.nodeCount() ||
            restored.branches().size() != model.branches().size() ||
            restored.leaves().size() != model.leaves().size() ||
            restored.roots().size() != model.roots().size()) {
            err << "Round-trip verification failed: structure counts changed." << Qt::endl;
            return 4;
        }
        model = std::move(restored);
        out << "JSON written and verified: " << QFileInfo(outputPath).absoluteFilePath() << Qt::endl;
    }

    out << Qt::endl << model.toTreeString() << Qt::endl;
    return 0;
}
