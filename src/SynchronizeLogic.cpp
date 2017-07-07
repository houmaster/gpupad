#include "SynchronizeLogic.h"
#include "Singletons.h"
#include "FileCache.h"
#include "session/SessionModel.h"
#include "editors/EditorManager.h"
#include "editors/SourceEditor.h"
#include "editors/BinaryEditor.h"
#include "render/RenderSession.h"
#include <QTimer>

template<typename F> // F(const FileItem&)
void forEachFileItem(SessionModel& model, const F &function) {
    model.forEachItem([&](const Item& item) {
        if (item.itemType == ItemType::Buffer ||
            item.itemType == ItemType::Shader ||
            item.itemType == ItemType::Texture ||
            item.itemType == ItemType::Image ||
            item.itemType == ItemType::Script)
            function(static_cast<const FileItem&>(item));
    });
}

SynchronizeLogic::SynchronizeLogic(QObject *parent)
  : QObject(parent)
  , mModel(Singletons::sessionModel())
  , mUpdateTimer(new QTimer(this))
  , mRenderSession(new RenderSession())
{
    connect(mRenderSession.data(), &RenderTask::updated,
        this, &SynchronizeLogic::handleSessionRendered);

    connect(mUpdateTimer, &QTimer::timeout,
        [this]() { update(); });
    connect(&mModel, &SessionModel::dataChanged,
        this, &SynchronizeLogic::handleItemModified);
    connect(&mModel, &SessionModel::rowsInserted,
        this, &SynchronizeLogic::handleItemReordered);
    connect(&mModel, &SessionModel::rowsAboutToBeRemoved,
        this, &SynchronizeLogic::handleItemReordered);

    setEvaluationMode(false, false);
}

SynchronizeLogic::~SynchronizeLogic() = default;

void SynchronizeLogic::manualEvaluation()
{
    mRenderSessionInvalidated = true;
    update(true);
}

void SynchronizeLogic::setEvaluationMode(bool automatic, bool steady)
{
    mAutomaticEvaluation = automatic;
    mSteadyEvaluation = steady;
    mRenderSessionInvalidated = true;

    if (mSteadyEvaluation) {
        mUpdateTimer->setSingleShot(false);
        mUpdateTimer->start();
    }
    else if (mAutomaticEvaluation) {
        mUpdateTimer->setSingleShot(true);
        mUpdateTimer->stop();
        update();
    }
    else {
        mUpdateTimer->stop();
        Singletons::sessionModel().setActiveItems({ });
    }
}

void SynchronizeLogic::setEvaluationInterval(int interval)
{
    mUpdateTimer->setInterval(interval);
}

void SynchronizeLogic::handleSessionRendered()
{
    if (mAutomaticEvaluation || mSteadyEvaluation)
        Singletons::sessionModel().setActiveItems(mRenderSession->usedItems());
}

void SynchronizeLogic::handleFileItemsChanged(const QString &fileName)
{
    forEachFileItem(mModel,
        [&](const FileItem &item) {
            if (item.fileName == fileName) {
                auto index = mModel.index(&item);
                emit mModel.dataChanged(index, index);
            }
        });
}

void SynchronizeLogic::handleItemModified(const QModelIndex &index)
{
    if (auto buffer = mModel.item<Buffer>(index)) {
        mBuffersModified.insert(buffer->id);
    }
    else if (auto column = mModel.item<Column>(index)) {
        mBuffersModified.insert(column->parent->id);
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
        // TODO: remove - mark groups containing used items as used
        mRenderSessionInvalidated = true;
    }

    mUpdateTimer->start();
}

void SynchronizeLogic::handleItemReordered(const QModelIndex &parent, int first)
{
    mRenderSessionInvalidated = true;
    handleItemModified(mModel.index(first, 0, parent));
}

void SynchronizeLogic::handleFileRenamed(const QString &prevFileName,
    const QString &fileName)
{
    if (FileDialog::isUntitled(prevFileName))
        forEachFileItem(mModel, [&](const FileItem &item) {
            if (item.fileName == prevFileName)
                mModel.setData(mModel.index(&item, SessionModel::FileName),
                    fileName);
        });
}

void SynchronizeLogic::update(bool manualEvaluation)
{
    foreach (ItemId bufferId, mBuffersModified)
        if (auto buffer = mModel.findItem<Buffer>(bufferId))
            if (auto editor = Singletons::editorManager().getBinaryEditor(buffer->fileName))
                updateBinaryEditor(*buffer, *editor);
    mBuffersModified.clear();

    if (manualEvaluation || mAutomaticEvaluation || mSteadyEvaluation) {
        Singletons::fileCache().update(Singletons::editorManager());
        mRenderSession->update(mRenderSessionInvalidated, manualEvaluation);
    }
    mRenderSessionInvalidated = false;
}

void SynchronizeLogic::handleItemActivated(const QModelIndex &index, bool *handled)
{
    if (auto buffer = mModel.item<Buffer>(index)) {
        if (auto editor = Singletons::editorManager().openBinaryEditor(buffer->fileName)) {
            updateBinaryEditor(*buffer, *editor);
            editor->scrollToOffset();
            *handled = true;
        }
    }
    else if (auto texture = mModel.item<Texture>(index)) {
        if (Singletons::editorManager().openImageEditor(texture->fileName))
            *handled = true;
    }
    else if (auto image = mModel.item<Image>(index)) {
        if (Singletons::editorManager().openImageEditor(image->fileName))
            *handled = true;
    }
    else if (auto shader = mModel.item<Shader>(index)) {
        if (Singletons::editorManager().openSourceEditor(shader->fileName))
            *handled = true;
    }
    else if (auto script = mModel.item<Script>(index)) {
        if (Singletons::editorManager().openSourceEditor(script->fileName))
            *handled = true;
    }
}

void SynchronizeLogic::updateBinaryEditor(const Buffer &buffer, BinaryEditor &editor)
{
    auto mapDataType = [](Column::DataType type) {
        switch (type) {
            case Column::Int8: return BinaryEditor::DataType::Int8;
            case Column::Int16: return BinaryEditor::DataType::Int16;
            case Column::Int32: return BinaryEditor::DataType::Int32;
            case Column::Uint8: return BinaryEditor::DataType::Uint8;
            case Column::Uint16: return BinaryEditor::DataType::Uint16;
            case Column::Uint32: return BinaryEditor::DataType::Uint32;
            case Column::Float: return BinaryEditor::DataType::Float;
            case Column::Double: return BinaryEditor::DataType::Double;
        }
        return BinaryEditor::DataType::Int8;
    };

    editor.setColumnCount(buffer.items.size());
    editor.setOffset(buffer.offset);
    editor.setRowCount(buffer.rowCount);
    auto i = 0;
    foreach (Item *item, buffer.items) {
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
