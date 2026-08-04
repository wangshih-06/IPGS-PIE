#pragma once

#include <QMainWindow>

class QLabel;
class QSlider;
class SimulationEngine;
class Renderer;

class EngineWindow final : public QMainWindow {
    Q_OBJECT
public:
    explicit EngineWindow(SimulationEngine* simulationEngine, QWidget* parent = nullptr);

public slots:
    void showEngineMessage(const QString& message);

private slots:
    void onLightSliderChanged(int value);
    void onEnvironmentUpdated(float intensity);

private:
    void applyTheme();

    SimulationEngine* simulationEngine_ = nullptr;
    Renderer* renderer_ = nullptr;
    QSlider* lightSlider_ = nullptr;
    QLabel* lightValueLabel_ = nullptr;
    QLabel* engineStatusLabel_ = nullptr;
};
