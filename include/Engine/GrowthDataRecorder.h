#pragma once

#include <cstddef>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "Plant/PlantModel.h"

// A compact set of plant indicators collected for every simulation time step.
struct PlantGrowthMetrics {
    float height = 0.0f;
    float totalBranchLength = 0.0f;
    int branchCount = 0;
    int leafCount = 0;
    float canopyWidth = 0.0f;
};

// A complete replay frame. plantState contains a lossless PlantModel snapshot,
// while metrics is intentionally duplicated for fast charting/export.
struct GrowthDataFrame {
    int step = 0;
    float age = 0.0f;
    PlantLifeStage lifeStage = PlantLifeStage::Seedling;
    PlantGrowthMetrics metrics;
    QJsonObject plantState;
};

class GrowthDataRecorder {
public:
    const std::vector<GrowthDataFrame>& frames() const { return frames_; }
    std::size_t size() const { return frames_.size(); }
    bool empty() const { return frames_.empty(); }

    void clear();
    void truncateAfter(float age);
    const GrowthDataFrame& capture(const PlantModel& plant);
    const GrowthDataFrame* nearest(float age) const;
    const GrowthDataFrame* at(std::size_t index) const;

    static PlantGrowthMetrics measure(const PlantModel& plant);

    // Full archive includes lossless PlantModel snapshots for replay.
    QJsonObject toJson() const;
    // Lightweight archive is intended for the browser chart and downloads.
    QJsonObject metricsToJson() const;
    bool saveJson(const QString& filePath, QString* error = nullptr) const;
    bool saveCsv(const QString& filePath, QString* error = nullptr) const;

private:
    std::vector<GrowthDataFrame> frames_;
};
