#include "SynchronizeLogic.h"
#include "Singletons.h"
#include "FileCache.h"
#include "session/SessionModel.h"
#include "editors/EditorManager.h"
#include "editors/BinaryEditor.h"
#include "render/RenderSession.h"
#include "render/ProcessSource.h"
#include "render/CompositorSync.h"
#include <QTimer>

SynchronizeLogic::SynchronizeLogic(QObject *parent)
    : QObject(parent)
    , mModel(Singletons::sessionModel())
    , mUpdateEditorsTimer(new QTimer(this))
    , mEvaluationTimer(new QTimer(this))
    , mProcessSourceTimer(new QTimer(this))
    , mProcessSource(new ProcessSource(this))
{
    connect(mUpdateEditorsTimer, &QTimer::timeout,
        this, &SynchronizeLogic::updateEditors);
    connect(mEvaluationTimer, &QTimer::timeout,
        [this]() { evaluate(mEvaluationMode == EvaluationMode::Automatic ?
            EvaluationType::Automatic : EvaluationType::Steady);
        });
    connect(mProcessSourceTimer, &QTimer::timeout,
        this, &SynchronizeLogic::processSource);
    connect(&mModel, &SessionModel::dataChanged,
        this, &SynchronizeLogic::handleItemsModified);
    connect(&mModel, &SessionModel::rowsInserted,
        this, &SynchronizeLogic::handleItemReordered);
    connect(&mModel, &SessionModel::rowsAboutToBeRemoved,
        this, &SynchronizeLogic::handleItemReordered);
    connect(mProcessSource, &ProcessSource::outputChanged,
        this, &SynchronizeLogic::outputChanged);
    connect(&Singletons::fileCache(), &FileCache::fileChanged,
        this, &SynchronizeLogic::handleFileChanged);
    connect(&Singletons::editorManager(), &EditorManager::editorRenamed,
        this, &SynchronizeLogic::handleEditorFileRenamed);
    connect(&Singletons::editorManager(), &EditorManager::sourceTypeChanged,
        this, &SynchronizeLogic::handleSourceTypeChanged);

    resetRenderSession();

    mUpdateEditorsTimer->start(100);

    mProcessSourceTimer->setInterval(500);
    mProcessSourceTimer->setSingleShot(true);
}

SynchronizeLogic::~SynchronizeLogic() = default;

void SynchronizeLogic::setValidateSource(bool validate)
{
    if (mValidateSource != validate) {
        mValidateSource = validate;
        processSource();
    }
}

void SynchronizeLogic::setProcessSourceType(QString type)
{
    if (mProcessSourceType != type) {
        mProcessSourceType = type;
        processSource();
    }
}

void SynchronizeLogic::resetRenderSession()
{
    mRenderSession.reset(new RenderSession());
    connect(mRenderSession.data(), &RenderTask::updated,
        this, &SynchronizeLogic::handleSessionRendered);
}

void SynchronizeLogic::resetEvaluation()
{
    evaluate(EvaluationType::Reset);
    Singletons::fileCache().rewindVideoFiles();
}

void SynchronizeLogic::manualEvaluation()
{
    evaluate(EvaluationType::Manual);
}

void SynchronizeLogic::setEvaluationMode(EvaluationMode mode)
{
    if (mEvaluationMode == mode)
        return;

    // manual evaluation after steady evaluation
    if (mEvaluationMode == EvaluationMode::Steady) {
        manualEvaluation();
    }

    mEvaluationMode = mode;

    if (mEvaluationMode == EvaluationMode::Steady) {
        mEvaluationTimer->setSingleShot(false);
        mEvaluationTimer->start(10);
        Singletons::fileCache().playVideoFiles();
    }
    else if (mEvaluationMode == EvaluationMode::Automatic) {
        mEvaluationTimer->stop();
        mEvaluationTimer->setSingleShot(true);
        if (mRenderSessionInvalidated)
            mEvaluationTimer->start(0);
        Singletons::fileCache().pauseVideoFiles();
    }
    else {
        mEvaluationTimer->stop();
        Singletons::sessionModel().setActiveItems({ });
        Singletons::fileCache().pauseVideoFiles();
    }
}

void SynchronizeLogic::handleSessionRendered()
{
    if (mEvaluationMode != EvaluationMode::Paused)
        Singletons::sessionModel().setActiveItems(mRenderSession->usedItems());

    if (mEvaluationMode == EvaluationMode::Steady &&
        synchronizeToCompositor())
        mEvaluationTimer->setInterval(1);
}

void SynchronizeLogic::handleFileChanged(const QString &fileName)
{
    mModel.forEachFileItem(
        [&](const FileItem &item) {
            if (item.fileName == fileName) {
                auto index = mModel.getIndex(&item);
                Q_EMIT mModel.dataChanged(index, index);
            }
        });

    auto &editorManager = Singletons::editorManager();
    if (editorManager.currentEditorFileName() == fileName)
        mProcessSourceTimer->start();
}

void SynchronizeLogic::handleItemsModified(const QModelIndex &topLeft,
    const QModelIndex &bottomRight, const QVector<int> &roles)
{
    Q_UNUSED(bottomRight)
    // ignore ForegroundRole...
    if (roles.empty())
        handleItemModified(topLeft);
}

void SynchronizeLogic::handleItemModified(const QModelIndex &index)
{
    if (auto fileItem = mModel.item<FileItem>(index)) {
        if (index.column() == SessionModel::Name)
            handleFileItemRenamed(*fileItem);
        else if (index.column() == SessionModel::FileName)
            handleFileItemFileChanged(*fileItem);
    }

    if (index.column() != SessionModel::None) {
        if (auto buffer = mModel.item<Buffer>(index)) {
            mEditorItemsModified.insert(buffer->id);
        }
        else if (auto column = mModel.item<Column>(index)) {
            mEditorItemsModified.insert(column->parent->id);
        }
        else if (auto texture = mModel.item<Texture>(index)) {
            mEditorItemsModified.insert(texture->id);
        }
    }

    if (mRenderSession->usedItems().contains(mModel.getItemId(index))) {
        mRenderSessionInvalidated = true;
    }
    else if (index.column() == SessionModel::Name) {
        mRenderSessionInvalidated = true;
    }
    else if (auto call = mModel.item<Call>(index)) {
        if (call->checked)
            mRenderSessionInvalidated = true;
    }
    else if (mModel.item<Group>(index)) {
        mRenderSessionInvalidated = true;
    }
    else if (mModel.item<Binding>(index)) {
        mRenderSessionInvalidated = true;
    }

    if (mEvaluationMode == EvaluationMode::Automatic)
        mEvaluationTimer->start(100);
}

void SynchronizeLogic::handleItemReordered(const QModelIndex &parent, int first)
{
    mRenderSessionInvalidated = true;
    handleItemModified(mModel.index(first, 0, parent));
}

void SynchronizeLogic::handleEditorFileRenamed(const QString &prevFileName,
    const QString &fileName)
{
    // update item filenames
    mModel.forEachFileItem([&](const FileItem &item) {
        if (item.fileName == prevFileName)
            if (!fileName.isEmpty() || FileDialog::isUntitled(item.fileName))
                mModel.setData(mModel.getIndex(&item, SessionModel::FileName),
                    fileName);
    });
}

void SynchronizeLogic::handleFileItemFileChanged(const FileItem &item)
{
    // update item name
    const auto name = FileDialog::getFileTitle(item.fileName);
    if (name != item.name)
        mModel.setData(mModel.getIndex(&item, SessionModel::Name), name);
}

void SynchronizeLogic::handleFileItemRenamed(const FileItem &item)
{
    if (item.fileName.isEmpty() ||
        FileDialog::getFileTitle(item.fileName) == item.name)
        return;

    const auto prevFileName = item.fileName;
    if (!FileDialog::isEmptyOrUntitled(item.fileName)) {
        // try to rename file in filesystem
        const auto fileName = QFileInfo(item.fileName).dir().filePath(item.name);
        if (QFile(prevFileName).exists() &&
            !QFile::rename(prevFileName, fileName)) {
            // failed, rename item back
            const auto name = QFileInfo(item.fileName).fileName();
            // WORKAROUND: directly updating item, because redo() of update command segfaults
            // mModel.setData(mModel.getIndex(&item, SessionModel::Name), name);
            const_cast<FileItem&>(item).name = name;
            return;
        }

        // update item filename
        mModel.setData(mModel.getIndex(&item, SessionModel::FileName), fileName);
    }
    else {
        // update item filename
        const auto fileName = FileDialog::generateNextUntitledFileName(item.name);
        mModel.setData(mModel.getIndex(&item, SessionModel::FileName), fileName);
    }

    // rename editor filenames
    Singletons::editorManager().renameEditors(prevFileName, item.fileName);
}

void SynchronizeLogic::handleSourceTypeChanged(SourceType sourceType)
{
    Q_UNUSED(sourceType)
    processSource();
}

void SynchronizeLogic::evaluate(EvaluationType evaluationType)
{
    Singletons::fileCache().updateEditorFiles();
    mRenderSession->update(mRenderSessionInvalidated, evaluationType);
    mRenderSessionInvalidated = false;
}

void SynchronizeLogic::updateEditors()
{
    for (auto itemId : qAsConst(mEditorItemsModified))
        updateEditor(itemId, false);
    mEditorItemsModified.clear();
}

void SynchronizeLogic::updateEditor(ItemId itemId, bool activated)
{
    auto &editors = Singletons::editorManager();
    if (auto texture = mModel.findItem<Texture>(itemId)) {
        if (auto editor = editors.getTextureEditor(texture->fileName))
            updateTextureEditor(*texture, *editor);
    }
    else if (auto buffer = mModel.findItem<Buffer>(itemId)) {
        if (auto editor = editors.getBinaryEditor(buffer->fileName)) {
            updateBinaryEditor(*buffer, *editor);
            if (activated)
                editor->scrollToOffset();
        }
    }
}

void SynchronizeLogic::updateTextureEditor(const Texture &texture,
    TextureEditor &editor)
{
}

void SynchronizeLogic::updateBinaryEditor(const Buffer &buffer,
    BinaryEditor &editor)
{
    const auto mapDataType = [](Column::DataType type) {
        switch (type) {
            case Column::DataType::Int8: return BinaryEditor::DataType::Int8;
            case Column::DataType::Int16: return BinaryEditor::DataType::Int16;
            case Column::DataType::Int32: return BinaryEditor::DataType::Int32;
            case Column::DataType::Uint8: return BinaryEditor::DataType::Uint8;
            case Column::DataType::Uint16: return BinaryEditor::DataType::Uint16;
            case Column::DataType::Uint32: return BinaryEditor::DataType::Uint32;
            case Column::DataType::Float: return BinaryEditor::DataType::Float;
            case Column::DataType::Double: return BinaryEditor::DataType::Double;
        }
        return BinaryEditor::DataType::Int8;
    };

    editor.setColumnCount(buffer.items.size());
    editor.setOffset(buffer.offset);
    editor.setRowCount(buffer.rowCount);
    auto i = 0;
    for (Item *item : qAsConst(buffer.items)) {
        const auto &column = static_cast<Column&>(*item);
        editor.setColumnName(i, column.name);
        editor.setColumnType(i, mapDataType(column.dataType));
        editor.setColumnArity(i, column.count);
        editor.setColumnPadding(i, column.padding);
        i++;
    }
    editor.setStride();
    editor.updateColumns();
}

void SynchronizeLogic::processSource()
{
    if (!mValidateSource && mProcessSourceType.isEmpty())
        return;

    Singletons::fileCache().updateEditorFiles();

    mProcessSource->setSource(
        Singletons::editorManager().currentEditorFileName(),
        Singletons::editorManager().currentSourceType());
    mProcessSource->setValidateSource(mValidateSource);
    mProcessSource->setProcessType(mProcessSourceType);
    mProcessSource->update();
}
