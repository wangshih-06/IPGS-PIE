#pragma once

#include <QString>

#include "Common/MathTypes.h"

struct Branch {
    int id = -1;
    int parentNodeId = -1;
    int childNodeId = -1;
    float age = 0.0f;
    float stiffness = 1.0f;
    bool active = true;
};

struct Leaf {
    int id = -1;
    int parentNodeId = -1;
    Vec3 position = Vec3::Zero();
    Vec3 direction = Vec3::UnitY();
    Vec2 size = Vec2(0.12f, 0.06f);
    float age = 0.0f;
    float health = 1.0f;
    bool active = true;
};

struct Root {
    int id = -1;
    int parentNodeId = -1;
    Vec3 position = Vec3::Zero();
    Vec3 direction = -Vec3::UnitY();
    float radius = 0.05f;
    float length = 0.2f;
    float age = 0.0f;
    int depth = 0;
    bool active = true;
};

enum class PlantLifeStage {
    Seed,
    Seedling,
    Vegetative,
    Mature,
    Flowering,
    Dormant,
    Senescent
};

enum class PlantGrowthState {
    Active,
    Paused,
    Dormant,
    Stressed,
    Completed
};

QString toString(PlantLifeStage stage);
QString toString(PlantGrowthState state);
PlantLifeStage plantLifeStageFromString(const QString& value);
PlantGrowthState plantGrowthStateFromString(const QString& value);
