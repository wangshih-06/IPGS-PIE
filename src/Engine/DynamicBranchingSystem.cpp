#include "Engine/DynamicBranchingSystem.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace {
constexpr float kEpsilon = 1.0e-5f;
}

float GrowthResourceState::availability() const {
    const float lightFactor = std::clamp(light, 0.0f, 1.0f);
    const float moistureFactor = std::clamp(moisture, 0.0f, 1.0f);
    const float nutritionFactor = std::clamp(nutrition, 0.0f, 1.0f);
    const float temperatureFactor = std::clamp(1.0f - std::abs(temperature - 22.0f) / 28.0f, 0.0f, 1.0f);
    const float windFactor = std::clamp(1.0f - wind * 0.35f, 0.0f, 1.0f);
    return std::clamp(0.34f * lightFactor + 0.28f * moistureFactor +
                      0.24f * nutritionFactor + 0.10f * temperatureFactor +
                      0.04f * windFactor, 0.0f, 1.0f);
}

DynamicBranchingSystem::DynamicBranchingSystem(const DynamicBranchingSettings& settings)
    : settings_(settings) {}

void DynamicBranchingSystem::reset() {
    lastBranchAttemptBucket_.clear();
    deathReported_.clear();
    stopReported_.clear();
}

float DynamicBranchingSystem::clamp01(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

float DynamicBranchingSystem::hash01(int nodeId, int ageBucket) {
    std::uint32_t value = static_cast<std::uint32_t>(nodeId * 747796405u) ^
                          static_cast<std::uint32_t>(ageBucket * 2891336453u);
    value ^= value >> 16;
    value *= 2246822519u;
    value ^= value >> 13;
    return static_cast<float>(value & 0x00ffffffu) / static_cast<float>(0x01000000u);
}

Vec3 DynamicBranchingSystem::branchDirection(const PlantNode& parent, int childIndex, int ageBucket) {
    const float phase = hash01(parent.id + childIndex * 31, ageBucket) * 6.283185307f;
    const float tilt = 0.35f + 0.55f * hash01(parent.id + 17, ageBucket + childIndex);
    Vec3 axis = parent.direction.cross(Vec3::UnitY());
    if (axis.squaredNorm() < kEpsilon) axis = Vec3::UnitX();
    axis.normalize();
    Vec3 tangent = axis.cross(parent.direction);
    if (tangent.squaredNorm() < kEpsilon) tangent = Vec3::UnitZ();
    tangent.normalize();
    Vec3 direction = parent.direction * (1.0f - tilt) +
                     axis * (std::cos(phase) * tilt) +
                     tangent * (std::sin(phase) * tilt);
    return direction.squaredNorm() > kEpsilon ? direction.normalized() : Vec3::UnitY();
}

void DynamicBranchingSystem::collectNodes(PlantNode* node, std::vector<PlantNode*>* output) {
    if (!node || !output) return;
    output->push_back(node);
    for (auto& child : node->children) collectNodes(child.get(), output);
}

int DynamicBranchingSystem::leafCount(const PlantModel& plant, int nodeId) {
    int count = 0;
    for (const Leaf& leaf : plant.leaves()) {
        if (leaf.parentNodeId == nodeId && leaf.active) ++count;
    }
    return count;
}

void DynamicBranchingSystem::updateNodeHealth(PlantNode& node, float currentAge,
                                               float deltaYears, float resource,
                                               GrowthEventManager* events) {
    if (!node.active || deltaYears <= 0.0f) return;
    if (resource < settings_.healthDecayThreshold) {
        node.health = clamp01(node.health - (settings_.healthDecayThreshold - resource) *
                              settings_.healthDecayRate * deltaYears);
    } else {
        node.health = clamp01(node.health + settings_.healthRecoveryRate * deltaYears);
    }
    if (node.health <= settings_.deathThreshold) {
        markSubtreeDead(node, currentAge, events);
        return;
    }
    if (node.age >= settings_.growthStopAge || resource < settings_.resourceThreshold) {
        if (node.growing) {
            node.growing = false;
            if (events && stopReported_.insert(node.id).second) {
                events->record(currentAge, GrowthEvent::Type::GrowthStopped, node.id, node.parentId,
                               -1, QStringLiteral("Node growth stopped by age/resource constraint"));
            }
        }
    }
    if (node.growing) {
        const float smoothing = std::max(settings_.smoothingYears, 0.01f);
        node.growthProgress = clamp01(node.growthProgress + deltaYears / smoothing);
    }
}

void DynamicBranchingSystem::markSubtreeDead(PlantNode& node, float currentAge,
                                              GrowthEventManager* events) {
    if (!node.active) return;
    node.active = false;
    node.growing = false;
    node.growthProgress = 0.0f;
    if (events && deathReported_.insert(node.id).second) {
        events->record(currentAge, GrowthEvent::Type::BranchDied, node.id, node.parentId,
                       -1, QStringLiteral("Branch died from insufficient growth resources"));
    }
    for (auto& child : node.children) markSubtreeDead(*child, currentAge, events);
}

void DynamicBranchingSystem::tryCreateBranch(PlantModel& plant, PlantNode& node,
                                               float currentAge, float resource,
                                               GrowthEventManager* events) {
    if (!node.active || !node.growing || node.depth >= settings_.maxDepth ||
        static_cast<int>(plant.branches().size()) >= settings_.maxTotalBranches ||
        static_cast<int>(node.children.size()) >= settings_.maxChildrenPerNode ||
        node.age < settings_.branchStartAge) return;

    const int ageBucket = static_cast<int>(std::floor(node.age / std::max(settings_.branchInterval, 0.05f)));
    auto attempt = lastBranchAttemptBucket_.find(node.id);
    if (attempt != lastBranchAttemptBucket_.end() && attempt->second >= ageBucket) return;
    lastBranchAttemptBucket_[node.id] = ageBucket;

    bool apical = true;
    for (const auto& sibling : node.children) {
        if (sibling->active && sibling->direction.y() > node.direction.y() + 0.05f) {
            apical = false;
            break;
        }
    }
    const float dominanceFactor = apical ? 1.0f : (1.0f - clamp01(settings_.apicalDominance));
    const float resourceFactor = std::pow(clamp01(resource), std::max(settings_.resourceExponent, 0.01f));
    const float probability = clamp01(settings_.branchProbability * resourceFactor * dominanceFactor);
    if (hash01(node.id, ageBucket) > probability) return;

    const int childIndex = static_cast<int>(node.children.size());
    const Vec3 direction = branchDirection(node, childIndex, ageBucket);
    const Vec3 position = node.position + node.direction * std::max(node.length, 0.18f);
    const float length = std::max(0.08f, std::max(node.length, 0.18f) * settings_.branchLengthRatio);
    const float radius = std::max(0.012f, node.radius * settings_.branchRadiusRatio);
    PlantNode* child = plant.addNode(node.id, position, direction, radius, length, 0.0f,
                                     true, PlantNodeType::Branch, node.generation + 1);
    if (!child) return;
    child->growthProgress = 0.04f;
    child->growing = true;
    child->health = clamp01(0.72f + 0.25f * resource);
    if (events) {
        events->record(currentAge, GrowthEvent::Type::BranchCreated, child->id, node.id,
                       -1, QStringLiteral("Age/resource triggered branch creation"),
                       QJsonObject{{QStringLiteral("probability"), probability},
                                   {QStringLiteral("resource"), resource},
                                   {QStringLiteral("generation"), child->generation}});
    }
}

void DynamicBranchingSystem::trySproutLeaf(PlantModel& plant, PlantNode& node,
                                             float currentAge, float resource,
                                             GrowthEventManager* events) {
    if (!node.active || !node.growing || node.age < settings_.leafSproutAge ||
        leafCount(plant, node.id) >= settings_.maxLeavesPerNode) return;
    const int existing = leafCount(plant, node.id);
    const int ageBucket = static_cast<int>(std::floor(node.age / std::max(settings_.branchInterval, 0.05f)));
    const float probability = clamp01(settings_.leafProbability * (0.55f + 0.45f * resource));
    if (hash01(node.id + existing * 101, ageBucket + 97) > probability) return;
    const Vec3 direction = branchDirection(node, existing + 11, ageBucket + 19);
    const Vec3 position = node.position + direction * std::max(node.length * 0.72f, 0.08f);
    Leaf& leaf = plant.addLeaf(node.id, position, direction,
                               Vec2(settings_.leafSize, settings_.leafSize * 0.48f),
                               0.0f, clamp01(0.72f + resource * 0.25f), true);
    leaf.growthProgress = 0.03f;
    leaf.growing = true;
    if (events) {
        events->record(currentAge, GrowthEvent::Type::LeafSprouted, node.id, node.id, leaf.id,
                       QStringLiteral("Leaf bud germinated"));
    }
}

void DynamicBranchingSystem::update(PlantModel& plant, float currentAge, float deltaYears,
                                     const GrowthResourceState& resources,
                                     GrowthEventManager* events) {
    if (!plant.rootNode() || deltaYears <= 0.0f) return;
    const float resource = resources.availability();
    std::vector<PlantNode*> nodes;
    collectNodes(plant.rootNode(), &nodes);
    for (PlantNode* node : nodes) {
        updateNodeHealth(*node, currentAge, deltaYears, resource, events);
        if (node->active) {
            tryCreateBranch(plant, *node, currentAge, resource, events);
            trySproutLeaf(plant, *node, currentAge, resource, events);
        }
    }
    for (Leaf& leaf : plant.mutableLeaves()) {
        if (!leaf.active) continue;
        if (resource < settings_.healthDecayThreshold) {
            leaf.health = clamp01(leaf.health - (settings_.healthDecayThreshold - resource) *
                                  settings_.healthDecayRate * deltaYears);
        }
        if (leaf.health <= settings_.deathThreshold) {
            leaf.active = false;
            leaf.growing = false;
            leaf.growthProgress = 0.0f;
            if (events) events->record(currentAge, GrowthEvent::Type::LeafDied, -1,
                                       leaf.parentNodeId, leaf.id, QStringLiteral("Leaf senesced"));
        } else if (leaf.growing) {
            leaf.growthProgress = clamp01(leaf.growthProgress + deltaYears /
                                          std::max(settings_.smoothingYears, 0.01f));
        }
    }
    plant.syncBranchStates();
}
