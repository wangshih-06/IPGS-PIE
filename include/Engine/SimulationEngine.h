// ============================================================================
// SimulationEngine - environment, plant state bridge, and growth time controller
// ============================================================================
#pragma once

#include <cstddef>

#include <QObject>

#include <QJsonObject>

#include "Algorithm/LSystem.h"
#include "Engine/EnvironmentParams.h"
#include "Engine/GrowthClock.h"
#include "Engine/GrowthStateReport.h"
#include "Engine/DynamicBranchingSystem.h"
#include "Engine/GrowthEventManager.h"
#include "Engine/GrowthKeyframeStore.h"
#include "Engine/GrowthTimeline.h"
#include "Geometry/MarchingCubes.h"
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

    // 第9周：时间轴访问器
    const GrowthTimeline& growthTimeline() const { return growthClock_.timeline(); }
    GrowthTimeline&       growthTimeline()       { return growthClock_.timeline(); }
    const GrowthClock&    growthClock()    const { return growthClock_; }
    GrowthClock&          growthClock()          { return growthClock_; }

    // 第9周：植物表面网格（Metaball 场 → Marching Cubes），供渲染动画使用
    const SurfaceMesh& plantSurface() const { return plantSurface_; }
    const GrowthEventManager& growthEvents() const { return growthEvents_; }
    const GrowthKeyframeStore& growthKeyframes() const { return keyframes_; }

public slots:
    void setLightIntensity(float intensity);

    // 第9周：生长时间轴控制
    void startGrowth();
    void pauseGrowth();
    void resumeGrowth();
    void resetGrowth(float initialYears = 0.0f);
    void setGrowthSpeed(float speed);                 // 0.1 .. 8.0
    void stepGrowth(float deltaYears);
    void captureGrowthKeyframe(const QString& label = QString());
    bool saveGrowthKeyframes(const QString& filePath, QString* error = nullptr) const;                // 显式步进（Step 模式）

signals:
    void environmentUpdated(float lightIntensity);
    void logMessage(const QString& message);

    // 第9周：生长状态广播
    void growthUpdated(const GrowthStateReport& report);
    void growthLogMessage(const QString& message);
    void growthEventAdded(const QString& type, float age, int nodeId, int leafId);
    void growthKeyframeCaptured(int keyframeId, float age);
    // 第9周：植物表面网格更新（每次生长 tick 后重新提取）
    void plantSurfaceUpdated(const SurfaceMesh& mesh);

private slots:
    // 内部槽：GrowthClock::tickProduced 进来后写入 PlantModel + 重建 metaball + emit
    void onGrowthTickProduced(const GrowthSample& sample);
    void onGrowthClockLog(const QString& message);

private:
    void rebuildMetaballField();
    void rebuildPlantSurface();
    GrowthStateReport buildReport(const GrowthSample& sample) const;
    GrowthResourceState resourceState() const;

    EnvironmentParams environment_;
    LSystem lSystem_;
    QString plantProgram_;
    PlantModel plantModel_;
    MetaballField metaballField_;
    MetaballFieldSettings metaballSettings_;
    SurfaceMesh plantSurface_;
    GrowthClock growthClock_;
    DynamicBranchingSystem dynamicBranching_;
    GrowthEventManager growthEvents_;
    GrowthKeyframeStore keyframes_;
    QJsonObject initialPlantSnapshot_;
    float nextAutoKeyframeAge_ = 1.0f;          // Q_OBJECT 子对象，自动 setParent(this)
};