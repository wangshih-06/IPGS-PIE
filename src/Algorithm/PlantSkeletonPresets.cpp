#include "Algorithm/PlantSkeletonPresets.h"

namespace {
PlantSkeletonPreset pinePreset() {
    PlantSkeletonPreset preset;
    preset.kind = PlantSkeletonPresetKind::Pine;
    preset.key = QStringLiteral("pine");
    preset.displayName = QStringLiteral("松树");
    preset.description = QStringLiteral("主干明显、层级分枝紧凑的针叶树骨架");
    preset.iterations = 5;
    preset.system.setAxiom(QStringLiteral("X"));
    preset.system.setRule(QChar('X'), QStringLiteral("F[+X][-X][&X]FX"));
    preset.system.setRule(QChar('F'), QStringLiteral("FF"));
    preset.system.setMaxGeneratedLength(500000);

    preset.turtleRule.angleDegrees = 22.0f;
    preset.turtleRule.angleVariationDegrees = 4.0f;
    preset.turtleRule.length = 0.55f;
    preset.turtleRule.radius = 0.18f;
    preset.turtleRule.lengthScale = 0.94f;
    preset.turtleRule.radiusScale = 0.975f;
    preset.turtleRule.lengthVariation = 0.06f;
    preset.turtleRule.radiusVariation = 0.025f;
    preset.turtleRule.minimumLength = 0.045f;
    preset.turtleRule.minimumRadius = 0.008f;
    return preset;
}

PlantSkeletonPreset willowPreset() {
    PlantSkeletonPreset preset;
    preset.kind = PlantSkeletonPresetKind::Willow;
    preset.key = QStringLiteral("willow");
    preset.displayName = QStringLiteral("柳树");
    preset.description = QStringLiteral("细长、带俯仰和扭转的下垂枝条骨架");
    preset.iterations = 5;
    preset.system.setAxiom(QStringLiteral("X"));
    preset.system.addProduction(QChar('X'), QStringLiteral("F[&+X][&-X][/&X]FX"), 3.0);
    preset.system.addProduction(QChar('X'), QStringLiteral("F[&+X][\\&X]F[-X]"), 1.0);
    preset.system.setRule(QChar('F'), QStringLiteral("FF"));
    preset.system.setMaxGeneratedLength(500000);

    preset.turtleRule.angleDegrees = 30.0f;
    preset.turtleRule.angleVariationDegrees = 8.0f;
    preset.turtleRule.length = 0.62f;
    preset.turtleRule.radius = 0.15f;
    preset.turtleRule.lengthScale = 0.955f;
    preset.turtleRule.radiusScale = 0.978f;
    preset.turtleRule.lengthVariation = 0.12f;
    preset.turtleRule.radiusVariation = 0.035f;
    preset.turtleRule.minimumLength = 0.04f;
    preset.turtleRule.minimumRadius = 0.006f;
    return preset;
}

PlantSkeletonPreset cherryPreset() {
    PlantSkeletonPreset preset;
    preset.kind = PlantSkeletonPresetKind::Cherry;
    preset.key = QStringLiteral("cherry");
    preset.displayName = QStringLiteral("樱花树");
    preset.description = QStringLiteral("树冠横向舒展、分枝较圆润的随机骨架");
    preset.iterations = 5;
    preset.system.setAxiom(QStringLiteral("X"));
    preset.system.addProduction(QChar('X'), QStringLiteral("F[+X][-X][&X]B"), 2.0);
    preset.system.addProduction(QChar('X'), QStringLiteral("F[+X][-/X][^X]B"), 1.2);
    preset.system.addProduction(QChar('X'), QStringLiteral("F[\\+X][/&X][-X]B"), 0.8);
    preset.system.setRule(QChar('F'), QStringLiteral("FF"));
    preset.system.setMaxGeneratedLength(500000);

    preset.turtleRule.angleDegrees = 31.0f;
    preset.turtleRule.angleVariationDegrees = 10.0f;
    preset.turtleRule.length = 0.48f;
    preset.turtleRule.radius = 0.17f;
    preset.turtleRule.lengthScale = 0.95f;
    preset.turtleRule.radiusScale = 0.976f;
    preset.turtleRule.lengthVariation = 0.1f;
    preset.turtleRule.radiusVariation = 0.04f;
    preset.turtleRule.minimumLength = 0.035f;
    preset.turtleRule.minimumRadius = 0.006f;
    return preset;
}

PlantSkeletonPreset shrubPreset() {
    PlantSkeletonPreset preset;
    preset.kind = PlantSkeletonPresetKind::Shrub;
    preset.key = QStringLiteral("shrub");
    preset.displayName = QStringLiteral("灌木");
    preset.description = QStringLiteral("低矮、分枝密集并向多个三维方向扩张的骨架");
    preset.iterations = 4;
    preset.system.setAxiom(QStringLiteral("X"));
    preset.system.addProduction(QChar('X'), QStringLiteral("F[+X][-X][&X][^X]"), 3.0);
    preset.system.addProduction(QChar('X'), QStringLiteral("F[+/X][-\\X][&X][^X]"), 1.0);
    preset.system.setRule(QChar('F'), QStringLiteral("F"));
    preset.system.setMaxGeneratedLength(500000);

    preset.turtleRule.angleDegrees = 38.0f;
    preset.turtleRule.angleVariationDegrees = 12.0f;
    preset.turtleRule.length = 0.38f;
    preset.turtleRule.radius = 0.12f;
    preset.turtleRule.lengthScale = 0.93f;
    preset.turtleRule.radiusScale = 0.965f;
    preset.turtleRule.lengthVariation = 0.14f;
    preset.turtleRule.radiusVariation = 0.05f;
    preset.turtleRule.minimumLength = 0.03f;
    preset.turtleRule.minimumRadius = 0.005f;
    return preset;
}
}

PlantSkeletonPreset PlantSkeletonPresets::create(PlantSkeletonPresetKind kind) {
    switch (kind) {
    case PlantSkeletonPresetKind::Willow:
        return willowPreset();
    case PlantSkeletonPresetKind::Cherry:
        return cherryPreset();
    case PlantSkeletonPresetKind::Shrub:
        return shrubPreset();
    case PlantSkeletonPresetKind::Pine:
    default:
        return pinePreset();
    }
}

bool PlantSkeletonPresets::fromName(const QString& name,
                                    PlantSkeletonPreset* output) {
    if (!output) {
        return false;
    }

    const QString key = name.trimmed().toLower();
    if (key == QStringLiteral("pine") || key == QStringLiteral("松树")) {
        *output = create(PlantSkeletonPresetKind::Pine);
    } else if (key == QStringLiteral("willow") || key == QStringLiteral("柳树")) {
        *output = create(PlantSkeletonPresetKind::Willow);
    } else if (key == QStringLiteral("cherry") || key == QStringLiteral("樱花树")) {
        *output = create(PlantSkeletonPresetKind::Cherry);
    } else if (key == QStringLiteral("shrub") || key == QStringLiteral("灌木")) {
        *output = create(PlantSkeletonPresetKind::Shrub);
    } else {
        return false;
    }
    return true;
}

QStringList PlantSkeletonPresets::names() {
    return {QStringLiteral("pine"),
            QStringLiteral("willow"),
            QStringLiteral("cherry"),
            QStringLiteral("shrub")};
}
