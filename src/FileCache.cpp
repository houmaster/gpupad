#include "FileCache.h"
#include "Singletons.h"
#include "editors/EditorManager.h"
#include "editors/SourceEditor.h"
#include "editors/TextureEditor.h"
#include "editors/BinaryEditor.h"
#include <QThread>
#include <QTextStream>

namespace {
    bool loadSource(const QString &fileName, QString *source)
    {
        if (!source)
            return false;

        if (FileDialog::isEmptyOrUntitled(fileName)) {
            *source = "";
            return true;
        }
        QFile file(fileName);
        if (!file.open(QFile::ReadOnly | QFile::Text))
            return false;

        const auto unprintable = [](const auto &string) {
          return (std::find_if(string.constBegin(), string.constEnd(),
              [](QChar c) { return (!c.isPrint() && !c.isSpace()); }) != string.constEnd());
        };

        QTextStream stream(&file);
        auto string = stream.readAll();
        if (!string.isSimpleText() || unprintable(string)) {
    #if (QT_VERSION < QT_VERSION_CHECK(6, 0, 0))
          stream.setCodec("Windows-1250");
          stream.seek(0);
          string = stream.readAll();
    #endif
          if (unprintable(string))
              return false;
        }
        *source = string;
        return true;
    }

    bool loadTexture(const QString &fileName, bool flipVertically, TextureData *texture)
    {
        if (!texture || FileDialog::isEmptyOrUntitled(fileName))
            return false;

        auto file = TextureData();
        if (!file.load(fileName, flipVertically))
            return false;

        *texture = file;
        return true;
    }

    bool loadBinary(const QString &fileName, QByteArray *binary)
    {
        if (!binary || FileDialog::isEmptyOrUntitled(fileName))
            return false;

        QFile file(fileName);
        if (!file.open(QFile::ReadOnly))
            return false;
        *binary = file.readAll();
        return true;
    }
} // namespace

FileCache::FileCache(QObject *parent) : QObject(parent)
{
    connect(&mFileSystemWatcher, &QFileSystemWatcher::fileChanged,
        this, &FileCache::handleFileSystemFileChanged);
    connect(&mUpdateFileSystemWatchesTimer, &QTimer::timeout,
        this, &FileCache::updateFileSystemWatches);

    updateFileSystemWatches();
}

FileCache::~FileCache() = default;

void FileCache::advertiseEditorSave(const QString &fileName)
{
    Q_ASSERT(onMainThread());
    QMutexLocker lock(&mMutex);
    mEditorSaveAdvertised.insert(fileName);
}

void FileCache::invalidateEditorFile(const QString &fileName, bool emitFileChanged)
{
    Q_ASSERT(onMainThread());
    mEditorFilesInvalidated.insert(fileName);

    if (emitFileChanged)
        Q_EMIT fileChanged(fileName);
}

void FileCache::updateEditorFiles()
{
    Q_ASSERT(onMainThread());
    QMutexLocker lock(&mMutex);
    auto &editorManager = Singletons::editorManager();
    for (const auto &fileName : qAsConst(mEditorFilesInvalidated)) {
        if (auto editor = editorManager.getSourceEditor(fileName)) {
            mSources[fileName] = editor->source();
        }
        else if (auto editor = editorManager.getBinaryEditor(fileName)) {
            mBinaries[fileName] = editor->data();
        }
        else if (auto editor = editorManager.getTextureEditor(fileName)) {
            mTextures[TextureKey(fileName, false)] = editor->texture();
        }
        else {
            mSources.remove(fileName);
            mBinaries.remove(fileName);
            mTextures.remove(TextureKey(fileName, true));
            mTextures.remove(TextureKey(fileName, false));
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
    if (!loadSource(fileName, source))
        return false;
    mSources[fileName] = *source;
    return true;
}

bool FileCache::getTexture(const QString &fileName, bool flipVertically, TextureData *texture) const
{
    Q_ASSERT(texture);
    QMutexLocker lock(&mMutex);
    const auto key = TextureKey(fileName, flipVertically);
    if (mTextures.contains(key)) {
        *texture = mTextures[key];
        return true;
    }

    addFileSystemWatch(fileName);

    if (FileDialog::isVideoFileName(fileName)) {
        texture->create(QOpenGLTexture::Target2D,
            QOpenGLTexture::RGB8_UNorm, 1, 1, 1, 1, 1);
        texture->clear();
        Q_EMIT videoPlayerRequested(fileName, flipVertically);
    }
    else if (!loadTexture(fileName, flipVertically, texture)) {
        return false;
    }
    mTextures[key] = *texture;
    return true;
}

bool FileCache::updateTexture(const QString &fileName, bool flippedVertically, TextureData texture) const
{
    QMutexLocker lock(&mMutex);
    const auto key = TextureKey(fileName, flippedVertically);
    if (!mTextures.contains(key))
        return false;
    mTextures[key] = std::move(texture);
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
    if (!loadBinary(fileName, binary))
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

    for (const auto &fileName : qAsConst(filesChanged)) {
        mSources.remove(fileName);
        mBinaries.remove(fileName);
        mTextures.remove(TextureKey(fileName, true));
        mTextures.remove(TextureKey(fileName, false));
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

    for (const auto &fileName : qAsConst(filesChanged)) {
        if (auto editor = getEditor(fileName)) {
            if (!mEditorSaveAdvertised.remove(fileName)) {
                editor->load();
            }
        }
        Q_EMIT fileChanged(fileName);
    }

    // enqueue update
    mUpdateFileSystemWatchesTimer.start(5);
}
