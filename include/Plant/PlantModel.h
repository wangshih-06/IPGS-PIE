#pragma once

#include <cstddef>
#include <vector>

#include <QJsonObject>
#include <QString>

#include "Algorithm/PlantNode.h"
#include "Plant/PlantTypes.h"

class PlantModel {
public:
    PlantModel() = default;
    PlantModel(PlantModel&&) noexcept = default;
    PlantModel& operator=(PlantModel&&) noexcept = default;
    PlantModel(const PlantModel&) = delete;
    PlantModel& operator=(const PlantModel&) = delete;

    int id = 1;
    QString name = QStringLiteral("Untitled Plant");
    QString species = QStringLiteral("Unknown");
    float age = 0.0f;
    PlantLifeStage lifeStage = PlantLifeStage::Seedling;
    PlantGrowthState growthState = PlantGrowthState::Active;

    const PlantNode* rootNode() const;
    PlantNode* rootNode();
    int rootNodeId() const;

    void clear();
    PlantNode* createRootNode(const Vec3& position = Vec3::Zero(),
                              const Vec3& direction = Vec3::UnitY(),
                              float radius = 0.1f,
                              float nodeAge = 0.0f,
                              bool active = true);
    void setRootNode(PlantNodePtr root);

    PlantNode* addNode(int parentId,
                       const Vec3& position,
                       const Vec3& direction,
                       float radius,
                       float length,
                       float nodeAge = 0.0f,
                       bool active = true,
                       PlantNodeType type = PlantNodeType::Branch,
                       int generation = 0);
    Leaf& addLeaf(int parentNodeId,
                  const Vec3& position,
                  const Vec3& direction,
                  const Vec2& size,
                  float leafAge = 0.0f,
                  float health = 1.0f,
                  bool active = true);
    Root& addRoot(int parentNodeId,
                  const Vec3& position,
                  const Vec3& direction,
                  float radius,
                  float length,
                  float rootAge = 0.0f,
                  int depth = 0,
                  bool active = true);

    PlantNode* findNode(int nodeId);
    const PlantNode* findNode(int nodeId) const;
    std::size_t nodeCount() const;

    const std::vector<Branch>& branches() const;
    const std::vector<Leaf>& leaves() const;
    const std::vector<Root>& roots() const;

    void rebuildBranchesFromSkeleton();
    void advanceAge(float deltaYears);
    bool validate(QString* error = nullptr) const;

    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& json, PlantModel* output, QString* error = nullptr);
    bool saveJson(const QString& filePath, QString* error = nullptr) const;
    static bool loadJson(const QString& filePath, PlantModel* output, QString* error = nullptr);

    QString toTreeString() const;
    static PlantModel createDemoTree();

private:
    int allocateNodeId();
    int allocateLeafId();
    int allocateRootId();

    PlantNodePtr rootNode_;
    std::vector<Branch> branches_;
    std::vector<Leaf> leaves_;
    std::vector<Root> roots_;
    int nextNodeId_ = 0;
    int nextLeafId_ = 0;
    int nextRootId_ = 0;
};
