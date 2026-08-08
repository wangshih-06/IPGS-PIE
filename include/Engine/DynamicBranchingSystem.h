#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Common/MathTypes.h"
#include "Engine/EnvironmentParams.h"
#include "Engine/GrowthEventManager.h"
#include "Plant/PlantModel.h"

struct GrowthResourceState {
    float light = 0.8f;
    float moisture = 0.72f;
    float nutrition = 0.64f;
    float temperature = 22.0f;
    float wind = 0.28f;

    // 第11周扩展：环境参数与骨架节点位置（用于计算局部受光与阴影遮挡）
    EnvironmentParams environment;
    std::vector<Vec3> allNodePositions;

    float availability() const;
    float localLightExposure(const Vec3& position) const;
};

struct DynamicBranchingSettings {
    float branchStartAge = 0.65f;
    float branchInterval = 0.8f;
    float branchProbability = 0.72f;
    float resourceThreshold = 0.28f;
    float resourceExponent = 1.25f;
    float apicalDominance = 0.42f;
    float branchLengthRatio = 0.58f;
    float branchRadiusRatio = 0.62f;
    float smoothingYears = 0.42f;
    float leafSproutAge = 0.85f;
    float leafProbability = 0.84f;
    float leafSize = 0.24f;
    int maxDepth = 5;
    int maxChildrenPerNode = 3;
    int maxTotalBranches = 64;
    int maxLeavesPerNode = 4;
    float healthDecayThreshold = 0.36f;
    float healthDecayRate = 0.18f;
    float healthRecoveryRate = 0.04f;
    float deathThreshold = 0.05f;
    float growthStopAge = 18.0f;
};

class DynamicBranchingSystem {
public:
    explicit DynamicBranchingSystem(const DynamicBranchingSettings& settings = {});

    const DynamicBranchingSettings& settings() const { return settings_; }
    void setSettings(const DynamicBranchingSettings& settings) { settings_ = settings; }
    void reset();

    void update(PlantModel& plant, float currentAge, float deltaYears,
                const GrowthResourceState& resources, GrowthEventManager* events = nullptr);

    // 第11周：向光性与向地性方向计算静态辅助方法
    static Vec3 calculateTropismDirection(const PlantNode& parent, int childIndex, int ageBucket,
                                          PlantNodeType nodeType, const EnvironmentParams& env);

private:
    static void collectNodes(PlantNode* node, std::vector<PlantNode*>* output);
    static float clamp01(float value);
    static float hash01(int nodeId, int ageBucket);
    static int leafCount(const PlantModel& plant, int nodeId);

    void updateNodeHealth(PlantNode& node, float currentAge, float deltaYears,
                          const GrowthResourceState& resources, GrowthEventManager* events);
    void markSubtreeDead(PlantNode& node, float currentAge, GrowthEventManager* events);
    void tryCreateBranch(PlantModel& plant, PlantNode& node, float currentAge,
                         const GrowthResourceState& resources, GrowthEventManager* events);
    void trySproutLeaf(PlantModel& plant, PlantNode& node, float currentAge,
                       const GrowthResourceState& resources, GrowthEventManager* events);

    DynamicBranchingSettings settings_;
    std::unordered_map<int, int> lastBranchAttemptBucket_;
    std::unordered_set<int> deathReported_;
    std::unordered_set<int> stopReported_;
};
