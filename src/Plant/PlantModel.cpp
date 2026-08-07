#include "Plant/PlantModel.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <set>
#include <utility>

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>
#include <QStringList>

namespace {
constexpr int kSchemaVersion = 1;

double jsonNumber(float value) {
    return std::round(static_cast<double>(value) * 1000000.0) / 1000000.0;
}

QJsonArray vec2ToJson(const Vec2& value) {
    return QJsonArray{jsonNumber(value.x()), jsonNumber(value.y())};
}

QJsonArray vec3ToJson(const Vec3& value) {
    return QJsonArray{jsonNumber(value.x()), jsonNumber(value.y()),
                      jsonNumber(value.z())};
}

bool readVec2(const QJsonValue& value, Vec2* output) {
    if (!output || !value.isArray()) return false;
    const QJsonArray array = value.toArray();
    if (array.size() != 2 || !array[0].isDouble() || !array[1].isDouble()) return false;
    *output = Vec2(static_cast<float>(array[0].toDouble()), static_cast<float>(array[1].toDouble()));
    return output->allFinite();
}

bool readVec3(const QJsonValue& value, Vec3* output) {
    if (!output || !value.isArray()) return false;
    const QJsonArray array = value.toArray();
    if (array.size() != 3 || !array[0].isDouble() || !array[1].isDouble() || !array[2].isDouble()) return false;
    *output = Vec3(static_cast<float>(array[0].toDouble()), static_cast<float>(array[1].toDouble()),
                   static_cast<float>(array[2].toDouble()));
    return output->allFinite();
}

QString nodeTypeToString(PlantNodeType type) {
    switch (type) {
    case PlantNodeType::Stem: return QStringLiteral("stem");
    case PlantNodeType::Branch: return QStringLiteral("branch");
    case PlantNodeType::Bud: return QStringLiteral("bud");
    case PlantNodeType::Root: return QStringLiteral("root");
    }
    return QStringLiteral("stem");
}

PlantNodeType nodeTypeFromString(const QString& value) {
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("branch")) return PlantNodeType::Branch;
    if (normalized == QStringLiteral("bud")) return PlantNodeType::Bud;
    if (normalized == QStringLiteral("root")) return PlantNodeType::Root;
    return PlantNodeType::Stem;
}

void writeNodeDepthFirst(const PlantNode& node, QJsonArray* nodes) {
    nodes->append(QJsonObject{
        {QStringLiteral("id"), node.id}, {QStringLiteral("parentId"), node.parentId},
        {QStringLiteral("type"), nodeTypeToString(node.type)},
        {QStringLiteral("position"), vec3ToJson(node.position)},
        {QStringLiteral("direction"), vec3ToJson(node.direction)},
        {QStringLiteral("radius"), jsonNumber(node.radius)},
        {QStringLiteral("length"), jsonNumber(node.length)},
        {QStringLiteral("age"), jsonNumber(node.age)},
        {QStringLiteral("depth"), node.depth}, {QStringLiteral("generation"), node.generation},
        {QStringLiteral("active"), node.active}, {QStringLiteral("health"), jsonNumber(node.health)},
        {QStringLiteral("growthProgress"), jsonNumber(node.growthProgress)},
        {QStringLiteral("growing"), node.growing}
    });
    for (const auto& child : node.children) writeNodeDepthFirst(*child, nodes);
}

void visitMutableNodes(PlantNode* node, const std::function<void(PlantNode&)>& visitor) {
    if (!node) return;
    visitor(*node);
    for (auto& child : node->children) visitMutableNodes(child.get(), visitor);
}

void visitConstNodes(const PlantNode* node, const std::function<void(const PlantNode&)>& visitor) {
    if (!node) return;
    visitor(*node);
    for (const auto& child : node->children) visitConstNodes(child.get(), visitor);
}

void setError(QString* error, const QString& message) {
    if (error) *error = message;
}
}

QString toString(PlantLifeStage stage) {
    switch (stage) {
    case PlantLifeStage::Seed: return QStringLiteral("seed");
    case PlantLifeStage::Seedling: return QStringLiteral("seedling");
    case PlantLifeStage::Vegetative: return QStringLiteral("vegetative");
    case PlantLifeStage::Mature: return QStringLiteral("mature");
    case PlantLifeStage::Flowering: return QStringLiteral("flowering");
    case PlantLifeStage::Dormant: return QStringLiteral("dormant");
    case PlantLifeStage::Senescent: return QStringLiteral("senescent");
    }
    return QStringLiteral("seedling");
}

QString toString(PlantGrowthState state) {
    switch (state) {
    case PlantGrowthState::Active: return QStringLiteral("active");
    case PlantGrowthState::Paused: return QStringLiteral("paused");
    case PlantGrowthState::Dormant: return QStringLiteral("dormant");
    case PlantGrowthState::Stressed: return QStringLiteral("stressed");
    case PlantGrowthState::Completed: return QStringLiteral("completed");
    }
    return QStringLiteral("active");
}

PlantLifeStage plantLifeStageFromString(const QString& value) {
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("seed")) return PlantLifeStage::Seed;
    if (normalized == QStringLiteral("vegetative")) return PlantLifeStage::Vegetative;
    if (normalized == QStringLiteral("mature")) return PlantLifeStage::Mature;
    if (normalized == QStringLiteral("flowering")) return PlantLifeStage::Flowering;
    if (normalized == QStringLiteral("dormant")) return PlantLifeStage::Dormant;
    if (normalized == QStringLiteral("senescent")) return PlantLifeStage::Senescent;
    return PlantLifeStage::Seedling;
}

PlantGrowthState plantGrowthStateFromString(const QString& value) {
    const QString normalized = value.trimmed().toLower();
    if (normalized == QStringLiteral("paused")) return PlantGrowthState::Paused;
    if (normalized == QStringLiteral("dormant")) return PlantGrowthState::Dormant;
    if (normalized == QStringLiteral("stressed")) return PlantGrowthState::Stressed;
    if (normalized == QStringLiteral("completed")) return PlantGrowthState::Completed;
    return PlantGrowthState::Active;
}

const PlantNode* PlantModel::rootNode() const { return rootNode_.get(); }
PlantNode* PlantModel::rootNode() { return rootNode_.get(); }
int PlantModel::rootNodeId() const { return rootNode_ ? rootNode_->id : -1; }

void PlantModel::clear() {
    rootNode_.reset();
    branches_.clear(); leaves_.clear(); roots_.clear();
    nextNodeId_ = nextLeafId_ = nextRootId_ = 0;
}

PlantNode* PlantModel::createRootNode(const Vec3& position, const Vec3& direction,
                                      float radius, float nodeAge, bool active) {
    clear();
    rootNode_ = std::make_unique<PlantNode>(position, direction, radius, 0.0f, 0,
                                            allocateNodeId(), -1, 0, nodeAge, active,
                                            PlantNodeType::Stem);
    captureBaselines();
    return rootNode_.get();
}

void PlantModel::setRootNode(PlantNodePtr root) {
    rootNode_ = std::move(root);
    branches_.clear();
    leaves_.clear();
    roots_.clear();
    nextNodeId_ = nextLeafId_ = nextRootId_ = 0;
    std::set<int> usedIds;
    std::function<void(PlantNode*, PlantNode*, int)> normalize =
        [&](PlantNode* node, PlantNode* parent, int depth) {
            if (!node) return;
            if (node->id < 0 || usedIds.count(node->id) != 0) {
                while (usedIds.count(nextNodeId_) != 0) ++nextNodeId_;
                node->id = nextNodeId_++;
            }
            usedIds.insert(node->id);
            nextNodeId_ = std::max(nextNodeId_, node->id + 1);
            node->parent = parent;
            node->parentId = parent ? parent->id : -1;
            node->depth = depth;
            for (auto& child : node->children) normalize(child.get(), node, depth + 1);
        };
    normalize(rootNode_.get(), nullptr, 0);
    rebuildBranchesFromSkeleton();
    captureBaselines();
}

PlantNode* PlantModel::addNode(int parentId, const Vec3& position, const Vec3& direction,
                               float radius, float length, float nodeAge, bool active,
                               PlantNodeType type, int generation) {
    PlantNode* parent = findNode(parentId);
    if (!parent) return nullptr;
    PlantNode* child = parent->addChild(position, direction, radius, length, generation,
                                        allocateNodeId(), nodeAge, active, type);
    int branchId = 0;
    for (const Branch& branch : branches_) branchId = std::max(branchId, branch.id + 1);
    branches_.push_back(Branch{branchId, parent->id, child->id, nodeAge, 1.0f, 1.0f, 1.0f, true, active});
    // 同步扩展 baseline 表
    if (child->id >= static_cast<int>(baselineNodeLength_.size())) {
        baselineNodeLength_.resize(static_cast<std::size_t>(nextNodeId_), 0.0f);
        baselineNodeRadius_.resize(static_cast<std::size_t>(nextNodeId_), 0.0f);
    }
    baselineNodeLength_[child->id] = child->length;
    baselineNodeRadius_[child->id] = child->radius;
    return child;
}

Leaf& PlantModel::addLeaf(int parentNodeId, const Vec3& position, const Vec3& direction,
                          const Vec2& size, float leafAge, float health, bool active) {
    Leaf leaf;
    leaf.id = allocateLeafId(); leaf.parentNodeId = parentNodeId; leaf.position = position;
    leaf.direction = direction.squaredNorm() > 1.0e-8f ? direction.normalized() : Vec3::UnitY();
    leaf.size = size.cwiseMax(Vec2::Zero()); leaf.age = std::max(0.0f, leafAge);
    leaf.health = std::max(0.0f, std::min(1.0f, health)); leaf.active = active;
    leaves_.push_back(leaf);
    // 同步扩展 baseline 表，避免 applyGrowthSample 漏掉新叶子
    if (leaf.id >= static_cast<int>(baselineLeafSize_.size())) {
        baselineLeafSize_.resize(static_cast<std::size_t>(nextLeafId_), Vec2::Zero());
    }
    baselineLeafSize_[leaf.id] = leaf.size;
    return leaves_.back();
}

Root& PlantModel::addRoot(int parentNodeId, const Vec3& position, const Vec3& direction,
                          float radius, float length, float rootAge, int depth, bool active) {
    Root root;
    root.id = allocateRootId(); root.parentNodeId = parentNodeId; root.position = position;
    root.direction = direction.squaredNorm() > 1.0e-8f ? direction.normalized() : -Vec3::UnitY();
    root.radius = std::max(0.0f, radius); root.length = std::max(0.0f, length);
    root.age = std::max(0.0f, rootAge); root.depth = std::max(0, depth); root.active = active;
    roots_.push_back(root);
    return roots_.back();
}

PlantNode* PlantModel::findNode(int nodeId) { return rootNode_ ? rootNode_->find(nodeId) : nullptr; }
const PlantNode* PlantModel::findNode(int nodeId) const { return rootNode_ ? rootNode_->find(nodeId) : nullptr; }
std::size_t PlantModel::nodeCount() const { return rootNode_ ? rootNode_->countNodes() : 0; }
const std::vector<Branch>& PlantModel::branches() const { return branches_; }
const std::vector<Leaf>& PlantModel::leaves() const { return leaves_; }
std::vector<Leaf>& PlantModel::mutableLeaves() { return leaves_; }
const std::vector<Root>& PlantModel::roots() const { return roots_; }

void PlantModel::rebuildBranchesFromSkeleton() {
    branches_.clear();
    int branchId = 0;
    visitConstNodes(rootNode_.get(), [&](const PlantNode& node) {
        for (const auto& child : node.children)
            branches_.push_back(Branch{branchId++, node.id, child->id, child->age, 1.0f,
                                       child->health, child->growthProgress, child->growing,
                                       child->active});
    });
}

void PlantModel::syncBranchStates() {
    rebuildBranchesFromSkeleton();
}

void PlantModel::advanceAge(float deltaYears) {
    if (deltaYears <= 0.0f) return;
    age += deltaYears;
    visitMutableNodes(rootNode_.get(), [deltaYears](PlantNode& node) { node.age += deltaYears; });
    for (Branch& value : branches_) value.age += deltaYears;
    for (Leaf& value : leaves_) value.age += deltaYears;
    for (Root& value : roots_) value.age += deltaYears;
}

// ============================================================================
// 第9周：生长基线与 sample 应用
//   - captureBaselines() 在 setRootNode/createRootNode/addLeaf/loadJson 末尾被调；
//     之后 applyGrowthSample 只在 baseline × scale 上重写 length/radius/size。
//   - 多次推进或反向推进都安全（不会累积漂移）。
// ============================================================================
void PlantModel::captureBaselines() {
    const int nodeCapacity = nextNodeId_ > 0 ? nextNodeId_ : 64;
    const int leafCapacity = nextLeafId_ > 0 ? nextLeafId_ : 32;
    baselineNodeLength_.assign(static_cast<std::size_t>(nodeCapacity), 0.0f);
    baselineNodeRadius_.assign(static_cast<std::size_t>(nodeCapacity), 0.0f);
    baselineLeafSize_.assign(static_cast<std::size_t>(leafCapacity), Vec2::Zero());

    visitMutableNodes(rootNode_.get(), [this](PlantNode& node) {
        if (node.id >= 0 &&
            node.id < static_cast<int>(baselineNodeLength_.size())) {
            baselineNodeLength_[node.id] = node.length;
            baselineNodeRadius_[node.id] = node.radius;
        }
    });
    for (const Leaf& leaf : leaves_) {
        if (leaf.id >= 0 && leaf.id < static_cast<int>(baselineLeafSize_.size())) {
            baselineLeafSize_[leaf.id] = leaf.size;
        }
    }
    baselinesCaptured_ = true;
}

void PlantModel::applyGrowthSample(const GrowthSample& sample) {
    if (!baselinesCaptured_) captureBaselines();
    // 把 plant 总年龄同步到时间轴当前 age（GrowthClock 通过 tick 推进）
    age = sample.age;

    // 前序遍历：父先于子，因此可在同一趟里完成
    //   1) 用 基线 × scale 重写 length / radius；
    //   2) 按新 length 沿节点自身方向重算 position（枝干伸长），
    //      root 保持原位，子节点 = 父新位置 + direction × 新 length。
    visitMutableNodes(rootNode_.get(), [this, &sample](PlantNode& node) {
        if (node.id < 0 || node.id >= static_cast<int>(baselineNodeLength_.size())) return;
        const float baseL = baselineNodeLength_[node.id];
        const float baseR = baselineNodeRadius_[node.id];
        // 防止基线被吃掉（首次 capture 时节点 length 仍是 0）
        const float progress = std::clamp(node.growthProgress, 0.0f, 1.0f);
        node.length = ((baseL > 0.0f) ? baseL : node.length) * sample.lengthScale * progress;
        node.radius = ((baseR > 0.0f) ? baseR : node.radius) * sample.radiusScale * progress;
        if (node.parent) {
            node.position = node.parent->position + node.direction * node.length;
        }
    });
    for (Leaf& leaf : leaves_) {
        if (leaf.id < 0 || leaf.id >= static_cast<int>(baselineLeafSize_.size())) continue;
        const Vec2 base = baselineLeafSize_[leaf.id];
        const Vec2 effective = (base.x() > 0.0f && base.y() > 0.0f)
                                   ? base
                                   : leaf.size;
        const float progress = std::clamp(leaf.growthProgress, 0.0f, 1.0f);
        leaf.size = effective.cwiseProduct(sample.leafScale) * progress;
    }

    lifeStage = sample.lifeStage;
    growthState = (sample.age >= 30.0f) ? PlantGrowthState::Completed
                   : (sample.age <= 0.0f ? growthState : PlantGrowthState::Active);
}

bool PlantModel::validate(QString* error) const {
    if (!rootNode_) {
        setError(error, QStringLiteral("Plant model has no skeleton root node."));
        return false;
    }
    if (!std::isfinite(age) || age < 0.0f) {
        setError(error, QStringLiteral("Plant age must be a finite non-negative value."));
        return false;
    }
    QSet<int> nodeIds;
    bool nodesValid = true;
    QString nodeError;
    visitConstNodes(rootNode_.get(), [&](const PlantNode& node) {
        if (!nodesValid) return;
        if (node.id < 0 || nodeIds.contains(node.id)) {
            nodesValid = false;
            nodeError = QStringLiteral("Node IDs must be unique non-negative integers (invalid id %1).").arg(node.id);
            return;
        }
        nodeIds.insert(node.id);
        if (!node.position.allFinite() || !node.direction.allFinite() || !std::isfinite(node.radius) ||
            !std::isfinite(node.length) || !std::isfinite(node.age) ||
            node.radius < 0.0f || node.length < 0.0f || node.age < 0.0f) {
            nodesValid = false;
            nodeError = QStringLiteral("Node %1 contains invalid numeric values.").arg(node.id);
            return;
        }
        if (!node.parent) {
            if (&node != rootNode_.get() || node.parentId != -1 || node.depth != 0) {
                nodesValid = false;
                nodeError = QStringLiteral("Root node relationship metadata is inconsistent.");
            }
        } else if (node.parentId != node.parent->id || node.depth != node.parent->depth + 1) {
            nodesValid = false;
            nodeError = QStringLiteral("Parent relationship is inconsistent for node %1.").arg(node.id);
        }
    });
    if (!nodesValid) { setError(error, nodeError); return false; }

    if (branches_.size() != nodeCount() - 1) {
        setError(error, QStringLiteral("Branch count must equal skeleton edge count."));
        return false;
    }
    QSet<int> branchIds;
    QSet<int> branchChildIds;
    for (const Branch& branch : branches_) {
        const PlantNode* child = findNode(branch.childNodeId);
        if (branch.id < 0 || branchIds.contains(branch.id) || branchChildIds.contains(branch.childNodeId) ||
            !nodeIds.contains(branch.parentNodeId) || !child || child->parentId != branch.parentNodeId ||
            !std::isfinite(branch.age) || !std::isfinite(branch.stiffness) ||
            branch.age < 0.0f || branch.stiffness < 0.0f) {
            setError(error, QStringLiteral("Branch %1 has an invalid ID or skeleton edge.").arg(branch.id));
            return false;
        }
        branchIds.insert(branch.id);
        branchChildIds.insert(branch.childNodeId);
    }
    QSet<int> leafIds;
    for (const Leaf& leaf : leaves_) {
        if (leaf.id < 0 || leafIds.contains(leaf.id) || !nodeIds.contains(leaf.parentNodeId) ||
            !leaf.position.allFinite() || !leaf.direction.allFinite() || !leaf.size.allFinite() ||
            !std::isfinite(leaf.age) || !std::isfinite(leaf.health) || leaf.age < 0.0f ||
            leaf.health < 0.0f || leaf.health > 1.0f || (leaf.size.array() < 0.0f).any()) {
            setError(error, QStringLiteral("Leaf %1 has invalid data or parent node.").arg(leaf.id));
            return false;
        }
        leafIds.insert(leaf.id);
    }
    QSet<int> rootIds;
    for (const Root& root : roots_) {
        if (root.id < 0 || rootIds.contains(root.id) || !nodeIds.contains(root.parentNodeId) ||
            !root.position.allFinite() || !root.direction.allFinite() || !std::isfinite(root.radius) ||
            !std::isfinite(root.length) || !std::isfinite(root.age) || root.radius < 0.0f ||
            root.length < 0.0f || root.age < 0.0f || root.depth < 0) {
            setError(error, QStringLiteral("Root %1 has invalid data or parent node.").arg(root.id));
            return false;
        }
        rootIds.insert(root.id);
    }
    return true;
}

QJsonObject PlantModel::toJson() const {
    QJsonArray nodes;
    if (rootNode_) writeNodeDepthFirst(*rootNode_, &nodes);

    QJsonArray branches;
    for (const Branch& branch : branches_) {
        branches.append(QJsonObject{
            {QStringLiteral("id"), branch.id}, {QStringLiteral("parentNodeId"), branch.parentNodeId},
            {QStringLiteral("childNodeId"), branch.childNodeId},
            {QStringLiteral("age"), jsonNumber(branch.age)},
            {QStringLiteral("stiffness"), jsonNumber(branch.stiffness)},
            {QStringLiteral("active"), branch.active}
        });
    }
    QJsonArray leaves;
    for (const Leaf& leaf : leaves_) {
        leaves.append(QJsonObject{
            {QStringLiteral("id"), leaf.id}, {QStringLiteral("parentNodeId"), leaf.parentNodeId},
            {QStringLiteral("position"), vec3ToJson(leaf.position)},
            {QStringLiteral("direction"), vec3ToJson(leaf.direction)},
            {QStringLiteral("size"), vec2ToJson(leaf.size)},
            {QStringLiteral("age"), jsonNumber(leaf.age)},
            {QStringLiteral("health"), jsonNumber(leaf.health)},
            {QStringLiteral("active"), leaf.active}
        });
    }
    QJsonArray roots;
    for (const Root& root : roots_) {
        roots.append(QJsonObject{
            {QStringLiteral("id"), root.id}, {QStringLiteral("parentNodeId"), root.parentNodeId},
            {QStringLiteral("position"), vec3ToJson(root.position)},
            {QStringLiteral("direction"), vec3ToJson(root.direction)},
            {QStringLiteral("radius"), jsonNumber(root.radius)},
            {QStringLiteral("length"), jsonNumber(root.length)},
            {QStringLiteral("age"), jsonNumber(root.age)},
            {QStringLiteral("depth"), root.depth}, {QStringLiteral("active"), root.active}
        });
    }

    return QJsonObject{
        {QStringLiteral("schema"), QStringLiteral("plantsim.skeleton")},
        {QStringLiteral("version"), kSchemaVersion},
        {QStringLiteral("plant"), QJsonObject{
            {QStringLiteral("id"), id}, {QStringLiteral("name"), name},
            {QStringLiteral("species"), species}, {QStringLiteral("age"), jsonNumber(age)},
            {QStringLiteral("lifeStage"), toString(lifeStage)},
            {QStringLiteral("growthState"), toString(growthState)}
        }},
        {QStringLiteral("skeleton"), QJsonObject{
            {QStringLiteral("rootNodeId"), rootNodeId()}, {QStringLiteral("nodes"), nodes},
            {QStringLiteral("branches"), branches}, {QStringLiteral("leaves"), leaves},
            {QStringLiteral("roots"), roots}
        }}
    };
}

bool PlantModel::fromJson(const QJsonObject& json, PlantModel* output, QString* error) {
    if (!output) { setError(error, QStringLiteral("Output PlantModel pointer is null.")); return false; }
    if (json.value(QStringLiteral("schema")).toString() != QStringLiteral("plantsim.skeleton")) {
        setError(error, QStringLiteral("Unsupported or missing JSON schema.")); return false;
    }
    if (json.value(QStringLiteral("version")).toInt(-1) != kSchemaVersion) {
        setError(error, QStringLiteral("Unsupported skeleton schema version.")); return false;
    }
    const QJsonObject plantJson = json.value(QStringLiteral("plant")).toObject();
    const QJsonObject skeletonJson = json.value(QStringLiteral("skeleton")).toObject();
    const QJsonArray nodesJson = skeletonJson.value(QStringLiteral("nodes")).toArray();
    if (nodesJson.isEmpty()) { setError(error, QStringLiteral("Skeleton nodes array is empty.")); return false; }

    struct NodeRecord {
        int id = -1;
        int parentId = -1;
        Vec3 position = Vec3::Zero();
        Vec3 direction = Vec3::UnitY();
        float radius = 0.0f;
        float length = 0.0f;
        float age = 0.0f;
        int generation = 0;
        int depth = 0;
        bool active = true;
        float health = 1.0f;
        float growthProgress = 1.0f;
        bool growing = true;
        PlantNodeType type = PlantNodeType::Stem;
    };
    std::vector<NodeRecord> records;
    records.reserve(static_cast<std::size_t>(nodesJson.size()));
    QSet<int> ids;
    for (const QJsonValue& nodeValue : nodesJson) {
        if (!nodeValue.isObject()) { setError(error, QStringLiteral("Every skeleton node must be an object.")); return false; }
        const QJsonObject object = nodeValue.toObject();
        NodeRecord record;
        record.id = object.value(QStringLiteral("id")).toInt(-1);
        record.parentId = object.value(QStringLiteral("parentId")).toInt(-1);
        if (record.id < 0 || ids.contains(record.id)) {
            setError(error, QStringLiteral("Duplicate or invalid node id %1.").arg(record.id)); return false;
        }
        if (!readVec3(object.value(QStringLiteral("position")), &record.position) ||
            !readVec3(object.value(QStringLiteral("direction")), &record.direction)) {
            setError(error, QStringLiteral("Node %1 has an invalid vector.").arg(record.id)); return false;
        }
        record.radius = static_cast<float>(object.value(QStringLiteral("radius")).toDouble(-1.0));
        record.length = static_cast<float>(object.value(QStringLiteral("length")).toDouble(-1.0));
        record.age = static_cast<float>(object.value(QStringLiteral("age")).toDouble(0.0));
        record.generation = object.value(QStringLiteral("generation")).toInt(0);
        record.depth = object.value(QStringLiteral("depth")).toInt(-1);
        record.active = object.value(QStringLiteral("active")).toBool(true);
        record.health = static_cast<float>(object.value(QStringLiteral("health")).toDouble(1.0));
        record.growthProgress = static_cast<float>(object.value(QStringLiteral("growthProgress")).toDouble(record.active ? 1.0 : 0.0));
        record.growing = object.value(QStringLiteral("growing")).toBool(record.active);
        record.type = nodeTypeFromString(object.value(QStringLiteral("type")).toString());
        if (record.radius < 0.0f || record.length < 0.0f || record.age < 0.0f) {
            setError(error, QStringLiteral("Node %1 has negative geometry or age.").arg(record.id)); return false;
        }
        ids.insert(record.id);
        records.push_back(record);
    }

    const int rootId = skeletonJson.value(QStringLiteral("rootNodeId")).toInt(-1);
    if (!ids.contains(rootId)) { setError(error, QStringLiteral("rootNodeId does not reference a node.")); return false; }
    const auto rootRecord = std::find_if(records.begin(), records.end(),
        [rootId](const NodeRecord& record) { return record.id == rootId; });
    if (rootRecord == records.end() || rootRecord->parentId != -1) {
        setError(error, QStringLiteral("The root node must have parentId = -1.")); return false;
    }
    for (const NodeRecord& record : records) {
        if (record.id != rootId && !ids.contains(record.parentId)) {
            setError(error, QStringLiteral("Node %1 references missing parent %2.").arg(record.id).arg(record.parentId));
            return false;
        }
    }

    QSet<int> building;
    QSet<int> built;
    bool buildOk = true;
    QString buildError;
    std::function<PlantNodePtr(int, PlantNode*, int)> buildNode =
        [&](int nodeId, PlantNode* parent, int depth) -> PlantNodePtr {
            if (building.contains(nodeId)) {
                buildOk = false;
                buildError = QStringLiteral("Cycle detected at node %1.").arg(nodeId);
                return nullptr;
            }
            const auto recordIt = std::find_if(records.begin(), records.end(),
                [nodeId](const NodeRecord& record) { return record.id == nodeId; });
            if (recordIt == records.end()) {
                buildOk = false;
                buildError = QStringLiteral("Missing node record %1.").arg(nodeId);
                return nullptr;
            }
            if (recordIt->depth != depth) {
                buildOk = false;
                buildError = QStringLiteral("Node %1 has inconsistent depth %2 (expected %3).")
                                 .arg(nodeId).arg(recordIt->depth).arg(depth);
                return nullptr;
            }
            building.insert(nodeId);
            auto node = std::make_unique<PlantNode>(recordIt->position, recordIt->direction,
                recordIt->radius, recordIt->length, recordIt->generation, recordIt->id,
                parent ? parent->id : -1, depth, recordIt->age, recordIt->active, recordIt->type);
            node->health = std::clamp(recordIt->health, 0.0f, 1.0f);
            node->growthProgress = std::clamp(recordIt->growthProgress, 0.0f, 1.0f);
            node->growing = recordIt->growing;
            node->parent = parent;
            for (const NodeRecord& possibleChild : records) {
                if (possibleChild.parentId == nodeId) {
                    PlantNodePtr child = buildNode(possibleChild.id, node.get(), depth + 1);
                    if (!buildOk || !child) return nullptr;
                    node->children.push_back(std::move(child));
                }
            }
            building.remove(nodeId);
            built.insert(nodeId);
            return node;
        };

    PlantModel model;
    model.id = plantJson.value(QStringLiteral("id")).toInt(1);
    model.name = plantJson.value(QStringLiteral("name")).toString(QStringLiteral("Untitled Plant"));
    model.species = plantJson.value(QStringLiteral("species")).toString(QStringLiteral("Unknown"));
    model.age = static_cast<float>(plantJson.value(QStringLiteral("age")).toDouble(0.0));
    model.lifeStage = plantLifeStageFromString(plantJson.value(QStringLiteral("lifeStage")).toString());
    model.growthState = plantGrowthStateFromString(plantJson.value(QStringLiteral("growthState")).toString());
    model.rootNode_ = buildNode(rootId, nullptr, 0);
    if (!buildOk || !model.rootNode_ || built.size() != static_cast<int>(records.size())) {
        setError(error, buildError.isEmpty() ? QStringLiteral("Skeleton contains disconnected nodes.") : buildError);
        return false;
    }
    for (int nodeId : ids) model.nextNodeId_ = std::max(model.nextNodeId_, nodeId + 1);

    const QJsonArray branchesJson = skeletonJson.value(QStringLiteral("branches")).toArray();
    for (const QJsonValue& value : branchesJson) {
        if (!value.isObject()) { setError(error, QStringLiteral("Every branch must be an object.")); return false; }
        const QJsonObject object = value.toObject();
        model.branches_.push_back(Branch{
            object.value(QStringLiteral("id")).toInt(-1),
            object.value(QStringLiteral("parentNodeId")).toInt(-1),
            object.value(QStringLiteral("childNodeId")).toInt(-1),
            static_cast<float>(object.value(QStringLiteral("age")).toDouble(0.0)),
            static_cast<float>(object.value(QStringLiteral("stiffness")).toDouble(1.0)),
            static_cast<float>(object.value(QStringLiteral("health")).toDouble(1.0)),
            static_cast<float>(object.value(QStringLiteral("growthProgress")).toDouble(1.0)),
            object.value(QStringLiteral("growing")).toBool(true),
            object.value(QStringLiteral("active")).toBool(true)
        });
    }
    if (model.branches_.empty()) model.rebuildBranchesFromSkeleton();

    const QJsonArray leavesJson = skeletonJson.value(QStringLiteral("leaves")).toArray();
    for (const QJsonValue& value : leavesJson) {
        if (!value.isObject()) { setError(error, QStringLiteral("Every leaf must be an object.")); return false; }
        const QJsonObject object = value.toObject();
        Leaf leaf;
        leaf.id = object.value(QStringLiteral("id")).toInt(-1);
        leaf.parentNodeId = object.value(QStringLiteral("parentNodeId")).toInt(-1);
        if (!readVec3(object.value(QStringLiteral("position")), &leaf.position) ||
            !readVec3(object.value(QStringLiteral("direction")), &leaf.direction) ||
            !readVec2(object.value(QStringLiteral("size")), &leaf.size)) {
            setError(error, QStringLiteral("Leaf %1 contains an invalid vector.").arg(leaf.id)); return false;
        }
        leaf.age = static_cast<float>(object.value(QStringLiteral("age")).toDouble(0.0));
        leaf.health = static_cast<float>(object.value(QStringLiteral("health")).toDouble(1.0));
        leaf.growthProgress = std::clamp(static_cast<float>(object.value(QStringLiteral("growthProgress")).toDouble(1.0)), 0.0f, 1.0f);
        leaf.growing = object.value(QStringLiteral("growing")).toBool(leaf.active);
        leaf.active = object.value(QStringLiteral("active")).toBool(true);
        model.leaves_.push_back(leaf);
        model.nextLeafId_ = std::max(model.nextLeafId_, leaf.id + 1);
    }

    const QJsonArray rootsJson = skeletonJson.value(QStringLiteral("roots")).toArray();
    for (const QJsonValue& value : rootsJson) {
        if (!value.isObject()) { setError(error, QStringLiteral("Every root must be an object.")); return false; }
        const QJsonObject object = value.toObject();
        Root root;
        root.id = object.value(QStringLiteral("id")).toInt(-1);
        root.parentNodeId = object.value(QStringLiteral("parentNodeId")).toInt(-1);
        if (!readVec3(object.value(QStringLiteral("position")), &root.position) ||
            !readVec3(object.value(QStringLiteral("direction")), &root.direction)) {
            setError(error, QStringLiteral("Root %1 contains an invalid vector.").arg(root.id)); return false;
        }
        root.radius = static_cast<float>(object.value(QStringLiteral("radius")).toDouble(0.0));
        root.length = static_cast<float>(object.value(QStringLiteral("length")).toDouble(0.0));
        root.age = static_cast<float>(object.value(QStringLiteral("age")).toDouble(0.0));
        root.depth = object.value(QStringLiteral("depth")).toInt(0);
        root.active = object.value(QStringLiteral("active")).toBool(true);
        model.roots_.push_back(root);
        model.nextRootId_ = std::max(model.nextRootId_, root.id + 1);
    }

    QString validationError;
    if (!model.validate(&validationError)) {
        setError(error, QStringLiteral("Invalid plant model: %1").arg(validationError)); return false;
    }
    *output = std::move(model);
    return true;
}

bool PlantModel::saveJson(const QString& filePath, QString* error) const {
    QString validationError;
    if (!validate(&validationError)) {
        setError(error, QStringLiteral("Cannot save invalid plant model: %1").arg(validationError)); return false;
    }
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setError(error, QStringLiteral("Cannot open %1 for writing: %2").arg(filePath, file.errorString()));
        return false;
    }
    if (file.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented)) < 0) {
        setError(error, QStringLiteral("Failed to write %1: %2").arg(filePath, file.errorString()));
        return false;
    }
    return true;
}

bool PlantModel::loadJson(const QString& filePath, PlantModel* output, QString* error) {
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        setError(error, QStringLiteral("Cannot open %1 for reading: %2").arg(filePath, file.errorString()));
        return false;
    }
    QJsonParseError parseError{};
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        setError(error, QStringLiteral("Invalid JSON in %1: %2").arg(filePath, parseError.errorString()));
        return false;
    }
    if (!fromJson(document.object(), output, error)) return false;
    output->captureBaselines();
    return true;
}

QString PlantModel::toTreeString() const {
    QStringList lines;
    lines << QStringLiteral("Plant #%1: %2 [%3]").arg(id).arg(name, species)
          << QStringLiteral("age=%1 years, lifeStage=%2, growthState=%3")
                 .arg(age, 0, 'f', 2).arg(toString(lifeStage), toString(growthState))
          << QStringLiteral("nodes=%1, branches=%2, leaves=%3, roots=%4")
                 .arg(static_cast<qulonglong>(nodeCount()))
                 .arg(static_cast<qulonglong>(branches_.size()))
                 .arg(static_cast<qulonglong>(leaves_.size()))
                 .arg(static_cast<qulonglong>(roots_.size()));
    if (!rootNode_) {
        lines << QStringLiteral("(empty skeleton)");
        return lines.join(QChar('\n'));
    }

    std::function<void(const PlantNode*, const QString&, bool)> appendNode =
        [&](const PlantNode* node, const QString& prefix, bool last) {
            const bool isRoot = node == rootNode_.get();
            const QString connector = isRoot ? QString() : (last ? QStringLiteral("`-- ") : QStringLiteral("|-- "));
            QString edgeLabel;
            if (!isRoot) {
                const auto edge = std::find_if(branches_.begin(), branches_.end(),
                    [node](const Branch& branch) { return branch.childNodeId == node->id; });
                if (edge != branches_.end()) edgeLabel = QStringLiteral("Branch #%1 -> ").arg(edge->id);
            }
            lines << prefix + connector + edgeLabel + QStringLiteral("Node #%1 <%2> parent=%3 depth=%4 age=%5 %6")
                .arg(node->id).arg(nodeTypeToString(node->type)).arg(node->parentId).arg(node->depth)
                .arg(node->age, 0, 'f', 2)
                .arg(node->active ? QStringLiteral("ACTIVE") : QStringLiteral("INACTIVE"));

            QString childPrefix = prefix;
            if (!isRoot) childPrefix += last ? QStringLiteral("    ") : QStringLiteral("|   ");

            QStringList attachments;
            for (const Leaf& leaf : leaves_) {
                if (leaf.parentNodeId == node->id) {
                    attachments << QStringLiteral("Leaf #%1 size=(%2, %3) health=%4")
                        .arg(leaf.id).arg(leaf.size.x(), 0, 'f', 2).arg(leaf.size.y(), 0, 'f', 2)
                        .arg(leaf.health, 0, 'f', 2);
                }
            }
            for (const Root& root : roots_) {
                if (root.parentNodeId == node->id) {
                    attachments << QStringLiteral("Root #%1 depth=%2 length=%3")
                        .arg(root.id).arg(root.depth).arg(root.length, 0, 'f', 2);
                }
            }

            const std::size_t total = node->children.size() + static_cast<std::size_t>(attachments.size());
            std::size_t index = 0;
            for (const auto& child : node->children) appendNode(child.get(), childPrefix, ++index == total);
            for (const QString& attachment : attachments) {
                const bool attachmentLast = ++index == total;
                lines << childPrefix + (attachmentLast ? QStringLiteral("`-- ") : QStringLiteral("|-- ")) + attachment;
            }
        };
    appendNode(rootNode_.get(), QString(), true);
    return lines.join(QChar('\n'));
}

PlantModel PlantModel::createDemoTree() {
    PlantModel model;
    model.id = 1001;
    model.name = QStringLiteral("Week 4 Demo Tree");
    model.species = QStringLiteral("Prunus serrulata");
    model.age = 2.5f;
    model.lifeStage = PlantLifeStage::Vegetative;
    model.growthState = PlantGrowthState::Active;

    PlantNode* base = model.createRootNode(Vec3::Zero(), Vec3::UnitY(), 0.18f, 2.5f);
    PlantNode* trunk1 = model.addNode(base->id, Vec3(0.0f, 0.8f, 0.0f), Vec3::UnitY(),
                                      0.15f, 0.8f, 2.0f, true, PlantNodeType::Stem);
    PlantNode* trunk2 = model.addNode(trunk1->id, Vec3(0.0f, 1.55f, 0.0f), Vec3::UnitY(),
                                      0.11f, 0.75f, 1.5f, true, PlantNodeType::Stem);
    PlantNode* left = model.addNode(trunk1->id, Vec3(-0.65f, 1.25f, 0.05f),
                                    Vec3(-0.78f, 0.62f, 0.06f), 0.07f, 0.76f, 1.1f,
                                    true, PlantNodeType::Branch, 1);
    PlantNode* right = model.addNode(trunk2->id, Vec3(0.62f, 2.0f, 0.18f),
                                     Vec3(0.78f, 0.57f, 0.23f), 0.065f, 0.79f, 0.8f,
                                     true, PlantNodeType::Branch, 1);
    PlantNode* crown = model.addNode(trunk2->id, Vec3(0.05f, 2.3f, -0.08f),
                                     Vec3(0.07f, 0.99f, -0.11f), 0.07f, 0.76f, 0.7f,
                                     true, PlantNodeType::Bud, 1);

    model.addLeaf(left->id, left->position, Vec3(-0.6f, 0.75f, 0.2f), Vec2(0.22f, 0.10f), 0.35f, 0.96f);
    model.addLeaf(right->id, right->position, Vec3(0.7f, 0.65f, 0.3f), Vec2(0.24f, 0.11f), 0.28f, 0.93f);
    model.addLeaf(crown->id, crown->position, Vec3(0.1f, 0.98f, -0.1f), Vec2(0.20f, 0.09f), 0.18f, 0.99f);
    model.addRoot(base->id, Vec3::Zero(), Vec3(-0.65f, -0.7f, 0.12f), 0.10f, 0.72f, 2.2f, 1);
    model.addRoot(base->id, Vec3::Zero(), Vec3(0.55f, -0.75f, -0.2f), 0.09f, 0.68f, 2.0f, 1);
    return model;
}

int PlantModel::allocateNodeId() { return nextNodeId_++; }
int PlantModel::allocateLeafId() { return nextLeafId_++; }
int PlantModel::allocateRootId() { return nextRootId_++; }






