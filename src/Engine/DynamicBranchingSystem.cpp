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

float GrowthResourceState::localLightExposure(const Vec3& position) const {
    float exposure = environment.calculateLightExposure(position, allNodePositions);
    return std::clamp(light * exposure, 0.05f, 1.0f);
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

Vec3 DynamicBranchingSystem::calculateTropismDirection(const PlantNode& parent, int childIndex, int ageBucket,
                                                        PlantNodeType nodeType, const EnvironmentParams& env) {
    // 1. 基础惯性/随机偏移
    const float phase = hash01(parent.id + childIndex * 31, ageBucket) * 6.283185307f;
    const float tilt = 0.35f + 0.55f * hash01(parent.id + 17, ageBucket + childIndex);
    Vec3 axis = parent.direction.cross(Vec3::UnitY());
    if (axis.squaredNorm() < kEpsilon) axis = Vec3::UnitX();
    axis.normalize();
    Vec3 tangent = axis.cross(parent.direction);
    if (tangent.squaredNorm() < kEpsilon) tangent = Vec3::UnitZ();
    tangent.normalize();
    Vec3 baseDir = parent.direction * (1.0f - tilt) +
                   axis * (std::cos(phase) * tilt) +
                   tangent * (std::sin(phase) * tilt);
    if (baseDir.squaredNorm() < kEpsilon) baseDir = Vec3::UnitY();
    baseDir.normalize();

    // 2. 向光性 (Phototropism) 矢量计算
    Vec3 lightDir = env.calculateEffectiveLightDirection(parent.position);

    // 3. 向地性 (Gravitropism/Geotropism) 矢量计算
    // 茎枝: 负向地性 (向上, +Y)； 根系: 正向地性 (向下, -Y)
    Vec3 gravDir = (nodeType == PlantNodeType::Root) ? Vec3(0.0f, -1.0f, 0.0f) : Vec3(0.0f, 1.0f, 0.0f);

    // 4. 加权融合
    const float wp = std::clamp(env.phototropismWeight, 0.0f, 0.9f);
    const float wg = std::clamp(env.gravitropismWeight, 0.0f, 0.9f);
    const float wInertia = std::max(0.1f, 1.0f - wp - wg);

    Vec3 blendedDir = baseDir * wInertia + lightDir * wp + gravDir * wg;
    return blendedDir.squaredNorm() > kEpsilon ? blendedDir.normalized() : baseDir;
}

Vec3 DynamicBranchingSystem::calculateSpatialAvoidanceDirection(
    const PlantNode& parent, const Vec3& desiredDirection, float proposedLength,
    const GrowthResourceState& resources, const DynamicBranchingSettings& settings) {
    const Vec3 fallback = parent.direction.squaredNorm() > kEpsilon
                              ? parent.direction.normalized()
                              : Vec3::UnitY();
    if (!settings.spatialAvoidanceEnabled || proposedLength <= kEpsilon) {
        return desiredDirection.squaredNorm() > kEpsilon ? desiredDirection.normalized() : fallback;
    }

    const Vec3 boundsMin = settings.growthBoundsMin.cwiseMin(settings.growthBoundsMax);
    const Vec3 boundsMax = settings.growthBoundsMin.cwiseMax(settings.growthBoundsMax);
    const Vec3 extent = boundsMax - boundsMin;
    if ((extent.array() <= kEpsilon).any()) return fallback;

    Vec3 direction = desiredDirection.squaredNorm() > kEpsilon ? desiredDirection.normalized() : fallback;
    const float margin = std::max(0.0f, settings.boundaryAvoidanceDistance);
    const Vec3 safeMin = boundsMin + Vec3::Constant(margin).cwiseMin(extent * 0.45f);
    const Vec3 safeMax = boundsMax - Vec3::Constant(margin).cwiseMin(extent * 0.45f);
    const float length = std::max(proposedLength, kEpsilon);

    // A few projection passes guarantee the predicted endpoint remains in the
    // configurable box, even if tropism initially points straight through a face.
    for (int iteration = 0; iteration < 3; ++iteration) {
        const Vec3 endpoint = parent.position + direction * length;
        Vec3 correction = Vec3::Zero();
        for (int axis = 0; axis < 3; ++axis) {
            if (endpoint[axis] < safeMin[axis]) {
                correction[axis] += (safeMin[axis] - endpoint[axis]) / length;
            } else if (endpoint[axis] > safeMax[axis]) {
                correction[axis] -= (endpoint[axis] - safeMax[axis]) / length;
            } else {
                const float distanceToMin = endpoint[axis] - boundsMin[axis];
                const float distanceToMax = boundsMax[axis] - endpoint[axis];
                if (distanceToMin < margin) correction[axis] += (margin - distanceToMin) / length;
                if (distanceToMax < margin) correction[axis] -= (margin - distanceToMax) / length;
            }
        }

        if (settings.nodeClearance > kEpsilon) {
            for (const Vec3& occupied : resources.allNodePositions) {
                if ((occupied - parent.position).squaredNorm() < kEpsilon) continue;
                const Vec3 delta = endpoint - occupied;
                const float distance = delta.norm();
                if (distance > kEpsilon && distance < settings.nodeClearance) {
                    correction += delta / distance *
                                  ((settings.nodeClearance - distance) / settings.nodeClearance) *
                                  std::max(0.0f, settings.nodeAvoidanceWeight);
                }
            }
        }
        if (correction.squaredNorm() < kEpsilon) break;
        const Vec3 adjusted = direction + correction * std::max(0.0f, settings.boundaryAvoidanceWeight);
        if (adjusted.squaredNorm() < kEpsilon) break;
        direction = adjusted.normalized();
    }
    return direction;
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
                                               float deltaYears, const GrowthResourceState& resources,
                                               GrowthEventManager* events) {
    if (!node.active || deltaYears <= 0.0f) return;

    const float nodeExposure = resources.localLightExposure(node.position);
    const float effectiveResource = resources.availability() * (0.4f + 0.6f * nodeExposure);

    if (effectiveResource < settings_.healthDecayThreshold) {
        node.health = clamp01(node.health - (settings_.healthDecayThreshold - effectiveResource) *
                              settings_.healthDecayRate * deltaYears);
    } else {
        node.health = clamp01(node.health + settings_.healthRecoveryRate * deltaYears);
    }
    if (node.health <= settings_.deathThreshold) {
        markSubtreeDead(node, currentAge, events);
        return;
    }
    if (node.age >= settings_.growthStopAge || effectiveResource < settings_.resourceThreshold) {
        if (node.growing) {
            node.growing = false;
            if (events && stopReported_.insert(node.id).second) {
                events->record(currentAge, GrowthEvent::Type::GrowthStopped, node.id, node.parentId,
                               -1, QStringLiteral("Node growth stopped by age/resource constraint"));
            }
        }
    }
    if (node.growing) {
        const float growthSpeed = 0.3f + 0.7f * nodeExposure;
        const float smoothing = std::max(settings_.smoothingYears, 0.01f);
        node.growthProgress = clamp01(node.growthProgress + (deltaYears * growthSpeed) / smoothing);
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
                       -1, QStringLiteral("Branch died from insufficient growth resources / shade"));
    }
    for (auto& child : node.children) markSubtreeDead(*child, currentAge, events);
}

void DynamicBranchingSystem::tryCreateBranch(PlantModel& plant, PlantNode& node,
                                               float currentAge, const GrowthResourceState& resources,
                                               GrowthEventManager* events) {
    if (!node.active || !node.growing || node.depth >= settings_.maxDepth ||
        static_cast<int>(plant.branches().size()) >= settings_.maxTotalBranches ||
        static_cast<int>(node.children.size()) >= settings_.maxChildrenPerNode ||
        node.age < settings_.branchStartAge) return;

    const float nodeExposure = resources.localLightExposure(node.position);
    const float resource = resources.availability() * (0.35f + 0.65f * nodeExposure);

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
    const PlantNodeType childType = (node.type == PlantNodeType::Root) ? PlantNodeType::Root : PlantNodeType::Branch;
    const Vec3 desiredDirection = calculateTropismDirection(node, childIndex, ageBucket, childType, resources.environment);
    const float length = std::max(0.08f, std::max(node.length, 0.18f) * settings_.branchLengthRatio);
    const Vec3 direction = calculateSpatialAvoidanceDirection(node, desiredDirection, length, resources, settings_);
    const Vec3 position = node.position + direction * length;
    const float radius = std::max(0.012f, node.radius * settings_.branchRadiusRatio);
    PlantNode* child = plant.addNode(node.id, position, direction, radius, length, 0.0f,
                                     true, childType, node.generation + 1);
    if (!child) return;
    child->growthProgress = 0.04f;
    child->growing = true;
    child->health = clamp01(0.72f + 0.25f * resource);
    if (events) {
        events->record(currentAge, GrowthEvent::Type::BranchCreated, child->id, node.id,
                       -1, QStringLiteral("Age/resource triggered branch creation"),
                       QJsonObject{{QStringLiteral("probability"), probability},
                                   {QStringLiteral("resource"), resource},
                                   {QStringLiteral("lightExposure"), nodeExposure},
                                   {QStringLiteral("generation"), child->generation}});
    }
}

void DynamicBranchingSystem::trySproutLeaf(PlantModel& plant, PlantNode& node,
                                             float currentAge, const GrowthResourceState& resources,
                                             GrowthEventManager* events) {
    if (!node.active || !node.growing || node.type == PlantNodeType::Root || node.age < settings_.leafSproutAge ||
        leafCount(plant, node.id) >= settings_.maxLeavesPerNode) return;

    const float nodeExposure = resources.localLightExposure(node.position);
    const float resource = resources.availability() * (0.35f + 0.65f * nodeExposure);

    const int existing = leafCount(plant, node.id);
    const int ageBucket = static_cast<int>(std::floor(node.age / std::max(settings_.branchInterval, 0.05f)));
    const float probability = clamp01(settings_.leafProbability * (0.55f + 0.45f * resource));
    if (hash01(node.id + existing * 101, ageBucket + 97) > probability) return;
    const float leafOffset = std::max(node.length * 0.72f, 0.08f);
    const Vec3 desiredDirection = calculateTropismDirection(node, existing + 11, ageBucket + 19, PlantNodeType::Branch, resources.environment);
    const Vec3 direction = calculateSpatialAvoidanceDirection(node, desiredDirection, leafOffset, resources, settings_);
    const Vec3 position = node.position + direction * leafOffset;
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

    std::vector<PlantNode*> nodes;
    collectNodes(plant.rootNode(), &nodes);

    for (PlantNode* node : nodes) {
        updateNodeHealth(*node, currentAge, deltaYears, resources, events);
        if (node->active) {
            tryCreateBranch(plant, *node, currentAge, resources, events);
            trySproutLeaf(plant, *node, currentAge, resources, events);
        }
    }
    for (Leaf& leaf : plant.mutableLeaves()) {
        if (!leaf.active) continue;
        const float nodeExposure = resources.localLightExposure(leaf.position);
        const float effectiveResource = resources.availability() * (0.4f + 0.6f * nodeExposure);

        if (effectiveResource < settings_.healthDecayThreshold) {
            leaf.health = clamp01(leaf.health - (settings_.healthDecayThreshold - effectiveResource) *
                                  settings_.healthDecayRate * deltaYears);
        }
        if (leaf.health <= settings_.deathThreshold) {
            leaf.active = false;
            leaf.growing = false;
            leaf.growthProgress = 0.0f;
            if (events) events->record(currentAge, GrowthEvent::Type::LeafDied, -1,
                                       leaf.parentNodeId, leaf.id, QStringLiteral("Leaf senesced from low light"));
        } else if (leaf.growing) {
            leaf.growthProgress = clamp01(leaf.growthProgress + deltaYears /
                                          std::max(settings_.smoothingYears, 0.01f));
        }
    }
    plant.syncBranchStates();
}
