#pragma once

#include <cstddef>
#include <deque>
#include <optional>

#include <QJsonObject>
#include <QString>

class PlantModel;

// Bounded, transaction-oriented edit history. A drag begins one transaction,
// may produce many preview updates, and stores exactly one pre-edit snapshot
// when it is committed.
class EditHistory {
public:
    explicit EditHistory(std::size_t capacity = 50);

    void begin(const PlantModel& model);
    bool commit(const PlantModel& model);
    bool cancel(PlantModel* model, QString* error = nullptr);
    bool undo(PlantModel* model, QString* error = nullptr);

    bool hasActiveEdit() const { return activeSnapshot_.has_value(); }
    bool canUndo() const { return !undoSnapshots_.empty(); }
    std::size_t size() const { return undoSnapshots_.size(); }
    std::size_t capacity() const { return capacity_; }
    void clear();

private:
    bool restore(const QJsonObject& snapshot, PlantModel* model, QString* error) const;

    std::size_t capacity_;
    std::deque<QJsonObject> undoSnapshots_;
    std::optional<QJsonObject> activeSnapshot_;
};
