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
    webSocketServer.setSimulationEngine(&simulationEngine);

    EngineWindow window(&simulationEngine);
    QObject::connect(&webSocketServer, &WebSocketServer::lightIntensityRequested,
                     &simulationEngine, &SimulationEngine::setLightIntensity);
    QObject::connect(&webSocketServer, &WebSocketServer::phototropismRequested,
                     &simulationEngine, &SimulationEngine::setPhototropismWeight);
    QObject::connect(&webSocketServer, &WebSocketServer::gravitropismRequested,
                     &simulationEngine, &SimulationEngine::setGravitropismWeight);
    QObject::connect(&webSocketServer, &WebSocketServer::lightPositionRequested,
                     &simulationEngine, &SimulationEngine::setLightSourcePosition);
    QObject::connect(&webSocketServer, &WebSocketServer::growthStartRequested,
                     &simulationEngine, &SimulationEngine::startGrowth);
    QObject::connect(&webSocketServer, &WebSocketServer::growthPauseRequested,
                     &simulationEngine, &SimulationEngine::pauseGrowth);
    QObject::connect(&webSocketServer, &WebSocketServer::growthResumeRequested,
                     &simulationEngine, &SimulationEngine::resumeGrowth);
    QObject::connect(&webSocketServer, &WebSocketServer::growthResetRequested,
                     [&simulationEngine]() { simulationEngine.resetGrowth(0.0f); });
    QObject::connect(&webSocketServer, &WebSocketServer::growthSeekRequested,
                     &simulationEngine, &SimulationEngine::seekGrowth);
    QObject::connect(&webSocketServer, &WebSocketServer::growthStageRequested,
                     &simulationEngine, &SimulationEngine::jumpToGrowthStage);
    QObject::connect(&webSocketServer, &WebSocketServer::growthSpeedRequested,
                     &simulationEngine, &SimulationEngine::setGrowthSpeed);
    QObject::connect(&webSocketServer, &WebSocketServer::growthDataRequested,
                     &simulationEngine, &SimulationEngine::requestGrowthData);

    QObject::connect(&simulationEngine, &SimulationEngine::environmentUpdated,
                     &webSocketServer, &WebSocketServer::broadcastState);
    QObject::connect(&simulationEngine, &SimulationEngine::tropismUpdated,
                     &webSocketServer, &WebSocketServer::broadcastTropismState);
    QObject::connect(&simulationEngine, &SimulationEngine::growthUpdated,
                     &webSocketServer, &WebSocketServer::broadcastGrowthState);
    QObject::connect(&simulationEngine, &SimulationEngine::growthDataAvailable,
                     &webSocketServer, &WebSocketServer::broadcastGrowthData);

    QObject::connect(&webSocketServer, &WebSocketServer::logMessage,
                     [&window](const QString& message) {
                         qInfo().noquote() << message;
                         window.showEngineMessage(message);
                     });

    webSocketServer.listen(4317);
    window.show();

    return app.exec();
}
