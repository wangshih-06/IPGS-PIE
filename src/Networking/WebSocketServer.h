// ============================================================================
// WebSocketServer - Qt WebSockets transport for the local PlantSim control API
// ============================================================================
#pragma once

#include <QObject>
#include <QByteArray>
#include <QDateTime>
#include <QHash>
#include <QTimer>

#include "Engine/GrowthStateReport.h"

class QWebSocket;
class QWebSocketServer;

class WebSocketServer : public QObject {
    Q_OBJECT
public:
    explicit WebSocketServer(QObject* parent = nullptr);

    bool listen(quint16 port = 4317);
    quint16 port() const;

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

    void processTextMessage(QWebSocket* socket, const QByteArray& payload);
    void sendJson(QWebSocket* socket, const QByteArray& payload);
    void broadcastJson(const QByteArray& payload);
    void broadcastGrowthStateNow(const GrowthStateReport& report);
    void removeClient(QWebSocket* socket);

    QWebSocketServer* server_ = nullptr;
    QHash<QWebSocket*, ClientState> clients_;
    QTimer growthBroadcastTimer_;
    QTimer heartbeatTimer_;
    GrowthStateReport pendingGrowthReport_;
    bool hasPendingGrowthReport_ = false;
    quint16 port_ = 0;
};
