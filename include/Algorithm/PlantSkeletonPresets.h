#pragma once

#include <QString>
#include <QStringList>

#include "Algorithm/LSystem.h"
#include "Algorithm/TurtleInterpreter.h"

enum class PlantSkeletonPresetKind {
    Pine,
    Willow,
    Cherry,
    Shrub
};

struct PlantSkeletonPreset {
    PlantSkeletonPresetKind kind = PlantSkeletonPresetKind::Pine;
    QString key;
    QString displayName;
    QString description;
    int iterations = 4;
    LSystem system;
    PlantRule turtleRule;
};

class PlantSkeletonPresets {
public:
    static PlantSkeletonPreset create(PlantSkeletonPresetKind kind);
    static bool fromName(const QString& name, PlantSkeletonPreset* output);
    static QStringList names();
};
