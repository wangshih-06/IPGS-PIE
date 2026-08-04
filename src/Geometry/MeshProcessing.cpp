// ============================================================================
// MeshProcessing implementation
// ============================================================================
#include "Geometry/MeshProcessing.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace {

// 从三角形索引建立逐顶点邻接表（去重后的邻居列表）。
std::vector<std::vector<std::uint32_t>> buildAdjacency(const SurfaceMesh& mesh) {
    std::vector<std::vector<std::uint32_t>> adjacency(mesh.positions.size());
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t a = mesh.indices[i];
        const std::uint32_t b = mesh.indices[i + 1];
        const std::uint32_t c = mesh.indices[i + 2];
        auto link = [&adjacency](std::uint32_t from, std::uint32_t to) {
            auto& neighbors = adjacency[from];
            if (std::find(neighbors.begin(), neighbors.end(), to) == neighbors.end()) {
                neighbors.push_back(to);
            }
        };
        link(a, b); link(a, c);
        link(b, a); link(b, c);
        link(c, a); link(c, b);
    }
    return adjacency;
}

// 单步拉普拉斯：p += factor * (邻居平均 - p)。mask 非空时只更新标记顶点。
void smoothingPass(SurfaceMesh& mesh,
                   const std::vector<std::vector<std::uint32_t>>& adjacency,
                   float factor,
                   const std::vector<bool>* mask) {
    std::vector<Vec3> updated = mesh.positions;
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        if (mask && !(*mask)[i]) {
            continue;
        }
        const auto& neighbors = adjacency[i];
        if (neighbors.empty()) {
            continue;
        }
        Vec3 average = Vec3::Zero();
        for (const std::uint32_t neighbor : neighbors) {
            average += mesh.positions[neighbor];
        }
        average /= static_cast<float>(neighbors.size());
        updated[i] = mesh.positions[i] + factor * (average - mesh.positions[i]);
    }
    mesh.positions.swap(updated);
}

// 量化坐标打包为聚类键（各轴 21 位，足以覆盖植物尺度下的 LOD 格子）。
std::int64_t clusterKey(const Vec3& position, float cellSize) {
    const auto quantize = [cellSize](float v) {
        return static_cast<std::int64_t>(std::floor(v / cellSize)) & 0x1FFFFF;
    };
    return (quantize(position.x()) << 42) |
           (quantize(position.y()) << 21) |
           quantize(position.z());
}

} // namespace

void MeshProcessing::laplacianSmooth(SurfaceMesh& mesh,
                                     const LaplacianSmoothingSettings& settings,
                                     const std::vector<JunctionRegion>& junctions) {
    if (!mesh.isValid()) {
        return;
    }
    const std::vector<std::vector<std::uint32_t>> adjacency = buildAdjacency(mesh);

    // 全局平滑：正步收缩 + Taubin 负步回胀，保持枝杆体积。
    for (int iteration = 0; iteration < settings.iterations; ++iteration) {
        smoothingPass(mesh, adjacency, settings.lambda, nullptr);
        if (settings.taubinCompensation) {
            smoothingPass(mesh, adjacency, settings.mu, nullptr);
        }
    }

    // 连接处凹陷修复：junction 场源附近的顶点做额外强平滑。
    if (!junctions.empty() && settings.junctionExtraIterations > 0) {
        std::vector<bool> mask(mesh.positions.size(), false);
        std::size_t masked = 0;
        for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
            for (const JunctionRegion& region : junctions) {
                const float radius = region.radius * settings.junctionRadiusScale;
                if ((mesh.positions[i] - region.center).squaredNorm() <= radius * radius) {
                    mask[i] = true;
                    ++masked;
                    break;
                }
            }
        }
        if (masked > 0) {
            for (int iteration = 0; iteration < settings.junctionExtraIterations; ++iteration) {
                smoothingPass(mesh, adjacency, settings.junctionLambda, &mask);
                if (settings.taubinCompensation) {
                    smoothingPass(mesh, adjacency, -settings.junctionLambda * 0.9f, &mask);
                }
            }
        }
    }

    // 平滑只移动顶点，连接关系不变；法向量按新几何重算，统计信息同步更新。
    recomputeNormals(mesh);
    computeStats(mesh);
}

SurfaceMesh MeshProcessing::simplifyVertexClustering(const SurfaceMesh& mesh, float cellSize) {
    SurfaceMesh simplified;
    if (!mesh.isValid() || cellSize <= 0.0f) {
        return simplified;
    }

    // 1) 顶点聚类：同一格子内的顶点合并为一个代表点（取平均位置）。
    struct Cluster {
        Vec3 sum = Vec3::Zero();
        std::uint32_t count = 0;
        std::uint32_t representative = 0;
    };
    std::unordered_map<std::int64_t, Cluster> clusters;
    clusters.reserve(mesh.positions.size() / 2);
    std::vector<std::int64_t> vertexCluster(mesh.positions.size());
    for (std::size_t i = 0; i < mesh.positions.size(); ++i) {
        const std::int64_t key = clusterKey(mesh.positions[i], cellSize);
        vertexCluster[i] = key;
        Cluster& cluster = clusters[key];
        cluster.sum += mesh.positions[i];
        ++cluster.count;
    }
    for (auto& entry : clusters) {
        Cluster& cluster = entry.second;
        cluster.representative = static_cast<std::uint32_t>(simplified.positions.size());
        simplified.positions.push_back(cluster.sum / static_cast<float>(cluster.count));
    }

    // 2) 三角形重连：丢弃坍缩成点/线的退化面与完全重复的三角形。
    std::unordered_set<std::uint64_t> seenTriangles;
    simplified.indices.reserve(mesh.indices.size());
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t tri[3] = {
            clusters[vertexCluster[mesh.indices[i]]].representative,
            clusters[vertexCluster[mesh.indices[i + 1]]].representative,
            clusters[vertexCluster[mesh.indices[i + 2]]].representative
        };
        if (tri[0] == tri[1] || tri[1] == tri[2] || tri[2] == tri[0]) {
            continue;
        }
        std::uint32_t sorted[3] = {tri[0], tri[1], tri[2]};
        std::sort(sorted, sorted + 3);
        const std::uint64_t triangleKey =
            (static_cast<std::uint64_t>(sorted[0]) << 42) |
            (static_cast<std::uint64_t>(sorted[1]) << 21) |
            static_cast<std::uint64_t>(sorted[2]);
        if (!seenTriangles.insert(triangleKey).second) {
            continue;
        }
        simplified.indices.push_back(tri[0]);
        simplified.indices.push_back(tri[1]);
        simplified.indices.push_back(tri[2]);
    }

    simplified.normals.resize(simplified.positions.size(), Vec3::UnitY());
    recomputeNormals(simplified);
    computeStats(simplified);
    return simplified;
}

void MeshProcessing::recomputeNormals(SurfaceMesh& mesh) {
    mesh.normals.assign(mesh.positions.size(), Vec3::Zero());
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const std::uint32_t a = mesh.indices[i];
        const std::uint32_t b = mesh.indices[i + 1];
        const std::uint32_t c = mesh.indices[i + 2];
        // 叉积模长即 2 倍面积，天然完成面积加权。
        const Vec3 faceNormal = (mesh.positions[b] - mesh.positions[a])
                                    .cross(mesh.positions[c] - mesh.positions[a]);
        mesh.normals[a] += faceNormal;
        mesh.normals[b] += faceNormal;
        mesh.normals[c] += faceNormal;
    }
    for (Vec3& normal : mesh.normals) {
        if (normal.squaredNorm() < 1e-20f) {
            normal = Vec3::UnitY();
        } else {
            normal.normalize();
        }
    }
}

void MeshProcessing::computeStats(SurfaceMesh& mesh, bool allowOrientationFlip) {
    SurfaceMeshStats& stats = mesh.stats;
    stats.vertexCount = mesh.positions.size();
    stats.triangleCount = mesh.indices.size() / 3;

    stats.bounds.reset();
    for (const Vec3& position : mesh.positions) {
        stats.bounds.expand(position);
    }

    // 有向体积（散度定理）：封闭朝外网格为正；为负则翻转全部三角形绕序。
    double volume = 0.0;
    double area = 0.0;
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        const Vec3& a = mesh.positions[mesh.indices[i]];
        const Vec3& b = mesh.positions[mesh.indices[i + 1]];
        const Vec3& c = mesh.positions[mesh.indices[i + 2]];
        volume += static_cast<double>(a.dot(b.cross(c))) / 6.0;
        area += static_cast<double>((b - a).cross(c - a).norm()) * 0.5;
    }
    stats.surfaceArea = area;
    if (allowOrientationFlip && volume < 0.0 && !mesh.indices.empty()) {
        for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
            std::swap(mesh.indices[i + 1], mesh.indices[i + 2]);
        }
        volume = -volume;
        stats.orientationFlipped = true;
    }
    stats.signedVolume = volume;

    // 连接性检查：封闭曲面中每条无向边应恰好被 2 个三角形共享。
    std::unordered_map<std::uint64_t, std::uint32_t> edgeUse;
    edgeUse.reserve(mesh.indices.size());
    for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3) {
        for (int e = 0; e < 3; ++e) {
            const std::uint32_t v0 = mesh.indices[i + e];
            const std::uint32_t v1 = mesh.indices[i + (e + 1) % 3];
            const std::uint64_t key =
                (static_cast<std::uint64_t>(std::min(v0, v1)) << 32) |
                static_cast<std::uint64_t>(std::max(v0, v1));
            ++edgeUse[key];
        }
    }
    stats.manifoldEdgeCount = 0;
    stats.boundaryEdgeCount = 0;
    for (const auto& entry : edgeUse) {
        if (entry.second == 2) {
            ++stats.manifoldEdgeCount;
        } else {
            ++stats.boundaryEdgeCount;
        }
    }
    stats.watertight = stats.triangleCount > 0 && stats.boundaryEdgeCount == 0;
}
