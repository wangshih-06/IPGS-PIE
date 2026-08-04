// ============================================================================
// PlantSimulationSystem - 程序入口
// 基于物理约束与隐式曲面的智能植物生长模拟与交互式编辑系统
// ============================================================================
#include <QApplication>
#include <QDebug>
#include <QSurfaceFormat>

#include "Engine/EngineWindow.h"
#include "Engine/SimulationEngine.h"
#include "Networking/WebSocketServer.h"

int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("PlantSim"));
    app.setApplicationDisplayName(QStringLiteral("PlantSim | Growth Lab"));

    QSurfaceFormat::setDefaultFormat(QSurfaceFormat::defaultFormat());
    SimulationEngine simulationEngine;
    WebSocketServer webSocketServer;

    EngineWindow window(&simulationEngine);
    QObject::connect(&webSocketServer, &WebSocketServer::lightIntensityRequested,
                     &simulationEngine, &SimulationEngine::setLightIntensity);
    QObject::connect(&simulationEngine, &SimulationEngine::environmentUpdated,
                     &webSocketServer, &WebSocketServer::broadcastState);
    QObject::connect(&webSocketServer, &WebSocketServer::logMessage,
                     [&window](const QString& message) {
                         qInfo().noquote() << message;
                         window.showEngineMessage(message);
                     });

    webSocketServer.listen(4317);
    window.show();

    return app.exec();
}
