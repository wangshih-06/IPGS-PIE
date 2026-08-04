#pragma once

#include "Common/MathTypes.h"

class Camera {
public:
    Camera();

    Mat4 viewMatrix() const;
    Mat4 projectionMatrix() const;
    Vec3 position() const;

    void setAspect(float aspect);
    void rotate(float yawDelta, float pitchDelta);
    void zoom(float factor);
    void pan(const Vec2& delta);
    void reset();

private:
    Vec3 target_ = Vec3::Zero();
    float distance_ = 8.0f;
    float yaw_ = 0.35f;
    float pitch_ = 0.28f;
    float fov_ = 52.0f;
    float aspect_ = 1.0f;
};
