#include "Rendering/Shader.h"

#include <QOpenGLShader>

bool Shader::create(const char* vertexSource, const char* fragmentSource) {
    const bool vertexOk = program_.addShaderFromSourceCode(QOpenGLShader::Vertex, vertexSource);
    const bool fragmentOk = vertexOk && program_.addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentSource);
    const bool linked = fragmentOk && program_.link();
    if (!linked) {
        log_ = program_.log();
    }
    return linked;
}

bool Shader::bind() {
    return program_.bind();
}

void Shader::release() {
    program_.release();
}

QOpenGLShaderProgram& Shader::program() {
    return program_;
}

const QString& Shader::log() const {
    return log_;
}
