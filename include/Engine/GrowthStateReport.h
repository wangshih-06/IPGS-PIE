// ============================================================================
// GrowthStateReport - 第9周 单帧生长快照（用于信号传递与 EngineWindow 显示）
// ============================================================================
#pragma once

#include <QJsonObject>

#include "Common/MathTypes.h"
#include "Engine/GrowthTimeline.h"
#include "Engine/GrowthDataRecorder.h"
#include "Plant/PlantTypes.h"

struct GrowthStateReport {
    float age = 0.0f;
    PlantLifeStage   lifeStage   = PlantLifeStage::Seedling;
    PlantGrowthState growthState = PlantGrowthState::Paused;
    float            lengthScale = 0.0f;
    float            radiusScale = 0.0f;
    Vec2             leafScale   = Vec2::Zero();
    float            speed       = 1.0f;
    int              mode        = 0;       // GrowthPlaybackMode as int
    int              nodeCount   = 0;
    int              branchCount = 0;
    int              leafCount   = 0;
    PlantGrowthMetrics metrics;
    int              recordedFrameCount = 0;
    float            recordedEndAge = 0.0f;
    // Exact PlantModel snapshot used by browser replay preview.
    QJsonObject      plantState;
};
