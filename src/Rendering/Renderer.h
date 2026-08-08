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
#include "Physics/PlantPhysicsSolver.h"

class QMouseEvent;
class QWheelEvent;
struct SurfaceMesh;

class Renderer : public QOpenGLWidget, protected QOpenGLFunctions_4_3_Core {
    Q_OBJECT
public:
    explicit Renderer(QWidget* parent = nullptr);

public slots:
    void setEnvironment(const EnvironmentParams& environment);
    void setLightIntensity(float intensity);
    void resetCamera();
    void setAutoRotate(bool enabled);
    // 第9周：接收植物表面网格（每次生长 tick 由 SimulationEngine 重新提取）
    void setPlantSurface(const SurfaceMesh& mesh);
    void clearPlantSurface();
    void setPhysicsDebugSnapshot(const PlantPhysicsDebugSnapshot& snapshot);
    void setPhysicsDebugVisible(bool visible);

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
    void buildPlantMesh(const SurfaceMesh& mesh);
    void uploadPlantMeshIfNeeded();
    void buildPhysicsDebugMesh(const PlantPhysicsDebugSnapshot& snapshot);
    void uploadPhysicsDebugMeshIfNeeded();

    Shader shader_;
    Mesh mesh_;
    Mesh plantMesh_;
    Mesh physicsDebugMesh_;
    bool hasPlantMesh_ = false;
    bool plantMeshUploaded_ = false;
    bool hasPhysicsDebugMesh_ = false;
    bool physicsDebugMeshUploaded_ = false;
    bool physicsDebugVisible_ = false;
    Camera camera_;
    Light light_;
    EnvironmentParams environment_;
    QElapsedTimer clock_;
    QPoint lastMousePosition_;
    bool autoRotate_ = true;
};
