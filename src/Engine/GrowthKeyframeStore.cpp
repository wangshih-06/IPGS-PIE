#include "Engine/GrowthKeyframeStore.h"

#include <algorithm>

#include <QFile>
#include <QJsonDocument>

const GrowthKeyframe& GrowthKeyframeStore::capture(const PlantModel& plant, float age,
                                                   const QString& label, int eventSequence) {
    GrowthKeyframe keyframe;
    keyframe.id = static_cast<int>(keyframes_.size());
    keyframe.age = std::max(0.0f, age);
    keyframe.label = label.isEmpty() ? QStringLiteral("keyframe_%1").arg(keyframe.id) : label;
    keyframe.eventSequence = std::max(0, eventSequence);
    keyframe.plant = plant.toJson();
    keyframes_.push_back(std::move(keyframe));
    return keyframes_.back();
}

bool GrowthKeyframeStore::restore(std::size_t index, PlantModel* plant, QString* error) const {
    if (!plant || index >= keyframes_.size()) {
        if (error) *error = QStringLiteral("Invalid keyframe index or null plant output.");
        return false;
    }
    return PlantModel::fromJson(keyframes_[index].plant, plant, error);
}

void GrowthKeyframeStore::clear() { keyframes_.clear(); }

QJsonObject GrowthKeyframeStore::toJson() const {
    QJsonArray keyframes;
    for (const GrowthKeyframe& keyframe : keyframes_) {
        keyframes.append(QJsonObject{
            {QStringLiteral("id"), keyframe.id},
            {QStringLiteral("age"), keyframe.age},
            {QStringLiteral("label"), keyframe.label},
            {QStringLiteral("eventSequence"), keyframe.eventSequence},
            {QStringLiteral("plant"), keyframe.plant}
        });
    }
    return QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("plantsim.growth_keyframes")},
        {QStringLiteral("version"), 1},
        {QStringLiteral("keyframes"), keyframes}
    };
}

bool GrowthKeyframeStore::saveJson(const QString& filePath, QString* error) const {
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

bool GrowthKeyframeStore::fromJson(const QJsonObject& json, GrowthKeyframeStore* output,
                                   QString* error) {
    if (!output || json.value(QStringLiteral("schema")).toString() != QStringLiteral("plantsim.growth_keyframes")) {
        if (error) *error = QStringLiteral("Unsupported keyframe JSON schema.");
        return false;
    }
    GrowthKeyframeStore store;
    for (const QJsonValue& value : json.value(QStringLiteral("keyframes")).toArray()) {
        if (!value.isObject()) {
            if (error) *error = QStringLiteral("Every keyframe must be an object.");
            return false;
        }
        const QJsonObject object = value.toObject();
        if (!object.value(QStringLiteral("plant")).isObject()) {
            if (error) *error = QStringLiteral("Keyframe plant snapshot is missing.");
            return false;
        }
        GrowthKeyframe keyframe;
        keyframe.id = object.value(QStringLiteral("id")).toInt(static_cast<int>(store.size()));
        keyframe.age = static_cast<float>(object.value(QStringLiteral("age")).toDouble(0.0));
        keyframe.label = object.value(QStringLiteral("label")).toString();
        keyframe.eventSequence = object.value(QStringLiteral("eventSequence")).toInt(0);
        keyframe.plant = object.value(QStringLiteral("plant")).toObject();
        store.keyframes_.push_back(std::move(keyframe));
    }
    *output = std::move(store);
    return true;
}

bool GrowthKeyframeStore::loadJson(const QString& filePath, GrowthKeyframeStore* output, QString* error) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("Cannot open %1: %2").arg(filePath, file.errorString());
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = QStringLiteral("Invalid keyframe JSON: %1").arg(parseError.errorString());
        return false;
    }
    return fromJson(document.object(), output, error);
}
