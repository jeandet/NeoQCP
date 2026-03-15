# Async Data Pipeline Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the bespoke `QCPColormapResampler` + `QCPResamplerScheduler` with a generic async data transformation pipeline that serves both QCPGraph2 and QCPColorMap2.

**Architecture:** Three layers — `QCPPipelineScheduler` (shared prioritized thread pool), `QCPAsyncPipelineBase` (non-templated coalescing/caching/generation machinery), `QCPAsyncPipeline<In,Out>` (typed template). Plottables own a pipeline instance; `QCustomPlot` owns the shared scheduler.

**Tech Stack:** C++20, Qt6 (QThreadPool, QMutex, signals/slots, Qt::QueuedConnection), Meson build system

**Spec:** `docs/specs/2026-03-14-async-pipeline-design.md`

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `src/datasource/pipeline-scheduler.h` | Create | `QCPPipelineScheduler` — prioritized QThreadPool wrapper |
| `src/datasource/pipeline-scheduler.cpp` | Create | `QCPPipelineScheduler` implementation |
| `src/datasource/async-pipeline.h` | Create | `ViewportParams`, `TransformKind`, `QCPAsyncPipelineBase`, `QCPAsyncPipeline<In,Out>` template, type aliases |
| `src/datasource/async-pipeline.cpp` | Create | `QCPAsyncPipelineBase` implementation |
| `src/plottables/plottable-graph2.h` | Modify | Add `QCPGraphPipeline`, wire `draw()` to pipeline result |
| `src/plottables/plottable-graph2.cpp` | Modify | Wire `setDataSource()` → pipeline, connect viewport signals |
| `src/plottables/plottable-colormap2.h` | Modify | Replace `QCPColormapResampler*` with `QCPColormapPipeline` |
| `src/plottables/plottable-colormap2.cpp` | Modify | Migrate resampling to pipeline transform |
| `src/core.h` | Modify | Add `QCPPipelineScheduler` member, `setMaxPipelineThreads()` |
| `src/core.cpp` | Modify | Initialize scheduler, pass to plottables |
| `meson.build` | Modify | Add new source files and MOC headers |
| `tests/auto/test-pipeline/test-pipeline.h` | Create | `TestPipeline` test class |
| `tests/auto/test-pipeline/test-pipeline.cpp` | Create | Pipeline unit and integration tests |
| `tests/auto/autotest.cpp` | Modify | Add `TestPipeline` |
| `tests/auto/meson.build` | Modify | Add test-pipeline sources |

---

## Chunk 1: Pipeline Scheduler + Pipeline Base

### Task 1: QCPPipelineScheduler

**Files:**
- Create: `src/datasource/pipeline-scheduler.h`
- Create: `src/datasource/pipeline-scheduler.cpp`
- Modify: `meson.build` (add source + MOC header)

- [ ] **Step 1: Write the test header**

Create `tests/auto/test-pipeline/test-pipeline.h`:

```cpp
#pragma once
#include <QtTest/QtTest>

class QCustomPlot;

class TestPipeline : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    // Scheduler tests
    void schedulerSubmitHeavy();
    void schedulerSubmitFast();
    void schedulerFastPriority();

private:
    QCustomPlot* mPlot = nullptr;
};
```

- [ ] **Step 2: Write the initial test file**

Create `tests/auto/test-pipeline/test-pipeline.cpp`:

```cpp
#include "test-pipeline.h"
#include <qcustomplot.h>
#include <datasource/pipeline-scheduler.h>
#include <QSignalSpy>

void TestPipeline::init()
{
    mPlot = new QCustomPlot();
    mPlot->resize(400, 300);
}

void TestPipeline::cleanup()
{
    delete mPlot;
    mPlot = nullptr;
}

void TestPipeline::schedulerSubmitHeavy()
{
    QCPPipelineScheduler scheduler;
    std::atomic<int> counter{0};
    QEventLoop loop;

    scheduler.submit(QCPPipelineScheduler::Heavy, [&counter]{ ++counter; });

    // Give pool thread time to execute
    QTimer::singleShot(100, &loop, &QEventLoop::quit);
    loop.exec();

    QCOMPARE(counter.load(), 1);
}

void TestPipeline::schedulerSubmitFast()
{
    QCPPipelineScheduler scheduler;
    std::atomic<int> counter{0};
    QEventLoop loop;

    scheduler.submit(QCPPipelineScheduler::Fast, [&counter]{ ++counter; });

    QTimer::singleShot(100, &loop, &QEventLoop::quit);
    loop.exec();

    QCOMPARE(counter.load(), 1);
}

void TestPipeline::schedulerFastPriority()
{
    // Submit a blocking heavy job, then fast + heavy, verify fast runs first
    QCPPipelineScheduler scheduler(1); // 1 thread
    std::atomic<bool> gate{false};
    std::vector<int> order;
    QMutex orderMutex;
    QEventLoop loop;

    // Block the single thread
    std::atomic<bool> blockerStarted{false};
    scheduler.submit(QCPPipelineScheduler::Heavy, [&gate, &blockerStarted]{
        blockerStarted.store(true);
        while (!gate.load()) QThread::msleep(5);
    });
    while (!blockerStarted.load()) QThread::msleep(1); // wait for blocker to start

    // Queue a heavy then a fast — fast should run first after unblock
    scheduler.submit(QCPPipelineScheduler::Heavy, [&order, &orderMutex]{
        QMutexLocker lock(&orderMutex);
        order.push_back(1); // heavy
    });
    scheduler.submit(QCPPipelineScheduler::Fast, [&order, &orderMutex]{
        QMutexLocker lock(&orderMutex);
        order.push_back(2); // fast
    });

    gate.store(true); // unblock

    QTimer::singleShot(200, &loop, &QEventLoop::quit);
    loop.exec();

    QVERIFY(order.size() == 2);
    QCOMPARE(order[0], 2); // fast ran first
    QCOMPARE(order[1], 1);
}
```

- [ ] **Step 3: Register test in build system**

Add to `tests/auto/meson.build` — add `'test-pipeline/test-pipeline.cpp'` to `test_srcs` and `'test-pipeline/test-pipeline.h'` to `test_headers`.

Add to `tests/auto/autotest.cpp` — add `#include "test-pipeline/test-pipeline.h"` and `QCPTEST(TestPipeline);`.

- [ ] **Step 4: Run tests to verify they fail**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: compilation error — `pipeline-scheduler.h` not found.

- [ ] **Step 5: Implement QCPPipelineScheduler**

Create `src/datasource/pipeline-scheduler.h`:

```cpp
#pragma once
#include <QObject>
#include <QThreadPool>
#include <QMutex>
#include <QRunnable>
#include <functional>
#include <deque>

class QCPPipelineScheduler : public QObject
{
    Q_OBJECT

public:
    enum Priority { Fast, Heavy };

    explicit QCPPipelineScheduler(int maxThreads = 0, QObject* parent = nullptr);
    ~QCPPipelineScheduler() override;

    void submit(Priority priority, std::function<void()> work);
    void setMaxThreads(int count);
    int maxThreads() const;

private:
    void scheduleNext();

    QThreadPool mPool;
    QMutex mMutex;
    std::deque<std::function<void()>> mFastQueue;
    std::deque<std::function<void()>> mHeavyQueue;
    int mRunning = 0;
};
```

Create `src/datasource/pipeline-scheduler.cpp`:

```cpp
#include "pipeline-scheduler.h"
#include <QThread>
#include <algorithm>

QCPPipelineScheduler::QCPPipelineScheduler(int maxThreads, QObject* parent)
    : QObject(parent)
{
    int threads = maxThreads > 0 ? maxThreads
                                 : std::max(1, QThread::idealThreadCount() / 2);
    mPool.setMaxThreadCount(threads);
}

QCPPipelineScheduler::~QCPPipelineScheduler()
{
    mPool.waitForDone();
}

void QCPPipelineScheduler::submit(Priority priority, std::function<void()> work)
{
    QMutexLocker lock(&mMutex);
    if (priority == Fast)
        mFastQueue.push_back(std::move(work));
    else
        mHeavyQueue.push_back(std::move(work));

    lock.unlock();
    scheduleNext();
}

void QCPPipelineScheduler::scheduleNext()
{
    QMutexLocker lock(&mMutex);
    while (mRunning < mPool.maxThreadCount())
    {
        std::function<void()> work;
        if (!mFastQueue.empty())
        {
            work = std::move(mFastQueue.front());
            mFastQueue.pop_front();
        }
        else if (!mHeavyQueue.empty())
        {
            work = std::move(mHeavyQueue.front());
            mHeavyQueue.pop_front();
        }
        else
            break;

        ++mRunning;
        auto* self = this;
        mPool.start([self, work = std::move(work)]() mutable {
            try { work(); } catch (...) { }
            {
                QMutexLocker lock(&self->mMutex);
                --self->mRunning;
            }
            self->scheduleNext();
        });
    }
}

void QCPPipelineScheduler::setMaxThreads(int count)
{
    mPool.setMaxThreadCount(std::max(1, count));
}

int QCPPipelineScheduler::maxThreads() const
{
    return mPool.maxThreadCount();
}
```


- [ ] **Step 6: Register in meson.build**

Add `'src/datasource/pipeline-scheduler.cpp'` to the static library sources list (after `resampler-scheduler.cpp`).
Add `'src/datasource/pipeline-scheduler.h'` to `neoqcp_moc_headers` (it has Q_OBJECT).

- [ ] **Step 7: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: all 3 scheduler tests pass.

- [ ] **Step 8: Commit**

```bash
git add src/datasource/pipeline-scheduler.h src/datasource/pipeline-scheduler.cpp \
        tests/auto/test-pipeline/test-pipeline.h tests/auto/test-pipeline/test-pipeline.cpp \
        tests/auto/autotest.cpp tests/auto/meson.build meson.build
git commit -m "feat: add QCPPipelineScheduler — prioritized thread pool"
```

---

### Task 2: ViewportParams and TransformKind

**Files:**
- Create: `src/datasource/async-pipeline.h` (partial — types only for now)

- [ ] **Step 1: Create header with types**

Create `src/datasource/async-pipeline.h`:

```cpp
#pragma once
#include <QObject>
#include <QMutex>
#include <axis/range.h>
#include <functional>
#include <memory>
#include <any>
#include <atomic>

class QCPPipelineScheduler;

struct ViewportParams {
    QCPRange keyRange;
    QCPRange valueRange;
    int plotWidthPx = 0;
    int plotHeightPx = 0;
    bool keyLogScale = false;
    bool valueLogScale = false;
};

enum class TransformKind { ViewportIndependent, ViewportDependent };
```

This file will grow in Task 3. No tests needed for pure data types.

- [ ] **Step 2: Commit**

```bash
git add src/datasource/async-pipeline.h
git commit -m "feat: add ViewportParams and TransformKind types"
```

---

### Task 3: QCPAsyncPipelineBase

**Files:**
- Modify: `src/datasource/async-pipeline.h` (add base class declaration)
- Create: `src/datasource/async-pipeline.cpp`
- Modify: `tests/auto/test-pipeline/test-pipeline.h` (add pipeline base tests)
- Modify: `tests/auto/test-pipeline/test-pipeline.cpp` (add pipeline base tests)
- Modify: `meson.build` (add source + MOC header)

- [ ] **Step 1: Add pipeline base tests to test header**

Add to `tests/auto/test-pipeline/test-pipeline.h` private slots:

```cpp
    // Pipeline base tests
    void pipelinePassthrough();
    void pipelineTransformRuns();
    void pipelineCoalescing();
    void pipelineViewportIndependentSkipsViewport();
    void pipelineViewportDependentRuns();
    void pipelineCachePreservedOnViewport();
    void pipelineCacheClearedOnDataChange();
    void pipelineInterimResult();
    void pipelineDestructionWhileRunning();
```

- [ ] **Step 2: Write the QCPAsyncPipelineBase declaration**

Add to `src/datasource/async-pipeline.h` after the `TransformKind` enum:

```cpp
class QCPAsyncPipelineBase : public QObject
{
    Q_OBJECT

public:
    explicit QCPAsyncPipelineBase(QCPPipelineScheduler* scheduler,
                                  QObject* parent = nullptr);
    ~QCPAsyncPipelineBase() override;

    bool isBusy() const;

    void onDataChanged();
    void onViewportChanged(const ViewportParams& vp);

Q_SIGNALS:
    void finished(uint64_t generation);
    void busyChanged(bool busy);

protected:
    // Subclass implements: build a job lambda that calls the typed transform
    virtual std::function<void()> makeJob(
        const ViewportParams& vp, std::any cache, uint64_t generation) = 0;

    // Called on GUI thread when a job completes
    virtual void applyResult(uint64_t generation) = 0;

    void deliverResult(uint64_t generation, std::any cache);

    QCPPipelineScheduler* mScheduler;
    TransformKind mKind = TransformKind::ViewportIndependent;
    std::atomic<uint64_t> mGeneration{0};
    uint64_t mDisplayedGeneration = 0;

    QMutex mMutex; // guards: mSource*, mCache, mPending, mRunningGeneration
    std::any mCache;
    std::function<void()> mPending;
    QCPPipelineScheduler::Priority mPendingPriority = QCPPipelineScheduler::Heavy;
    uint64_t mRunningGeneration = 0;
    bool mJobRunning = false;

    ViewportParams mLastViewport;
    bool mWasBusy = false;

    std::shared_ptr<std::atomic<bool>> mDestroyed;
};
```

- [ ] **Step 3: Implement QCPAsyncPipelineBase**

Create `src/datasource/async-pipeline.cpp`:

```cpp
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
    mCache = std::any{}; // clear cache

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
    mLastViewport = vp;
    if (mKind == TransformKind::ViewportIndependent)
        return;

    uint64_t gen = ++mGeneration;
    QMutexLocker lock(&mMutex);

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

void QCPAsyncPipelineBase::deliverResult(uint64_t generation, std::any cache)
{
    if (mDestroyed->load())
        return;

    // Move cache back and check for pending work
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

    // Display result if newer than current
    if (generation > mDisplayedGeneration)
    {
        mDisplayedGeneration = generation;
        applyResult(generation);
        Q_EMIT finished(generation);
    }

    bool busy = isBusy();
    if (busy != mWasBusy)
    {
        mWasBusy = busy;
        Q_EMIT busyChanged(busy);
    }
}
```

- [ ] **Step 4: Register in meson.build**

Add `'src/datasource/async-pipeline.cpp'` to library sources.
Add `'src/datasource/async-pipeline.h'` to `neoqcp_moc_headers`.

- [ ] **Step 5: Build to verify compilation**

Run: `meson compile -C build`
Expected: compiles without errors.

- [ ] **Step 6: Commit**

```bash
git add src/datasource/async-pipeline.h src/datasource/async-pipeline.cpp meson.build
git commit -m "feat: add QCPAsyncPipelineBase — coalescing/caching pipeline core"
```

---

### Task 4: QCPAsyncPipeline<In, Out> template

**Files:**
- Modify: `src/datasource/async-pipeline.h` (add template class + aliases)
- Modify: `tests/auto/test-pipeline/test-pipeline.cpp` (implement pipeline tests)

- [ ] **Step 1: Add the typed template to async-pipeline.h**

Append after `QCPAsyncPipelineBase` declaration:

```cpp
class QCPAbstractDataSource;
class QCPAbstractDataSource2D;
class QCPColorMapData;

template<typename In, typename Out>
class QCPAsyncPipeline : public QCPAsyncPipelineBase
{
public:
    using TransformFn = std::function<
        std::shared_ptr<Out>(const In& source,
                             const ViewportParams& viewport,
                             std::any& cache)>;

    explicit QCPAsyncPipeline(QCPPipelineScheduler* scheduler,
                               QObject* parent = nullptr)
        : QCPAsyncPipelineBase(scheduler, parent) {}

    void setTransform(TransformKind kind, TransformFn fn)
    {
        mKind = kind;
        mTransform = std::move(fn);
    }

    void setSource(const In* source)
    {
        {
            QMutexLocker lock(&mMutex);
            mSource = source;
        }
        if (mTransform)
            onDataChanged();
    }

    const Out* result() const
    {
        if (!mTransform)
        {
            if constexpr (std::is_same_v<In, Out>)
                return mSource; // passthrough (same type)
            else
                return nullptr; // heterogeneous pipeline requires a transform
        }
        return mResult.get();
    }

    bool hasTransform() const { return !!mTransform; }

protected:
    std::function<void()> makeJob(
        const ViewportParams& vp, std::any cache, uint64_t generation) override
    {
        if (!mSource || !mTransform) return {};

        auto source = mSource;
        auto transform = mTransform;
        auto destroyed = mDestroyed;
        auto* self = this;

        return [source, transform, vp, cache = std::move(cache),
                generation, destroyed, self]() mutable {
            auto result = transform(*source, vp, cache);
            if (destroyed->load()) return;

            self->mPendingResult = std::move(result);
            self->mPendingCache = std::move(cache);
            self->mPendingGeneration = generation;

            QMetaObject::invokeMethod(self, [self]{
                self->deliverResult(self->mPendingGeneration,
                                    std::move(self->mPendingCache));
            }, Qt::QueuedConnection);
        };
    }

    void applyResult(uint64_t /*generation*/) override
    {
        mResult = std::move(mPendingResult);
    }

private:
    const In* mSource = nullptr;
    TransformFn mTransform;
    std::shared_ptr<Out> mResult;

    // Staging area for cross-thread delivery
    std::shared_ptr<Out> mPendingResult;
    std::any mPendingCache;
    uint64_t mPendingGeneration = 0;
};

using QCPGraphPipeline = QCPAsyncPipeline<QCPAbstractDataSource, QCPAbstractDataSource>;
using QCPColormapPipeline = QCPAsyncPipeline<QCPAbstractDataSource2D, QCPColorMapData>;
```

- [ ] **Step 2: Write pipeline tests**

Add to `tests/auto/test-pipeline/test-pipeline.cpp`:

```cpp
#include <datasource/async-pipeline.h>
#include <datasource/soa-datasource.h>

void TestPipeline::pipelinePassthrough()
{
    QCPPipelineScheduler scheduler;
    auto source = std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
        std::vector<double>{1, 2, 3}, std::vector<double>{4, 5, 6});

    QCPGraphPipeline pipeline(&scheduler);
    pipeline.setSource(source.get());

    // No transform set — passthrough
    QVERIFY(!pipeline.hasTransform());
    QCOMPARE(pipeline.result(), source.get());
}

void TestPipeline::pipelineTransformRuns()
{
    QCPPipelineScheduler scheduler;
    auto source = std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
        std::vector<double>{1, 2, 3}, std::vector<double>{4, 5, 6});

    QCPGraphPipeline pipeline(&scheduler);
    QSignalSpy spy(&pipeline, &QCPGraphPipeline::finished);

    pipeline.setTransform(TransformKind::ViewportIndependent,
        [](const QCPAbstractDataSource& src, const ViewportParams&, std::any&)
            -> std::shared_ptr<QCPAbstractDataSource> {
            // Identity transform — just wrap the source
            return std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
                std::vector<double>{1, 2, 3}, std::vector<double>{10, 20, 30});
        });

    pipeline.setSource(source.get());

    // Wait for async completion
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);

    QVERIFY(pipeline.result() != nullptr);
    QCOMPARE(pipeline.result()->valueAt(0), 10.0);
}

void TestPipeline::pipelineCoalescing()
{
    QCPPipelineScheduler scheduler(1);
    std::atomic<int> runCount{0};

    auto source = std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
        std::vector<double>{1}, std::vector<double>{1});

    QCPGraphPipeline pipeline(&scheduler);
    QSignalSpy spy(&pipeline, &QCPGraphPipeline::finished);

    pipeline.setTransform(TransformKind::ViewportIndependent,
        [&runCount](const QCPAbstractDataSource&, const ViewportParams&, std::any&)
            -> std::shared_ptr<QCPAbstractDataSource> {
            ++runCount;
            QThread::msleep(50); // slow transform
            return std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
                std::vector<double>{1}, std::vector<double>{1});
        });

    // Rapid-fire 5 data changes — should coalesce
    for (int i = 0; i < 5; ++i)
        pipeline.setSource(source.get());

    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 2000);
    QThread::msleep(200); // let everything settle

    // Should have run at most 2-3 times (first + coalesced), not 5
    QVERIFY2(runCount.load() <= 3,
        qPrintable(QString("Expected <= 3 runs, got %1").arg(runCount.load())));
}

void TestPipeline::pipelineViewportIndependentSkipsViewport()
{
    QCPPipelineScheduler scheduler;
    std::atomic<int> runCount{0};

    auto source = std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
        std::vector<double>{1}, std::vector<double>{1});

    QCPGraphPipeline pipeline(&scheduler);
    QSignalSpy spy(&pipeline, &QCPGraphPipeline::finished);

    pipeline.setTransform(TransformKind::ViewportIndependent,
        [&runCount](const QCPAbstractDataSource&, const ViewportParams&, std::any&)
            -> std::shared_ptr<QCPAbstractDataSource> {
            ++runCount;
            return std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
                std::vector<double>{1}, std::vector<double>{1});
        });

    pipeline.setSource(source.get());
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);

    int countAfterData = runCount.load();
    pipeline.onViewportChanged(ViewportParams{});
    QThread::msleep(100);

    QCOMPARE(runCount.load(), countAfterData); // no extra run
}

void TestPipeline::pipelineViewportDependentRuns()
{
    QCPPipelineScheduler scheduler;
    std::atomic<int> runCount{0};

    auto source = std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
        std::vector<double>{1}, std::vector<double>{1});

    QCPGraphPipeline pipeline(&scheduler);
    QSignalSpy spy(&pipeline, &QCPGraphPipeline::finished);

    pipeline.setTransform(TransformKind::ViewportDependent,
        [&runCount](const QCPAbstractDataSource&, const ViewportParams&, std::any&)
            -> std::shared_ptr<QCPAbstractDataSource> {
            ++runCount;
            return std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
                std::vector<double>{1}, std::vector<double>{1});
        });

    pipeline.setSource(source.get());
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);

    pipeline.onViewportChanged(ViewportParams{{0, 10}, {0, 10}, 400, 300, false, false});
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 1000);

    QVERIFY(runCount.load() >= 2);
}

void TestPipeline::pipelineCachePreservedOnViewport()
{
    QCPPipelineScheduler scheduler;

    auto source = std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
        std::vector<double>{1}, std::vector<double>{1});

    QCPGraphPipeline pipeline(&scheduler);
    QSignalSpy spy(&pipeline, &QCPGraphPipeline::finished);

    pipeline.setTransform(TransformKind::ViewportDependent,
        [](const QCPAbstractDataSource&, const ViewportParams&, std::any& cache)
            -> std::shared_ptr<QCPAbstractDataSource> {
            int* counter = std::any_cast<int>(&cache);
            if (!counter)
            {
                cache = 1;
            }
            else
            {
                *counter += 1;
                cache = *counter;
            }
            return std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
                std::vector<double>{1}, std::vector<double>{static_cast<double>(std::any_cast<int>(cache))});
        });

    pipeline.setSource(source.get());
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);

    pipeline.onViewportChanged(ViewportParams{});
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 1000);

    // Cache should have been preserved — counter incremented to 2
    QCOMPARE(pipeline.result()->valueAt(0), 2.0);
}

void TestPipeline::pipelineCacheClearedOnDataChange()
{
    QCPPipelineScheduler scheduler;

    auto source = std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
        std::vector<double>{1}, std::vector<double>{1});

    QCPGraphPipeline pipeline(&scheduler);
    QSignalSpy spy(&pipeline, &QCPGraphPipeline::finished);

    pipeline.setTransform(TransformKind::ViewportDependent,
        [](const QCPAbstractDataSource&, const ViewportParams&, std::any& cache)
            -> std::shared_ptr<QCPAbstractDataSource> {
            int val = cache.has_value() ? std::any_cast<int>(cache) + 1 : 1;
            cache = val;
            return std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
                std::vector<double>{1}, std::vector<double>{static_cast<double>(val)});
        });

    // First data set — cache starts empty, val=1
    pipeline.setSource(source.get());
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);
    QCOMPARE(pipeline.result()->valueAt(0), 1.0);

    // Second data set — cache cleared, val=1 again
    pipeline.setSource(source.get());
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 2, 1000);
    QCOMPARE(pipeline.result()->valueAt(0), 1.0);
}

void TestPipeline::pipelineInterimResult()
{
    QCPPipelineScheduler scheduler(1);

    auto source = std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
        std::vector<double>{1}, std::vector<double>{1});

    QCPGraphPipeline pipeline(&scheduler);
    QSignalSpy spy(&pipeline, &QCPGraphPipeline::finished);

    std::atomic<bool> gate{false};

    pipeline.setTransform(TransformKind::ViewportIndependent,
        [&gate](const QCPAbstractDataSource&, const ViewportParams&, std::any&)
            -> std::shared_ptr<QCPAbstractDataSource> {
            while (!gate.load()) QThread::msleep(5);
            return std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
                std::vector<double>{1}, std::vector<double>{42});
        });

    // Start first job (will block on gate)
    pipeline.setSource(source.get());
    QThread::msleep(20);

    // Queue second data change — first result should still display as interim
    pipeline.setSource(source.get());

    gate.store(true);

    // Should get at least one finished signal (interim from gen 1)
    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 2000);
    QCOMPARE(pipeline.result()->valueAt(0), 42.0);
}

void TestPipeline::pipelineDestructionWhileRunning()
{
    QCPPipelineScheduler scheduler;
    std::atomic<bool> gate{false};
    std::atomic<bool> started{false};

    {
        auto source = std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
            std::vector<double>{1}, std::vector<double>{1});

        auto pipeline = std::make_unique<QCPGraphPipeline>(&scheduler);
        pipeline->setTransform(TransformKind::ViewportIndependent,
            [&gate, &started](const QCPAbstractDataSource&, const ViewportParams&, std::any&)
                -> std::shared_ptr<QCPAbstractDataSource> {
                started.store(true);
                while (!gate.load()) QThread::msleep(5);
                return std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
                    std::vector<double>{1}, std::vector<double>{1});
            });

        pipeline->setSource(source.get());
        while (!started.load()) QThread::msleep(5);

        // Destroy pipeline while job is running — should not crash
        pipeline.reset();
    }

    gate.store(true);
    QThread::msleep(100); // let pool thread finish — no crash = pass
}
```

- [ ] **Step 3: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: all pipeline tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/datasource/async-pipeline.h src/datasource/async-pipeline.cpp \
        tests/auto/test-pipeline/test-pipeline.h tests/auto/test-pipeline/test-pipeline.cpp \
        meson.build
git commit -m "feat: add QCPAsyncPipeline<In,Out> — typed async transform template"
```

---

## Chunk 2: QCustomPlot Integration + QCPGraph2 Pipeline

### Task 5: Add QCPPipelineScheduler to QCustomPlot

**Files:**
- Modify: `src/core.h`
- Modify: `src/core.cpp`

- [ ] **Step 1: Add scheduler member to QCustomPlot**

In `src/core.h`, add forward declaration and member:

```cpp
// Forward declaration (near top, with other forwards)
class QCPPipelineScheduler;

// In the QCustomPlot class, public section:
QCPPipelineScheduler* pipelineScheduler() const { return mPipelineScheduler; }
void setMaxPipelineThreads(int count);

// In the QCustomPlot class, private section:
QCPPipelineScheduler* mPipelineScheduler;
```

- [ ] **Step 2: Initialize scheduler in constructor**

In `src/core.cpp`, add `#include <datasource/pipeline-scheduler.h>` and in the constructor body:

```cpp
mPipelineScheduler = new QCPPipelineScheduler(0, this);
```

Add `setMaxPipelineThreads`:

```cpp
void QCustomPlot::setMaxPipelineThreads(int count)
{
    mPipelineScheduler->setMaxThreads(count);
}
```

- [ ] **Step 3: Build and run existing tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: all existing tests still pass (no behavioral change).

- [ ] **Step 4: Commit**

```bash
git add src/core.h src/core.cpp
git commit -m "feat: add QCPPipelineScheduler to QCustomPlot"
```

---

### Task 6: Wire QCPGraph2 to Pipeline

**Files:**
- Modify: `src/plottables/plottable-graph2.h`
- Modify: `src/plottables/plottable-graph2.cpp`
- Modify: `tests/auto/test-pipeline/test-pipeline.h` (add graph2 pipeline tests)
- Modify: `tests/auto/test-pipeline/test-pipeline.cpp`

- [ ] **Step 1: Add graph2 pipeline tests to header**

Add to `tests/auto/test-pipeline/test-pipeline.h` private slots:

```cpp
    // QCPGraph2 pipeline integration
    void graph2PipelinePassthrough();
    void graph2PipelineTransform();
```

- [ ] **Step 2: Write the graph2 pipeline tests**

Add to `tests/auto/test-pipeline/test-pipeline.cpp`:

```cpp
void TestPipeline::graph2PipelinePassthrough()
{
    auto* graph = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    graph->setData(std::vector<double>{1, 2, 3}, std::vector<double>{4, 5, 6});

    // No transform — passthrough, draw should work
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QCOMPARE(graph->dataSource()->size(), 3);
}

void TestPipeline::graph2PipelineTransform()
{
    auto* graph = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);

    graph->pipeline().setTransform(TransformKind::ViewportIndependent,
        [](const QCPAbstractDataSource& src, const ViewportParams&, std::any&)
            -> std::shared_ptr<QCPAbstractDataSource> {
            int n = src.size();
            std::vector<double> keys(n), values(n);
            for (int i = 0; i < n; ++i)
            {
                keys[i] = src.keyAt(i);
                values[i] = src.valueAt(i) * 2; // double all values
            }
            return std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
                std::move(keys), std::move(values));
        });

    graph->setData(std::vector<double>{1, 2, 3}, std::vector<double>{4, 5, 6});

    QSignalSpy spy(&graph->pipeline(), &QCPGraphPipeline::finished);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);

    // Pipeline result should have doubled values
    QCOMPARE(graph->pipeline().result()->valueAt(0), 8.0);
    QCOMPARE(graph->pipeline().result()->valueAt(1), 10.0);
    QCOMPARE(graph->pipeline().result()->valueAt(2), 12.0);

    // Draw should use pipeline result
    mPlot->replot(QCustomPlot::rpImmediateRefresh);
}
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: compilation error — `graph->pipeline()` does not exist.

- [ ] **Step 4: Modify QCPGraph2 header**

In `src/plottables/plottable-graph2.h`, add include and pipeline member:

```cpp
#include "datasource/async-pipeline.h"
```

Add to public section:

```cpp
    QCPGraphPipeline& pipeline() { return mPipeline; }
    const QCPGraphPipeline& pipeline() const { return mPipeline; }
```

Add to private section:

```cpp
    QCPGraphPipeline mPipeline;
```

- [ ] **Step 5: Modify QCPGraph2 constructor and methods**

In `src/plottables/plottable-graph2.cpp`:

Constructor — initialize pipeline with scheduler and connect viewport signals:

```cpp
QCPGraph2::QCPGraph2(QCPAxis* keyAxis, QCPAxis* valueAxis)
    : QCPAbstractPlottable(keyAxis, valueAxis)
    , mPipeline(parentPlot() ? parentPlot()->pipelineScheduler() : nullptr, this)
{
    setPen(QPen(Qt::blue, 0));
    setBrush(Qt::NoBrush);

    // Connect axis range changes for viewport-dependent transforms
    if (keyAxis)
    {
        connect(keyAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged),
                this, &QCPGraph2::onViewportChanged);
    }
    if (valueAxis)
    {
        connect(valueAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged),
                this, &QCPGraph2::onViewportChanged);
    }

    // Replot when pipeline produces a result
    connect(&mPipeline, &QCPGraphPipeline::finished,
            this, [this](uint64_t) {
                if (parentPlot())
                    parentPlot()->replot(QCustomPlot::rpQueuedReplot);
            });
}
```

Add a private `onViewportChanged` helper (add declaration to header too):

```cpp
void QCPGraph2::onViewportChanged()
{
    if (!mKeyAxis || !mValueAxis) return;
    auto* axisRect = mKeyAxis->axisRect();
    if (!axisRect) return;

    ViewportParams vp;
    vp.keyRange = mKeyAxis->range();
    vp.valueRange = mValueAxis->range();
    vp.plotWidthPx = axisRect->width();
    vp.plotHeightPx = axisRect->height();
    vp.keyLogScale = (mKeyAxis->scaleType() == QCPAxis::stLogarithmic);
    vp.valueLogScale = (mValueAxis->scaleType() == QCPAxis::stLogarithmic);

    mPipeline.onViewportChanged(vp);
}
```

`setDataSource` methods — wire to pipeline:

```cpp
void QCPGraph2::setDataSource(std::unique_ptr<QCPAbstractDataSource> source)
{
    mDataSource = std::move(source);
    mPipeline.setSource(mDataSource.get());
}

void QCPGraph2::setDataSource(std::shared_ptr<QCPAbstractDataSource> source)
{
    mDataSource = std::move(source);
    mPipeline.setSource(mDataSource.get());
}
```

`dataChanged()` — forward to pipeline:

```cpp
void QCPGraph2::dataChanged()
{
    if (mPipeline.hasTransform())
        mPipeline.onDataChanged();
}
```

`draw()` — read from pipeline result when available:

In the `draw()` method, replace direct `mDataSource->` calls with pipeline result. The key change is the data source used for `getOptimizedLineData` / `getLines`:

```cpp
const QCPAbstractDataSource* ds = mPipeline.hasTransform()
    ? mPipeline.result()
    : mDataSource.get();
if (!ds) return; // pipeline result not yet available
```

Use `ds` instead of `mDataSource.get()` for `findBegin`, `findEnd`, `getOptimizedLineData`, `getLines` in draw(). Also use `ds` for the `dataCount() == 0` early return check.

- [ ] **Step 6: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: all tests pass including new graph2 pipeline tests. Existing graph2 tests still pass (passthrough mode).

- [ ] **Step 7: Commit**

```bash
git add src/plottables/plottable-graph2.h src/plottables/plottable-graph2.cpp \
        tests/auto/test-pipeline/test-pipeline.h tests/auto/test-pipeline/test-pipeline.cpp
git commit -m "feat: wire QCPGraph2 to async pipeline"
```

---

## Chunk 3: QCPColorMap2 Migration

### Task 7: Migrate QCPColorMap2 to Pipeline

**Files:**
- Modify: `src/plottables/plottable-colormap2.h`
- Modify: `src/plottables/plottable-colormap2.cpp`
- Modify: `tests/auto/test-pipeline/test-pipeline.h`
- Modify: `tests/auto/test-pipeline/test-pipeline.cpp`

- [ ] **Step 1: Add colormap2 pipeline tests**

Add to `tests/auto/test-pipeline/test-pipeline.h` private slots:

```cpp
    // QCPColorMap2 pipeline integration
    void colormap2PipelineDefault();
    void colormap2PipelineResample();
```

- [ ] **Step 2: Write the colormap2 pipeline tests**

Add to `tests/auto/test-pipeline/test-pipeline.cpp`:

```cpp
#include <datasource/soa-datasource-2d.h>
#include <plottables/plottable-colormap.h> // for QCPColorMapData

void TestPipeline::colormap2PipelineDefault()
{
    auto* cm = new QCPColorMap2(mPlot->xAxis, mPlot->yAxis);

    std::vector<double> x = {0, 1, 2};
    std::vector<double> y = {0, 1};
    std::vector<double> z = {1, 2, 3, 4, 5, 6};
    cm->setData(std::move(x), std::move(y), std::move(z));

    // Default transform should be the resampler
    QVERIFY(cm->pipeline().hasTransform());

    QSignalSpy spy(&cm->pipeline(), &QCPColormapPipeline::finished);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);

    QVERIFY(cm->pipeline().result() != nullptr);
}

void TestPipeline::colormap2PipelineResample()
{
    auto* cm = new QCPColorMap2(mPlot->xAxis, mPlot->yAxis);
    mPlot->xAxis->setRange(0, 2);
    mPlot->yAxis->setRange(0, 1);

    std::vector<double> x = {0, 1, 2};
    std::vector<double> y = {0, 1};
    std::vector<double> z = {1, 2, 3, 4, 5, 6};
    cm->setData(std::move(x), std::move(y), std::move(z));

    QSignalSpy spy(&cm->pipeline(), &QCPColormapPipeline::finished);
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);

    // Resampled data should be a grid
    auto* result = cm->pipeline().result();
    QVERIFY(result != nullptr);
    QVERIFY(result->keySize() > 0);
    QVERIFY(result->valueSize() > 0);

    mPlot->replot(QCustomPlot::rpImmediateRefresh);
}
```

- [ ] **Step 3: Run tests to verify they fail**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: compilation error — `cm->pipeline()` does not exist.

- [ ] **Step 4: Modify QCPColorMap2 header**

In `src/plottables/plottable-colormap2.h`:

Replace `#include "colormap-resampler.h"` with `#include <datasource/async-pipeline.h>`.

Add to public section:

```cpp
    QCPColormapPipeline& pipeline() { return mPipeline; }
    const QCPColormapPipeline& pipeline() const { return mPipeline; }
```

Replace the private member `QCPColormapResampler* mResampler;` with:

```cpp
    QCPColormapPipeline mPipeline;
```

Remove `void requestResample();` and `void onResampleFinished(uint64_t generation, QCPColorMapData* data);` — these are subsumed by the pipeline.

Keep `uint64_t mCurrentGeneration` — remove it (pipeline tracks generation internally).

- [ ] **Step 5: Modify QCPColorMap2 implementation**

In `src/plottables/plottable-colormap2.cpp`:

Replace `#include "colormap-resampler.h"` with `#include <datasource/async-pipeline.h>` and `#include <datasource/pipeline-scheduler.h>`.

Add `#include <datasource/resample.h>`.

Rewrite constructor — set up pipeline with resample transform as default:

```cpp
QCPColorMap2::QCPColorMap2(QCPAxis* keyAxis, QCPAxis* valueAxis)
    : QCPAbstractPlottable(keyAxis, valueAxis)
    , mPipeline(parentPlot() ? parentPlot()->pipelineScheduler() : nullptr, this)
{
    mGradient.loadPreset(QCPColorGradient::gpCold);

    // Default transform: resample to viewport resolution
    mPipeline.setTransform(TransformKind::ViewportDependent,
        [gapThreshold = &mGapThreshold](
            const QCPAbstractDataSource2D& src,
            const ViewportParams& vp,
            std::any& /*cache*/) -> std::shared_ptr<QCPColorMapData> {
            if (src.xSize() < 2) return nullptr;

            bool found = false;
            auto xRange = src.xRange(found);
            if (!found) return nullptr;
            auto yRange = src.yRange(found);
            if (!found) return nullptr;

            int xBegin = src.findXBegin(vp.keyRange.lower);
            int xEnd = src.findXEnd(vp.keyRange.upper);
            if (xEnd <= xBegin) return nullptr;

            double visibleFractionX = vp.keyRange.size() / xRange.size();
            double visibleFractionY = vp.valueRange.size() / yRange.size();
            int w = std::min(vp.plotWidthPx * 4,
                    static_cast<int>((xEnd - xBegin) / std::max(0.01, visibleFractionX)));
            int h = std::min(vp.plotHeightPx * 4,
                    static_cast<int>(src.ySize() / std::max(0.01, visibleFractionY)));
            if (w <= 0 || h <= 0) return nullptr;

            auto* raw = qcp::algo2d::resample(src, xBegin, xEnd,
                vp.keyRange, vp.valueRange, w, h, vp.valueLogScale, *gapThreshold);
            return std::shared_ptr<QCPColorMapData>(raw);
        });

    // Connect axis signals to pipeline viewport updates
    if (keyAxis)
    {
        connect(keyAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged),
                this, &QCPColorMap2::onViewportChanged);
    }
    if (valueAxis)
    {
        connect(valueAxis, QOverload<const QCPRange&>::of(&QCPAxis::rangeChanged),
                this, &QCPColorMap2::onViewportChanged);
        connect(valueAxis, &QCPAxis::scaleTypeChanged,
                this, &QCPColorMap2::onViewportChanged);
    }

    connect(&mPipeline, &QCPColormapPipeline::finished,
            this, [this](uint64_t) {
                mMapImageInvalidated = true;
                if (parentPlot())
                    parentPlot()->replot(QCustomPlot::rpQueuedReplot);
            });
}
```

Add a private `onViewportChanged` helper method:

```cpp
void QCPColorMap2::onViewportChanged()
{
    if (!mKeyAxis || !mValueAxis || !mDataSource) return;
    auto* axisRect = mKeyAxis->axisRect();
    if (!axisRect) return;

    ViewportParams vp;
    vp.keyRange = mKeyAxis->range();
    vp.valueRange = mValueAxis->range();
    vp.plotWidthPx = axisRect->width();
    vp.plotHeightPx = axisRect->height();
    vp.keyLogScale = (mKeyAxis->scaleType() == QCPAxis::stLogarithmic);
    vp.valueLogScale = (mValueAxis->scaleType() == QCPAxis::stLogarithmic);

    mPipeline.onViewportChanged(vp);
}
```

Update `setDataSource`:

```cpp
void QCPColorMap2::setDataSource(std::shared_ptr<QCPAbstractDataSource2D> source)
{
    mDataSource = std::move(source);
    mPipeline.setSource(mDataSource.get());
}
```

Remove the old `requestResample()` and `onResampleFinished()` methods entirely.

Update destructor — remove `mResampler->stop()` and `delete mResampledData`:

```cpp
QCPColorMap2::~QCPColorMap2()
{
    if (mRhiLayer)
    {
        if (auto* plot = parentPlot())
            plot->unregisterColormapRhiLayer(mRhiLayer);
        delete mRhiLayer;
    }
}
```

Update `dataChanged()` — forward to pipeline:

```cpp
void QCPColorMap2::dataChanged()
{
    mPipeline.onDataChanged();
}
```

Update `draw()` — read from pipeline result:

Replace all `mResampledData` usage with `mPipeline.result()`:

```cpp
void QCPColorMap2::draw(QCPPainter* painter)
{
    auto* resampledData = mPipeline.result();
    if (!resampledData)
    {
        if (mDataSource)
            onViewportChanged(); // trigger first resample
        return;
    }
    // ... rest of draw using resampledData instead of mResampledData
}
```

Update `updateMapImage()` — replace `mResampledData->` with `mPipeline.result()->`:

All calls to `mResampledData->keySize()`, `mResampledData->valueSize()`, `mResampledData->cell()` etc. become `mPipeline.result()->keySize()` etc. Add a null guard at the top:

```cpp
void QCPColorMap2::updateMapImage()
{
    auto* data = mPipeline.result();
    if (!data) return;
    // ... use data-> instead of mResampledData->
}
```

Update `rescaleDataRange()` — replace `mResampledData` reference:

```cpp
void QCPColorMap2::rescaleDataRange(bool recalc)
{
    auto* data = mPipeline.result();
    if (!data) return;
    if (recalc) data->recalculateDataBounds();
    setDataRange(data->dataBounds());
}
```

Remove the `mResampledData` member, `mCurrentGeneration`, and `delete mResampledData` from the header and destructor — pipeline manages result lifetime now.

Also add `void onViewportChanged();` as a private slot declaration in the header.

- [ ] **Step 6: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: all tests pass including new colormap2 pipeline tests. Existing colormap tests still pass.

- [ ] **Step 7: Commit**

```bash
git add src/plottables/plottable-colormap2.h src/plottables/plottable-colormap2.cpp \
        tests/auto/test-pipeline/test-pipeline.h tests/auto/test-pipeline/test-pipeline.cpp
git commit -m "feat: migrate QCPColorMap2 to async pipeline"
```

---

### Task 8: Remove Obsolete QCPColormapResampler + QCPResamplerScheduler

**Files:**
- Delete: `src/datasource/resampler-scheduler.h`
- Delete: `src/datasource/resampler-scheduler.cpp`
- Delete: `src/plottables/colormap-resampler.h`
- Delete: `src/plottables/colormap-resampler.cpp`
- Modify: `meson.build` (remove old sources and MOC headers)

- [ ] **Step 1: Verify no remaining references**

Search the codebase for `#include.*resampler-scheduler` and `#include.*colormap-resampler` — should only appear in the files being deleted.

Search for `QCPResamplerScheduler` and `QCPColormapResampler` — should have no remaining references.

- [ ] **Step 2: Remove files and update meson.build**

Delete the 4 files. Remove their entries from `meson.build`:
- Remove `'src/datasource/resampler-scheduler.cpp'` from library sources
- Remove `'src/plottables/colormap-resampler.cpp'` from library sources
- Remove `'src/datasource/resampler-scheduler.h'` from `neoqcp_moc_headers`
- Remove `'src/plottables/colormap-resampler.h'` from `neoqcp_moc_headers`

- [ ] **Step 3: Build and run all tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: all tests pass. No references to old classes remain.

- [ ] **Step 4: Commit**

```bash
git rm src/datasource/resampler-scheduler.h src/datasource/resampler-scheduler.cpp \
       src/plottables/colormap-resampler.h src/plottables/colormap-resampler.cpp
git add meson.build
git commit -m "refactor: remove obsolete QCPColormapResampler and QCPResamplerScheduler"
```

---

## Chunk 4: Final Verification

### Task 9: End-to-End Integration Test

**Files:**
- Modify: `tests/auto/test-pipeline/test-pipeline.h`
- Modify: `tests/auto/test-pipeline/test-pipeline.cpp`

- [ ] **Step 1: Add end-to-end tests**

Add to `tests/auto/test-pipeline/test-pipeline.h` private slots:

```cpp
    // End-to-end
    void graph2DataFromExternalThread();
    void colormap2DataFromExternalThread();
```

- [ ] **Step 2: Write the e2e tests**

```cpp
void TestPipeline::graph2DataFromExternalThread()
{
    auto* graph = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    QSignalSpy spy(&graph->pipeline(), &QCPGraphPipeline::finished);

    graph->pipeline().setTransform(TransformKind::ViewportIndependent,
        [](const QCPAbstractDataSource& src, const ViewportParams&, std::any&)
            -> std::shared_ptr<QCPAbstractDataSource> {
            int n = src.size();
            std::vector<double> k(n), v(n);
            for (int i = 0; i < n; ++i) { k[i] = src.keyAt(i); v[i] = src.valueAt(i); }
            return std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
                std::move(k), std::move(v));
        });

    // Set data from a different thread
    auto source = std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
        std::vector<double>{1, 2, 3}, std::vector<double>{4, 5, 6});

    QThread thread;
    thread.start();
    QMetaObject::invokeMethod(&thread, [&]{
        graph->setDataSource(source);
    }, Qt::BlockingQueuedConnection);

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 1000);
    QVERIFY(graph->pipeline().result() != nullptr);
    QCOMPARE(graph->pipeline().result()->size(), 3);

    thread.quit();
    thread.wait();
}

void TestPipeline::colormap2DataFromExternalThread()
{
    auto* cm = new QCPColorMap2(mPlot->xAxis, mPlot->yAxis);
    mPlot->xAxis->setRange(0, 2);
    mPlot->yAxis->setRange(0, 1);

    QSignalSpy spy(&cm->pipeline(), &QCPColormapPipeline::finished);

    auto source = std::make_shared<QCPSoADataSource2D<
        std::vector<double>, std::vector<double>, std::vector<double>>>(
        std::vector<double>{0, 1, 2},
        std::vector<double>{0, 1},
        std::vector<double>{1, 2, 3, 4, 5, 6});

    QThread thread;
    thread.start();
    QMetaObject::invokeMethod(&thread, [&]{
        cm->setDataSource(source);
    }, Qt::BlockingQueuedConnection);

    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, 2000);
    QVERIFY(cm->pipeline().result() != nullptr);

    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    thread.quit();
    thread.wait();
}
```

- [ ] **Step 3: Build and run all tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: all tests pass.

- [ ] **Step 4: Run manual test app**

Run: `build/tests/manual/manual-test`
Expected: graphs and colormaps render correctly. Verify pan/zoom responsiveness.

- [ ] **Step 5: Commit**

```bash
git add tests/auto/test-pipeline/test-pipeline.h tests/auto/test-pipeline/test-pipeline.cpp
git commit -m "test: add end-to-end pipeline tests with cross-thread data"
```
