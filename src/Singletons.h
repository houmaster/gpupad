#ifndef SINGLETONS_H
#define SINGLETONS_H

#include <QScopedPointer>

class QMainWindow;
class MessageList;
class Settings;
class FileCache;
class FileDialog;
class EditorManager;
class SynchronizeLogic;
class SessionModel;
class Renderer;
class FindReplaceBar;
class CustomActions;

bool onMainThread();

class Singletons
{
public:
    static Renderer &renderer();
    static MessageList &messageList();
    static Settings &settings();
    static FileCache& fileCache();
    static FileDialog &fileDialog();
    static EditorManager &editorManager();
    static SessionModel &sessionModel();
    static SynchronizeLogic &synchronizeLogic();
    static FindReplaceBar &findReplaceBar();
    static CustomActions &customActions();

    explicit Singletons(QMainWindow *window);
    ~Singletons();

private:
    static Singletons *sInstance;

    QScopedPointer<Renderer> mRenderer;
    QScopedPointer<MessageList> mMessageList;
    QScopedPointer<Settings> mSettings;
    QScopedPointer<FileCache> mFileCache;
    QScopedPointer<FileDialog> mFileDialog;
    QScopedPointer<EditorManager> mEditorManager;
    QScopedPointer<SessionModel> mSessionModel;
    QScopedPointer<SynchronizeLogic> mSynchronizeLogic;
    QScopedPointer<FindReplaceBar> mFindReplaceBar;
    QScopedPointer<CustomActions> mCustomActions;
};

#endif // SINGLETONS_H
