#ifndef MESSAGELIST_H
#define MESSAGELIST_H

#include <QSharedPointer>
#include <QString>
#include <QSet>
#include <QMutex>

using ItemId = int;

enum MessageType
{
    OpenGLVersionNotAvailable,
    LoadingFileFailed,
    UnsupportedShaderType,
    CreatingFramebufferFailed,
    DownloadingImageFailed,
    UnformNotSet,
    ShaderInfo,
    ShaderWarning,
    ShaderError,
    CallDuration,
    ScriptError,
};

struct Message
{
    MessageType type;
    QString text;
    ItemId itemId;
    QString fileName;
    int line;
};

using MessagePtr = QSharedPointer<const Message>;
using MessagePtrSet = QSet<MessagePtr>;

class MessageList
{
public:
    MessagePtr insert(QString fileName, int line, MessageType type,
        QString text = "", bool deduplicate = true);
    MessagePtr insert(ItemId itemId, MessageType type,
        QString text = "", bool deduplicate = true);

    QList<MessagePtr> messages() const;

private:
    mutable QMutex mMessagesMutex;
    mutable QList<QWeakPointer<const Message>> mMessages;
};

#endif // MESSAGELIST_H
