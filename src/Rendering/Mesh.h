#pragma once

#include <QOpenGLBuffer>
#include <QOpenGLFunctions_4_3_Core>
#include <QOpenGLVertexArrayObject>
#include <QVector>

#include "Common/MathTypes.h"

struct MeshVertex {
    Vec3 position;
    Vec3 normal;
    Vec3 color;
};

class Mesh {
public:
    Mesh();

    void setVertices(const QVector<MeshVertex>& vertices);
    bool upload(QOpenGLFunctions_4_3_Core* functions);
    void draw(QOpenGLFunctions_4_3_Core* functions);
    int vertexCount() const;

private:
    QVector<MeshVertex> vertices_;
    QOpenGLBuffer vertexBuffer_;
    QOpenGLVertexArrayObject vertexArray_;
    int vertexCount_ = 0;
};
