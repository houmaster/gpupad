#ifndef FILEDIALOG_H
#define FILEDIALOG_H

#include <QObject>
#include <QDir>

class QMainWindow;

class FileDialog : public QObject
{
    Q_OBJECT
public:
    static void resetNextUntitledFileIndex();
    static QString generateNextUntitledFileName(const QString &base);
    static QString getUntitledSessionFileName();
    static bool isUntitled(const QString &fileName);
    static QString getFileTitle(const QString &fileName);
    static QString getWindowTitle(const QString &fileName);
    static bool isSessionFileName(const QString& fileName);
    static bool isBinaryFileName(const QString& fileName);

    enum OptionBit
    {
        Loading             = 1 << 0,
        Saving              = 1 << 1,
        ShaderExtensions    = 1 << 2,
        ImageExtensions     = 1 << 3,
        BinaryExtensions    = 1 << 4,
        SessionExtensions   = 1 << 5,
        ScriptExtensions    = 1 << 6,
        SupportedExtensions = 1 << 7,
        AllExtensionFilters = ShaderExtensions | ImageExtensions |
            BinaryExtensions | ScriptExtensions | SessionExtensions | SupportedExtensions
    };
    Q_DECLARE_FLAGS(Options, OptionBit)

    explicit FileDialog(QMainWindow *window);
    ~FileDialog();

    QDir directory() const;
    void setDirectory(QDir directory);
    QString fileName() const;
    QStringList fileNames() const;
    bool exec(Options options, QString fileName = "");

private:
    QMainWindow *mWindow;
    QStringList mFileNames;
    QDir mDirectory;
};

#endif // FILEDIALOG_H
