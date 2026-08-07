#pragma once

#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

struct GrowthEvent {
    enum class Type {
        BranchCreated,
        BranchDied,
        GrowthStopped,
        LeafSprouted,
        LeafDied,
        KeyframeCaptured
    };

    int sequence = 0;
    float age = 0.0f;
    Type type = Type::BranchCreated;
    int nodeId = -1;
    int parentId = -1;
    int leafId = -1;
    QString message;
    QJsonObject data;
};

QString toString(GrowthEvent::Type type);
GrowthEvent::Type growthEventTypeFromString(const QString& value);

class GrowthEventManager {
public:
    const std::vector<GrowthEvent>& events() const { return events_; }
    std::size_t size() const { return events_.size(); }

    const GrowthEvent& record(float age, GrowthEvent::Type type, int nodeId = -1,
                              int parentId = -1, int leafId = -1,
                              const QString& message = QString(),
                              const QJsonObject& data = QJsonObject());
    void clear();
    std::vector<GrowthEvent> since(std::size_t index) const;

    QJsonArray toJson() const;
    bool saveJson(const QString& filePath, QString* error = nullptr) const;
    static bool fromJson(const QJsonArray& json, GrowthEventManager* output,
                         QString* error = nullptr);
    static bool loadJson(const QString& filePath, GrowthEventManager* output,
                         QString* error = nullptr);

private:
    std::vector<GrowthEvent> events_;
};
