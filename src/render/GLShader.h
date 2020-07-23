#ifndef GLSHADER_H
#define GLSHADER_H

#include "GLItem.h"
#include "GLPrintf.h"

class GLShader
{
public:
    static void parseLog(const QString &log,
        MessagePtrSet &messages, ItemId itemId,
        QList<QString> fileNames);

    explicit GLShader(const QList<const Shader*> &shaders);
    bool operator==(const GLShader &rhs) const;
    Shader::ShaderType type() const { return mType; }

    QString getSource() const;
    bool compile(GLPrintf *printf = nullptr, bool silent = false);
    GLuint shaderObject() const { return mShaderObject; }
    QString getAssembly();

private:
    QStringList getPatchedSources(GLPrintf *printf);

    ItemId mItemId{ };
    MessagePtrSet mMessages;
    QList<QString> mFileNames;
    QList<QString> mSources;
    Shader::ShaderType mType;
    GLObject mShaderObject;
};

#endif // GLSHADER_H
