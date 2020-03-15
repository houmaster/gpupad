#include "GLTexture.h"
#include <QOpenGLPixelTransferOptions>
#include <cmath>

GLTexture::GLTexture(const Texture &texture)
    : mItemId(texture.id)
    , mFileName(texture.fileName)
    , mTarget(texture.target)
    , mFormat(texture.format)
    , mWidth(texture.width)
    , mHeight(texture.height)
    , mDepth(texture.depth)
    , mLayers(texture.layers)
    , mSamples(texture.samples)
    , mKind(getKind(texture))
{
    if (mKind.dimensions < 2)
        mHeight = 1;
    if (mKind.dimensions < 3)
        mDepth = 1;
    if (!mKind.array)
        mLayers = 1;

    mUsedItems += texture.id;
}

bool GLTexture::operator==(const GLTexture &rhs) const
{
    return std::tie(mFileName, mTarget, mFormat, mWidth, mHeight, mDepth, mLayers, mSamples) ==
           std::tie(rhs.mFileName, rhs.mTarget, rhs.mFormat, rhs.mWidth, rhs.mHeight, rhs.mDepth, rhs.mLayers, rhs.mSamples);
}

GLuint GLTexture::getReadOnlyTextureId()
{
    reload();
    createTexture();
    upload();
    return mTextureObject;
}

GLuint GLTexture::getReadWriteTextureId()
{
    reload();
    createTexture();
    upload();
    mDeviceCopyModified = true;
    mMipmapsInvalidated = true;
    return mTextureObject;
}

bool GLTexture::clear(std::array<double, 4> color, double depth, int stencil)
{
    auto &gl = GLContext::currentContext();
    auto fbo = createFramebuffer(getReadWriteTextureId(), 0);
    if (!fbo)
        return false;

    gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.glColorMask(true, true, true, true);
    gl.glDepthMask(true);
    gl.glStencilMask(0xFF);

    auto succeeded = true;
    if (mKind.depth && mKind.stencil) {
        gl.glClearBufferfi(GL_DEPTH_STENCIL, 0,
            static_cast<float>(depth), stencil);
    }
    else if (mKind.depth) {
        const auto d = static_cast<float>(depth);
        gl.glClearBufferfv(GL_DEPTH, 0, &d);
    }
    else if (mKind.stencil) {
        gl.glClearBufferiv(GL_STENCIL, 0, &stencil);
    }
    else {
        const auto srgbToLinear = [](auto value) {
            if (value <= 0.0404482362771082)
                return value / 12.92;
            return std::pow((value + 0.055) / 1.055, 2.4);
        };
        const auto multiplyRGBA = [&](auto factor) {
            for (auto &c : color)
                c *= factor;
        };

        const auto dataType = getTextureDataType(mFormat);
        switch (dataType) {
            case TextureDataType::Normalized_sRGB:
            case TextureDataType::Float:
                for (auto i = 0u; i < 3; ++i)
                    color[i] = srgbToLinear(color[i]);
                break;
            case TextureDataType::Int8: multiplyRGBA(0x7F); break;
            case TextureDataType::Int16: multiplyRGBA(0x7FFF); break;
            case TextureDataType::Int32: multiplyRGBA(0x7FFFFFFF); break;
            case TextureDataType::Uint8: multiplyRGBA(0xFF); break;
            case TextureDataType::Uint16: multiplyRGBA(0xFFFF); break;
            case TextureDataType::Uint32: multiplyRGBA(0xFFFFFFFF); break;
            case TextureDataType::Uint_10_10_10_2:
                for (auto i = 0u; i < 3; ++i)
                    color[i] *= 1024.0;
                color[3] *= 4.0;
                break;
            default:
                break;
        }

        switch (dataType) {
            case TextureDataType::Normalized:
            case TextureDataType::Normalized_sRGB:
            case TextureDataType::Float: {
                const auto values = std::array<float, 4>{
                    static_cast<float>(color[0]),
                    static_cast<float>(color[1]),
                    static_cast<float>(color[2]),
                    static_cast<float>(color[3])
                };
                gl.glClearBufferfv(GL_COLOR, 0, values.data());
                break;
            }

            case TextureDataType::Uint8:
            case TextureDataType::Uint16:
            case TextureDataType::Uint32:
            case TextureDataType::Uint_10_10_10_2: {
                const auto values = std::array<GLuint, 4>{
                    static_cast<GLuint>(color[0]),
                    static_cast<GLuint>(color[1]),
                    static_cast<GLuint>(color[2]),
                    static_cast<GLuint>(color[3])
                };
                gl.glClearBufferuiv(GL_COLOR, 0, values.data());
                break;
            }

            case TextureDataType::Int8:
            case TextureDataType::Int16:
            case TextureDataType::Int32: {
                const auto values = std::array<GLint, 4>{
                    static_cast<GLint>(color[0]),
                    static_cast<GLint>(color[1]),
                    static_cast<GLint>(color[2]),
                    static_cast<GLint>(color[3])
                };
                gl.glClearBufferiv(GL_COLOR, 0, values.data());
                break;
            }

            default:
                succeeded = false;
                break;
        }
    }
    gl.glBindFramebuffer(GL_FRAMEBUFFER, GL_NONE);
    return succeeded;
}

bool GLTexture::copy(GLTexture &source)
{
    return copyTexture(source.getReadOnlyTextureId(),
        getReadWriteTextureId(), 0);
}

bool GLTexture::updateMipmaps()
{
    Q_ASSERT(glGetError() == GL_NO_ERROR);
    if (std::exchange(mMipmapsInvalidated, false)) {
        auto &gl = GLContext::currentContext();
        gl.glBindTexture(target(), getReadWriteTextureId());
        gl.glGenerateMipmap(target());
    }
    return (glGetError() == GL_NO_ERROR);
}

void GLTexture::reload()
{
    auto fileData = TextureData{ };
    if (!FileDialog::isEmptyOrUntitled(mFileName))
        if (!Singletons::fileCache().getTexture(mFileName, &fileData))
            mMessages += MessageList::insert(mItemId,
                MessageType::LoadingFileFailed, mFileName);

    // reload file as long as dimensions match (format is ignored)
    const auto sameDimensions = [&](const TextureData &data) {
        return (mTarget == data.target() &&
                mWidth == data.width() &&
                mHeight == data.height() &&
                mDepth == data.depth() &&
                mLayers == data.layers());
    };
    if (sameDimensions(fileData)) {
        mSystemCopyModified |= !mData.isSharedWith(fileData);
        mData = fileData;
    }
    else if (mData.isNull()) {
        if (!mData.create(mTarget, mFormat, mWidth, mHeight, mDepth, mLayers, mSamples)) {
            mData.create(mTarget, Texture::Format::RGBA8_UNorm, 1, 1, 1, 1, 1);
            mMessages += MessageList::insert(mItemId,
                MessageType::CreatingTextureFailed);
        }
        mSystemCopyModified |= true;
    }
}

void GLTexture::createTexture()
{
    if (mTextureObject)
        return;

    auto &gl = GLContext::currentContext();
    const auto createTexture = [&]() {
        auto texture = GLuint{};
        gl.glGenTextures(1, &texture);
        return texture;
    };
    const auto freeTexture = [](GLuint texture) {
        auto &gl = GLContext::currentContext();
        gl.glDeleteTextures(1, &texture);
    };

    mTextureObject = GLObject(createTexture(), freeTexture);
}

void GLTexture::upload()
{
    if (!mSystemCopyModified)
        return;

    if (!mData.upload(mTextureObject, mFormat)) {
        mMessages += MessageList::insert(
            mItemId, MessageType::UploadingImageFailed);
        return;
    }
    mSystemCopyModified = mDeviceCopyModified = false;
}

bool GLTexture::download()
{
    if (!mDeviceCopyModified)
        return false;

    if (!mData.download(mTextureObject)) {
        mMessages += MessageList::insert(
            mItemId, MessageType::DownloadingImageFailed);
        return false;
    }
    mSystemCopyModified = mDeviceCopyModified = false;
    return true;
}

GLObject GLTexture::createFramebuffer(GLuint textureId, int level) const
{
    auto &gl = GLContext::currentContext();
    const auto createFBO = [&]() {
        auto fbo = GLuint{ };
        gl.glGenFramebuffers(1, &fbo);
        return fbo;
    };
    const auto freeFBO = [](GLuint fbo) {
        auto &gl = GLContext::currentContext();
        gl.glDeleteFramebuffers(1, &fbo);
    };

    auto attachment = GLenum{ GL_COLOR_ATTACHMENT0 };
    if (mKind.depth && mKind.stencil)
        attachment = GL_DEPTH_STENCIL_ATTACHMENT;
    else if (mKind.depth)
        attachment = GL_DEPTH_ATTACHMENT;
    else if (mKind.stencil)
        attachment = GL_STENCIL_ATTACHMENT;

    auto fbo = GLObject(createFBO(), freeFBO);
    gl.glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    gl.glFramebufferTexture(GL_FRAMEBUFFER, attachment, textureId, level);
    auto status = gl.glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE)
        fbo.reset();
    return fbo;
}

bool GLTexture::copyTexture(GLuint sourceTextureId,
    GLuint destTextureId, int level)
{
    auto &gl = GLContext::currentContext();
    const auto sourceFbo = createFramebuffer(sourceTextureId, level);
    const auto destFbo = createFramebuffer(destTextureId, level);
    if (!sourceFbo || !destFbo)
        return false;

    const auto width = mData.getLevelWidth(level);
    const auto height = mData.getLevelHeight(level);
    auto blitMask = GLbitfield{ GL_COLOR_BUFFER_BIT };
    if (mKind.depth && mKind.stencil)
        blitMask = GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT;
    else if (mKind.depth)
        blitMask = GL_DEPTH_BUFFER_BIT;
    else if (mKind.stencil)
        blitMask = GL_STENCIL_BUFFER_BIT;

    gl.glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFbo);
    gl.glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destFbo);
    gl.glBlitFramebuffer(0, 0, width, height, 0, 0,
        width, height, blitMask, GL_NEAREST);
    return true;
}
