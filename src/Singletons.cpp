#include "Singletons.h"
#include "MessageWindow.h"
#include "FileCache.h"
#include "FileDialog.h"
#include "SynchronizeLogic.h"
#include "editors/SourceEditorSettings.h"
#include "editors/EditorManager.h"
#include "editors/FindReplaceBar.h"
#include "session/SessionModel.h"
#include "render/Renderer.h"

Singletons *Singletons::sInstance;

Renderer &Singletons::renderer()
{
    return *sInstance->mRenderer;
}

MessageWindow &Singletons::messageWindow()
{
    return *sInstance->mMessageWindow;
}

SourceEditorSettings &Singletons::sourceEditorSettings()
{
    return *sInstance->mSourceEditorSettings;
}

FileCache &Singletons::fileCache()
{
    return *sInstance->mFileCache;
}

FileDialog &Singletons::fileDialog()
{
    return *sInstance->mFileDialog;
}

EditorManager &Singletons::editorManager()
{
    return *sInstance->mEditorManager;
}

SessionModel &Singletons::sessionModel()
{
    return *sInstance->mSessionModel;
}

SynchronizeLogic &Singletons::synchronizeLogic()
{
    return *sInstance->mSynchronizeLogic;
}

FindReplaceBar &Singletons::findReplaceBar()
{
    return *sInstance->mFindReplaceBar;
}

Singletons::Singletons()
{
    sInstance = this;
    mRenderer.reset(new Renderer());
    mMessageWindow.reset(new MessageWindow());
    mSourceEditorSettings.reset(new SourceEditorSettings());
    mFileCache.reset(new FileCache());
    mFileDialog.reset(new FileDialog());
    mEditorManager.reset(new EditorManager());
    mSessionModel.reset(new SessionModel());
    mSynchronizeLogic.reset(new SynchronizeLogic());
    mFindReplaceBar.reset(new FindReplaceBar());
}

Singletons::~Singletons() = default;
