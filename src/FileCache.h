#ifndef FILECACHE_H
#define FILECACHE_H

#include <QObject>
#include <QMap>
#include <QSet>
#include <QMutex>
#include <QTimer>
#include <QFileSystemWatcher>
#include "TextureData.h"

class FileCache final : public QObject
{
    Q_OBJECT
public:
    explicit FileCache(QObject *parent = nullptr);
    ~FileCache();

    bool getSource(const QString &fileName, QString *source) const;
    bool getTexture(const QString &fileName, bool flipVertically, TextureData *texture) const;
    bool getBinary(const QString &fileName, QByteArray *binary) const;
    bool updateTexture(const QString &fileName, bool flippedVertically, TextureData texture) const;

    // only call from main thread
    void invalidateEditorFile(const QString &fileName, bool emitFileChanged = true);
    void advertiseEditorSave(const QString &fileName);
    void updateEditorFiles();

Q_SIGNALS:
    void fileChanged(const QString &fileName);
    void videoPlayerRequested(const QString &fileName, bool flipVertically) const;

private:
    using TextureKey = QPair<QString, bool>;

    void handleFileSystemFileChanged(const QString &fileName);
    void addFileSystemWatch(const QString &fileName, bool changed = false) const;
    void updateFileSystemWatches();

    QSet<QString> mEditorFilesInvalidated;
    QTimer mUpdateFileSystemWatchesTimer;
    QSet<QString> mEditorSaveAdvertised;
    QFileSystemWatcher mFileSystemWatcher;

    mutable QMutex mMutex;
    mutable QMap<QString, QString> mSources;
    mutable QMap<TextureKey, TextureData> mTextures;
    mutable QMap<QString, QByteArray> mBinaries;
    mutable QMap<QString, bool> mFileSystemWatchesToAdd;
};

#endif // FILECACHE_H
