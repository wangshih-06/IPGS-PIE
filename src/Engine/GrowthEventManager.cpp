#include "Engine/GrowthEventManager.h"

#include <algorithm>

#include <QFile>
#include <QJsonDocument>

namespace {
QString typeName(GrowthEvent::Type type) {
    switch (type) {
    case GrowthEvent::Type::BranchCreated: return QStringLiteral("branch_created");
    case GrowthEvent::Type::BranchDied: return QStringLiteral("branch_died");
    case GrowthEvent::Type::GrowthStopped: return QStringLiteral("growth_stopped");
    case GrowthEvent::Type::LeafSprouted: return QStringLiteral("leaf_sprouted");
    case GrowthEvent::Type::LeafDied: return QStringLiteral("leaf_died");
    case GrowthEvent::Type::KeyframeCaptured: return QStringLiteral("keyframe_captured");
    }
    return QStringLiteral("unknown");
}
}

QString toString(GrowthEvent::Type type) { return typeName(type); }

GrowthEvent::Type growthEventTypeFromString(const QString& value) {
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("branch_died")) return GrowthEvent::Type::BranchDied;
    if (normalized == QStringLiteral("growth_stopped")) return GrowthEvent::Type::GrowthStopped;
    if (normalized == QStringLiteral("leaf_sprouted")) return GrowthEvent::Type::LeafSprouted;
    if (normalized == QStringLiteral("leaf_died")) return GrowthEvent::Type::LeafDied;
    if (normalized == QStringLiteral("keyframe_captured")) return GrowthEvent::Type::KeyframeCaptured;
    return GrowthEvent::Type::BranchCreated;
}

const GrowthEvent& GrowthEventManager::record(float age, GrowthEvent::Type type,
                                              int nodeId, int parentId, int leafId,
                                              const QString& message,
                                              const QJsonObject& data) {
    GrowthEvent event;
    event.sequence = static_cast<int>(events_.size());
    event.age = std::max(0.0f, age);
    event.type = type;
    event.nodeId = nodeId;
    event.parentId = parentId;
    event.leafId = leafId;
    event.message = message;
    event.data = data;
    events_.push_back(event);
    return events_.back();
}

void GrowthEventManager::clear() { events_.clear(); }

std::vector<GrowthEvent> GrowthEventManager::since(std::size_t index) const {
    if (index >= events_.size()) return {};
    return std::vector<GrowthEvent>(events_.begin() + static_cast<std::ptrdiff_t>(index), events_.end());
}

QJsonArray GrowthEventManager::toJson() const {
    QJsonArray result;
    for (const GrowthEvent& event : events_) {
        result.append(QJsonObject{
            {QStringLiteral("sequence"), event.sequence},
            {QStringLiteral("age"), event.age},
            {QStringLiteral("type"), typeName(event.type)},
            {QStringLiteral("nodeId"), event.nodeId},
            {QStringLiteral("parentId"), event.parentId},
            {QStringLiteral("leafId"), event.leafId},
            {QStringLiteral("message"), event.message},
            {QStringLiteral("data"), event.data}
        });
    }
    return result;
}

bool GrowthEventManager::saveJson(const QString& filePath, QString* error) const {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = QStringLiteral("Cannot open %1: %2").arg(filePath, file.errorString());
        return false;
    }
    if (file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented)) < 0) {
        if (error) *error = QStringLiteral("Cannot write %1: %2").arg(filePath, file.errorString());
        return false;
    }
    return true;
}

bool GrowthEventManager::fromJson(const QJsonArray& json, GrowthEventManager* output, QString* error) {
    if (!output) {
        if (error) *error = QStringLiteral("Output event manager is null.");
        return false;
    }
    GrowthEventManager manager;
    for (const QJsonValue& value : json) {
        if (!value.isObject()) {
            if (error) *error = QStringLiteral("Every growth event must be an object.");
            return false;
        }
        const QJsonObject object = value.toObject();
        const GrowthEvent& event = manager.record(
            static_cast<float>(object.value(QStringLiteral("age")).toDouble(0.0)),
            growthEventTypeFromString(object.value(QStringLiteral("type")).toString()),
            object.value(QStringLiteral("nodeId")).toInt(-1),
            object.value(QStringLiteral("parentId")).toInt(-1),
            object.value(QStringLiteral("leafId")).toInt(-1),
            object.value(QStringLiteral("message")).toString(),
            object.value(QStringLiteral("data")).toObject());
        manager.events_.back().sequence = object.value(QStringLiteral("sequence")).toInt(event.sequence);
    }
    *output = std::move(manager);
    return true;
}

bool GrowthEventManager::loadJson(const QString& filePath, GrowthEventManager* output, QString* error) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Cannot open %1: %2").arg(filePath, file.errorString());
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        if (error) *error = QStringLiteral("Invalid event JSON: %1").arg(parseError.errorString());
        return false;
    }
    return fromJson(document.array(), output, error);
}
