#include "Rendering/Renderer.h"

#include <QMatrix4x4>
#include <QMouseEvent>
#include <QOpenGLShader>
#include <QSurfaceFormat>
#include <QTimer>
#include <QtMath>
#include <QWheelEvent>

#include <algorithm>

#include <Eigen/Geometry>

#include "Geometry/MarchingCubes.h"

namespace {
using Vertex = MeshVertex;

Vec3 plantSurfaceColor(const Vec3& position, const Vec3& normal, float heightNormalized) {
    const Vec3 bark(0.42f, 0.27f, 0.16f);
    const Vec3 crown(0.30f, 0.58f, 0.30f);
    Vec3 color = bark * (1.0f - heightNormalized) + crown * heightNormalized;
    color += normal * 0.04f;
    return color.cwiseMax(Vec3::Zero()).cwiseMin(Vec3::Ones());
}

void addVertex(QVector<Vertex>& vertices, const Vec3& position, const Vec3& normal, const Vec3& color) {
    vertices.push_back(Vertex{position, normal, color});
}

void addFace(QVector<Vertex>& vertices, const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d,
             const Vec3& normal, const Vec3& color) {
    addVertex(vertices, a, normal, color);
    addVertex(vertices, b, normal, color);
    addVertex(vertices, c, normal, color);
    addVertex(vertices, a, normal, color);
    addVertex(vertices, c, normal, color);
    addVertex(vertices, d, normal, color);
}

void addCylinder(QVector<Vertex>& vertices, const Vec3& start, const Vec3& end,
                 float radius, const Vec3& color) {
    constexpr int kSides = 8;
    const Vec3 axis = (end - start).normalized();
    const Vec3 reference = qAbs(axis.y()) < 0.9f ? Vec3(0.0f, 1.0f, 0.0f) : Vec3(1.0f, 0.0f, 0.0f);
    const Vec3 u = axis.cross(reference).normalized();
    const Vec3 v = axis.cross(u).normalized();
    Vec3 startRing[kSides];
    Vec3 endRing[kSides];
    for (int i = 0; i < kSides; ++i) {
        const float angle = (2.0f * static_cast<float>(M_PI) * i) / kSides;
        const Vec3 radial = u * qCos(angle) + v * qSin(angle);
        startRing[i] = start + radial * radius;
        endRing[i] = end + radial * radius;
    }
    for (int i = 0; i < kSides; ++i) {
        const int next = (i + 1) % kSides;
        const Vec3 normal = (endRing[i] - startRing[i]).cross(endRing[next] - startRing[i]).normalized();
        addFace(vertices, startRing[i], endRing[i], endRing[next], startRing[next], normal, color);
    }
}

void addCalibrationCube(QVector<Vertex>& vertices) {
    const float s = 1.05f;
    const Vec3 p000(-s, 0.04f, -s);
    const Vec3 p100(s, 0.04f, -s);
    const Vec3 p110(s, 2.14f, -s);
    const Vec3 p010(-s, 2.14f, -s);
    const Vec3 p001(-s, 0.04f, s);
    const Vec3 p101(s, 0.04f, s);
    const Vec3 p111(s, 2.14f, s);
    const Vec3 p011(-s, 2.14f, s);
    addFace(vertices, p001, p101, p111, p011, Vec3(0, 0, 1), Vec3(0.22f, 0.54f, 0.46f));
    addFace(vertices, p100, p000, p010, p110, Vec3(0, 0, -1), Vec3(0.14f, 0.36f, 0.34f));
    addFace(vertices, p000, p001, p011, p010, Vec3(-1, 0, 0), Vec3(0.17f, 0.43f, 0.39f));
    addFace(vertices, p101, p100, p110, p111, Vec3(1, 0, 0), Vec3(0.29f, 0.66f, 0.54f));
    addFace(vertices, p010, p011, p111, p110, Vec3(0, 1, 0), Vec3(0.38f, 0.72f, 0.57f));
    addFace(vertices, p000, p100, p101, p001, Vec3(0, -1, 0), Vec3(0.09f, 0.22f, 0.24f));
}

// 绘制光源 Gizmo 标记
void addLightGizmo(QVector<Vertex>& vertices, const Vec3& pos, const Vec3& color) {
    const float r = 0.12f;
    const Vec3 pX0(pos.x() - r, pos.y(), pos.z());
    const Vec3 pX1(pos.x() + r, pos.y(), pos.z());
    const Vec3 pY0(pos.x(), pos.y() - r, pos.z());
    const Vec3 pY1(pos.x(), pos.y() + r, pos.z());
    const Vec3 pZ0(pos.x(), pos.y(), pos.z() - r);
    const Vec3 pZ1(pos.x(), pos.y(), pos.z() + r);
    addCylinder(vertices, pX0, pX1, 0.02f, color);
    addCylinder(vertices, pY0, pY1, 0.02f, color);
    addCylinder(vertices, pZ0, pZ1, 0.02f, color);
}
}

Renderer::Renderer(QWidget* parent)
    : QOpenGLWidget(parent) {
    setWindowTitle(QStringLiteral("PlantSim Engine | OpenGL 4.3 Core"));
    QSurfaceFormat format;
    format.setVersion(4, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    format.setSamples(4);
    setFormat(format);
    setMinimumSize(640, 420);
    setMouseTracking(true);
}

void Renderer::initializeGL() {
    initializeOpenGLFunctions();
    const GLubyte* version = glGetString(GL_VERSION);
    const GLubyte* renderer = glGetString(GL_RENDERER);
    qInfo("OpenGL 版本: %s", reinterpret_cast<const char*>(version));
    qInfo("GPU 渲染器: %s", reinterpret_cast<const char*>(renderer));

    glClearColor(0.035f, 0.060f, 0.075f, 1.0f);
    glEnable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    setupShaderProgram();
    buildReferenceMesh();
    mesh_.upload(this);
    qInfo("Renderer modules ready: Camera / Shader / Mesh / Light");

    clock_.start();
    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, [this]() { update(); });
    timer->start(16);
}

void Renderer::resizeGL(int width, int height) {
    glViewport(0, 0, width, height);
    camera_.setAspect(height > 0 ? static_cast<float>(width) / height : 1.0f);
}

void Renderer::paintGL() {
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    if (!shader_.bind()) {
        return;
    }

    const float seconds = autoRotate_ ? static_cast<float>(clock_.elapsed()) / 1000.0f : 0.0f;
    Mat4 model = Mat4::Identity();
    model.block<3, 3>(0, 0) = Eigen::AngleAxisf(seconds * 0.14f, Vec3::UnitY()).toRotationMatrix();
    const Mat4 mvp = camera_.projectionMatrix() * camera_.viewMatrix() * model;
    const Vec3 cameraPosition = camera_.position();
    auto& program = shader_.program();
    program.setUniformValue("uMvp", QMatrix4x4(mvp.data()));
    program.setUniformValue("uModel", QMatrix4x4(model.data()));
    program.setUniformValue("uLightDirection", light_.direction.x(), light_.direction.y(), light_.direction.z());
    program.setUniformValue("uLightColor", light_.color.x(), light_.color.y(), light_.color.z());
    program.setUniformValue("uLightIntensity", light_.intensity);
    program.setUniformValue("uCameraPosition", cameraPosition.x(), cameraPosition.y(), cameraPosition.z());
    mesh_.draw(this);
    if (hasPlantMesh_) {
        uploadPlantMeshIfNeeded();
        plantMesh_.draw(this);
    }
    shader_.release();
}

void Renderer::setupShaderProgram() {
    const char* vertexShader = R"glsl(
        #version 430 core
        layout(location = 0) in vec3 aPosition;
        layout(location = 1) in vec3 aNormal;
        layout(location = 2) in vec3 aColor;
        uniform mat4 uMvp;
        uniform mat4 uModel;
        out vec3 vWorldPosition;
        out vec3 vNormal;
        out vec3 vColor;
        void main() {
            vec4 world = uModel * vec4(aPosition, 1.0);
            vWorldPosition = world.xyz;
            vNormal = mat3(transpose(inverse(uModel))) * aNormal;
            vColor = aColor;
            gl_Position = uMvp * vec4(aPosition, 1.0);
        }
    )glsl";
    const char* fragmentShader = R"glsl(
        #version 430 core
        in vec3 vWorldPosition;
        in vec3 vNormal;
        in vec3 vColor;
        uniform vec3 uLightDirection;
        uniform vec3 uLightColor;
        uniform vec3 uCameraPosition;
        uniform float uLightIntensity;
        out vec4 fragColor;
        void main() {
            vec3 normal = normalize(vNormal);
            vec3 light = normalize(uLightDirection);
            vec3 viewDir = normalize(uCameraPosition - vWorldPosition);
            float diffuse = max(dot(normal, light), 0.0);
            vec3 halfVector = normalize(light + viewDir);
            float specular = pow(max(dot(normal, halfVector), 0.0), 28.0);
            float ambient = 0.18 + 0.12 * uLightIntensity;
            vec3 color = vColor * (ambient + diffuse * (0.42 + uLightIntensity * 0.55));
            color += uLightColor * specular * 0.16;
            fragColor = vec4(color, 1.0);
        }
    )glsl";
    if (!shader_.create(vertexShader, fragmentShader)) {
        qWarning().noquote() << "Shader setup failed:" << shader_.log();
    }
}

void Renderer::buildReferenceMesh() {
    QVector<MeshVertex> vertices;
    addCalibrationCube(vertices);
    addCylinder(vertices, Vec3(0, 0.02f, 0), Vec3(2.2f, 0.02f, 0), 0.025f, Vec3(0.78f, 0.32f, 0.28f));
    addCylinder(vertices, Vec3(0, 0.02f, 0), Vec3(0, 2.4f, 0), 0.025f, Vec3(0.47f, 0.84f, 0.55f));
    addCylinder(vertices, Vec3(0, 0.02f, 0), Vec3(0, 0.02f, 2.2f), 0.025f, Vec3(0.34f, 0.65f, 0.86f));

    // 绘制环境中的光源 Gizmos
    for (const auto& ls : environment_.lightSources) {
        if (ls.enabled) {
            addLightGizmo(vertices, ls.position, ls.color);
        }
    }
    mesh_.setVertices(vertices);
    mesh_.upload(this);
}

void Renderer::setPlantSurface(const SurfaceMesh& mesh) {
    buildPlantMesh(mesh);
    hasPlantMesh_ = plantMesh_.vertexCount() > 0;
    plantMeshUploaded_ = false;
    update();
}

void Renderer::clearPlantSurface() {
    hasPlantMesh_ = false;
    plantMeshUploaded_ = false;
    plantMesh_.setVertices(QVector<MeshVertex>());
    update();
}

void Renderer::uploadPlantMeshIfNeeded() {
    if (plantMeshUploaded_ || plantMesh_.vertexCount() == 0) return;
    if (!plantMesh_.upload(this)) return;
    plantMeshUploaded_ = true;
}

void Renderer::buildPlantMesh(const SurfaceMesh& mesh) {
    if (mesh.positions.empty() || mesh.indices.empty()) {
        plantMesh_.setVertices(QVector<MeshVertex>());
        return;
    }
    const float minY = mesh.stats.bounds.minimum.y();
    const float maxY = mesh.stats.bounds.maximum.y();
    const float heightSpan = std::max(1.0e-4f, maxY - minY);

    QVector<MeshVertex> vertices;
    vertices.reserve(static_cast<int>(mesh.indices.size()));
    for (const std::uint32_t index : mesh.indices) {
        if (index >= static_cast<std::uint32_t>(mesh.positions.size())) continue;
        const Vec3& position = mesh.positions[index];
        const Vec3 normal = index < static_cast<std::uint32_t>(mesh.normals.size())
                                ? mesh.normals[index] : Vec3::UnitY();
        const float height = (position.y() - minY) / heightSpan;
        vertices.push_back(MeshVertex{position, normal, plantSurfaceColor(position, normal, height)});
    }
    plantMesh_.setVertices(vertices);
}

void Renderer::setEnvironment(const EnvironmentParams& environment) {
    environment_ = environment;
    light_.setEnvironment(environment);
    buildReferenceMesh();
    update();
}

void Renderer::setLightIntensity(float intensity) {
    environment_.lightIntensity = qBound(0.0f, intensity, 1.0f);
    light_.setEnvironment(environment_);
    update();
}

void Renderer::resetCamera() {
    camera_.reset();
    update();
}

void Renderer::setAutoRotate(bool enabled) {
    autoRotate_ = enabled;
    update();
}

void Renderer::mousePressEvent(QMouseEvent* event) {
    lastMousePosition_ = event->pos();
    event->accept();
}

void Renderer::mouseMoveEvent(QMouseEvent* event) {
    const QPoint delta = event->pos() - lastMousePosition_;
    lastMousePosition_ = event->pos();
    if (event->buttons() & Qt::LeftButton) {
        camera_.rotate(delta.x() * 0.008f, delta.y() * 0.008f);
    } else if (event->buttons() & Qt::MiddleButton) {
        camera_.pan(Vec2(-delta.x() * 0.01f, delta.y() * 0.01f));
    }
    update();
    event->accept();
}

void Renderer::wheelEvent(QWheelEvent* event) {
    camera_.zoom(event->angleDelta().y() > 0 ? 0.9f : 1.1f);
    update();
    event->accept();
}
