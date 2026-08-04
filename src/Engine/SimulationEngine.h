// ============================================================================
// SimulationEngine - environment and plant state bridge
// ============================================================================
#pragma once

#include <cstddef>

#include <QObject>

#include "Algorithm/LSystem.h"
#include "Engine/EnvironmentParams.h"
#include "Implicit/MetaballField.h"
#include "Plant/PlantModel.h"

class SimulationEngine : public QObject {
    Q_OBJECT
public:
    explicit SimulationEngine(QObject* parent = nullptr);

    const EnvironmentParams& environment() const;
    const PlantModel& plantModel() const;
    const PlantNode* plantRoot() const;
    const QString& plantProgram() const;
    std::size_t plantNodeCount() const;
    const MetaballField& metaballField() const;

public slots:
    void setLightIntensity(float intensity);

signals:
    void environmentUpdated(float lightIntensity);
    void logMessage(const QString& message);

private:
    EnvironmentParams environment_;
    LSystem lSystem_;
    QString plantProgram_;
    PlantModel plantModel_;
    MetaballField metaballField_;
};

