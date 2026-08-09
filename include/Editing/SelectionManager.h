#pragma once

#include "Editing/EditTypes.h"

class SelectionManager {
public:
    void setPickMode(EditPickMode mode);
    EditPickMode pickMode() const;

    bool updateHover(const EditPickResult& pick);
    bool select(const EditPickResult& pick);
    bool selectHovered();
    bool clearHover();
    bool clearSelection();

    const EditPickResult& hovered() const;
    const EditPickResult& selected() const;
    bool hasHover() const;
    bool hasSelection() const;

    // Preselection is yellow, node selection is cyan, whole-plant selection is orange.
    static Vec3 hoverColor();
    static Vec3 nodeSelectionColor();
    static Vec3 wholePlantSelectionColor();

private:
    static bool equivalent(const EditPickResult& lhs, const EditPickResult& rhs);

    EditPickMode pickMode_ = EditPickMode::Node;
    EditPickResult hovered_;
    EditPickResult selected_;
};
