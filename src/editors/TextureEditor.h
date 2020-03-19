#ifndef TEXTUREEDITOR_H
#define TEXTUREEDITOR_H

#include "IEditor.h"
#include "TextureData.h"
#include <QGraphicsView>
#include <QOpenGLTexture>

class TextureItem;

class TextureEditor : public QGraphicsView, public IEditor
{
    Q_OBJECT
public:
    static bool load(const QString &fileName, TextureData *texture);

    explicit TextureEditor(QString fileName, QWidget *parent = nullptr);
    ~TextureEditor() override;

    QList<QMetaObject::Connection>
        connectEditActions(const EditActions &actions) override;
    QString fileName() const override { return mFileName; }
    void setFileName(QString fileName) override;
    bool load() override;
    bool reload() override;
    bool save() override;
    int tabifyGroup() override { return 1; }
    bool isModified() const { return mModified; }
    void replace(TextureData texture, bool invalidateFileCache = true);
    void updatePreviewTexture(QOpenGLTexture::Target target, GLuint textureId);
    const TextureData &texture() const { return mTexture; }

signals:
    void modificationChanged(bool modified);
    void fileNameChanged(const QString &fileName);

protected:
    void wheelEvent(QWheelEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    void updateMousePosition(QMouseEvent *event);
    void setBounds(QRect bounds);
    void setZoom(int zoom);
    QTransform getZoomTransform() const;
    void updateBackground();
    void setModified(bool modified);

    QString mFileName;
    bool mModified{ };
    TextureData mTexture;
    bool mPan{ };
    QRect mBounds{ };
    int mZoom{ };
    int mPanStartX{ };
    int mPanStartY{ };
    QGraphicsPathItem *mBorder{ };
    TextureItem *mTextureItem{ };
    QGraphicsPixmapItem *mPixmapItem{ };
};

#endif // TEXTUREEDITOR_H