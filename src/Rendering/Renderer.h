#pragma once

#include <QElapsedTimer>
#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLWidget>
#include <QPoint>
#include <QVector>

#include "Engine/EnvironmentParams.h"
#include "Rendering/Camera.h"
#include "Rendering/Light.h"
#include "Rendering/Mesh.h"
#include "Rendering/Shader.h"

class QMouseEvent;
class QWheelEvent;

class Renderer : public QOpenGLWidget, protected QOpenGLFunctions_4_3_Core {
    Q_OBJECT
public:
    explicit Renderer(QWidget* parent = nullptr);

public slots:
    void setEnvironment(const EnvironmentParams& environment);
    void setLightIntensity(float intensity);
    void resetCamera();
    void setAutoRotate(bool enabled);

protected:
    void initializeGL() override;
    void resizeGL(int width, int height) override;
    void paintGL() override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;

private:
    void setupShaderProgram();
    void buildReferenceMesh();

    Shader shader_;
    Mesh mesh_;
    Camera camera_;
    Light light_;
    EnvironmentParams environment_;
    QElapsedTimer clock_;
    QPoint lastMousePosition_;
    bool autoRotate_ = true;
};
