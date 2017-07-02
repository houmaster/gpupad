#ifndef GLSHADER_H
#define GLSHADER_H

#include "GLItem.h"

class GLShader
{
public:
    static void parseLog(const QString &log,
        MessagePtrList &messages, ItemId itemId,
        QList<QString> fileNames);

    explicit GLShader(const QList<const Shader*> &shaders);
    bool operator==(const GLShader &rhs) const;

    bool compile();
    GLuint shaderObject() const { return mShaderObject; }

private:
    ItemId mItemId{ };
    MessagePtrList mMessages;
    QList<QString> mFileNames;
    QList<QString> mSources;
    Shader::Type mType;
    GLObject mShaderObject;
};

#endif // GLSHADER_H
