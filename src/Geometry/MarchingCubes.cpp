// ============================================================================
// MarchingCubes implementation
// ============================================================================
#include "Geometry/MarchingCubes.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <unordered_map>

#include "Geometry/MarchingCubesTables.h"
#include "Geometry/MeshProcessing.h"

namespace {

// 立方体 8 个角点相对体素最小角的整数偏移，与查找表头注释中的编号一致。
constexpr int kCornerOffset[8][3] = {
    {0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1},
    {0, 1, 0}, {1, 1, 0}, {1, 1, 1}, {0, 1, 1}
};

// 12 条边的两个端点角点编号。
constexpr int kEdgeCorners[12][2] = {
    {0, 1}, {1, 2}, {2, 3}, {3, 0},
    {4, 5}, {5, 6}, {6, 7}, {7, 4},
    {0, 4}, {1, 5}, {2, 6}, {3, 7}
};

// 每条立方体边对应的“规范网格边”：
// 同一条网格边最多被 4 个体素共享，以边沿轴向的低端点为基准点，
// 用 (基准点线性索引 * 3 + 轴向) 作为全局唯一键，实现跨体素顶点去重。
constexpr int kEdgeAxis[12] = {0, 2, 0, 2, 0, 2, 0, 2, 1, 1, 1, 1};
constexpr int kEdgeBase[12][3] = {
    {0, 0, 0}, {1, 0, 0}, {0, 0, 1}, {0, 0, 0},
    {0, 1, 0}, {1, 1, 0}, {0, 1, 1}, {0, 1, 0},
    {0, 0, 0}, {1, 0, 0}, {1, 0, 1}, {0, 0, 1}
};

// 在规则采样网格上用中心差分求每个格点的场梯度（边界退化为前/后向差分）。
std::vector<Vec3> computeNodeGradients(const ScalarFieldGrid& grid) {
    const int nx = grid.dimensions.x();
    const int ny = grid.dimensions.y();
    const int nz = grid.dimensions.z();
    std::vector<Vec3> gradients(grid.sampleCount(), Vec3::Zero());

    for (int z = 0; z < nz; ++z) {
        for (int y = 0; y < ny; ++y) {
            for (int x = 0; x < nx; ++x) {
                const int xm = std::max(0, x - 1);
                const int xp = std::min(nx - 1, x + 1);
                const int ym = std::max(0, y - 1);
                const int yp = std::min(ny - 1, y + 1);
                const int zm = std::max(0, z - 1);
                const int zp = std::min(nz - 1, z + 1);
                const float gx = (grid.value(xp, y, z) - grid.value(xm, y, z)) /
                                 (grid.spacing.x() * static_cast<float>(xp - xm));
                const float gy = (grid.value(x, yp, z) - grid.value(x, ym, z)) /
                                 (grid.spacing.y() * static_cast<float>(yp - ym));
                const float gz = (grid.value(x, y, zm) - grid.value(x, y, zp)) /
                                 (grid.spacing.z() * static_cast<float>(zp - zm));
                gradients[grid.linearIndex(x, y, z)] = Vec3(gx, gy, gz);
            }
        }
    }
    return gradients;
}

// 等值面与边的交点参数：t = (iso - v0) / (v1 - v0)。
float edgeIntersection(float value0, float value1, float isoLevel) {
    const float denominator = value1 - value0;
    if (std::abs(denominator) < 1e-12f) {
        return 0.5f;
    }
    const float t = (isoLevel - value0) / denominator;
    if (!std::isfinite(t)) {
        return 0.5f;
    }
    return std::max(0.0f, std::min(1.0f, t));
}

void finalizeStats(SurfaceMesh& mesh) {
    // 体素遍历统计在 extract() 内累计，几何统计由第8周公共模块完成。
    MeshProcessing::computeStats(mesh);
}

} // namespace

bool SurfaceMesh::isValid() const {
    return !positions.empty() && positions.size() == normals.size() &&
           indices.size() % 3 == 0 && !indices.empty();
}

SurfaceMesh MarchingCubes::extract(const ScalarFieldGrid& grid, float isoLevel) {
    SurfaceMesh mesh;
    if (!grid.isValid()) {
        return mesh;
    }
    const int nx = grid.dimensions.x();
    const int ny = grid.dimensions.y();
    const int nz = grid.dimensions.z();
    if (nx < 2 || ny < 2 || nz < 2) {
        return mesh;
    }

    const std::vector<Vec3> nodeGradients = computeNodeGradients(grid);
    std::unordered_map<std::uint64_t, std::uint32_t> edgeVertexCache;
    edgeVertexCache.reserve(grid.sampleCount() / 4);

    SurfaceMeshStats& stats = mesh.stats;
    stats.voxelCount = static_cast<std::size_t>(nx - 1) *
                       static_cast<std::size_t>(ny - 1) *
                       static_cast<std::size_t>(nz - 1);

    for (int z = 0; z < nz - 1; ++z) {
        for (int y = 0; y < ny - 1; ++y) {
            for (int x = 0; x < nx - 1; ++x) {
                // 1) 立方体顶点状态判断：角点场值 >= 阈值则对应位置 1。
                float cornerValues[8];
                int caseIndex = 0;
                for (int c = 0; c < 8; ++c) {
                    const float value = grid.value(x + kCornerOffset[c][0],
                                                   y + kCornerOffset[c][1],
                                                   z + kCornerOffset[c][2]);
                    cornerValues[c] = value;
                    if (value >= isoLevel) {
                        caseIndex |= (1 << c);
                    }
                }
                if (caseIndex == 0) {
                    continue;
                }
                if (caseIndex == 0xFF) {
                    ++stats.insideVoxelCount;
                    continue;
                }
                ++stats.activeVoxelCount;

                // 2) 边表给出本立方体与等值面相交的边。
                const std::uint16_t edgeMask = MarchingCubesTables::kEdgeTable[caseIndex];
                std::uint32_t edgeVertices[12] = {0};

                for (int e = 0; e < 12; ++e) {
                    if ((edgeMask & (1u << e)) == 0) {
                        continue;
                    }
                    ++stats.cubeLocalVertexCount;

                    // 3) 规范边键去重：共享边的体素复用同一个顶点。
                    const std::size_t baseIndex = grid.linearIndex(
                        x + kEdgeBase[e][0], y + kEdgeBase[e][1], z + kEdgeBase[e][2]);
                    const std::uint64_t edgeKey =
                        static_cast<std::uint64_t>(baseIndex) * 3u +
                        static_cast<std::uint64_t>(kEdgeAxis[e]);
                    const auto cached = edgeVertexCache.find(edgeKey);
                    if (cached != edgeVertexCache.end()) {
                        edgeVertices[e] = cached->second;
                        continue;
                    }

                    // 4) 线性插值求等值面交点，并同步插值格点梯度得到法向量。
                    const int c0 = kEdgeCorners[e][0];
                    const int c1 = kEdgeCorners[e][1];
                    const float t = edgeIntersection(cornerValues[c0], cornerValues[c1],
                                                     isoLevel);
                    const Vec3 p0 = grid.position(x + kCornerOffset[c0][0],
                                                  y + kCornerOffset[c0][1],
                                                  z + kCornerOffset[c0][2]);
                    const Vec3 p1 = grid.position(x + kCornerOffset[c1][0],
                                                  y + kCornerOffset[c1][1],
                                                  z + kCornerOffset[c1][2]);
                    const Vec3 g0 = nodeGradients[grid.linearIndex(
                        x + kCornerOffset[c0][0], y + kCornerOffset[c0][1],
                        z + kCornerOffset[c0][2])];
                    const Vec3 g1 = nodeGradients[grid.linearIndex(
                        x + kCornerOffset[c1][0], y + kCornerOffset[c1][1],
                        z + kCornerOffset[c1][2])];

                    // 场值朝内部增大，外法向量与梯度方向相反。
                    Vec3 normal = -(g0 + t * (g1 - g0));
                    if (normal.squaredNorm() < 1e-20f) {
                        // 梯度退化（平坦区域）时退化为垂直于边的任意方向，
                        // 后续仍会被单位化，避免产生 NaN。
                        const Vec3 edgeDir = (p1 - p0).normalized();
                        const Vec3 reference = std::abs(edgeDir.x()) < 0.9f
                                                   ? Vec3::UnitX() : Vec3::UnitY();
                        normal = edgeDir.cross(reference);
                    }
                    normal.normalize();

                    const std::uint32_t vertexIndex =
                        static_cast<std::uint32_t>(mesh.positions.size());
                    mesh.positions.push_back(p0 + t * (p1 - p0));
                    mesh.normals.push_back(normal);
                    edgeVertexCache.emplace(edgeKey, vertexIndex);
                    edgeVertices[e] = vertexIndex;
                }

                // 5) 三角形表把相交边连接成 1~5 个三角形。
                const std::int8_t* row = MarchingCubesTables::kTriangleTable[caseIndex];
                for (int i = 0; row[i] != -1; i += 3) {
                    mesh.indices.push_back(edgeVertices[row[i]]);
                    mesh.indices.push_back(edgeVertices[row[i + 1]]);
                    mesh.indices.push_back(edgeVertices[row[i + 2]]);
                }
            }
        }
    }

    finalizeStats(mesh);
    return mesh;
}

bool MarchingCubes::validate(const SurfaceMesh& mesh, QString* error) {
    auto fail = [error](const QString& message) {
        if (error) {
            *error = message;
        }
        return false;
    };

    if (!mesh.isValid()) {
        return fail(QStringLiteral("Mesh is empty or position/normal/index arrays are inconsistent."));
    }
    for (std::size_t i = 0; i < mesh.indices.size(); ++i) {
        if (mesh.indices[i] >= mesh.positions.size()) {
            return fail(QStringLiteral("Triangle index %1 out of vertex range.").arg(i));
        }
    }
    for (std::size_t i = 0; i < mesh.normals.size(); ++i) {
        const float length = mesh.normals[i].norm();
        if (!std::isfinite(length) || std::abs(length - 1.0f) > 1e-3f) {
            return fail(QStringLiteral("Vertex normal %1 is not unit length.").arg(i));
        }
    }
    if (!mesh.stats.watertight) {
        return fail(QStringLiteral("Mesh is not watertight: %1 boundary edges.")
                        .arg(mesh.stats.boundaryEdgeCount));
    }
    if (mesh.stats.signedVolume <= 0.0) {
        return fail(QStringLiteral("Signed volume is not positive."));
    }
    return true;
}
