// ============================================================================
// LeafGenerator implementation
// ============================================================================
#include "Geometry/LeafGenerator.h"

#include <cmath>
#include <random>

#include "Algorithm/PlantNode.h"
#include "Geometry/MeshProcessing.h"
#include "Plant/PlantModel.h"

namespace {

constexpr float kPi = 3.14159265358979323846f;
// 黄金角：让同一节点上的多片叶子在方位角上均匀错开。
constexpr float kGoldenAngle = 2.39996322972865332f;

struct LeafBinding {
    const PlantNode* node = nullptr;
    int leafSlots = 0;
};

// 收集叶片绑定节点：末端节点多叶、高层级中间节点少叶，主干和根不绑定。
void collectBindings(const PlantNode* node,
                     const LeafGenerationSettings& settings,
                     std::vector<LeafBinding>* output) {
    if (!node) {
        return;
    }
    if (node->type != PlantNodeType::Root && node->generation >= settings.minGeneration) {
        if (node->children.empty()) {
            output->push_back(LeafBinding{node, settings.leavesPerTerminalNode});
        } else if (settings.leavesPerInnerNode > 0) {
            output->push_back(LeafBinding{node, settings.leavesPerInnerNode});
        }
    }
    for (const auto& child : node->children) {
        collectBindings(child.get(), settings, output);
    }
}

// 叶片横截面宽度轮廓：基部为 0，中部最宽，叶尖收尖。
float widthProfile(float t) {
    const float sine = std::sin(kPi * t);
    return std::pow(std::max(0.0f, sine), 0.8f);
}

} // namespace

GeneratedLeaves LeafGenerator::generate(const PlantModel& model,
                                        const LeafGenerationSettings& settings) {
    GeneratedLeaves result;
    const PlantNode* root = model.rootNode();
    if (!root) {
        return result;
    }

    std::vector<LeafBinding> bindings;
    collectBindings(root, settings, &bindings);
    result.boundNodeCount = static_cast<int>(bindings.size());

    std::mt19937 random(settings.seed);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    const int segments = std::max(2, settings.lengthSegments);
    const float tilt = settings.tiltDegrees * kPi / 180.0f;

    for (const LeafBinding& binding : bindings) {
        const PlantNode& node = *binding.node;
        const Vec3 branchDirection = node.direction.normalized();
        // 以枝干方向为轴建立正交基，用于分布叶片方位角。
        const Vec3 reference = std::abs(branchDirection.y()) < 0.9f
                                   ? Vec3::UnitY() : Vec3::UnitX();
        const Vec3 axisU = branchDirection.cross(reference).normalized();
        const Vec3 axisV = branchDirection.cross(axisU).normalized();

        for (int slot = 0; slot < binding.leafSlots; ++slot) {
            const int leafIndex = result.leafCount;
            const float azimuth = slot * kGoldenAngle + unit(random) * 0.6f;
            const float sizeScale = 1.0f + (unit(random) * 2.0f - 1.0f) * settings.sizeJitter;
            const float leafTilt = tilt * (0.85f + 0.3f * unit(random));

            // 叶片中脉方向：从枝干方向向侧面倾斜 tilt 角。
            const Vec3 radial = axisU * std::cos(azimuth) + axisV * std::sin(azimuth);
            const Vec3 midrib = (branchDirection * std::cos(leafTilt) +
                                 radial * std::sin(leafTilt)).normalized();
            // 叶宽方向尽量保持水平，法向量随之朝上。
            Vec3 widthDir = Vec3::UnitY().cross(midrib);
            if (widthDir.squaredNorm() < 1e-8f) {
                widthDir = axisU;
            }
            widthDir.normalize();
            Vec3 leafNormal = midrib.cross(widthDir).normalized();
            if (leafNormal.y() < 0.0f) {
                widthDir = -widthDir;
                leafNormal = -leafNormal;
            }

            const float length = settings.leafLength * sizeScale;
            const float halfWidth = 0.5f * settings.leafWidth * sizeScale;
            const Vec3 base = node.position;

            // 参数化叶片条带：(segments+1) 行 × 3 列（左缘 / 中脉 / 右缘）。
            const std::uint32_t rowStart =
                static_cast<std::uint32_t>(result.mesh.positions.size());
            for (int row = 0; row <= segments; ++row) {
                const float t = static_cast<float>(row) / static_cast<float>(segments);
                const Vec3 center = base + midrib * (t * length)
                                    - leafNormal * (settings.droop * length * t * t);
                const float width = halfWidth * widthProfile(t);
                const Vec3 edgeDrop = leafNormal * (settings.fold * width);
                result.mesh.positions.push_back(center - widthDir * width - edgeDrop);
                result.mesh.positions.push_back(center);
                result.mesh.positions.push_back(center + widthDir * width - edgeDrop);
            }
            for (int row = 0; row < segments; ++row) {
                const std::uint32_t r0 = rowStart + static_cast<std::uint32_t>(row * 3);
                const std::uint32_t r1 = r0 + 3;
                const std::uint16_t material =
                    static_cast<std::uint16_t>(leafIndex & 0x3); // 4 色调色板
                // 左半条带
                result.mesh.indices.push_back(r0);
                result.mesh.indices.push_back(r1);
                result.mesh.indices.push_back(r0 + 1);
                result.mesh.indices.push_back(r0 + 1);
                result.mesh.indices.push_back(r1);
                result.mesh.indices.push_back(r1 + 1);
                // 右半条带
                result.mesh.indices.push_back(r0 + 1);
                result.mesh.indices.push_back(r1 + 1);
                result.mesh.indices.push_back(r0 + 2);
                result.mesh.indices.push_back(r0 + 2);
                result.mesh.indices.push_back(r1 + 1);
                result.mesh.indices.push_back(r1 + 2);
                for (int quad = 0; quad < 4; ++quad) {
                    result.faceMaterials.push_back(material);
                }
            }
            ++result.leafCount;
        }
    }

    MeshProcessing::recomputeNormals(result.mesh);
    // 叶片是开放曲面，不按有向体积翻转绕序。
    MeshProcessing::computeStats(result.mesh, false);
    return result;
}
