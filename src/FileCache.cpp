#include "FileCache.h"
#include "Singletons.h"
#include "VideoPlayer.h"
#include "editors/EditorManager.h"
#include "editors/SourceEditor.h"
#include "editors/TextureEditor.h"
#include "editors/BinaryEditor.h"
#include <QThread>

FileCache::FileCache(QObject *parent) : QObject(parent)
{
    connect(&mFileSystemWatcher, &QFileSystemWatcher::fileChanged,
        this, &FileCache::handleFileSystemFileChanged);
    connect(&mUpdateFileSystemWatchesTimer, &QTimer::timeout,
        this, &FileCache::updateFileSystemWatches);
    mUpdateFileSystemWatchesTimer.start(250);
    connect(this, &FileCache::videoPlayerRequested,
        this, &FileCache::handleVideoPlayerRequested, Qt::QueuedConnection);
}

FileCache::~FileCache() = default;

void FileCache::advertiseEditorSave(const QString &fileName)
{
    Q_ASSERT(onMainThread());
    QMutexLocker lock(&mMutex);
    mEditorSaveAdvertised.insert(fileName);
}

void FileCache::invalidateEditorFile(const QString &fileName)
{
    Q_ASSERT(onMainThread());
    mEditorFilesInvalidated.insert(fileName);
    emit fileChanged(fileName);
}

void FileCache::updateEditorFiles()
{
    Q_ASSERT(onMainThread());
    QMutexLocker lock(&mMutex);
    auto &editorManager = Singletons::editorManager();
    for (const auto &fileName : mEditorFilesInvalidated) {
        if (auto editor = editorManager.getSourceEditor(fileName)) {
            mSources[fileName] = editor->source();
        }
        else if (auto editor = editorManager.getBinaryEditor(fileName)) {
            mBinaries[fileName] = editor->data();
        }
        else if (auto editor = editorManager.getTextureEditor(fileName)) {
            mTextures[fileName] = editor->texture();
        }
        else {
            mSources.remove(fileName);
            mBinaries.remove(fileName);
            mTextures.remove(fileName);
        }
    }
    mEditorFilesInvalidated.clear();
}

bool FileCache::getSource(const QString &fileName, QString *source) const
{
    Q_ASSERT(source);
    QMutexLocker lock(&mMutex);
    if (mSources.contains(fileName)) {
        *source = mSources[fileName];
        return true;
    }

    addFileSystemWatch(fileName);
    if (!SourceEditor::load(fileName, source))
        return false;
    mSources[fileName] = *source;
    return true;
}

bool FileCache::getTexture(const QString &fileName, TextureData *texture) const
{
    Q_ASSERT(texture);
    QMutexLocker lock(&mMutex);
    if (mTextures.contains(fileName)) {
        *texture = mTextures[fileName];
        return true;
    }

    addFileSystemWatch(fileName);
    if (!TextureEditor::load(fileName, texture))
        return false;
    mTextures[fileName] = *texture;
    return true;
}

bool FileCache::updateTexture(const QString &fileName, TextureData texture) const
{
    QMutexLocker lock(&mMutex);
    if (!mTextures.contains(fileName))
        return false;
    mTextures[fileName] = std::move(texture);
    return true;
}

bool FileCache::getBinary(const QString &fileName, QByteArray *binary) const
{
    Q_ASSERT(binary);
    QMutexLocker lock(&mMutex);
    if (mBinaries.contains(fileName)) {
        *binary = mBinaries[fileName];
        return true;
    }

    addFileSystemWatch(fileName);
    if (!BinaryEditor::load(fileName, binary))
        return false;
    mBinaries[fileName] = *binary;
    return true;
}

void FileCache::addFileSystemWatch(const QString &fileName, bool changed) const
{
    if (!FileDialog::isEmptyOrUntitled(fileName))
        mFileSystemWatchesToAdd[fileName] |= changed;
}

void FileCache::handleFileSystemFileChanged(const QString &fileName)
{
    QMutexLocker lock(&mMutex);
    addFileSystemWatch(fileName, true);
}

void FileCache::updateFileSystemWatches()
{
    QMutexLocker lock(&mMutex);
    auto filesChanged = QSet<QString>();
    for (auto it = mFileSystemWatchesToAdd.begin(); it != mFileSystemWatchesToAdd.end(); ) {
        const auto &fileName = it.key();
        const auto &changed = it.value();
        mFileSystemWatcher.removePath(it.key());
        if (QFileInfo(fileName).exists() &&
            mFileSystemWatcher.addPath(fileName)) {
            if (changed)
                filesChanged.insert(fileName);
            it = mFileSystemWatchesToAdd.erase(it);
        }
        else {
            ++it;
        }
    }
    lock.unlock();

    const auto getEditor = [](const auto &fileName) -> IEditor* {
        auto &editorManager = Singletons::editorManager();
        if (auto editor = editorManager.getSourceEditor(fileName))
            return editor;
        if (auto editor = editorManager.getBinaryEditor(fileName))
            return editor;
        else if (auto editor = editorManager.getTextureEditor(fileName))
            return editor;
        return nullptr;
    };

    for (auto fileName : qAsConst(filesChanged)) {
        if (auto editor = getEditor(fileName)) {
            if (!mEditorSaveAdvertised.remove(fileName))
                editor->reload();
        }
        else {
            QMutexLocker lock(&mMutex);
            mSources.remove(fileName);
            mBinaries.remove(fileName);
            mTextures.remove(fileName);
        }
        emit fileChanged(fileName);
    }
}

void FileCache::asyncOpenVideoPlayer(const QString &fileName)
{
    emit videoPlayerRequested(fileName, QPrivateSignal());
}

void FileCache::handleVideoPlayerRequested(const QString &fileName)
{
    Q_ASSERT(onMainThread());
    auto videoPlayer = new VideoPlayer(fileName);
    connect(videoPlayer, &VideoPlayer::loadingFinished,
        this, &FileCache::handleVideoPlayerLoaded);
}

void FileCache::handleVideoPlayerLoaded()
{
    Q_ASSERT(onMainThread());
    auto videoPlayer = qobject_cast<VideoPlayer*>(QObject::sender());
    if (videoPlayer->width()) {
        if (mVideosPlaying)
            videoPlayer->play();
        mVideoPlayers[videoPlayer->fileName()].reset(videoPlayer);
    }
    else {
        videoPlayer->deleteLater();
    }
}

void FileCache::playVideoFiles()
{
    Q_ASSERT(onMainThread());
    for (const auto &videoPlayer : mVideoPlayers)
        videoPlayer.second->play();
    mVideosPlaying = true;
}

void FileCache::pauseVideoFiles()
{
    Q_ASSERT(onMainThread());
    for (const auto &videoPlayer : mVideoPlayers)
        videoPlayer.second->pause();
    mVideosPlaying = false;
}

void FileCache::rewindVideoFiles()
{
    Q_ASSERT(onMainThread());
    for (const auto &videoPlayer : mVideoPlayers)
        videoPlayer.second->rewind();
}
