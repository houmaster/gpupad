#include "RenderTask.h"
#include "Singletons.h"
#include "Renderer.h"

RenderTask::RenderTask(QObject *parent) : QObject(parent)
{
}

RenderTask::~RenderTask()
{
    Q_ASSERT(mReleased);
}

void RenderTask::releaseResources()
{
    if (!std::exchange(mReleased, true))
        Singletons::renderer().release(this);
}

void RenderTask::update(bool itemsChanged, EvaluationType evaluationType)
{
    if (!std::exchange(mUpdating, true)) {
        mReleased = false;
        prepare(itemsChanged, evaluationType);
        Singletons::renderer().render(this);
    }
    else {
        mItemsChanged |= itemsChanged;
        mEvaluationType = std::max(mEvaluationType, evaluationType);
    }
}

void RenderTask::handleRendered()
{
    finish();
    mUpdating = false;

    emit updated();

    // restart when items were changed in the meantime
    if (mItemsChanged || mEvaluationType != EvaluationType::Automatic)
        update(std::exchange(mItemsChanged, false),
               std::exchange(mEvaluationType, EvaluationType::Automatic));
}
