// ============================================================================
// WebSocketServer - Qt WebSockets transport for the local PlantSim control API
// ============================================================================
#pragma once

#include <QObject>

#include <functional>
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QTimer>

#include "Engine/GrowthStateReport.h"

class QWebSocket;
class QWebSocketServer;
class SimulationEngine;

class WebSocketServer : public QObject {
    Q_OBJECT
public:
    explicit WebSocketServer(QObject* parent = nullptr);

    bool listen(quint16 port = 4317);
    quint16 port() const;
    // The local Qt WebSocket endpoint invokes interactive edits directly on the
    // engine so replies can include authoritative mesh and revision metadata.
    void setSimulationEngine(SimulationEngine* engine);

signals:
    void clientConnected();
    void clientDisconnected();
    void lightIntensityRequested(float intensity);
    void phototropismRequested(float weight);
    void gravitropismRequested(float weight);
    void lightPositionRequested(int lightId, float x, float y, float z);
    void growthStartRequested();
    void growthPauseRequested();
    void growthResumeRequested();
    void growthResetRequested();
    void growthSeekRequested(float age);
    void growthStageRequested(const QString& stage);
    void growthSpeedRequested(float speed);
    void growthDataRequested();
    void logMessage(const QString& message);

public slots:
    void broadcastState(float lightIntensity);
    void broadcastTropismState(float photoWeight, float graviWeight);
    void broadcastGrowthState(const GrowthStateReport& report);
    void broadcastGrowthData(const QJsonObject& data);

private slots:
    void onNewConnection();
    void onTextMessageReceived(const QString& message);
    void onPong(quint64 elapsedTime, const QByteArray& payload);
    void onDisconnected();
    void flushPendingGrowthState();
    void sendHeartbeat();

private:
    struct ClientState {
        QDateTime lastPongUtc;
    };

    using CommandHandler = std::function<void(QWebSocket*, const QJsonObject&)>;

    void initializeCommandHandlers();
    void processTextMessage(QWebSocket* socket, const QByteArray& payload);
    void sendError(QWebSocket* socket, const QString& code, const QString& message);
    void sendEditUpdated(QWebSocket* socket, const QJsonObject& command,
                         int nodeId, bool rebuildCompleted);
    bool resolveEditNode(const QJsonObject& command, int* nodeId, QString* error) const;
    void sendJson(QWebSocket* socket, const QByteArray& payload);
    void broadcastJson(const QByteArray& payload);
    void broadcastGrowthStateNow(const GrowthStateReport& report);
    void removeClient(QWebSocket* socket);

    QWebSocketServer* server_ = nullptr;
    SimulationEngine* simulationEngine_ = nullptr;
    QHash<QWebSocket*, ClientState> clients_;
    QHash<QString, CommandHandler> commandHandlers_;
    QTimer growthBroadcastTimer_;
    QTimer heartbeatTimer_;
    GrowthStateReport pendingGrowthReport_;
    bool hasPendingGrowthReport_ = false;
    quint16 port_ = 0;
};
