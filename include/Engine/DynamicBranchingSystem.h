#pragma once

#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "Common/MathTypes.h"
#include "Engine/GrowthEventManager.h"
#include "Plant/PlantModel.h"

struct GrowthResourceState {
    float light = 0.8f;
    float moisture = 0.72f;
    float nutrition = 0.64f;
    float temperature = 22.0f;
    float wind = 0.28f;

    float availability() const;
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

private:
    static void collectNodes(PlantNode* node, std::vector<PlantNode*>* output);
    static float clamp01(float value);
    static float hash01(int nodeId, int ageBucket);
    static Vec3 branchDirection(const PlantNode& parent, int childIndex, int ageBucket);
    static int leafCount(const PlantModel& plant, int nodeId);

    void updateNodeHealth(PlantNode& node, float currentAge, float deltaYears,
                          float resource, GrowthEventManager* events);
    void markSubtreeDead(PlantNode& node, float currentAge, GrowthEventManager* events);
    void tryCreateBranch(PlantModel& plant, PlantNode& node, float currentAge,
                         float resource, GrowthEventManager* events);
    void trySproutLeaf(PlantModel& plant, PlantNode& node, float currentAge,
                       float resource, GrowthEventManager* events);

    DynamicBranchingSettings settings_;
    std::unordered_map<int, int> lastBranchAttemptBucket_;
    std::unordered_set<int> deathReported_;
    std::unordered_set<int> stopReported_;
};
