// ============================================================================
// MarchingCubes - 从 ScalarFieldGrid 提取等值面三角网格
// ----------------------------------------------------------------------------
// 输入：第6周 MetaballField::sampleGrid() 生成的三维标量场。
// 输出：顶点去重后的三角形网格（位置 + 法向量 + 索引），以及体素遍历、
//       水密性、表面积、有向体积等统计信息，供调试验证和前端渲染使用。
// ============================================================================
#pragma once

#include <cstdint>
#include <vector>

#include "Common/MathTypes.h"
#include "Implicit/MetaballField.h"

class QString;

struct SurfaceMeshStats {
    std::size_t voxelCount = 0;          // 遍历的体素总数
    std::size_t activeVoxelCount = 0;    // 与等值面相交的体素数
    std::size_t insideVoxelCount = 0;    // 完全位于等值面内部的体素数
    std::size_t cubeLocalVertexCount = 0;// 去重前（逐体素）的顶点总数
    std::size_t vertexCount = 0;         // 全局去重后的顶点数
    std::size_t triangleCount = 0;
    std::size_t manifoldEdgeCount = 0;   // 被恰好两个三角形共享的边数
    std::size_t boundaryEdgeCount = 0;   // 共享次数 != 2 的边数（理想封闭网格为 0）
    bool watertight = false;             // boundaryEdgeCount == 0 且存在三角形
    bool orientationFlipped = false;     // 是否因体积为负自动翻转了三角形绕序
    double signedVolume = 0.0;           // 散度定理有向体积（朝外封闭网格为正）
    double surfaceArea = 0.0;
    BoundingBox3 bounds;                 // 网格顶点包围盒
};

struct SurfaceMesh {
    std::vector<Vec3> positions;         // 去重后的顶点位置
    std::vector<Vec3> normals;           // 与 positions 一一对应的单位外法向量
    std::vector<std::uint32_t> indices;  // 每 3 个索引构成一个三角形
    SurfaceMeshStats stats;

    bool isValid() const;
};

class MarchingCubes {
public:
    // 从标量场网格提取 isoLevel 等值面。
    // case index 第 i 位在角点场值 >= isoLevel 时置 1（与第6周 evaluateSigned 一致）。
    static SurfaceMesh extract(const ScalarFieldGrid& grid, float isoLevel);

    // 校验网格：索引范围、法向量单位化、水密性、有向体积符号。
    // 返回 false 时通过 error 输出第一项失败原因。
    static bool validate(const SurfaceMesh& mesh, QString* error = nullptr);
};
