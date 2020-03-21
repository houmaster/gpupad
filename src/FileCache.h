#ifndef FILECACHE_H
#define FILECACHE_H

#include <QObject>
#include <QMap>
#include <QSet>
#include <QMutex>
#include <QTimer>
#include <QFileSystemWatcher>
#include "TextureData.h"

class FileCache : public QObject
{
    Q_OBJECT
public:
    explicit FileCache(QObject *parent = nullptr);

    bool getSource(const QString &fileName, QString *source) const;
    bool getTexture(const QString &fileName, TextureData *texture) const;
    bool getBinary(const QString &fileName, QByteArray *binary) const;

    // only call from main thread
    void invalidateEditorFile(const QString &fileName);
    void updateEditorFiles();

signals:
    void fileChanged(const QString &fileName) const;

private:
    void addFileSystemWatch(const QString &fileName) const;
    void addFileSystemWatches();
    void removeFileSystemWatch(const QString &fileName) const;
    void handleFileSystemFileChanged(const QString &fileName);

    QSet<QString> mEditorFilesInvalidated;
    QTimer mFileSystemWatchUpdateTimer;

    mutable QMutex mMutex;
    mutable QMap<QString, QString> mSources;
    mutable QMap<QString, TextureData> mTextures;
    mutable QMap<QString, QByteArray> mBinaries;
    mutable QFileSystemWatcher mFileSystemWatcher;
    mutable QSet<QString> mFileSystemWatchesToAdd;
};

#endif // FILECACHE_H
