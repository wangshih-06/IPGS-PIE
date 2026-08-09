#include "Editing/EditHistory.h"

#include <QJsonDocument>

#include "Plant/PlantModel.h"

EditHistory::EditHistory(std::size_t capacity)
    : capacity_(capacity) {}

void EditHistory::begin(const PlantModel& model) {
    if (!activeSnapshot_) activeSnapshot_ = model.toJson();
}

bool EditHistory::commit(const PlantModel& model) {
    if (!activeSnapshot_) return false;
    const QJsonObject before = *activeSnapshot_;
    activeSnapshot_.reset();
    if (QJsonDocument(before) == QJsonDocument(model.toJson())) return false;
    if (capacity_ == 0) return true;
    if (undoSnapshots_.size() == capacity_) undoSnapshots_.pop_front();
    undoSnapshots_.push_back(before);
    return true;
}

bool EditHistory::cancel(PlantModel* model, QString* error) {
    if (!activeSnapshot_) return false;
    const QJsonObject snapshot = *activeSnapshot_;
    activeSnapshot_.reset();
    return restore(snapshot, model, error);
}

bool EditHistory::undo(PlantModel* model, QString* error) {
    if (activeSnapshot_) {
        return cancel(model, error);
    }
    if (undoSnapshots_.empty()) return false;
    const QJsonObject snapshot = undoSnapshots_.back();
    if (!restore(snapshot, model, error)) return false;
    undoSnapshots_.pop_back();
    return true;
}

void EditHistory::clear() {
    undoSnapshots_.clear();
    activeSnapshot_.reset();
}

bool EditHistory::restore(const QJsonObject& snapshot, PlantModel* model, QString* error) const {
    if (!model) {
        if (error) *error = QStringLiteral("Edit history restore requires a plant model.");
        return false;
    }
    PlantModel restored;
    if (!PlantModel::fromJson(snapshot, &restored, error)) return false;
    *model = std::move(restored);
    return true;
}
