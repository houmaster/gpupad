#include "ImageEditor.h"
#include "FileDialog.h"
#include <QGraphicsItem>
#include <QAction>
#include <QScrollBar>
#include <QWheelEvent>
#include <QOpenGLWidget>
#include <QOpenGLTexture>
#include <QOpenGLShader>
#include <QOpenGLFunctions_3_3_Core>

namespace {

    class ImageItem : public QGraphicsItem
    {
    static constexpr auto vertexShader = R"(
    #version 330

    uniform mat4 uTransform;
    uniform vec2 uSize;
    out vec2 vTexCoord;

    const vec2 data[4]= vec2[] (
    vec2(-0.5,  0.5),
    vec2(-0.5, -0.5),
    vec2( 0.5,  0.5),
    vec2( 0.5, -0.5)
    );

    void main() {
    vec2 pos = data[gl_VertexID];
    vTexCoord = pos + 0.5;
    gl_Position = uTransform * vec4(pos * uSize, 0.0, 1.0);
    }
    )";
    static constexpr auto fragmentShader = R"(
    #version 330

    uniform sampler2D uTexture;
    in vec2 vTexCoord;
    out vec4 oColor;

    void main() {
    oColor = texture(uTexture, vTexCoord);
    }
    )";

    private:
        QOpenGLFunctions_3_3_Core mGL;
        QScopedPointer<QOpenGLShaderProgram> mProgram;
        QOpenGLTexture *mTexture{ };
        QMetaObject::Connection mTextureContextConnection;
        QImage mUploadImage;
        QRect mBoundingRect;
        bool mMagnifyLinear{ };

    public:
        void setImage(QImage image)
        {
            prepareGeometryChange();
            mUploadImage = image;
            const auto w = image.width();
            const auto h = image.height();
            mBoundingRect = { -w / 2, -h / 2, w, h };
            update();
        }

        void setMagnifyLinear(bool magnifyLinear)
        {
            mMagnifyLinear = magnifyLinear;
        }

        QRectF boundingRect() const override
        {
            return mBoundingRect;
        }

        void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override
        {
            if (!mProgram)
                initialize();

            if (!mUploadImage.isNull()) {
                // delete previous version in current thread
                if (mTexture) {
                    QObject::disconnect(mTextureContextConnection);
                    delete mTexture;
                }

                mTexture = new QOpenGLTexture(mUploadImage);
                mUploadImage = { };

                // delete last version together with context
                auto context = QOpenGLContext::currentContext();
                auto surface = context->surface();
                mTextureContextConnection = QObject::connect(
                    context, &QOpenGLContext::aboutToBeDestroyed,
                    [context, surface, texture = mTexture]() mutable {
                        context->makeCurrent(surface);
                        delete texture;
                        context->doneCurrent();
                    });
            }

            if (mTexture) {
                painter->beginNativePainting();

                auto proj = QMatrix4x4{ };
                mGL.glGetFloatv(GL_PROJECTION_MATRIX, proj.data());

                auto model = QMatrix4x4{ };
                mGL.glGetFloatv(GL_MODELVIEW_MATRIX, model.data());

                mProgram->bind();
                mProgram->setUniformValue("uTexture", 0);
                mProgram->setUniformValue("uTransform", proj * model);
                mProgram->setUniformValue("uSize", mBoundingRect.size());

                mGL.glEnable(GL_TEXTURE_2D);
                mTexture->bind();

                mTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
                mTexture->setMinificationFilter(QOpenGLTexture::LinearMipMapLinear);
                mTexture->setMagnificationFilter(mMagnifyLinear ?
                    QOpenGLTexture::Linear : QOpenGLTexture::Nearest);

                mGL.glEnable(GL_BLEND);
                mGL.glBlendEquation(GL_ADD);
                mGL.glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

                mGL.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

                painter->endNativePainting();
            }
        }

    private:
        void initialize()
        {
            mGL.initializeOpenGLFunctions();

            auto vs = new QOpenGLShader(QOpenGLShader::Vertex);
            vs->compileSourceCode(vertexShader);

            auto fs = new QOpenGLShader(QOpenGLShader::Fragment);
            fs->compileSourceCode(fragmentShader);

            mProgram.reset(new QOpenGLShaderProgram());
            mProgram->addShader(vs);
            mProgram->addShader(fs);
            mProgram->link();
        }
    };
} // namespace

ImageEditor::ImageEditor(QString fileName, QWidget *parent)
    : QGraphicsView(parent)
    , mFileName(fileName)
{
    mImage.fill(Qt::black);
    setTransformationAnchor(AnchorUnderMouse);

    setViewport(new QOpenGLWidget(this));
    setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

    setScene(new QGraphicsScene(this));
    scene()->addItem(new ImageItem());

    replace(QImage(QSize(1, 1), QImage::Format_RGB888), false);
    setZoom(mZoom);
    updateTransform(1.0);
}

QList<QMetaObject::Connection> ImageEditor::connectEditActions(
        const EditActions &actions)
{
    auto c = QList<QMetaObject::Connection>();

    actions.windowFileName->setText(fileName());
    actions.windowFileName->setEnabled(isModified());
    c += connect(this, &ImageEditor::fileNameChanged,
                 actions.windowFileName, &QAction::setText);
    c += connect(this, &ImageEditor::modificationChanged,
                 actions.windowFileName, &QAction::setEnabled);

    return c;
}

void ImageEditor::setFileName(QString fileName)
{
    mFileName = fileName;
    emit fileNameChanged(mFileName);
}

bool ImageEditor::load(const QString &fileName, QImage *image)
{
    if (FileDialog::isEmptyOrUntitled(fileName))
        return false;

    auto file = QImage();
    if (!file.load(fileName))
        return false;

    *image = file;
    return true;
}

bool ImageEditor::load()
{
    auto image = QImage();
    if (!load(mFileName, &image))
        return false;

    replace(image, false);

    setModified(false);
    emit dataChanged();
    return true;
}

bool ImageEditor::save()
{
    if (!mImage.save(fileName()))
        return false;

    setModified(false);
    emit dataChanged();
    return true;
}

void ImageEditor::replace(QImage image, bool emitDataChanged)
{
    if (image.constBits() == mImage.constBits())
        return;

    mImage = image;
    auto& item = static_cast<ImageItem&>(*items().first());
    item.setImage(mImage);
    setBounds(item.boundingRect().toRect());

    if (!FileDialog::isEmptyOrUntitled(mFileName))
        setModified(true);

    if (emitDataChanged)
        emit dataChanged();
}

void ImageEditor::setModified(bool modified)
{
    if (mModified != modified) {
        mModified = modified;
        emit modificationChanged(modified);
    }
}

void ImageEditor::wheelEvent(QWheelEvent *event)
{
    if (!event->modifiers()) {
        const auto min = -3;
        const auto max = 4;
        auto delta = (event->delta() > 0 ? 1 : -1);
        setZoom(std::max(min, std::min(mZoom + delta, max)));
        return;
    }
    QGraphicsView::wheelEvent(event);
}

void ImageEditor::mouseDoubleClickEvent(QMouseEvent *event)
{
    setZoom(0);
    QGraphicsView::mouseDoubleClickEvent(event);
}

void ImageEditor::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MidButton) {
        mPan = true;
        mPanStartX = event->x();
        mPanStartY = event->y();
        setCursor(Qt::ClosedHandCursor);
        return;
    }
    QGraphicsView::mousePressEvent(event);
}

void ImageEditor::mouseMoveEvent(QMouseEvent *event)
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
    QGraphicsView::mouseMoveEvent(event);
}

void ImageEditor::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MidButton) {
        mPan = false;
        setCursor(Qt::ArrowCursor);
        return;
    }
    QGraphicsView::mouseReleaseEvent(event);
}

void ImageEditor::setBounds(QRect bounds)
{
    if (bounds == mBounds)
        return;
    mBounds = bounds;

    const auto margin = 15;
    bounds.adjust(-margin, -margin, margin, margin);
    setSceneRect(bounds);
}

void ImageEditor::setZoom(int zoom)
{
    if (mZoom == zoom)
        return;

    mZoom = zoom;
    auto scale = (mZoom < 0 ?
        1.0 / (1 << (-mZoom)) :
        1 << (mZoom));
    updateTransform(scale);

    auto& item = static_cast<ImageItem&>(*items().first());
    item.setMagnifyLinear(scale <= 4);
}

void ImageEditor::updateTransform(double scale)
{
    auto transform = QTransform().scale(scale, scale);
    setTransform(transform);

    // update background checkers pattern
    QPixmap pm(2, 2);
    QPainter pmp(&pm);
    pmp.fillRect(0, 0, 1, 1, Qt::lightGray);
    pmp.fillRect(1, 1, 1, 1, Qt::lightGray);
    pmp.fillRect(0, 1, 1, 1, Qt::gray);
    pmp.fillRect(1, 0, 1, 1, Qt::gray);
    pmp.end();

    auto brush = QBrush(pm);
    brush.setTransform(transform.inverted().scale(8, 8));
    setBackgroundBrush(brush);
}
