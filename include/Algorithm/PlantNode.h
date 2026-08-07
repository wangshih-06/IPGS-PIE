#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <vector>

#include "Common/MathTypes.h"

// Logical role of a node in the plant skeleton.
enum class PlantNodeType {
    Stem,
    Branch,
    Bud,
    Root
};

// A PlantNode is one point in the plant skeleton. The parent owns its children,
// while parent/parentId provide fast upward traversal and stable serialization.
class PlantNode {
public:
    PlantNode() = default;

    PlantNode(const Vec3& nodePosition,
              const Vec3& nodeDirection,
              float nodeRadius,
              float nodeLength,
              int nodeGeneration,
              int nodeId = -1,
              int nodeParentId = -1,
              int nodeDepth = 0,
              float nodeAge = 0.0f,
              bool nodeActive = true,
              PlantNodeType nodeType = PlantNodeType::Stem)
        : id(nodeId),
          parentId(nodeParentId),
          position(nodePosition),
          direction(safeDirection(nodeDirection)),
          radius(std::max(0.0f, nodeRadius)),
          length(std::max(0.0f, nodeLength)),
          age(std::max(0.0f, nodeAge)),
          depth(std::max(0, nodeDepth)),
          active(nodeActive),
          health(1.0f),
          growthProgress(nodeActive ? 1.0f : 0.0f),
          growing(nodeActive),
          type(nodeType),
          generation(std::max(0, nodeGeneration)) {
    }

    PlantNode* addChild(const Vec3& childPosition,
                        const Vec3& childDirection,
                        float childRadius,
                        float childLength,
                        int childGeneration,
                        int childId = -1,
                        float childAge = 0.0f,
                        bool childActive = true,
                        PlantNodeType childType = PlantNodeType::Branch) {
        auto child = std::make_unique<PlantNode>(childPosition,
                                                 childDirection,
                                                 childRadius,
                                                 childLength,
                                                 childGeneration,
                                                 childId,
                                                 id,
                                                 depth + 1,
                                                 childAge,
                                                 childActive,
                                                 childType);
        return attachChild(std::move(child));
    }

    PlantNode* attachChild(std::unique_ptr<PlantNode> child) {
        if (!child) {
            return nullptr;
        }
        bindSubtree(child.get(), this, depth + 1);
        PlantNode* rawChild = child.get();
        children.push_back(std::move(child));
        return rawChild;
    }

    PlantNode* find(int nodeId) {
        if (id == nodeId) {
            return this;
        }
        for (auto& child : children) {
            if (PlantNode* result = child->find(nodeId)) {
                return result;
            }
        }
        return nullptr;
    }

    const PlantNode* find(int nodeId) const {
        if (id == nodeId) {
            return this;
        }
        for (const auto& child : children) {
            if (const PlantNode* result = child->find(nodeId)) {
                return result;
            }
        }
        return nullptr;
    }

    std::size_t countNodes() const {
        std::size_t count = 1;
        for (const auto& child : children) {
            count += child->countNodes();
        }
        return count;
    }

    int maxGeneration() const {
        int result = generation;
        for (const auto& child : children) {
            result = std::max(result, child->maxGeneration());
        }
        return result;
    }

    int maxDepth() const {
        int result = depth;
        for (const auto& child : children) {
            result = std::max(result, child->maxDepth());
        }
        return result;
    }

    int id = -1;
    int parentId = -1;
    Vec3 position = Vec3::Zero();
    Vec3 direction = Vec3::UnitY();
    float radius = 0.05f;
    float length = 0.0f;
    float age = 0.0f;
    int depth = 0;
    bool active = true;
    float health = 1.0f;
    float growthProgress = 1.0f;
    bool growing = true;
    PlantNodeType type = PlantNodeType::Stem;

    // L-System bracket nesting level. It is intentionally separate from depth,
    // which counts every parent-child edge in the skeleton.
    int generation = 0;
    PlantNode* parent = nullptr;
    std::vector<std::unique_ptr<PlantNode>> children;

private:
    static void bindSubtree(PlantNode* node, PlantNode* parentNode, int nodeDepth) {
        node->parent = parentNode;
        node->parentId = parentNode ? parentNode->id : -1;
        node->depth = nodeDepth;
        for (auto& child : node->children) {
            bindSubtree(child.get(), node, nodeDepth + 1);
        }
    }

    static Vec3 safeDirection(const Vec3& value) {
        return value.squaredNorm() > 1.0e-8f ? value.normalized() : Vec3::UnitY();
    }
};

using PlantNodePtr = std::unique_ptr<PlantNode>;

