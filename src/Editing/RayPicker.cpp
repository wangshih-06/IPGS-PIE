#include "Editing/RayPicker.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kEpsilon = 1.0e-6f;

bool unproject(const Mat4& inverseViewProjection,
               float ndcX,
               float ndcY,
               float ndcZ,
               Vec3* point) {
    const Vec4 clip(ndcX, ndcY, ndcZ, 1.0f);
    const Vec4 world = inverseViewProjection * clip;
    if (std::abs(world.w()) < kEpsilon) return false;
    *point = world.head<3>() / world.w();
    return point->allFinite();
}

void considerHit(EditPickResult* result,
                 EditPickObject object,
                 int nodeId,
                 int leafId,
                 const Vec3& axis,
                 float distance,
                 const Vec3& point) {
    if (!result || distance >= result->distance) return;
    result->hit = true;
    result->object = object;
    result->nodeId = nodeId;
    result->leafId = leafId;
    result->axis = axis.squaredNorm() > kEpsilon ? axis.normalized() : Vec3::UnitY();
    result->distance = distance;
    result->hitPoint = point;
}

void pickNodes(const PlantNode* node,
               const EditRay& ray,
               float radius,
               EditPickResult* result) {
    if (!node || !result) return;
    float distance = 0.0f;
    Vec3 hitPoint;
    const float effectiveRadius = std::max(radius, node->radius);
    if (RayPicker::intersectSphere(ray, node->position, effectiveRadius, &distance, &hitPoint)) {
        considerHit(result, EditPickObject::Node, node->id, -1, node->direction, distance, hitPoint);
    }
    for (const auto& child : node->children) {
        pickNodes(child.get(), ray, radius, result);
    }
}
} // namespace

EditRay RayPicker::screenToWorldRay(float mouseX,
                                    float mouseY,
                                    float viewportWidth,
                                    float viewportHeight,
                                    const Mat4& viewMatrix,
                                    const Mat4& projectionMatrix) {
    if (viewportWidth <= 0.0f || viewportHeight <= 0.0f) return {};

    const float ndcX = 2.0f * mouseX / viewportWidth - 1.0f;
    const float ndcY = 1.0f - 2.0f * mouseY / viewportHeight;
    const Mat4 inverseViewProjection = (projectionMatrix * viewMatrix).inverse();

    Vec3 nearPoint;
    Vec3 farPoint;
    if (!unproject(inverseViewProjection, ndcX, ndcY, -1.0f, &nearPoint) ||
        !unproject(inverseViewProjection, ndcX, ndcY, 1.0f, &farPoint)) {
        return {};
    }

    const Vec3 direction = farPoint - nearPoint;
    if (direction.squaredNorm() < kEpsilon) return {};
    return {nearPoint, direction.normalized()};
}

bool RayPicker::intersectSphere(const EditRay& ray,
                                const Vec3& center,
                                float radius,
                                float* distance,
                                Vec3* hitPoint) {
    if (radius <= 0.0f || ray.direction.squaredNorm() < kEpsilon) return false;

    const Vec3 direction = ray.direction.normalized();
    const Vec3 offset = ray.origin - center;
    const float b = offset.dot(direction);
    const float c = offset.squaredNorm() - radius * radius;
    const float discriminant = b * b - c;
    if (discriminant < 0.0f) return false;

    const float root = std::sqrt(discriminant);
    float t = -b - root;
    if (t < 0.0f) t = -b + root;
    if (t < 0.0f) return false;

    if (distance) *distance = t;
    if (hitPoint) *hitPoint = ray.origin + direction * t;
    return true;
}

EditPickResult RayPicker::pick(const PlantModel& model,
                               const EditRay& ray,
                               EditPickMode mode,
                               const RayPickerSettings& settings) {
    EditPickResult result;
    if (!model.rootNode()) return result;

    pickNodes(model.rootNode(), ray, std::max(0.0f, settings.nodeRadius), &result);
    for (const Leaf& leaf : model.leaves()) {
        float distance = 0.0f;
        Vec3 hitPoint;
        const float radius = std::max({settings.leafRadius, leaf.size.x() * 0.5f, leaf.size.y()});
        if (intersectSphere(ray, leaf.position, radius, &distance, &hitPoint)) {
            considerHit(&result, EditPickObject::Leaf, leaf.parentNodeId, leaf.id,
                        leaf.direction, distance, hitPoint);
        }
    }

    if (result.hit) {
        result.plantId = model.id;
        result.wholePlant = mode == EditPickMode::WholePlant;
        if (result.wholePlant) {
            const PlantNode* root = model.rootNode();
            result.nodeId = root->id;
            result.axis = root->direction;
        }
    }
    return result;
}
