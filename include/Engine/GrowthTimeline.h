// ============================================================================
// GrowthTimeline - 第9周 时间驱动的植物生长时间轴
// 纯数学 / 状态类；不依赖 Qt signals/slots，方便单元测试与离线采样
// ============================================================================
#pragma once

#include <QJsonObject>
#include <QString>

#include "Common/MathTypes.h"
#include "Plant/PlantTypes.h"

// ----------------------------------------------------------------------------
// 时间轴状态机：realtime 由 QTimer 驱动；step 由显式 stepGrowth() 触发；
// paused 冻结 currentAge_；completed 在 age >= matureEndYear 时进入。
// ----------------------------------------------------------------------------
enum class GrowthPlaybackMode {
    Paused,
    Realtime,
    Step,
    Completed
};

QString toString(GrowthPlaybackMode mode);
GrowthPlaybackMode growthPlaybackModeFromString(const QString& value);

// ----------------------------------------------------------------------------
// 生命周期阈值（年）
//   Seedling    [0,        seedlingEndYear)
//   Vegetative  [seedlingEndYear, vegetativeEndYear)
//   Mature      [vegetativeEndYear, matureEndYear)
//   完成态     age >= matureEndYear → GrowthPlaybackMode::Completed
// ----------------------------------------------------------------------------
struct GrowthAxisThresholds {
    float seedlingEndYear = 0.5f;
    float vegetativeEndYear = 3.0f;
    float matureEndYear = 30.0f;
};

// ----------------------------------------------------------------------------
// Logistic 生长曲线参数（无量纲，全部相对 PlantRule / Leaf.size 基线）
// lengthScale / radiusScale / leafScale 含义：乘到当前节点/叶片的
// length / radius / size 之上。asymptoteRatio 即上限。
// ----------------------------------------------------------------------------
struct GrowthShapeParams {
    // 枝干长度
    float lengthLogisticRate = 1.6f;
    float lengthMidpoint = 1.5f;
    float lengthAsymptoteRatio = 1.0f;

    // 枝干半径
    float radiusLogisticRate = 0.9f;
    float radiusMidpoint = 0.7f;
    float radiusAsymptoteRatio = 1.45f;
    float secondaryGrowthRate = 0.03f;  // 成熟期后每年缓慢增粗

    // 叶片大小（Vec2 整体放缩，保持长宽比）
    float leafLogisticRate = 1.8f;
    float leafMidpoint = 1.0f;
    float leafAsymptoteRatio = 1.15f;

    // 通用下限，避免 sample() 在 age=0 时返回绝对 0（保持可绘制）
    float minimumScale = 0.0f;
};

// ----------------------------------------------------------------------------
// 单点时间轴采样（纯数据）
// ----------------------------------------------------------------------------
struct GrowthSample {
    float age = 0.0f;
    PlantLifeStage lifeStage = PlantLifeStage::Seedling;
    float lengthScale = 0.0f;
    float radiusScale = 0.0f;
    Vec2 leafScale = Vec2::Zero();
};

// ----------------------------------------------------------------------------
// 完整时间轴对象：负责 age 推进 + 阶段映射 + 比例曲线计算 + JSON 持久化
// ----------------------------------------------------------------------------
class GrowthTimeline {
public:
    GrowthTimeline();
    GrowthTimeline(const GrowthAxisThresholds& thresholds,
                   const GrowthShapeParams& shape,
                   float initialYears = 0.0f);

    // 纯函数：给一个年龄，得到对应样本。不修改任何成员。
    GrowthSample sample(float ageYears) const;

    // 把 currentAge_ 推进 deltaYears，返回新样本（同时内部 mode 自检）。
    GrowthSample advance(float deltaYears);

    // 工具查询
    PlantLifeStage lifeStageAt(float ageYears) const;
    PlantGrowthState growthStateAt(float ageYears) const;
    bool isCompleted() const { return currentAge_ >= thresholds_.matureEndYear; }

    // 状态变更
    void reset(float initialYears = 0.0f);
    void setSpeed(float speed);                       // clamp [0.1, 8.0]
    float speed() const { return speed_; }
    void setMode(GrowthPlaybackMode m);
    GrowthPlaybackMode mode() const { return mode_; }
    void setSecondsPerYear(float seconds);            // 墙钟换算
    float secondsPerYear() const { return secondsPerYear_; }
    void setThresholds(const GrowthAxisThresholds& t);
    GrowthAxisThresholds thresholds() const { return thresholds_; }
    void setShape(const GrowthShapeParams& s);
    GrowthShapeParams shape() const { return shape_; }
    float currentAge() const { return currentAge_; }

    // JSON 持久化（schema: plantsim.growth_timeline, version: 1）
    QJsonObject toJson() const;
    static bool fromJson(const QJsonObject& json, GrowthTimeline* output, QString* error = nullptr);
    bool saveJson(const QString& filePath, QString* error = nullptr) const;
    static bool loadJson(const QString& filePath, GrowthTimeline* output, QString* error = nullptr);

    // 字符串描述（用于 EngineWindow 状态显示）
    QString describeMode() const;
    QString describeLifeStage(PlantLifeStage stage) const;

    static constexpr float kMinSpeed = 0.1f;
    static constexpr float kMaxSpeed = 8.0f;

private:
    void recomputeModeByAge();

    GrowthAxisThresholds thresholds_;
    GrowthShapeParams shape_;
    float currentAge_ = 0.0f;
    float speed_ = 1.0f;
    float secondsPerYear_ = 2.0f;
    GrowthPlaybackMode mode_ = GrowthPlaybackMode::Paused;
};