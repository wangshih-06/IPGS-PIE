// ============================================================================
// WebSocketServer - small Qt Network based RFC 6455 text server
// 第11周：增加向光性/向地性与多光源控制 WebSocket 信号
// ============================================================================
#pragma once

#include <QObject>
#include <QByteArray>
#include <QHash>
#include <QPointer>
#include <QTimer>

#include "Engine/GrowthStateReport.h"

class QTcpServer;
class QTcpSocket;

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
    void onReadyRead();
    void onDisconnected();
    void flushPendingGrowthState();

private:
    struct ClientState {
        QByteArray buffer;
        bool handshaken = false;
    };

    void handleHttpHandshake(QTcpSocket* socket, ClientState& state);
    void processFrames(QTcpSocket* socket, ClientState& state);
    void processTextMessage(QTcpSocket* socket, const QByteArray& payload);
    void sendText(QTcpSocket* socket, const QByteArray& payload);
    void sendPong(QTcpSocket* socket, const QByteArray& payload);
    void broadcastGrowthStateNow(const GrowthStateReport& report);
    void removeClient(QTcpSocket* socket);

    QTcpServer* server_ = nullptr;
    QHash<QTcpSocket*, ClientState> clients_;
    QTimer growthBroadcastTimer_;
    GrowthStateReport pendingGrowthReport_;
    bool hasPendingGrowthReport_ = false;
    quint16 port_ = 0;
};
