#include "Rendering/Mesh.h"

Mesh::Mesh()
    : vertexBuffer_(QOpenGLBuffer::VertexBuffer) {
}

void Mesh::setVertices(const QVector<MeshVertex>& vertices) {
    vertices_ = vertices;
    vertexCount_ = vertices_.size();
}

bool Mesh::upload(QOpenGLFunctions_4_3_Core* functions) {
    if (!functions || vertices_.isEmpty()) {
        return false;
    }
    vertexArray_.create();
    vertexArray_.bind();
    vertexBuffer_.create();
    vertexBuffer_.bind();
    vertexBuffer_.allocate(vertices_.constData(), vertices_.size() * static_cast<int>(sizeof(MeshVertex)));
    functions->glEnableVertexAttribArray(0);
    functions->glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex), nullptr);
    functions->glEnableVertexAttribArray(1);
    functions->glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                                      reinterpret_cast<void*>(sizeof(float) * 3));
    functions->glEnableVertexAttribArray(2);
    functions->glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, sizeof(MeshVertex),
                                      reinterpret_cast<void*>(sizeof(float) * 6));
    vertexBuffer_.release();
    vertexArray_.release();
    return true;
}

void Mesh::draw(QOpenGLFunctions_4_3_Core* functions) {
    if (!functions || !vertexArray_.isCreated()) {
        return;
    }
    vertexArray_.bind();
    functions->glDrawArrays(GL_TRIANGLES, 0, vertexCount_);
    vertexArray_.release();
}

int Mesh::vertexCount() const {
    return vertexCount_;
}
