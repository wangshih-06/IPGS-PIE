#pragma once

#include <QOpenGLShaderProgram>

class Shader {
public:
    bool create(const char* vertexSource, const char* fragmentSource);
    bool bind();
    void release();
    QOpenGLShaderProgram& program();
    const QString& log() const;

private:
    QOpenGLShaderProgram program_;
    QString log_;
};
