#pragma once

#include <cstddef>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "Plant/PlantModel.h"

struct GrowthKeyframe {
    int id = -1;
    float age = 0.0f;
    QString label;
    int eventSequence = 0;
    QJsonObject plant;
};

class GrowthKeyframeStore {
public:
    const std::vector<GrowthKeyframe>& keyframes() const { return keyframes_; }
    std::size_t size() const { return keyframes_.size(); }

    const GrowthKeyframe& capture(const PlantModel& plant, float age,
                                  const QString& label = QString(),
                                  int eventSequence = 0);
    bool restore(std::size_t index, PlantModel* plant, QString* error = nullptr) const;
    void clear();

    QJsonObject toJson() const;
    bool saveJson(const QString& filePath, QString* error = nullptr) const;
    static bool fromJson(const QJsonObject& json, GrowthKeyframeStore* output,
                         QString* error = nullptr);
    static bool loadJson(const QString& filePath, GrowthKeyframeStore* output,
                         QString* error = nullptr);

private:
    std::vector<GrowthKeyframe> keyframes_;
};
