// ============================================================================
// LeafGenerator - 第8周：参数化叶片生成与枝干节点绑定
// ----------------------------------------------------------------------------
// 遍历 PlantNode 骨架，在末端节点和高层级枝干节点处生成弯曲的叶片条带
// 网格；叶片朝向由节点生长方向 + 黄金角方位分布 + 确定性随机抖动决定，
// 同一 seed 下结果完全可复现。
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "Common/MathTypes.h"
#include "Geometry/MarchingCubes.h"

class PlantModel;
class PlantNode;

struct LeafGenerationSettings {
    int minGeneration = 2;          // 绑定叶片的最低枝干层级（主干不长叶）
    int leavesPerTerminalNode = 3;  // 每个末端节点的叶片数
    int leavesPerInnerNode = 1;     // 每个高层级中间节点的叶片数
    float leafLength = 0.30f;       // 叶片中脉长度（米）
    float leafWidth = 0.12f;        // 叶片最大宽度（米）
    int lengthSegments = 5;         // 沿中脉的细分数（越高越平滑）
    float droop = 0.35f;            // 叶尖下垂弯曲量（相对叶长）
    float fold = 0.45f;             // 横截面沿中脉的下折量（相对半宽）
    float tiltDegrees = 55.0f;      // 叶片与枝干方向的夹角
    float sizeJitter = 0.30f;       // 尺寸随机抖动幅度
    std::uint32_t seed = 20260804u; // 确定性随机种子
};

struct GeneratedLeaves {
    SurfaceMesh mesh;                       // 全部叶片合并后的三角网格
    std::vector<std::uint16_t> faceMaterials; // 每个三角形的调色板材质索引
    int leafCount = 0;
    int boundNodeCount = 0;
};

class LeafGenerator {
public:
    // 从植物骨架生成叶片网格。model 仅用于读取骨架，不会被修改。
    static GeneratedLeaves generate(const PlantModel& model,
                                    const LeafGenerationSettings& settings = {});
};
