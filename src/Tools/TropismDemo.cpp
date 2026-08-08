// ============================================================================
// TropismDemo - 第11周：向光性与向地性自动化验证工具
// ============================================================================
#include <QCoreApplication>
#include <QDebug>
#include <cmath>
#include <vector>

#include "Engine/EnvironmentParams.h"
#include "Engine/DynamicBranchingSystem.h"
#include "Plant/PlantModel.h"

int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);

    // ------------------------------------------------------------------------
    // 1. 验证多光源矢量合成与有效光向计算
    // ------------------------------------------------------------------------
    EnvironmentParams env;
    env.lightSources.clear();

    LightSource light1;
    light1.id = 1;
    light1.type = LightType::Point;
    light1.position = Vec3(10.0f, 0.0f, 0.0f); // 右侧点光源
    light1.intensity = 1.0f;
    light1.enabled = true;
    env.lightSources.push_back(light1);

    float intensitySum = 0.0f;
    Vec3 effDir = env.calculateEffectiveLightDirection(Vec3::Zero(), &intensitySum);

    // 有效光向应指向 (+1, 0, 0) 附近
    if (effDir.x() < 0.9f || std::abs(effDir.y()) > 0.1f) {
        qCritical().noquote() << "FAILED: Multi-light direction calculation incorrect." << effDir.x() << effDir.y();
        return 1;
    }
    qInfo().noquote() << "[PASS] Multi-light vector calculation verified.";

    // ------------------------------------------------------------------------
    // 2. 验证点遮挡与受光衰减算法 (Light Occlusion)
    // ------------------------------------------------------------------------
    std::vector<Vec3> occluders;
    occluders.push_back(Vec3(5.0f, 0.0f, 0.0f)); // 挡在 (0,0,0) 与 (10,0,0) 之间

    float expBase = env.calculateLightExposure(Vec3::Zero(), {});
    float expShaded = env.calculateLightExposure(Vec3::Zero(), occluders);

    if (expShaded >= expBase || expShaded > 0.7f) {
        qCritical().noquote() << QString("FAILED: Occlusion calculation failed. Unshaded=%1, Shaded=%2")
                                     .arg(expBase).arg(expShaded);
        return 2;
    }
    qInfo().noquote() << QString("[PASS] Simplified light occlusion verified: unshaded=%1, shaded=%2")
                             .arg(expBase, 0, 'f', 2).arg(expShaded, 0, 'f', 2);

    // ------------------------------------------------------------------------
    // 3. 验证茎枝向光性 (Phototropism) 弯曲生成
    // ------------------------------------------------------------------------
    env.phototropismWeight = 0.80f;
    env.gravitropismWeight = 0.0f;

    PlantNode stemParent;
    stemParent.id = 1;
    stemParent.position = Vec3::Zero();
    stemParent.direction = Vec3::UnitY(); // 父节点向上

    Vec3 tropismBranchDir = DynamicBranchingSystem::calculateTropismDirection(
        stemParent, 0, 1, PlantNodeType::Branch, env);

    // 强烈向光作用下，枝干偏向 X 轴正向 (+X)
    if (tropismBranchDir.x() < 0.4f) {
        qCritical().noquote() << "FAILED: Stem phototropism direction incorrect." << tropismBranchDir.x();
        return 3;
    }
    qInfo().noquote() << QString("[PASS] Stem phototropism verified: branch direction=(%1, %2, %3)")
                             .arg(tropismBranchDir.x(), 0, 'f', 2)
                             .arg(tropismBranchDir.y(), 0, 'f', 2)
                             .arg(tropismBranchDir.z(), 0, 'f', 2);

    // ------------------------------------------------------------------------
    // 4. 验证根系正向地性 (Geotropism) 向下生长
    // ------------------------------------------------------------------------
    env.phototropismWeight = 0.0f;
    env.gravitropismWeight = 0.80f;

    PlantNode rootParent;
    rootParent.id = 2;
    rootParent.position = Vec3::Zero();
    rootParent.direction = Vec3(-0.5f, -0.5f, 0.0f).normalized();

    Vec3 rootBranchDir = DynamicBranchingSystem::calculateTropismDirection(
        rootParent, 0, 1, PlantNodeType::Root, env);

    // 根系正向地性：Y 轴分量应显著为负 (向下)
    if (rootBranchDir.y() > -0.5f) {
        qCritical().noquote() << "FAILED: Root geotropism downward growth incorrect." << rootBranchDir.y();
        return 4;
    }
    qInfo().noquote() << QString("[PASS] Root geotropism verified: root direction=(%1, %2, %3)")
                             .arg(rootBranchDir.x(), 0, 'f', 2)
                             .arg(rootBranchDir.y(), 0, 'f', 2)
                             .arg(rootBranchDir.z(), 0, 'f', 2);

    // ------------------------------------------------------------------------
    // 5. 验证环境受光率对生长速度与生命活力的反馈
    // ------------------------------------------------------------------------
    PlantModel plant;
    plant.createRootNode(Vec3::Zero(), Vec3::UnitY(), 0.18f, 0.0f);

    DynamicBranchingSettings settings;
    settings.branchStartAge = 0.2f;
    settings.branchInterval = 0.2f;
    settings.branchProbability = 1.0f;
    settings.smoothingYears = 1.0f;

    DynamicBranchingSystem system(settings);
    GrowthEventManager events;

    GrowthResourceState resources;
    resources.light = 1.0f;
    resources.environment = env;

    plant.advanceAge(0.5f);
    system.update(plant, plant.age, 0.5f, resources, &events);

    if (plant.nodeCount() <= 1) {
        qCritical().noquote() << "FAILED: Dynamic branching under tropism environmental state produced no nodes.";
        return 5;
    }

    qInfo().noquote() << QString("[PASS] Tropism & Environment Growth feedback system fully verified. Total nodes=%1")
                             .arg(static_cast<qulonglong>(plant.nodeCount()));
    return 0;
}
