#include "SessionProperties.h"
#include "TextureProperties.h"
#include "BindingProperties.h"
#include "AttachmentProperties.h"
#include "CallProperties.h"
#include "ui_GroupProperties.h"
#include "ui_BufferProperties.h"
#include "ui_BlockProperties.h"
#include "ui_FieldProperties.h"
#include "ui_ProgramProperties.h"
#include "ui_ShaderProperties.h"
#include "ui_StreamProperties.h"
#include "ui_AttributeProperties.h"
#include "ui_TargetProperties.h"
#include "ui_ScriptProperties.h"
#include "editors/EditorManager.h"
#include "Singletons.h"
#include "SessionModel.h"
#include "SynchronizeLogic.h"
#include "FileCache.h"
#include <QStackedWidget>
#include <QDataWidgetMapper>
#include <QTimer>

namespace {
  class StackedWidget final : public QStackedWidget
  {
  public:
      using QStackedWidget::QStackedWidget;

      QSize minimumSizeHint() const override
      {
          return currentWidget()->minimumSizeHint();
      }
  };

  template <typename T>
  void instantiate(QScopedPointer<T> &ptr)
  {
      ptr.reset(new T());
  }
} // namespace

QString splitPascalCase(QString str)
{
    return str.replace(QRegularExpression("([a-z])([A-Z])"), "\\1 \\2");
}

void setFormVisibility(QFormLayout *layout, QLabel *label,
    QWidget *widget, bool visible)
{
    if (label)
        layout->removeWidget(label);
    layout->removeWidget(widget);
    if (visible)
        layout->addRow(label, widget);
    if (label)
        label->setVisible(visible);
    widget->setVisible(visible);
}

void setFormEnabled(QLabel *label,
    QWidget* widget, bool enabled)
{
    label->setEnabled(enabled);
    widget->setEnabled(enabled);
}

SessionProperties::SessionProperties(QWidget *parent)
    : QScrollArea(parent)
    , mModel(Singletons::sessionModel())
    , mStack(new StackedWidget(this))
    , mMapper(new QDataWidgetMapper(this))
    , mSubmitTimer(new QTimer(this))
{
    setFrameShape(QFrame::NoFrame);
    setBackgroundRole(QPalette::ToolTipBase);

    mMapper->setModel(&mModel);
    connect(mSubmitTimer, &QTimer::timeout,
        mMapper, &QDataWidgetMapper::submit);
    mSubmitTimer->start(100);

    const auto add = [&](auto &ui) {
        auto widget = new QWidget(this);
        instantiate(ui);
        ui->setupUi(widget);
        mStack->addWidget(widget);
    };
    add(mGroupProperties);
    add(mBufferProperties);
    add(mBlockProperties);
    add(mFieldProperties);
    mTextureProperties = new TextureProperties(this);
    mStack->addWidget(mTextureProperties);
    add(mProgramProperties);
    add(mShaderProperties);
    mBindingProperties = new BindingProperties(this);
    mStack->addWidget(mBindingProperties);
    add(mStreamProperties);
    add(mAttributeProperties);
    add(mTargetProperties);
    mAttachmentProperties = new AttachmentProperties(this);
    mStack->addWidget(mAttachmentProperties);
    mCallProperties = new CallProperties(this);
    mStack->addWidget(mCallProperties);
    add(mScriptProperties);
    mStack->addWidget(new QWidget(this));

    setWidgetResizable(true);
    setWidget(mStack);

    connect(mShaderProperties->fileNew, &QToolButton::clicked,
        [this]() { saveCurrentItemFileAs(FileDialog::ShaderExtensions); });
    connect(mShaderProperties->fileBrowse, &QToolButton::clicked,
        [this]() { openCurrentItemFile(FileDialog::ShaderExtensions); });
    connect(mShaderProperties->file, &ReferenceComboBox::listRequired,
        [this]() { return getFileNames(Item::Type::Shader); });

    connect(mBufferProperties->fileNew, &QToolButton::clicked,
        [this]() { saveCurrentItemFileAs(FileDialog::BinaryExtensions); });
    connect(mBufferProperties->fileBrowse, &QToolButton::clicked,
        [this]() { openCurrentItemFile(FileDialog::BinaryExtensions); });
    connect(mBufferProperties->file, &ReferenceComboBox::listRequired,
        [this]() { return getFileNames(Item::Type::Buffer, true); });
    connect(mBlockProperties->deduceOffset, &QToolButton::clicked,
        this, &SessionProperties::deduceBlockOffset);
    connect(mBlockProperties->deduceRowCount, &QToolButton::clicked,
        this, &SessionProperties::deduceBlockRowCount);

    connect(mScriptProperties->fileNew, &QToolButton::clicked,
        [this]() { saveCurrentItemFileAs(FileDialog::ScriptExtensions); });
    connect(mScriptProperties->fileBrowse, &QToolButton::clicked,
        [this]() { openCurrentItemFile(FileDialog::ScriptExtensions); });
    connect(mScriptProperties->file, &ReferenceComboBox::listRequired,
        [this]() { return getFileNames(Item::Type::Script, true); });
    connect(mScriptProperties->file, &ReferenceComboBox::currentDataChanged,
        [this](QVariant data) { updateScriptWidgets(!data.toString().isEmpty()); });

    connect(mAttributeProperties->field, &ReferenceComboBox::listRequired,
        [this]() { return getItemIds(Item::Type::Field); });

    for (auto comboBox : { mShaderProperties->file, mBufferProperties->file, mScriptProperties->file })
        connect(comboBox, &ReferenceComboBox::textRequired,
            [](auto data) { return FileDialog::getFileTitle(data.toString()); });

    for (auto comboBox : { mAttributeProperties->field })
        connect(comboBox, &ReferenceComboBox::textRequired,
            [this](QVariant data) { return getItemName(data.toInt()); });

    setCurrentModelIndex({ });
    fillComboBoxes();
}

SessionProperties::~SessionProperties() = default;

void SessionProperties::fillComboBoxes()
{
    fillComboBox<Field::DataType>(mFieldProperties->type);
    fillComboBox<Target::FrontFace>(mTargetProperties->frontFace);
    fillComboBox<Target::CullMode>(mTargetProperties->cullMode);
    fillComboBox<Target::PolygonMode>(mTargetProperties->polygonMode);
    fillComboBox<Target::LogicOperation>(mTargetProperties->logicOperation);
    fillComboBox<Shader::ShaderType>(mShaderProperties->type);
    fillComboBox<Script::ExecuteOn>(mScriptProperties->executeOn);
}

QVariantList SessionProperties::getFileNames(Item::Type type, bool addNull) const
{
    auto result = QVariantList();
    if (addNull)
        result += "";

    const auto append = [&](const QStringList fileNames) {
        for (const auto &fileName : fileNames)
            if (!result.contains(fileName))
                result.append(fileName);
    };
    switch (type) {
        case Item::Type::Shader:
        case Item::Type::Script:
            append(Singletons::editorManager().getSourceFileNames());
            break;

        case Item::Type::Buffer:
            append(Singletons::editorManager().getBinaryFileNames());
            break;

        case Item::Type::Texture:
            append(Singletons::editorManager().getImageFileNames());
            break;

        default:
            break;
    }

    mModel.forEachItem([&](const Item &item) {
        if (item.type == type) {
            const auto &fileName = static_cast<const FileItem&>(item).fileName;
            if (!result.contains(fileName))
                result.append(fileName);
        }
    });
    return result;
}

QString SessionProperties::getItemName(ItemId itemId) const
{
    return mModel.getFullItemName(itemId);
}

QVariantList SessionProperties::getItemIds(Item::Type type, bool addNull) const
{
    auto result = QVariantList();
    if (addNull)
        result += 0;

    mModel.forEachItemScoped(currentModelIndex(), [&](const Item &item) {
        if (item.type == type)
            result.append(item.id);
    });
    return result;
}

void SessionProperties::updateModel()
{
    mMapper->submit();
}

QModelIndex SessionProperties::currentModelIndex(int column) const
{
    return mModel.index(mMapper->currentIndex(), column, mMapper->rootIndex());
}

void SessionProperties::setCurrentModelIndex(const QModelIndex &index)
{
    mMapper->submit();
    mMapper->clearMapping();

    if (!index.isValid()) {
        mStack->setCurrentIndex(mStack->count() - 1);
        setVisible(false);
        return;
    }
    setVisible(true);

    const auto map = [&](QWidget *control, SessionModel::ColumnType column) {
        mMapper->addMapping(control, column);
    };

    switch (mModel.getItemType(index)) {
        case Item::Type::Group:
            map(mGroupProperties->inlineScope, SessionModel::GroupInlineScope);
            break;

        case Item::Type::Buffer:
            map(mBufferProperties->file, SessionModel::FileName);
            break;

        case Item::Type::Block:
            map(mBlockProperties->offset, SessionModel::BlockOffset);
            map(mBlockProperties->rowCount, SessionModel::BlockRowCount);
            updateBlockWidgets(index);
            break;

        case Item::Type::Field:
            map(mFieldProperties->type, SessionModel::FieldDataType);
            map(mFieldProperties->count, SessionModel::FieldCount);
            map(mFieldProperties->padding, SessionModel::FieldPadding);
            break;

        case Item::Type::Texture:
            mTextureProperties->addMappings(*mMapper);
            break;

        case Item::Type::Program:
            break;

        case Item::Type::Shader:
            map(mShaderProperties->type, SessionModel::ShaderType);
            map(mShaderProperties->file, SessionModel::FileName);
            break;

        case Item::Type::Binding:
            mBindingProperties->addMappings(*mMapper);
            break;

        case Item::Type::Stream:
            break;

        case Item::Type::Attribute:
            map(mAttributeProperties->field, SessionModel::AttributeFieldId);
            map(mAttributeProperties->normalize, SessionModel::AttributeNormalize);
            map(mAttributeProperties->divisor, SessionModel::AttributeDivisor);
            break;

        case Item::Type::Target:
            map(mTargetProperties->width, SessionModel::TargetDefaultWidth);
            map(mTargetProperties->height, SessionModel::TargetDefaultHeight);
            map(mTargetProperties->layers, SessionModel::TargetDefaultLayers);
            map(mTargetProperties->samples, SessionModel::TargetDefaultSamples);
            map(mTargetProperties->frontFace, SessionModel::TargetFrontFace);
            map(mTargetProperties->cullMode, SessionModel::TargetCullMode);
            map(mTargetProperties->polygonMode, SessionModel::TargetPolygonMode);
            map(mTargetProperties->logicOperation, SessionModel::TargetLogicOperation);
            map(mTargetProperties->blendConstant, SessionModel::TargetBlendConstant);
            updateTargetWidgets(index);
            break;

        case Item::Type::Attachment:
            mAttachmentProperties->addMappings(*mMapper);
            break;

        case Item::Type::Call:
            mCallProperties->addMappings(*mMapper);
            break;

        case Item::Type::Script:
            map(mScriptProperties->file, SessionModel::FileName);
            map(mScriptProperties->executeOn, SessionModel::ScriptExecuteOn);
            map(mScriptProperties->expression, SessionModel::ScriptExpression);
            updateScriptWidgets(index);
            break;
    }

    mMapper->setRootIndex(mModel.parent(index));
    mMapper->setCurrentModelIndex(index);

    mStack->setCurrentIndex(static_cast<int>(mModel.getItemType(index)));
}

IEditor* SessionProperties::openEditor(const FileItem &fileItem)
{
    if (fileItem.fileName.isEmpty()) {
        const auto fileName = FileDialog::generateNextUntitledFileName(fileItem.name);
        mModel.setData(mModel.getIndex(&fileItem, SessionModel::FileName), fileName);
    }

    auto &editors = Singletons::editorManager();
    switch (fileItem.type) {
        case Item::Type::Texture:
            if (auto editor = editors.openTextureEditor(fileItem.fileName))
                return editor;
            return editors.openNewTextureEditor(fileItem.fileName);

        case Item::Type::Shader:
        case Item::Type::Script:
            if (auto editor = editors.openSourceEditor(fileItem.fileName))
                return editor;
            return editors.openNewSourceEditor(fileItem.fileName);

        case Item::Type::Buffer:
            if (auto editor = editors.openBinaryEditor(fileItem.fileName))
                return editor;
            return editors.openNewBinaryEditor(fileItem.fileName);

        default:
            return nullptr;
    }
}

IEditor* SessionProperties::openItemEditor(const QModelIndex &index)
{
    const auto& item = mModel.getItem(index);

    if (auto script = castItem<Script>(item))
        if (!script->expression.isEmpty())
            return nullptr;

    auto editor = std::add_pointer_t<IEditor>();
    if (auto program = castItem<Program>(item)) {
        for (auto item : program->items)
            if (auto shader = castItem<Shader>(item))
                editor = openEditor(*shader);
        return editor;
    }

    if (auto block = castItem<Block>(item)) {
        const auto &buffer = *static_cast<Buffer*>(block->parent);
        editor = openEditor(buffer);
        if (auto binaryEditor = static_cast<BinaryEditor*>(editor))
            for (auto i = 0; i < buffer.items.size(); ++i)
                if (buffer.items[i] == block) {
                    binaryEditor->setCurrentBlockIndex(i);
                    break;
                }
    }
    else if (const auto fileItem = castItem<FileItem>(item)) {
        editor = openEditor(*fileItem);
    }
    if (!editor)
        return nullptr;

    if (auto script = castItem<Script>(item)) {
        editor->setSourceType(SourceType::JavaScript);
    }
    else if (auto shader = castItem<Shader>(item)) {
        static const auto sMapping = QMap<Shader::ShaderType, SourceType>{
            { Shader::ShaderType::Vertex, SourceType::VertexShader },
            { Shader::ShaderType::Fragment, SourceType::FragmentShader },
            { Shader::ShaderType::Geometry, SourceType::GeometryShader },
            { Shader::ShaderType::TessellationControl, SourceType::TessellationControl },
            { Shader::ShaderType::TessellationEvaluation, SourceType::TessellationEvaluation },
            { Shader::ShaderType::Compute, SourceType::ComputeShader },
        };
        auto sourceType = sMapping[shader->shaderType];
        if (sourceType != SourceType::None)
            editor->setSourceType(sourceType);
    }
    else {
        Singletons::synchronizeLogic().updateEditor(item.id, true);
    }
    return editor;
}

QString SessionProperties::currentItemName() const
{
    return mModel.data(currentModelIndex(SessionModel::Name)).toString();
}

QString SessionProperties::currentItemFileName() const
{
    return mModel.data(currentModelIndex(SessionModel::FileName)).toString();
}

void SessionProperties::setCurrentItemFile(const QString &fileName)
{
    mModel.setData(currentModelIndex(SessionModel::FileName), fileName);
}

void SessionProperties::saveCurrentItemFileAs(FileDialog::Options options)
{
    options |= FileDialog::Saving;
    if (Singletons::fileDialog().exec(options, currentItemFileName())) {
        auto fileName = Singletons::fileDialog().fileName();
        if (auto editor = openItemEditor(currentModelIndex())) {
            editor->setFileName(fileName);
            setCurrentItemFile(fileName);
            editor->save();
        }
    }
}

void SessionProperties::openCurrentItemFile(FileDialog::Options options)
{
    if (Singletons::fileDialog().exec(options))
        setCurrentItemFile(Singletons::fileDialog().fileName());
}

void SessionProperties::updateBlockWidgets(const QModelIndex &index)
{
    auto stride = 0;
    auto isFirstBlock = true;
    auto isLastBlock = true;
    auto hasFile = false;
    if (auto block = mModel.item<Block>(index)) {
        const auto &buffer = *static_cast<Buffer*>(block->parent);
        stride = getBlockStride(*block);
        isFirstBlock = (buffer.items.first() == block);
        isLastBlock = (buffer.items.last() == block);
        if (!FileDialog::isEmptyOrUntitled(buffer.fileName))
            hasFile = true;
    }

    auto &ui = *mBlockProperties;
    ui.stride->setText(QString::number(stride));
    ui.deduceOffset->setVisible(!isFirstBlock);
    ui.deduceRowCount->setVisible(hasFile && isLastBlock);
}

void SessionProperties::updateTargetWidgets(const QModelIndex &index)
{
    const auto target = mModel.item<Target>(index);
    const auto hasAttachments = !target->items.empty();

    auto &ui = *mTargetProperties;
    setFormVisibility(ui.formLayout, ui.labelWidth, ui.width, !hasAttachments);
    setFormVisibility(ui.formLayout, ui.labelHeight, ui.height, !hasAttachments);
    setFormVisibility(ui.formLayout, ui.labelLayers, ui.layers, !hasAttachments);
    setFormVisibility(ui.formLayout, ui.labelSamples, ui.samples, !hasAttachments);
    setFormVisibility(ui.formLayout, ui.labelFrontFace, ui.frontFace, true);
    setFormVisibility(ui.formLayout, ui.labelCullMode, ui.cullMode, true);
    setFormVisibility(ui.formLayout, ui.labelPolygonMode, ui.polygonMode, true);
    setFormVisibility(ui.formLayout, ui.labelLogicOperation, ui.logicOperation, hasAttachments);
    setFormVisibility(ui.formLayout, ui.labelBlendConstant, ui.blendConstant, hasAttachments);
}

void SessionProperties::updateScriptWidgets(const QModelIndex &index)
{
    auto hasFile = false;
    if (auto script = mModel.item<Script>(index))
        hasFile = !script->fileName.isEmpty();
    updateScriptWidgets(hasFile);
}

void SessionProperties::updateScriptWidgets(bool hasFile)
{
    auto &ui = *mScriptProperties;
    setFormVisibility(ui.formLayout, ui.labelExpression, ui.expression, !hasFile);
}

void SessionProperties::deduceBlockOffset()
{
    auto ok = true;
    auto toInt = [&](const QString &expression) {
        return (ok ? expression.toInt(&ok) : 0);
    };

    const auto &block = *mModel.item<Block>(currentModelIndex());
    auto offset = 0;
    for (auto item : qAsConst(block.parent->items)) {
        if (item == &block)
            break;

        const auto &prevBlock = *static_cast<const Block*>(item);
        offset = std::max(offset,
            toInt(prevBlock.offset) + getBlockStride(prevBlock) * toInt(prevBlock.rowCount));
    }

    if (ok)
        mModel.setData(mModel.getIndex(currentModelIndex(),
            SessionModel::BlockOffset), offset);
}

void SessionProperties::deduceBlockRowCount()
{
    const auto &block = *mModel.item<Block>(currentModelIndex());
    auto ok = true;
    const auto blockOffset = block.offset.toInt(&ok);
    if (ok) {
        const auto &buffer = *static_cast<const Buffer*>(block.parent);
        auto binary = QByteArray();
        if (Singletons::fileCache().getBinary(buffer.fileName, &binary))
            mModel.setData(mModel.getIndex(currentModelIndex(), SessionModel::BlockRowCount),
                (binary.size() - blockOffset) / getBlockStride(block));
    }
}
