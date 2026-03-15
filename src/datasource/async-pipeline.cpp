#include "async-pipeline.h"
#include "pipeline-scheduler.h"

QCPAsyncPipelineBase::QCPAsyncPipelineBase(QCPPipelineScheduler* scheduler,
                                             QObject* parent)
    : QObject(parent)
    , mScheduler(scheduler)
    , mDestroyed(std::make_shared<std::atomic<bool>>(false))
{
}

QCPAsyncPipelineBase::~QCPAsyncPipelineBase()
{
    mDestroyed->store(true);
}

bool QCPAsyncPipelineBase::isBusy() const
{
    return mGeneration.load() > mDisplayedGeneration;
}

void QCPAsyncPipelineBase::onDataChanged()
{
    uint64_t gen = ++mGeneration;
    QMutexLocker lock(&mMutex);
    mCache = std::any{};

    auto job = makeJob(mLastViewport, std::any{}, gen);
    if (!job) return;

    if (mJobRunning)
    {
        mPending = std::move(job);
        mPendingPriority = QCPPipelineScheduler::Heavy;
    }
    else
    {
        mJobRunning = true;
        mRunningGeneration = gen;
        lock.unlock();
        mScheduler->submit(QCPPipelineScheduler::Heavy, std::move(job));
    }
}

void QCPAsyncPipelineBase::onViewportChanged(const ViewportParams& vp)
{
    if (mKind == TransformKind::ViewportIndependent)
    {
        QMutexLocker lock(&mMutex);
        mLastViewport = vp;
        return;
    }

    uint64_t gen = ++mGeneration;
    QMutexLocker lock(&mMutex);
    mLastViewport = vp;

    auto cache = std::move(mCache);
    auto job = makeJob(vp, std::move(cache), gen);
    if (!job) return;

    if (mJobRunning)
    {
        mPending = std::move(job);
        mPendingPriority = QCPPipelineScheduler::Fast;
    }
    else
    {
        mJobRunning = true;
        mRunningGeneration = gen;
        lock.unlock();
        mScheduler->submit(QCPPipelineScheduler::Fast, std::move(job));
    }
}

void QCPAsyncPipelineBase::deliverResult(uint64_t generation, std::any cache, std::any result)
{
    if (mDestroyed->load())
        return;

    QMutexLocker lock(&mMutex);
    mCache = std::move(cache);

    if (mPending)
    {
        auto job = std::move(mPending);
        mPending = nullptr;
        auto priority = mPendingPriority;
        mRunningGeneration = mGeneration.load();
        lock.unlock();
        mScheduler->submit(priority, std::move(job));
    }
    else
    {
        mJobRunning = false;
        lock.unlock();
    }

    if (generation > mDisplayedGeneration)
    {
        mDisplayedGeneration = generation;
        applyResult(generation, std::move(result));
        Q_EMIT finished(generation);
    }

    bool busy = isBusy();
    if (busy != mWasBusy)
    {
        mWasBusy = busy;
        Q_EMIT busyChanged(busy);
    }
}
