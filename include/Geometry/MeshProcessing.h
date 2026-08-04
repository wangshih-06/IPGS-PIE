// ============================================================================
// MeshProcessing - 第8周：Marching Cubes 网格的后处理
// ----------------------------------------------------------------------------
// - laplacianSmooth：邻域平均平滑（Taubin 负步补偿收缩），并对枝干连接处
//   （Metaball junction 场源附近）做额外迭代，消除连接凹陷；
// - simplifyVertexClustering：顶点聚类简化，用于生成 LOD1/LOD2；
// - recomputeNormals：面积加权面法向量累积；
// - computeStats：包围盒 / 水密性 / 有向体积 / 表面积统计（供提取与简化共用）。
// ============================================================================
#pragma once

#include <vector>

#include "Common/MathTypes.h"
#include "Geometry/MarchingCubes.h"

struct LaplacianSmoothingSettings {
    int iterations = 8;              // 全局平滑迭代次数（一次 = 正步 + Taubin 负步）
    float lambda = 0.5f;             // 正步收缩系数 (0, 1)
    bool taubinCompensation = true;  // 是否追加负步抑制体积收缩
    float mu = -0.53f;               // Taubin 负步系数（绝对值略大于 lambda）
    int junctionExtraIterations = 6; // 连接处凹陷区域的额外局部平滑次数
    float junctionRadiusScale = 1.3f;// 凹陷区域半径 = 场源影响半径 * 该系数
    float junctionLambda = 0.65f;    // 凹陷区域使用的平滑系数
};

// 枝干连接处（分叉点）的空间区域，来自 MetaballField 的 junction 节点场源。
struct JunctionRegion {
    Vec3 center = Vec3::Zero();
    float radius = 0.1f;
};

namespace MeshProcessing {

// 原地拉普拉斯平滑。junctions 内的顶点额外做 junctionExtraIterations 次
// 更强平滑，用于修复 Metaball 分叉连接处的凹陷。
void laplacianSmooth(SurfaceMesh& mesh,
                     const LaplacianSmoothingSettings& settings,
                     const std::vector<JunctionRegion>& junctions = {});

// 顶点聚类简化：把顶点按 cellSize 空间格子聚类为代表点，重连三角形并
// 丢弃退化面。返回简化后的新网格（法向量与统计信息已更新）。
SurfaceMesh simplifyVertexClustering(const SurfaceMesh& mesh, float cellSize);

// 用面积加权的面法向量累加重算逐顶点法向量（简化后网格不再对应原标量场）。
void recomputeNormals(SurfaceMesh& mesh);

// 重新计算包围盒、顶点/三角形计数、水密性、有向体积与表面积。
// allowOrientationFlip 为 true 且有向体积为负时自动翻转三角形绕序
// （仅适用于封闭网格；叶片等开放网格应传 false，保持生成时的绕序）。
void computeStats(SurfaceMesh& mesh, bool allowOrientationFlip = true);

} // namespace MeshProcessing
