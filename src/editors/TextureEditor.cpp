#include "TextureEditor.h"
#include "TextureEditorToolBar.h"
#include "FileDialog.h"
#include "Singletons.h"
#include "FileCache.h"
#include "SynchronizeLogic.h"
#include "Settings.h"
#include "render/GLContext.h"
#include "session/Item.h"
#include "TextureItem.h"
#include <QAction>
#include <QApplication>
#include <QOpenGLWidget>
#include <QWheelEvent>
#include <QScrollBar>
#include <cstring>

bool createFromRaw(const QByteArray &binary,
    const TextureEditor::RawFormat &r, TextureData *texture)
{
    if (!texture->create(r.target, r.format, r.width, r.height,
                         r.depth, r.layers, r.samples))
        return false;

    if (!binary.isEmpty()) {
        std::memcpy(texture->getWriteonlyData(0, 0, 0), binary.constData(),
            static_cast<size_t>(std::min(static_cast<int>(binary.size()), texture->getLevelSize(0))));
    }
    else {
        texture->clear();
    }
    return true;
}

TextureEditor::TextureEditor(QString fileName, 
      TextureEditorToolBar *editorToolBar, 
      QWidget *parent)
    : QGraphicsView(parent)
    , mEditorToolBar(*editorToolBar)
    , mFileName(fileName)
{
    setTransformationAnchor(AnchorUnderMouse);

    setViewport(new QOpenGLWidget(this));
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

    setScene(new QGraphicsScene(this));

    auto pen = QPen();
    auto color = QColor(Qt::gray);
    pen.setWidth(1);
    pen.setCosmetic(true);
    pen.setColor(color);
    mBorder = new QGraphicsPathItem();
    mBorder->setPen(pen);
    mBorder->setBrush(Qt::NoBrush);
    mBorder->setZValue(1);
    scene()->addItem(mBorder);

    mTextureItem = new TextureItem();
    scene()->addItem(mTextureItem);

    connect(&Singletons::settings(), &Settings::darkThemeChanged,
        this, &TextureEditor::updateBackground);
    updateBackground();
}

TextureEditor::~TextureEditor() 
{
    auto texture = mTextureItem->resetTexture();
    auto glWidget = qobject_cast<QOpenGLWidget*>(viewport());
    if (auto context = glWidget->context())
        if (auto surface = context->surface())
            if (context->makeCurrent(surface)) {
                auto& gl = *context->functions();
                gl.glDeleteTextures(1, &texture);
                mTextureItem->releaseGL();
                context->doneCurrent();
            }
    delete scene();

    if (isModified())
        Singletons::fileCache().handleEditorFileChanged(mFileName);
}

QList<QMetaObject::Connection> TextureEditor::connectEditActions(
        const EditActions &actions)
{
    auto c = QList<QMetaObject::Connection>();

    actions.windowFileName->setText(fileName());
    actions.windowFileName->setEnabled(isModified());
    c += connect(this, &TextureEditor::fileNameChanged,
                 actions.windowFileName, &QAction::setText);
    c += connect(this, &TextureEditor::modificationChanged,
                 actions.windowFileName, &QAction::setEnabled);

    updateEditorToolBar();

    c += connect(&mEditorToolBar, &TextureEditorToolBar::levelChanged,
        mTextureItem, &TextureItem::setLevel);
    c += connect(&mEditorToolBar, &TextureEditorToolBar::layerChanged,
        mTextureItem, &TextureItem::setLayer);
    c += connect(&mEditorToolBar, &TextureEditorToolBar::sampleChanged,
        mTextureItem, &TextureItem::setSample);
    c += connect(&mEditorToolBar, &TextureEditorToolBar::faceChanged,
        mTextureItem, &TextureItem::setFace);
    c += connect(&mEditorToolBar, &TextureEditorToolBar::filterChanged,
        mTextureItem, &TextureItem::setMagnifyLinear);
    c += connect(&mEditorToolBar, &TextureEditorToolBar::flipVerticallyChanged,
        mTextureItem, &TextureItem::setFlipVertically);

    return c;
}

void TextureEditor::updateEditorToolBar() 
{
    mEditorToolBar.setMaxLevel(std::max(mTexture.levels() - 1, 0));
    mEditorToolBar.setLevel(mTextureItem->level());

    mEditorToolBar.setMaxLayer(
        std::max(mTexture.layers() - 1, 0), mTexture.depth());
    mEditorToolBar.setLayer(mTextureItem->layer());

    // disabled for now, since all samples are identical after download
    mEditorToolBar.setMaxSample(0); // std::max(mTexture.samples() - 1, 0)
    mEditorToolBar.setSample(mTextureItem->sample());

    mEditorToolBar.setMaxFace(std::max(mTexture.faces() - 1, 0));
    mEditorToolBar.setFace(mTextureItem->face());

    mEditorToolBar.setCanFilter(!mTexture.isMultisample());
    mEditorToolBar.setFilter(mTextureItem->magnifyLinear());

    mEditorToolBar.setCanFlipVertically(
      mTexture.dimensions() == 2 || mTexture.isCubemap());
    mEditorToolBar.setFlipVertically(mTextureItem->flipVertically());
}

void TextureEditor::setFileName(QString fileName)
{
    if (mFileName != fileName) {
        mFileName = fileName;
        Q_EMIT fileNameChanged(mFileName);
    }
}

void TextureEditor::setRawFormat(RawFormat rawFormat)
{
    if (!std::memcmp(&mRawFormat, &rawFormat, sizeof(RawFormat)))
        return;

    mRawFormat = rawFormat;
    if (mTexture.isNull() || mIsRaw)
        load();
}

bool TextureEditor::load()
{
    auto texture = TextureData();
    if (!Singletons::fileCache().getTexture(mFileName, true, &texture)) {
        auto binary = QByteArray();
        if (!Singletons::fileCache().getBinary(mFileName, &binary))
            return false;
        if (!createFromRaw(binary, mRawFormat, &texture))
            return false;
        mIsRaw = true;
    }

    replace(texture);
    setModified(false);
    return true;
}

bool TextureEditor::save()
{
    if (!mTexture.save(fileName(), !mTextureItem->flipVertically()))
        return false;

    setModified(false);
    return true;
}

void TextureEditor::replace(TextureData texture, bool emitFileChanged)
{
    if (texture == mTexture)
        return;

    mTextureItem->setImage(texture);
    setBounds(mTextureItem->boundingRect().toRect());
    if (mTexture.isNull())
        centerOn(mTextureItem->boundingRect().topLeft());
    mTexture = texture;

    if (!FileDialog::isEmptyOrUntitled(mFileName))
        setModified(true);

    Singletons::fileCache().handleEditorFileChanged(mFileName, emitFileChanged);

    if (qApp->focusWidget() == this)
      updateEditorToolBar();
}

void TextureEditor::updatePreviewTexture(
    QOpenGLTexture::Target target, GLuint textureId)
{
    mTextureItem->setPreviewTexture(target, textureId);
}

void TextureEditor::setModified(bool modified)
{
    if (mModified != modified) {
        mModified = modified;
        Q_EMIT modificationChanged(modified);
    }
}

void TextureEditor::wheelEvent(QWheelEvent *event)
{
    setFocus();

    if (!event->modifiers()) {
        const auto min = -3;
        const auto max = 4;
        auto delta = (event->angleDelta().y() > 0 ? 1 : -1);
        setZoom(std::max(min, std::min(mZoom + delta, max)));
        return;
    }

    event->setModifiers(event->modifiers() & ~(Qt::ShiftModifier | Qt::ControlModifier));
    QGraphicsView::wheelEvent(event);
}

void TextureEditor::mouseDoubleClickEvent(QMouseEvent *event)
{
    setZoom(0);
    QGraphicsView::mouseDoubleClickEvent(event);
}

void TextureEditor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        mPan = true;
        mPanStartX = event->x();
        mPanStartY = event->y();
        setCursor(Qt::ClosedHandCursor);
        return;
    }

    updateMousePosition(event);

    QGraphicsView::mousePressEvent(event);
}

void TextureEditor::mouseMoveEvent(QMouseEvent *event)
{
    if (mPan) {
        horizontalScrollBar()->setValue(
            horizontalScrollBar()->value() - (event->x() - mPanStartX));
        verticalScrollBar()->setValue(
            verticalScrollBar()->value() - (event->y() - mPanStartY));
        mPanStartX = event->x();
        mPanStartY = event->y();
        return;
    }

    updateMousePosition(event);

    QGraphicsView::mouseMoveEvent(event);
}

void TextureEditor::updateMousePosition(QMouseEvent *event)
{
    auto pos = mTextureItem->mapFromScene(
        mapToScene(event->pos() - QPoint(1, 1))) - mTextureItem->boundingRect().topLeft();

    if (!mTextureItem->flipVertically())
        pos.setY(mTextureItem->boundingRect().height() - pos.y());

    const auto fragmentCoord =
        QPointF(qRound(pos.x() - 0.5) + 0.5, qRound(pos.y() - 0.5) + 0.5);
    Singletons::synchronizeLogic().setMousePosition(fragmentCoord);
}

void TextureEditor::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        mPan = false;
        setCursor(Qt::ArrowCursor);
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void TextureEditor::setBounds(QRect bounds)
{
    if (bounds == mBounds)
        return;
    mBounds = bounds;

    auto inside = QPainterPath();
    inside.addRect(bounds);
    mBorder->setPath(inside);

    const auto margin = 15;
    bounds.adjust(-margin, -margin, margin, margin);
    setSceneRect(bounds);
}

void TextureEditor::setZoom(int zoom)
{
    if (mZoom == zoom)
        return;

    mZoom = zoom;
    setTransform(getZoomTransform());

    updateBackground();
}

QTransform TextureEditor::getZoomTransform() const
{
    const auto scale = (mZoom < 0 ?
        1.0 / (1 << (-mZoom)) :
        1 << (mZoom));
    return QTransform().scale(scale, scale);
}

void TextureEditor::updateBackground()
{
    const auto color1 = QPalette().window().color().darker(115);
    const auto color2 = QPalette().window().color().darker(110);

    QPixmap pm(2, 2);
    QPainter pmp(&pm);
    pmp.fillRect(0, 0, 1, 1, color1);
    pmp.fillRect(1, 1, 1, 1, color1);
    pmp.fillRect(0, 1, 1, 1, color2);
    pmp.fillRect(1, 0, 1, 1, color2);
    pmp.end();

    auto brush = QBrush(pm);
    brush.setTransform(getZoomTransform().inverted().translate(0, 1).scale(16, 16));
    setBackgroundBrush(brush);
}
