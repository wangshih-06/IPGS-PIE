#include "Rendering/Camera.h"

#include <QtMath>

namespace {
Mat4 perspective(float fovDegrees, float aspect, float nearPlane, float farPlane) {
    const float f = 1.0f / qTan(qDegreesToRadians(fovDegrees) * 0.5f);
    Mat4 result = Mat4::Zero();
    result(0, 0) = f / aspect;
    result(1, 1) = f;
    result(2, 2) = (farPlane + nearPlane) / (nearPlane - farPlane);
    result(2, 3) = (2.0f * farPlane * nearPlane) / (nearPlane - farPlane);
    result(3, 2) = -1.0f;
    return result;
}

Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up) {
    const Vec3 forward = (target - eye).normalized();
    const Vec3 side = forward.cross(up).normalized();
    const Vec3 correctedUp = side.cross(forward);
    Mat4 result = Mat4::Identity();
    result.block<1, 3>(0, 0) = side.transpose();
    result.block<1, 3>(1, 0) = correctedUp.transpose();
    result.block<1, 3>(2, 0) = -forward.transpose();
    result(0, 3) = -side.dot(eye);
    result(1, 3) = -correctedUp.dot(eye);
    result(2, 3) = forward.dot(eye);
    return result;
}
}

Camera::Camera() = default;

Mat4 Camera::viewMatrix() const {
    const float horizontal = distance_ * qCos(pitch_);
    const Vec3 eye(
        target_.x() + horizontal * qSin(yaw_),
        target_.y() + distance_ * qSin(pitch_),
        target_.z() + horizontal * qCos(yaw_));
    return lookAt(eye, target_, Vec3::UnitY());
}

Mat4 Camera::projectionMatrix() const {
    return perspective(fov_, aspect_, 0.1f, 100.0f);
}

Vec3 Camera::position() const {
    const float horizontal = distance_ * qCos(pitch_);
    return Vec3(
        target_.x() + horizontal * qSin(yaw_),
        target_.y() + distance_ * qSin(pitch_),
        target_.z() + horizontal * qCos(yaw_));
}

void Camera::setAspect(float aspect) {
    aspect_ = qMax(0.1f, aspect);
}

void Camera::rotate(float yawDelta, float pitchDelta) {
    yaw_ += yawDelta;
    pitch_ = qBound(-1.35f, pitch_ + pitchDelta, 1.35f);
}

void Camera::zoom(float factor) {
    distance_ = qBound(3.5f, distance_ * factor, 24.0f);
}

void Camera::pan(const Vec2& delta) {
    const Vec3 right(qCos(yaw_), 0.0f, -qSin(yaw_));
    target_ += right * delta.x() + Vec3(0.0f, delta.y(), 0.0f);
}

void Camera::reset() {
    target_ = Vec3::Zero();
    distance_ = 8.0f;
    yaw_ = 0.35f;
    pitch_ = 0.28f;
}
