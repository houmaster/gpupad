#include "FileDialog.h"
#include <QFileDialog>
#include <QImageReader>
#include <QMainWindow>
#include <QMap>

namespace {
    const auto UntitledTag = QStringLiteral("/UT/");
    const auto SessionFileExtension = QStringLiteral("gpjs");
    const auto ShaderFileExtensions = { "glsl", "vs", "fs", "gs",
        "vert", "tesc", "tese", "geom", "frag", "comp" };
    const auto BinaryFileExtensions = { "bin", "raw" };
    const auto ScriptFileExtensions = { "js" };

    static QMap<QString, int> gNextUntitledFileIndex;
} // namespace

void FileDialog::resetNextUntitledFileIndex()
{
    gNextUntitledFileIndex.clear();
}

QString FileDialog::generateNextUntitledFileName(QString base, bool startWithZero)
{
    auto fileName = UntitledTag + base;
    auto index = gNextUntitledFileIndex[base]++;
    if (!startWithZero)
        index++;
    if (index)
        fileName += " " + QString::number(index);
    return fileName;
}

bool FileDialog::isUntitled(const QString &fileName)
{
    return fileName.startsWith(UntitledTag);
}

bool FileDialog::isEmptyOrUntitled(const QString &fileName)
{
    return fileName.isEmpty() || isUntitled(fileName);
}

QString FileDialog::getFileTitle(const QString &fileName)
{
    if (!fileName.startsWith(UntitledTag))
        return QFileInfo(fileName).fileName();

    return QString(fileName).remove(0, UntitledTag.size());
}

QString FileDialog::getWindowTitle(const QString &fileName)
{
    return "[*]" + getFileTitle(fileName);
}

bool FileDialog::isSessionFileName(const QString &fileName)
{
    return fileName.endsWith(SessionFileExtension);
}

bool FileDialog::isBinaryFileName(const QString &fileName)
{
    for (const auto &ext : BinaryFileExtensions)
        if (fileName.endsWith(ext))
            return true;
    return false;
}

FileDialog::FileDialog(QMainWindow *window)
    : mWindow(window)
{
}

FileDialog::~FileDialog() = default;

QDir FileDialog::directory() const
{
    return mDirectory;
}

void FileDialog::setDirectory(QDir directory)
{
    mDirectory = directory;
}

QString FileDialog::fileName() const
{
    if (mFileNames.isEmpty())
        return { };
    return mFileNames.first();
}

QStringList FileDialog::fileNames() const
{
    return mFileNames;
}

bool FileDialog::exec(Options options, QString currentFileName)
{
    QFileDialog dialog(mWindow);

    if (options & Saving) {
        if (currentFileName.isEmpty())
            dialog.setWindowTitle(tr("New File"));
        else
            dialog.setWindowTitle(tr("Save '%1' As")
                .arg(getFileTitle(currentFileName)));

        dialog.setAcceptMode(QFileDialog::AcceptSave);
        dialog.setFileMode(QFileDialog::AnyFile);
    }
    else {
        auto importing = ((options & Importing) != 0);
        dialog.setWindowTitle(importing ? tr("Import File") : tr("Open File"));
        dialog.setAcceptMode(QFileDialog::AcceptOpen);
        dialog.setFileMode(QFileDialog::ExistingFiles);
    }

    auto shaderFileFilter = QString();
    for (const auto &ext : ShaderFileExtensions)
        shaderFileFilter = shaderFileFilter + " *." + ext;

    auto imageFileFilter = QString();
    foreach (const QByteArray &format, QImageReader::supportedImageFormats())
        imageFileFilter += " *." + QString(format) + " ";

    auto binaryFileFilter = QString();
    for (const auto &ext : BinaryFileExtensions)
        binaryFileFilter = binaryFileFilter + " *" + ext;

    auto scriptFileFilter = QString();
    for (const auto &ext : ScriptFileExtensions)
        scriptFileFilter = scriptFileFilter + " *." + ext;

    auto supportedFileFilter = "*." + SessionFileExtension +
        shaderFileFilter + scriptFileFilter + imageFileFilter + binaryFileFilter;

    auto filters = QStringList();
    if (options & SupportedExtensions)
        filters.append(tr("Supported files") + " (" + supportedFileFilter + ")");
    if (options & SessionExtensions) {
        filters.append(qApp->applicationName() + tr(" session") +
            " (*." + SessionFileExtension + ")");
        dialog.setDefaultSuffix(SessionFileExtension);
    }
    if (options & ShaderExtensions) {
        filters.append(tr("GLSL shader files") + " (" + shaderFileFilter + ")");
        dialog.setDefaultSuffix(*begin(ShaderFileExtensions));
    }
    if (options & ImageExtensions) {
        filters.append(tr("Image files") + " (" + imageFileFilter + ")");
        dialog.setDefaultSuffix("png");
    }
    if (options & BinaryExtensions) {
        filters.append(tr("Binary files") + " (" + binaryFileFilter + ")");
        dialog.setDefaultSuffix(*begin(BinaryFileExtensions));
    }
    if (options & ScriptExtensions) {
        filters.append(tr("JavaScript files") + " (" + scriptFileFilter + ")");
        dialog.setDefaultSuffix(*begin(ScriptFileExtensions));
    }
    if (options & SupportedExtensions)
        dialog.setDefaultSuffix("");

    filters.append(tr("All Files (*)"));

    dialog.setNameFilters(filters);

    if (isUntitled(currentFileName))
        currentFileName = "";

    dialog.selectFile(currentFileName);
    if (currentFileName.isEmpty())
        dialog.setDirectory(mDirectory.exists() ?
            mDirectory : QDir::current());

    auto result = (dialog.exec() == QDialog::Accepted);

    if (result) {
        mFileNames = dialog.selectedFiles();
        mDirectory = dialog.directory();
    }
    return result;
}
