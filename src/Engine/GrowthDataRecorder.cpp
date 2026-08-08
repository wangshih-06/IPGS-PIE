#include "Engine/GrowthDataRecorder.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QFile>
#include <QJsonDocument>
#include <QTextStream>

namespace {

void setError(QString* error, const QString& value) {
    if (error) *error = value;
}

QJsonObject metricsJson(const PlantGrowthMetrics& metrics) {
    return QJsonObject{
        {"height", static_cast<double>(metrics.height)},
        {"totalBranchLength", static_cast<double>(metrics.totalBranchLength)},
        {"branchCount", metrics.branchCount},
        {"leafCount", metrics.leafCount},
        {"canopyWidth", static_cast<double>(metrics.canopyWidth)}
    };
}

QJsonObject frameJson(const GrowthDataFrame& frame, bool includePlantState) {
    QJsonObject result{
        {"step", frame.step},
        {"age", static_cast<double>(frame.age)},
        {"lifeStage", toString(frame.lifeStage)},
        {"metrics", metricsJson(frame.metrics)}
    };
    if (includePlantState) result.insert("plantState", frame.plantState);
    return result;
}

void includePoint(const Vec3& point, float padding,
                  float* minY, float* maxY,
                  float* minX, float* maxX,
                  float* minZ, float* maxZ) {
    *minY = std::min(*minY, point.y() - padding);
    *maxY = std::max(*maxY, point.y() + padding);
    *minX = std::min(*minX, point.x() - padding);
    *maxX = std::max(*maxX, point.x() + padding);
    *minZ = std::min(*minZ, point.z() - padding);
    *maxZ = std::max(*maxZ, point.z() + padding);
}

void measureNodes(const PlantNode* node,
                  PlantGrowthMetrics* metrics,
                  float* minY, float* maxY,
                  float* minX, float* maxX,
                  float* minZ, float* maxZ) {
    if (!node) return;
    includePoint(node->position, std::max(0.0f, node->radius), minY, maxY, minX, maxX, minZ, maxZ);
    for (const auto& child : node->children) {
        if (child->active) {
            ++metrics->branchCount;
            const float geometricLength = (child->position - node->position).norm();
            metrics->totalBranchLength += geometricLength > 1.0e-5f ? geometricLength : std::max(0.0f, child->length);
        }
        measureNodes(child.get(), metrics, minY, maxY, minX, maxX, minZ, maxZ);
    }
}

}  // namespace

void GrowthDataRecorder::clear() {
    frames_.clear();
}

void GrowthDataRecorder::truncateAfter(float age) {
    const auto firstFuture = std::upper_bound(frames_.begin(), frames_.end(), age,
        [](float value, const GrowthDataFrame& frame) { return value < frame.age; });
    frames_.erase(firstFuture, frames_.end());
}

const GrowthDataFrame& GrowthDataRecorder::capture(const PlantModel& plant) {
    GrowthDataFrame frame;
    frame.step = static_cast<int>(frames_.size());
    frame.age = plant.age;
    frame.lifeStage = plant.lifeStage;
    frame.metrics = measure(plant);
    frame.plantState = plant.toJson();
    frames_.push_back(std::move(frame));
    return frames_.back();
}

const GrowthDataFrame* GrowthDataRecorder::nearest(float age) const {
    if (frames_.empty()) return nullptr;
    const auto it = std::lower_bound(frames_.begin(), frames_.end(), age,
        [](const GrowthDataFrame& frame, float value) { return frame.age < value; });
    if (it == frames_.begin()) return &(*it);
    if (it == frames_.end()) return &frames_.back();
    const GrowthDataFrame& after = *it;
    const GrowthDataFrame& before = *(it - 1);
    return std::abs(after.age - age) < std::abs(age - before.age) ? &after : &before;
}

const GrowthDataFrame* GrowthDataRecorder::at(std::size_t index) const {
    return index < frames_.size() ? &frames_[index] : nullptr;
}

PlantGrowthMetrics GrowthDataRecorder::measure(const PlantModel& plant) {
    PlantGrowthMetrics metrics;
    float minY = std::numeric_limits<float>::max();
    float maxY = std::numeric_limits<float>::lowest();
    float minX = std::numeric_limits<float>::max();
    float maxX = std::numeric_limits<float>::lowest();
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();

    measureNodes(plant.rootNode(), &metrics, &minY, &maxY, &minX, &maxX, &minZ, &maxZ);
    for (const Leaf& leaf : plant.leaves()) {
        if (!leaf.active) continue;
        ++metrics.leafCount;
        const float spread = std::max(0.0f, std::max(leaf.size.x(), leaf.size.y()) * 0.5f);
        includePoint(leaf.position, spread, &minY, &maxY, &minX, &maxX, &minZ, &maxZ);
    }

    if (minY != std::numeric_limits<float>::max()) {
        metrics.height = std::max(0.0f, maxY - minY);
        metrics.canopyWidth = std::max(maxX - minX, maxZ - minZ);
    }
    return metrics;
}

QJsonObject GrowthDataRecorder::toJson() const {
    QJsonArray frames;
    for (const GrowthDataFrame& frame : frames_) frames.append(frameJson(frame, true));
    return QJsonObject{
        {"schema", "plantsim.growth_recording"},
        {"version", 1},
        {"frameCount", static_cast<int>(frames_.size())},
        {"frames", frames}
    };
}

QJsonObject GrowthDataRecorder::metricsToJson() const {
    QJsonArray frames;
    for (const GrowthDataFrame& frame : frames_) frames.append(frameJson(frame, false));
    return QJsonObject{
        {"schema", "plantsim.growth_metrics"},
        {"version", 1},
        {"frameCount", static_cast<int>(frames_.size())},
        {"frames", frames}
    };
}

bool GrowthDataRecorder::saveJson(const QString& filePath, QString* error) const {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(error, QStringLiteral("Cannot write growth recording: %1").arg(file.errorString()));
        return false;
    }
    file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
    return true;
}

bool GrowthDataRecorder::saveCsv(const QString& filePath, QString* error) const {
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        setError(error, QStringLiteral("Cannot write growth CSV: %1").arg(file.errorString()));
        return false;
    }
    QTextStream stream(&file);
    stream.setRealNumberNotation(QTextStream::FixedNotation);
    stream.setRealNumberPrecision(6);
    stream << "step,age_years,life_stage,height,total_branch_length,branch_count,leaf_count,canopy_width\n";
    for (const GrowthDataFrame& frame : frames_) {
        const PlantGrowthMetrics& metrics = frame.metrics;
        stream << frame.step << ',' << frame.age << ',' << toString(frame.lifeStage) << ','
               << metrics.height << ',' << metrics.totalBranchLength << ','
               << metrics.branchCount << ',' << metrics.leafCount << ','
               << metrics.canopyWidth << '\n';
    }
    return true;
}
