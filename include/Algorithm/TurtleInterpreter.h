#pragma once

#include <QString>
#include <QtGlobal>

#include "Algorithm/PlantNode.h"

struct PlantRule {
    float angleDegrees = 25.0f;
    float angleVariationDegrees = 0.0f;
    float length = 1.0f;
    float radius = 0.12f;
    float lengthScale = 0.92f;
    float radiusScale = 0.72f;
    float lengthVariation = 0.0f;
    float radiusVariation = 0.0f;
    float minimumLength = 0.001f;
    float minimumRadius = 0.001f;
    bool decayLengthOnForward = true;
    bool decayRadiusOnForward = true;
    quint32 randomSeed = 5489u;
    Vec3 origin = Vec3::Zero();
    Vec3 direction = Vec3::UnitY();
};

struct TurtleState {
    Vec3 position = Vec3::Zero();
    Vec3 direction = Vec3::UnitY();
    float length = 1.0f;
    float radius = 0.12f;
    int generation = 0;
    Quat orientation = Quat::Identity();
};

struct TurtleInterpretationStats {
    int programLength = 0;
    int segmentCount = 0;
    int moveCount = 0;
    int rotationCount = 0;
    int pushCount = 0;
    int popCount = 0;
    int maximumStackDepth = 0;
    int unmatchedClosingBrackets = 0;
    int unclosedBranches = 0;
    int leafMarkers = 0;
    int rootMarkers = 0;
    int budMarkers = 0;
    int ignoredSymbols = 0;
};

struct TurtleInterpretationResult {
    PlantNodePtr root;
    TurtleInterpretationStats stats;
};

class TurtleInterpreter {
public:
    PlantNodePtr interpret(const QString& program,
                           const PlantRule& rule = PlantRule()) const;
    TurtleInterpretationResult interpretDetailed(
        const QString& program,
        const PlantRule& rule = PlantRule()) const;

private:
    static Quat orientationFromDirection(const Vec3& direction);
    static void rotateLocal(TurtleState& state, const Vec3& localAxis, float degrees);
};
