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
constexpr int kProtocolVersion = 1;
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
    initializeCommandHandlers();
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

void WebSocketServer::initializeCommandHandlers() {
    commandHandlers_.insert(QStringLiteral("adjust_light"), [this](QWebSocket*, const QJsonObject& command) {
        emit lightIntensityRequested(clampLight(static_cast<float>(command.value("value").toDouble(0.0))));
    });
    commandHandlers_.insert(QStringLiteral("set_phototropism"), [this](QWebSocket*, const QJsonObject& command) {
        emit phototropismRequested(qBound(0.0f, static_cast<float>(command.value("value").toDouble(1.0)), 2.0f));
    });
    commandHandlers_.insert(QStringLiteral("set_gravitropism"), [this](QWebSocket*, const QJsonObject& command) {
        emit gravitropismRequested(qBound(0.0f, static_cast<float>(command.value("value").toDouble(1.0)), 2.0f));
    });
    commandHandlers_.insert(QStringLiteral("set_light_position"), [this](QWebSocket*, const QJsonObject& command) {
        emit lightPositionRequested(command.value("id").toInt(0),
                                    static_cast<float>(command.value("x").toDouble(0.0)),
                                    static_cast<float>(command.value("y").toDouble(0.0)),
                                    static_cast<float>(command.value("z").toDouble(0.0)));
    });
    commandHandlers_.insert(QStringLiteral("growth_start"), [this](QWebSocket*, const QJsonObject&) { emit growthStartRequested(); });
    commandHandlers_.insert(QStringLiteral("growth_pause"), [this](QWebSocket*, const QJsonObject&) { emit growthPauseRequested(); });
    commandHandlers_.insert(QStringLiteral("growth_resume"), [this](QWebSocket*, const QJsonObject&) { emit growthResumeRequested(); });
    commandHandlers_.insert(QStringLiteral("growth_reset"), [this](QWebSocket*, const QJsonObject&) { emit growthResetRequested(); });
    commandHandlers_.insert(QStringLiteral("growth_seek"), [this](QWebSocket*, const QJsonObject& command) {
        emit growthSeekRequested(std::max(0.0f, static_cast<float>(command.value("age").toDouble(0.0))));
    });
    commandHandlers_.insert(QStringLiteral("growth_stage"), [this](QWebSocket*, const QJsonObject& command) {
        emit growthStageRequested(command.value("stage").toString());
    });
    commandHandlers_.insert(QStringLiteral("growth_speed"), [this](QWebSocket*, const QJsonObject& command) {
        emit growthSpeedRequested(qBound(0.1f, static_cast<float>(command.value("speed").toDouble(1.0)), 8.0f));
    });
    commandHandlers_.insert(QStringLiteral("request_growth_data"), [this](QWebSocket*, const QJsonObject&) {
        emit growthDataRequested();
    });
    commandHandlers_.insert(QStringLiteral("ping"), [this](QWebSocket* socket, const QJsonObject&) {
        sendJson(socket, QJsonDocument(QJsonObject{{"type", "pong"}, {"protocolVersion", kProtocolVersion}})
                             .toJson(QJsonDocument::Compact));
    });
}

void WebSocketServer::sendError(QWebSocket* socket, const QString& code, const QString& message) {
    sendJson(socket, QJsonDocument(QJsonObject{
        {"type", "error"}, {"protocolVersion", kProtocolVersion},
        {"code", code}, {"message", message}
    }).toJson(QJsonDocument::Compact));
}

void WebSocketServer::processTextMessage(QWebSocket* socket, const QByteArray& payload) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        sendError(socket, QStringLiteral("invalid_json"), QStringLiteral("Command must be a JSON object."));
        return;
    }

    const QJsonObject command = document.object();
    const QJsonValue protocolValue = command.value("protocolVersion");
    if (!protocolValue.isDouble() || protocolValue.toDouble() != static_cast<double>(protocolValue.toInt())) {
        sendError(socket, QStringLiteral("invalid_protocol_version"),
                  QStringLiteral("protocolVersion must be an integer."));
        return;
    }
    if (protocolValue.toInt() != kProtocolVersion) {
        sendError(socket, QStringLiteral("unsupported_protocol_version"),
                  QStringLiteral("This server supports protocolVersion %1.").arg(kProtocolVersion));
        return;
    }

    const QString type = command.value("type").toString();
    if (type.isEmpty()) {
        sendError(socket, QStringLiteral("invalid_command"), QStringLiteral("Command type is required."));
        return;
    }
    const auto handler = commandHandlers_.constFind(type);
    if (handler == commandHandlers_.cend()) {
        sendError(socket, QStringLiteral("unknown_command"),
                  QStringLiteral("Unsupported command type: %1").arg(type));
        return;
    }
    (*handler)(socket, command);
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
        {"protocolVersion", kProtocolVersion},
        {"message", "Environment Updated"},
        {"lightIntensity", static_cast<double>(lightIntensity)}
    };
    broadcastJson(QJsonDocument(state).toJson(QJsonDocument::Compact));
}

void WebSocketServer::broadcastTropismState(float photoWeight, float graviWeight) {
    const QJsonObject state{
        {"type", "tropism_updated"},
        {"protocolVersion", kProtocolVersion},
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
        {"protocolVersion", kProtocolVersion},
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
    payloadObject.insert("protocolVersion", kProtocolVersion);
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
