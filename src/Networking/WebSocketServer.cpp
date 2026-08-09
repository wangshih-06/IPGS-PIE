// ============================================================================
// WebSocketServer implementation
// ============================================================================
#include "Networking/WebSocketServer.h"

#include <algorithm>

#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QWebSocket>
#include <QWebSocketServer>

namespace {
constexpr int kGrowthBroadcastIntervalMs = 66;
constexpr int kHeartbeatIntervalMs = 15000;
constexpr int kHeartbeatTimeoutMs = 45000;
constexpr int kCompressionThresholdBytes = 4096;
constexpr char kCompressedPayloadMagic[] = "PSZ1";

float clampLight(float value) {
    return qBound(0.0f, value, 1.0f);
}
}

WebSocketServer::WebSocketServer(QObject* parent)
    : QObject(parent),
      server_(new QWebSocketServer(QStringLiteral("PlantSim local control"),
                                   QWebSocketServer::NonSecureMode, this)) {
    growthBroadcastTimer_.setInterval(kGrowthBroadcastIntervalMs);
    growthBroadcastTimer_.setSingleShot(true);
    heartbeatTimer_.setInterval(kHeartbeatIntervalMs);

    connect(server_, &QWebSocketServer::newConnection,
            this, &WebSocketServer::onNewConnection);
    connect(&growthBroadcastTimer_, &QTimer::timeout,
            this, &WebSocketServer::flushPendingGrowthState);
    connect(&heartbeatTimer_, &QTimer::timeout,
            this, &WebSocketServer::sendHeartbeat);
}

bool WebSocketServer::listen(quint16 port) {
    if (!server_->listen(QHostAddress::LocalHost, port)) {
        emit logMessage(QStringLiteral("WebSocket listen failed: %1").arg(server_->errorString()));
        return false;
    }
    port_ = server_->serverPort();
    heartbeatTimer_.start();
    emit logMessage(QStringLiteral("WebSocket listening on ws://127.0.0.1:%1").arg(port_));
    return true;
}

quint16 WebSocketServer::port() const {
    return port_;
}

void WebSocketServer::onNewConnection() {
    while (server_->hasPendingConnections()) {
        QWebSocket* socket = server_->nextPendingConnection();
        socket->setParent(this);
        clients_.insert(socket, {QDateTime::currentDateTimeUtc()});
        connect(socket, &QWebSocket::textMessageReceived,
                this, &WebSocketServer::onTextMessageReceived);
        connect(socket, &QWebSocket::pong, this, &WebSocketServer::onPong);
        connect(socket, &QWebSocket::disconnected,
                this, &WebSocketServer::onDisconnected);
        emit clientConnected();
        emit logMessage(QStringLiteral("WebSocket client connected"));
    }
}

void WebSocketServer::onTextMessageReceived(const QString& message) {
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (!socket || !clients_.contains(socket)) {
        return;
    }
    clients_[socket].lastPongUtc = QDateTime::currentDateTimeUtc();
    processTextMessage(socket, message.toUtf8());
}

void WebSocketServer::onPong(quint64, const QByteArray&) {
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    if (socket && clients_.contains(socket)) {
        clients_[socket].lastPongUtc = QDateTime::currentDateTimeUtc();
    }
}

void WebSocketServer::processTextMessage(QWebSocket* socket, const QByteArray& payload) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        sendJson(socket, QJsonDocument(QJsonObject{
            {"type", "error"}, {"message", "Invalid JSON command"}
        }).toJson(QJsonDocument::Compact));
        return;
    }

    const QJsonObject command = document.object();
    const QString type = command.value("type").toString();
    if (type == QStringLiteral("adjust_light")) {
        const float requested = static_cast<float>(command.value("value").toDouble(0.0));
        emit lightIntensityRequested(clampLight(requested));
        return;
    }
    if (type == QStringLiteral("set_phototropism")) {
        emit phototropismRequested(qBound(0.0f, static_cast<float>(command.value("value").toDouble(1.0)), 2.0f));
        return;
    }
    if (type == QStringLiteral("set_gravitropism")) {
        emit gravitropismRequested(qBound(0.0f, static_cast<float>(command.value("value").toDouble(1.0)), 2.0f));
        return;
    }
    if (type == QStringLiteral("set_light_position")) {
        emit lightPositionRequested(command.value("id").toInt(0),
                                    static_cast<float>(command.value("x").toDouble(0.0)),
                                    static_cast<float>(command.value("y").toDouble(0.0)),
                                    static_cast<float>(command.value("z").toDouble(0.0)));
        return;
    }
    if (type == QStringLiteral("growth_start")) {
        emit growthStartRequested();
        return;
    }
    if (type == QStringLiteral("growth_pause")) {
        emit growthPauseRequested();
        return;
    }
    if (type == QStringLiteral("growth_resume")) {
        emit growthResumeRequested();
        return;
    }
    if (type == QStringLiteral("growth_reset")) {
        emit growthResetRequested();
        return;
    }
    if (type == QStringLiteral("growth_seek")) {
        emit growthSeekRequested(std::max(0.0f, static_cast<float>(command.value("age").toDouble(0.0))));
        return;
    }
    if (type == QStringLiteral("growth_stage")) {
        emit growthStageRequested(command.value("stage").toString());
        return;
    }
    if (type == QStringLiteral("growth_speed")) {
        emit growthSpeedRequested(qBound(0.1f, static_cast<float>(command.value("speed").toDouble(1.0)), 8.0f));
        return;
    }
    if (type == QStringLiteral("request_growth_data")) {
        emit growthDataRequested();
        return;
    }
    if (type == QStringLiteral("ping")) {
        sendJson(socket, QByteArray("{\"type\":\"pong\"}"));
    }
}

void WebSocketServer::sendJson(QWebSocket* socket, const QByteArray& payload) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    // Qt WebSockets handles RFC 6455 masking, fragmentation, control frames and
    // multi-client lifecycle. Large state archives are additionally sent as a
    // compact binary qCompress payload (PSZ1 + qCompress JSON) to save localhost
    // bandwidth and browser JSON parsing pressure.
    if (payload.size() >= kCompressionThresholdBytes) {
        socket->sendBinaryMessage(QByteArray(kCompressedPayloadMagic, 4) + qCompress(payload, 6));
    } else {
        socket->sendTextMessage(QString::fromUtf8(payload));
    }
}

void WebSocketServer::broadcastJson(const QByteArray& payload) {
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        sendJson(it.key(), payload);
    }
}

void WebSocketServer::broadcastState(float lightIntensity) {
    const QJsonObject state{
        {"type", "environment_updated"},
        {"message", "Environment Updated"},
        {"lightIntensity", static_cast<double>(lightIntensity)}
    };
    broadcastJson(QJsonDocument(state).toJson(QJsonDocument::Compact));
}

void WebSocketServer::broadcastTropismState(float photoWeight, float graviWeight) {
    const QJsonObject state{
        {"type", "tropism_updated"},
        {"phototropismWeight", static_cast<double>(photoWeight)},
        {"gravitropismWeight", static_cast<double>(graviWeight)}
    };
    broadcastJson(QJsonDocument(state).toJson(QJsonDocument::Compact));
}

void WebSocketServer::broadcastGrowthState(const GrowthStateReport& report) {
    if (!report.plantState.isEmpty()) {
        hasPendingGrowthReport_ = false;
        growthBroadcastTimer_.stop();
        broadcastGrowthStateNow(report);
        return;
    }

    pendingGrowthReport_ = report;
    hasPendingGrowthReport_ = true;
    if (!growthBroadcastTimer_.isActive()) {
        growthBroadcastTimer_.start();
    }
}

void WebSocketServer::flushPendingGrowthState() {
    if (!hasPendingGrowthReport_) {
        return;
    }
    broadcastGrowthStateNow(pendingGrowthReport_);
    hasPendingGrowthReport_ = false;
}

void WebSocketServer::broadcastGrowthStateNow(const GrowthStateReport& report) {
    const PlantGrowthMetrics& metrics = report.metrics;
    QJsonObject state{
        {"type", "growth_state"},
        {"age", static_cast<double>(report.age)},
        {"lifeStage", toString(report.lifeStage)},
        {"mode", report.mode},
        {"speed", static_cast<double>(report.speed)},
        {"nodeCount", report.nodeCount},
        {"branchCount", report.branchCount},
        {"leafCount", report.leafCount},
        {"height", static_cast<double>(metrics.height)},
        {"totalBranchLength", static_cast<double>(metrics.totalBranchLength)},
        {"canopyWidth", static_cast<double>(metrics.canopyWidth)},
        {"recordedFrameCount", report.recordedFrameCount},
        {"recordedEndAge", static_cast<double>(report.recordedEndAge)}
    };
    if (!report.plantState.isEmpty()) {
        state.insert("plantState", report.plantState);
    }
    broadcastJson(QJsonDocument(state).toJson(QJsonDocument::Compact));
}

void WebSocketServer::broadcastGrowthData(const QJsonObject& data) {
    QJsonObject payloadObject = data;
    payloadObject.insert("type", "growth_data");
    broadcastJson(QJsonDocument(payloadObject).toJson(QJsonDocument::Compact));
}

void WebSocketServer::sendHeartbeat() {
    const QDateTime now = QDateTime::currentDateTimeUtc();
    QList<QWebSocket*> expired;
    for (auto it = clients_.cbegin(); it != clients_.cend(); ++it) {
        QWebSocket* socket = it.key();
        if (!socket || socket->state() != QAbstractSocket::ConnectedState ||
            it.value().lastPongUtc.msecsTo(now) > kHeartbeatTimeoutMs) {
            expired.push_back(socket);
            continue;
        }
        socket->ping("plantsim-heartbeat");
    }
    for (QWebSocket* socket : expired) {
        if (socket) {
            socket->close(QWebSocketProtocol::CloseCodeGoingAway,
                          QStringLiteral("Heartbeat timeout"));
        }
    }
}

void WebSocketServer::onDisconnected() {
    QWebSocket* socket = qobject_cast<QWebSocket*>(sender());
    removeClient(socket);
    emit clientDisconnected();
}

void WebSocketServer::removeClient(QWebSocket* socket) {
    if (!socket) return;
    clients_.remove(socket);
    socket->deleteLater();
}
