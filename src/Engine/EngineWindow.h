#pragma once

#include <QMainWindow>

#include "Engine/GrowthStateReport.h"

class QLabel;
class QPushButton;
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

    // 第11周：向光性与向地性 UI 槽
    void onPhotoSliderChanged(int value);
    void onGraviSliderChanged(int value);
    void onTropismUpdated(float photoWeight, float graviWeight);

    // 生长时间轴 UI
    void onStartClicked();
    void onPauseClicked();
    void onResumeClicked();
    void onResetClicked();
    void onSpeedSliderChanged(int value);
    void onGrowthUpdated(const GrowthStateReport& report);

private:
    void applyTheme();
    void setGrowthButtonsEnabled(bool running);

    SimulationEngine* simulationEngine_ = nullptr;
    Renderer* renderer_ = nullptr;
    QSlider* lightSlider_ = nullptr;
    QLabel* lightValueLabel_ = nullptr;
    QLabel* engineStatusLabel_ = nullptr;

    // 第11周：向性控制控件与标签
    QSlider* photoSlider_ = nullptr;
    QLabel* photoValueLabel_ = nullptr;
    QSlider* graviSlider_ = nullptr;
    QLabel* graviValueLabel_ = nullptr;

    // 生长时间轴控件
    QPushButton* startButton_ = nullptr;
    QPushButton* pauseButton_ = nullptr;
    QPushButton* resumeButton_ = nullptr;
    QPushButton* resetButton_ = nullptr;
    QSlider*     speedSlider_ = nullptr;
    QLabel*      speedValueLabel_ = nullptr;
    QLabel*      ageValueLabel_ = nullptr;
    QLabel*      stageValueLabel_ = nullptr;
    QLabel*      stateValueLabel_ = nullptr;
    QLabel*      lengthScaleValueLabel_ = nullptr;
    QLabel*      radiusScaleValueLabel_ = nullptr;
};
