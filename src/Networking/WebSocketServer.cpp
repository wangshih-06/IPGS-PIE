// ============================================================================
// WebSocketServer implementation
// 第11周：向光性与向地性控制指令解析
// ============================================================================
#include "Networking/WebSocketServer.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QHostAddress>
#include <QTcpServer>
#include <QTcpSocket>

#include <limits>
#include <algorithm>

namespace {
constexpr char kWebSocketGuid[] = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

QByteArray websocketFrame(quint8 opcode, const QByteArray& payload) {
    QByteArray frame;
    frame.append(static_cast<char>(0x80 | (opcode & 0x0f)));

    const quint64 length = static_cast<quint64>(payload.size());
    if (length < 126) {
        frame.append(static_cast<char>(length));
    } else if (length <= 0xffff) {
        frame.append(static_cast<char>(126));
        frame.append(static_cast<char>((length >> 8) & 0xff));
        frame.append(static_cast<char>(length & 0xff));
    } else {
        frame.append(static_cast<char>(127));
        for (int shift = 56; shift >= 0; shift -= 8) {
            frame.append(static_cast<char>((length >> shift) & 0xff));
        }
    }
    frame.append(payload);
    return frame;
}

float clampLight(float value) {
    return qBound(0.0f, value, 1.0f);
}
}

WebSocketServer::WebSocketServer(QObject* parent)
    : QObject(parent), server_(new QTcpServer(this)) {
    growthBroadcastTimer_.setInterval(66); // 15 Hz: merge high-frequency growth ticks.
    growthBroadcastTimer_.setSingleShot(true);
    connect(server_, &QTcpServer::newConnection, this, &WebSocketServer::onNewConnection);
    connect(&growthBroadcastTimer_, &QTimer::timeout,
            this, &WebSocketServer::flushPendingGrowthState);
}

bool WebSocketServer::listen(quint16 port) {
    if (!server_->listen(QHostAddress::LocalHost, port)) {
        emit logMessage(QStringLiteral("WebSocket listen failed: %1").arg(server_->errorString()));
        return false;
    }
    port_ = server_->serverPort();
    emit logMessage(QStringLiteral("WebSocket listening on ws://127.0.0.1:%1").arg(port_));
    return true;
}

quint16 WebSocketServer::port() const {
    return port_;
}

void WebSocketServer::onNewConnection() {
    while (server_->hasPendingConnections()) {
        auto* socket = server_->nextPendingConnection();
        clients_.insert(socket, ClientState{});
        connect(socket, &QTcpSocket::readyRead, this, &WebSocketServer::onReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &WebSocketServer::onDisconnected);
    }
}

void WebSocketServer::onReadyRead() {
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket || !clients_.contains(socket)) {
        return;
    }

    auto& state = clients_[socket];
    state.buffer.append(socket->readAll());

    if (!state.handshaken) {
        handleHttpHandshake(socket, state);
        if (!state.handshaken) {
            return;
        }
    }
    processFrames(socket, state);
}

void WebSocketServer::handleHttpHandshake(QTcpSocket* socket, ClientState& state) {
    const int headerEnd = state.buffer.indexOf("\r\n\r\n");
    if (headerEnd < 0) {
        return;
    }

    const QByteArray request = state.buffer.left(headerEnd);
    state.buffer.remove(0, headerEnd + 4);

    QByteArray key;
    const QList<QByteArray> lines = request.split('\n');
    for (QByteArray line : lines) {
        line = line.trimmed();
        if (line.toLower().startsWith("sec-websocket-key:")) {
            key = line.mid(line.indexOf(':') + 1).trimmed();
            break;
        }
    }
    if (key.isEmpty()) {
        socket->disconnectFromHost();
        return;
    }

    const QByteArray accept = QCryptographicHash::hash(
        key + QByteArray(kWebSocketGuid), QCryptographicHash::Sha1).toBase64();
    QByteArray response;
    response += "HTTP/1.1 101 Switching Protocols\r\n";
    response += "Upgrade: websocket\r\n";
    response += "Connection: Upgrade\r\n";
    response += "Sec-WebSocket-Accept: " + accept + "\r\n\r\n";
    socket->write(response);
    state.handshaken = true;
    emit clientConnected();
    emit logMessage(QStringLiteral("WebSocket client connected"));
}

void WebSocketServer::processFrames(QTcpSocket* socket, ClientState& state) {
    while (state.buffer.size() >= 2) {
        const auto* bytes = reinterpret_cast<const unsigned char*>(state.buffer.constData());
        const quint8 opcode = bytes[0] & 0x0f;
        const bool masked = (bytes[1] & 0x80) != 0;
        quint64 payloadLength = bytes[1] & 0x7f;
        int headerLength = 2;

        if (payloadLength == 126) {
            if (state.buffer.size() < 4) return;
            payloadLength = (static_cast<quint64>(bytes[2]) << 8) | bytes[3];
            headerLength = 4;
        } else if (payloadLength == 127) {
            if (state.buffer.size() < 10) return;
            payloadLength = 0;
            for (int i = 0; i < 8; ++i) {
                payloadLength = (payloadLength << 8) | bytes[2 + i];
            }
            headerLength = 10;
        }

        const int maskLength = masked ? 4 : 0;
        if (payloadLength > static_cast<quint64>(std::numeric_limits<int>::max()) ||
            state.buffer.size() < headerLength + maskLength + static_cast<int>(payloadLength)) {
            return;
        }

        const int payloadOffset = headerLength + maskLength;
        QByteArray payload = state.buffer.mid(payloadOffset, static_cast<int>(payloadLength));
        if (masked) {
            const auto* mask = bytes + headerLength;
            for (int i = 0; i < payload.size(); ++i) {
                payload[i] = static_cast<char>(payload[i] ^ mask[i % 4]);
            }
        }
        state.buffer.remove(0, payloadOffset + static_cast<int>(payloadLength));

        if (opcode == 0x8) {
            socket->disconnectFromHost();
            return;
        }
        if (opcode == 0x9) {
            sendPong(socket, payload);
        } else if (opcode == 0x1) {
            processTextMessage(socket, payload);
        }
    }
}

void WebSocketServer::processTextMessage(QTcpSocket* socket, const QByteArray& payload) {
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(payload, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        sendText(socket, QJsonDocument(QJsonObject{
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
    if (type == QStringLiteral("set_environment")) {
        const float requested = static_cast<float>(command.value("lightIntensity").toDouble(0.8));
        emit lightIntensityRequested(clampLight(requested));
        return;
    }
    if (type == QStringLiteral("set_tropism")) {
        if (command.contains("phototropism")) {
            emit phototropismRequested(clampLight(static_cast<float>(command.value("phototropism").toDouble(0.45))));
        }
        if (command.contains("gravitropism")) {
            emit gravitropismRequested(clampLight(static_cast<float>(command.value("gravitropism").toDouble(0.35))));
        }
        return;
    }
    if (type == QStringLiteral("set_light_position")) {
        const int lightId = command.value("id").toInt(1);
        const QJsonArray pos = command.value("position").toArray();
        if (pos.size() == 3) {
            emit lightPositionRequested(lightId,
                                         static_cast<float>(pos[0].toDouble()),
                                         static_cast<float>(pos[1].toDouble()),
                                         static_cast<float>(pos[2].toDouble()));
        }
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
        sendText(socket, QByteArray("{\"type\":\"pong\"}"));
    }
}

void WebSocketServer::sendText(QTcpSocket* socket, const QByteArray& payload) {
    if (socket && socket->state() == QAbstractSocket::ConnectedState) {
        socket->write(websocketFrame(0x1, payload));
    }
}

void WebSocketServer::sendPong(QTcpSocket* socket, const QByteArray& payload) {
    if (socket && socket->state() == QAbstractSocket::ConnectedState) {
        socket->write(websocketFrame(0xA, payload));
    }
}

void WebSocketServer::broadcastState(float lightIntensity) {
    const QJsonObject state{
        {"type", "environment_updated"},
        {"message", "Environment Updated"},
        {"lightIntensity", static_cast<double>(lightIntensity)}
    };
    const QByteArray payload = QJsonDocument(state).toJson(QJsonDocument::Compact);
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        if (it.value().handshaken) {
            sendText(it.key(), payload);
        }
    }
}

void WebSocketServer::broadcastTropismState(float photoWeight, float graviWeight) {
    const QJsonObject state{
        {"type", "tropism_updated"},
        {"phototropismWeight", static_cast<double>(photoWeight)},
        {"gravitropismWeight", static_cast<double>(graviWeight)}
    };
    const QByteArray payload = QJsonDocument(state).toJson(QJsonDocument::Compact);
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        if (it.value().handshaken) {
            sendText(it.key(), payload);
        }
    }
}

void WebSocketServer::broadcastGrowthState(const GrowthStateReport& report) {
    // Seek/stage restoration carries an explicit full snapshot and must not wait
    // for the metrics cadence. Ordinary ticks replace the pending report so a
    // slow client only receives the newest lightweight state at 15 Hz.
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
        growthBroadcastTimer_.stop();
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
    const QByteArray payload = QJsonDocument(state).toJson(QJsonDocument::Compact);
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        if (it.value().handshaken) sendText(it.key(), payload);
    }
}

void WebSocketServer::broadcastGrowthData(const QJsonObject& data) {
    QJsonObject payloadObject = data;
    payloadObject.insert("type", "growth_data");
    const QByteArray payload = QJsonDocument(payloadObject).toJson(QJsonDocument::Compact);
    for (auto it = clients_.begin(); it != clients_.end(); ++it) {
        if (it.value().handshaken) sendText(it.key(), payload);
    }
}

void WebSocketServer::onDisconnected() {
    auto* socket = qobject_cast<QTcpSocket*>(sender());
    removeClient(socket);
    emit clientDisconnected();
}

void WebSocketServer::removeClient(QTcpSocket* socket) {
    if (!socket) return;
    clients_.remove(socket);
    socket->deleteLater();
}
