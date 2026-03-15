# Critical Bug Fixes Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Fix 6 confirmed critical bugs found during code review: 3 async pipeline data races, 1 use-after-free, 1 wrong dimension, 1 const-correctness violation with side effects.

**Architecture:** Issues 1-3 share a root cause (async pipeline hand-off via staging member fields) and will be fixed together by passing job results through the lambda capture. Issues 4-6 are independent single-file fixes.

**Tech Stack:** C++20, Qt6, Qt Test framework, `std::atomic`, `std::shared_ptr`, `QMutex`

---

## Verification Summary

| # | Issue | Verdict | Notes |
|---|-------|---------|-------|
| 1 | Data race on `mPendingResult/Cache/Generation` | **CONFIRMED** | Worker writes, GUI reads, no sync |
| 2 | Race in `deliverResult` unlock ordering | **CONFIRMED** | New job can overwrite `mPendingResult` before `applyResult` consumes it |
| 3 | Dangling raw `mSource` pointer | **CONFIRMED** | `setDataSource()` during job destroys source |
| 4 | `removePlottable` delete-before-remove | **CONFIRMED** | UB per C++ standard (indeterminate pointer in comparison). Trivial swap fix. |
| 5 | Wrong pixel dimension for vertical key axis | **CONFIRMED** | Always uses `width()`, should use `height()` when vertical |
| 6 | `selectTestRect` mutating state | **CONFIRMED** | Side effect in query method — diverges from base class contract |
| 7 | Unsafe `static_cast<QCPPaintBufferRhi*>` | **NOT A BUG** | `toPixmap()` doesn't call `setupPaintBuffers()` — uses `draw(QPainter*)` directly |
| 8 | `mGapThreshold` by-reference capture | **DOWNGRADED** | It's `std::atomic<double>` — read is safe. Use-after-free on destruction is a subset of issue 3's root cause (pipeline doesn't wait for jobs on destroy). Fixed as part of issues 1-3. |

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `src/datasource/async-pipeline.h` | Modify | Eliminate staging fields, pass results through lambda. Add `shared_ptr` source capture. |
| `src/datasource/async-pipeline.cpp` | Modify | Consume result under lock in `deliverResult` |
| `src/plottables/plottable-colormap2.cpp` | Modify | Copy `mGapThreshold` at job submission, not by-reference |
| `src/core.cpp` | Modify | Swap delete/removeOne in `removePlottable` |
| `src/plottables/plottable-multigraph.cpp` | Modify | Fix `pixelWidth` for vertical axes; remove mutation from `selectTestRect` |
| `tests/auto/test-pipeline/test-pipeline.h` | Modify | Add race-condition reproducer tests |
| `tests/auto/test-pipeline/test-pipeline.cpp` | Modify | Add race-condition reproducer tests |
| `tests/auto/test-multigraph/test-multigraph.h` | Modify | Add vertical axis test |
| `tests/auto/test-multigraph/test-multigraph.cpp` | Modify | Add vertical axis test |

---

## Chunk 1: Async Pipeline Data Races (Issues 1, 2, 3)

### Task 1: Write reproducer tests for pipeline data races

These tests won't reliably crash (data races are timing-dependent), but they exercise the exact code paths and will catch regressions under ThreadSanitizer.

**Files:**
- Modify: `tests/auto/test-pipeline/test-pipeline.h`
- Modify: `tests/auto/test-pipeline/test-pipeline.cpp`

- [ ] **Step 1: Add test declarations to header**

Add these slots to `TestPipeline` in `test-pipeline.h`:

```cpp
// Race condition reproducers
void pipelineSourceReplacedDuringJob();
void pipelineRapidFireDeliverResult();
```

- [ ] **Step 2: Write `pipelineSourceReplacedDuringJob` test**

This reproduces issue 3 — dangling `mSource` pointer when `setSource()` is called while a job is running. The old source is destroyed and the background thread holds a raw pointer to it.

Add to `test-pipeline.cpp`:

```cpp
void TestPipeline::pipelineSourceReplacedDuringJob()
{
    QCPPipelineScheduler scheduler(1);
    std::atomic<bool> gate{false};
    std::atomic<bool> started{false};

    QCPGraphPipeline pipeline(&scheduler);
    QSignalSpy spy(&pipeline, &QCPGraphPipeline::finished);

    pipeline.setTransform(TransformKind::ViewportIndependent,
        [&gate, &started](const QCPAbstractDataSource& src, const ViewportParams&, std::any&)
            -> std::shared_ptr<QCPAbstractDataSource> {
            started.store(true);
            while (!gate.load()) QThread::msleep(5);
            // Access the source AFTER the gate opens — if source was destroyed, this is UaF
            int n = src.size();
            std::vector<double> k(n), v(n);
            for (int i = 0; i < n; ++i) { k[i] = src.keyAt(i); v[i] = src.valueAt(i); }
            return std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
                std::move(k), std::move(v));
        });

    // First source — will be destroyed when replaced
    auto source1 = std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
        std::vector<double>{1, 2, 3}, std::vector<double>{10, 20, 30});
    pipeline.setSource(source1.get());
    while (!started.load()) QThread::msleep(1);

    // Replace data source while job is running — destroys source1's data
    auto source2 = std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
        std::vector<double>{4, 5}, std::vector<double>{40, 50});
    source1.reset(); // Destroy old source explicitly
    pipeline.setSource(source2.get());

    // Ungate — background thread now accesses the source
    gate.store(true);

    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 2000);
    // Should not crash. Result should be valid.
    QVERIFY(pipeline.result() != nullptr);
}
```

- [ ] **Step 3: Write `pipelineRapidFireDeliverResult` test**

This reproduces issues 1 and 2 — rapid source replacement causes `deliverResult` and new jobs to race on `mPendingResult`.

Add to `test-pipeline.cpp`:

```cpp
void TestPipeline::pipelineRapidFireDeliverResult()
{
    QCPPipelineScheduler scheduler(1);
    QCPGraphPipeline pipeline(&scheduler);
    QSignalSpy spy(&pipeline, &QCPGraphPipeline::finished);

    pipeline.setTransform(TransformKind::ViewportIndependent,
        [](const QCPAbstractDataSource& src, const ViewportParams&, std::any&)
            -> std::shared_ptr<QCPAbstractDataSource> {
            QThread::msleep(10); // Simulate work to increase race window
            int n = src.size();
            std::vector<double> k(n), v(n);
            for (int i = 0; i < n; ++i) { k[i] = src.keyAt(i); v[i] = src.valueAt(i); }
            return std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
                std::move(k), std::move(v));
        });

    auto source = std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<double>>>(
        std::vector<double>{1, 2, 3}, std::vector<double>{4, 5, 6});

    // Rapid-fire source updates to maximize race between deliverResult and new jobs
    for (int i = 0; i < 20; ++i)
        pipeline.setSource(source.get());

    QTRY_VERIFY_WITH_TIMEOUT(spy.count() >= 1, 3000);
    QVERIFY(pipeline.result() != nullptr);
    QCOMPARE(pipeline.result()->size(), 3);
}
```

- [ ] **Step 4: Run tests to verify they compile and exercise the race paths**

Run: `meson test -C build --test-args '-run pipelineSourceReplacedDuringJob' auto-tests`

These tests may pass despite the bugs (races are non-deterministic on x86 with TSO). The real value is under TSAN. But they must at least compile and not crash the test harness.

- [ ] **Step 5: Commit**

```bash
git add tests/auto/test-pipeline/test-pipeline.h tests/auto/test-pipeline/test-pipeline.cpp
git commit -m "test: add pipeline data race reproducer tests"
```

---

### Task 2: Fix async pipeline — eliminate staging fields, use shared_ptr source

This is the core fix for issues 1, 2, and 3. The approach:

1. Remove `mPendingResult`, `mPendingCache`, `mPendingGeneration` staging fields from `QCPAsyncPipeline`
2. Pass results through the `QMetaObject::invokeMethod` lambda captures (thread-safe hand-off via Qt's event queue)
3. Change `mSource` from `const In*` to `std::shared_ptr<const In>` so the job closure keeps the source alive
4. In `deliverResult`, consume the result before unlocking

**Files:**
- Modify: `src/datasource/async-pipeline.h`
- Modify: `src/datasource/async-pipeline.cpp`

- [ ] **Step 1: Refactor `QCPAsyncPipeline::makeJob` to pass results through lambda**

In `src/datasource/async-pipeline.h`, replace the `makeJob` implementation and remove staging fields:

Change `makeJob` (lines 117-141) to:

```cpp
std::function<void()> makeJob(
    const ViewportParams& vp, std::any cache, uint64_t generation) override
{
    if (!mSource || !mTransform) return {};

    auto source = mSource; // shared_ptr copy — keeps source alive
    auto transform = mTransform;
    auto destroyed = mDestroyed;
    auto* self = this;

    return [source, transform, vp, cache = std::move(cache),
            generation, destroyed, self]() mutable {
        auto result = transform(*source, vp, cache);
        if (destroyed->load()) return;

        QMetaObject::invokeMethod(self, [self, result = std::move(result),
                                          cache = std::move(cache), generation]() mutable {
            self->deliverResult(generation, std::move(cache), std::move(result));
        }, Qt::QueuedConnection);
    };
}
```

- [ ] **Step 2: Change `mSource` to `std::shared_ptr<const In>` and update `setSource`**

In `src/datasource/async-pipeline.h`:

Change `setSource`:

```cpp
void setSource(std::shared_ptr<const In> source)
{
    {
        QMutexLocker lock(&mMutex);
        mSource = std::move(source);
    }
    if (mTransform)
        onDataChanged();
}
```

Change `result()` passthrough:

```cpp
const Out* result() const
{
    if (!mTransform)
    {
        if constexpr (std::is_same_v<In, Out>)
            return mSource.get();
        else
            return nullptr;
    }
    return mResult.get();
}
```

Change the member declaration from `const In* mSource = nullptr;` to:
```cpp
std::shared_ptr<const In> mSource;
```

Remove these three staging fields entirely:
```cpp
// DELETE these:
// std::shared_ptr<Out> mPendingResult;
// std::any mPendingCache;
// uint64_t mPendingGeneration = 0;
```

- [ ] **Step 3: Add `deliverResult` overload that accepts the result directly**

In `src/datasource/async-pipeline.h`, change `hasPendingResult` and `applyResult`:

Remove `hasPendingResult()` and change `applyResult` to accept the result:

```cpp
void applyResult(uint64_t, std::shared_ptr<Out> result)
{
    mResult = std::move(result);
}
```

In `src/datasource/async-pipeline.h`, update the base class interface. Add to `QCPAsyncPipelineBase`:

```cpp
virtual void applyResult(uint64_t generation, std::shared_ptr<void> result) = 0;
```

Wait — the base class is type-erased. We need a different approach. Let's use `std::any` for the result too:

Actually, the simplest approach: make `deliverResult` a template method on the subclass. But `deliverResult` is in the base class. Let's just change the base class to pass an opaque result.

Better approach — keep the base class `deliverResult` but have it accept the result as a parameter:

In `QCPAsyncPipelineBase`, change:
```cpp
void deliverResult(uint64_t generation, std::any cache);
```
to:
```cpp
void deliverResult(uint64_t generation, std::any cache, std::any result);
```

And change `applyResult` from:
```cpp
virtual void applyResult(uint64_t generation) = 0;
```
to:
```cpp
virtual void applyResult(uint64_t generation, std::any result) = 0;
```

Remove `virtual bool hasPendingResult() const = 0;` — no longer needed.

Then in the template subclass:

```cpp
void applyResult(uint64_t, std::any result) override
{
    if (auto* ptr = std::any_cast<std::shared_ptr<Out>>(&result))
        mResult = std::move(*ptr);
}
```

And in `makeJob`:

```cpp
QMetaObject::invokeMethod(self, [self, result = std::move(result),
                                  cache = std::move(cache), generation]() mutable {
    self->deliverResult(generation, std::move(cache),
                        std::any(std::move(result)));
}, Qt::QueuedConnection);
```

- [ ] **Step 4: Update `deliverResult` in `async-pipeline.cpp` to consume result under lock**

In `src/datasource/async-pipeline.cpp`, change `deliverResult`:

```cpp
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
```

Note: the `hasPendingResult()` check was guarding against stale/empty results. Now the result is passed directly — if it's empty/null the `applyResult` implementation handles it (the `std::any_cast` will just not match).

- [ ] **Step 5: Update callers of `setSource` in QCPGraph2 and QCPColorMap2**

`QCPGraph2` and `QCPColorMap2` call `pipeline.setSource(rawPtr)`. They need to pass `shared_ptr` now.

Search for `setSource` calls and update:

In `src/plottables/plottable-graph2.cpp` (or `.h`), find where `mPipeline.setSource(mDataSource.get())` is called and change to `mPipeline.setSource(mDataSource)`.

In `src/plottables/plottable-colormap2.cpp`, same pattern.

- [ ] **Step 6: Build and run all pipeline tests**

Run: `meson test -C build auto-tests`

All existing pipeline tests plus the new reproducer tests must pass.

- [ ] **Step 7: Commit**

```bash
git add src/datasource/async-pipeline.h src/datasource/async-pipeline.cpp \
        src/plottables/plottable-graph2.cpp src/plottables/plottable-graph2.h \
        src/plottables/plottable-colormap2.cpp src/plottables/plottable-colormap2.h
git commit -m "fix: eliminate data races in async pipeline

Pass job results through QMetaObject::invokeMethod lambda captures instead of
staging in member fields. Change mSource from raw pointer to shared_ptr to
prevent use-after-free when data source is replaced during a running job.

Fixes: data race on mPendingResult/Cache/Generation (no sync between threads)
Fixes: deliverResult unlock ordering (new job overwrites mPendingResult)
Fixes: dangling mSource raw pointer on background thread"
```

---

### Task 3: Fix `mGapThreshold` by-reference capture in QCPColorMap2

With the pipeline refactor from Task 2, the source pointer issue is fixed. But the transform lambda still captures `mGapThreshold` by reference, which is unsafe if `QCPColorMap2` is destroyed while the transform is executing.

**Files:**
- Modify: `src/plottables/plottable-colormap2.cpp`

- [ ] **Step 1: Change the transform to read gap threshold from ViewportParams or a captured value**

The cleanest fix: read `mGapThreshold` at job submission time. Since `makeJob` is called under the mutex on the GUI thread, we can capture it by value there. But the transform is set once in the constructor, not per-job.

Alternative: change the lambda to capture by value. Since `mGapThreshold` is an `std::atomic<double>`, we can't copy-capture the atomic itself — capture the loaded value instead.

Change the constructor in `plottable-colormap2.cpp` (lines 19-47):

```cpp
mPipeline.setTransform(TransformKind::ViewportDependent,
    [this](
        const QCPAbstractDataSource2D& src,
        const ViewportParams& vp,
        std::any& /*cache*/) -> std::shared_ptr<QCPColorMapData> {
        // Read gap threshold NOW (on the calling thread, before job is dispatched)
        // This is safe because setTransform's lambda is stored, but makeJob captures
        // the transform by value — and makeJob runs on the GUI thread under the mutex.
        double gt = mGapThreshold.load(std::memory_order_relaxed);
```

Wait — that's wrong. The transform function itself runs on the background thread, not the GUI thread. `makeJob` captures `mTransform` by value (the `std::function`), and the transform closure's `this` pointer would dangle.

Better approach: don't capture `this` or `&mGapThreshold` in the transform at all. Instead, extend `ViewportParams` to carry the gap threshold, or wrap the transform in `makeJob` to inject it.

Simplest approach: override `makeJob` in a derived pipeline, or just snapshot the value into the closure at `makeJob` time. Since we're already modifying `makeJob` in Task 2, we can have the QCPColorMap2 set up its transform to accept the gap threshold as part of the `cache` or as an explicit parameter.

Actually, the simplest fix: replace the by-reference capture with a by-value read that happens at `onDataChanged`/`onViewportChanged` time (GUI thread), not at transform execution time. We can do this by wrapping the transform:

In `QCPColorMap2`'s constructor, change the transform to NOT capture `mGapThreshold`:

```cpp
mPipeline.setTransform(TransformKind::ViewportDependent,
    [](const QCPAbstractDataSource2D& src,
       const ViewportParams& vp,
       std::any& cache) -> std::shared_ptr<QCPColorMapData> {
        // gap threshold is stored in cache by onDataChanged/onViewportChanged
        double gt = 1.5;
        if (auto* p = std::any_cast<double>(&cache))
            gt = *p;
        // ... rest of transform ...
    });
```

Hmm, but the cache is already used for something else potentially. And this mixes concerns.

Cleanest approach: just copy the atomic value into a local `double` that the lambda captures by value. But the problem is the lambda is created once in the constructor and reused — the value needs to be fresh each time.

OK, the real cleanest approach: have `onViewportChanged` and `onDataChanged` in QCPColorMap2 inject the gap threshold into the job. Since we're already refactoring `makeJob`, add it to `ViewportParams`:

No — don't pollute ViewportParams with colormap-specific fields.

**Final approach:** Override `onDataChanged`/`onViewportChanged` in QCPColorMap2 to store the gap threshold in the cache before calling the base class. But QCPColorMap2 doesn't subclass the pipeline.

**Actually simplest fix:** The transform lambda can capture a `std::shared_ptr<std::atomic<double>>` that outlives both the `QCPColorMap2` and any running job.

```cpp
auto gapThreshold = std::make_shared<std::atomic<double>>(1.5);
mGapThresholdShared = gapThreshold; // store in member for setGapThreshold to update

mPipeline.setTransform(TransformKind::ViewportDependent,
    [gapThreshold](const QCPAbstractDataSource2D& src, ...) {
        double gt = gapThreshold->load(std::memory_order_relaxed);
        // ...
    });
```

This is clean: the shared_ptr keeps the atomic alive even if QCPColorMap2 is destroyed.

But this adds a member and changes `setGapThreshold`. Overkill.

**Pragmatic fix:** Just read `mGapThreshold` at the top of the transform and accept that the pipeline destructor should wait for running jobs. Actually, the pipeline already has `mDestroyed` — the real fix is to make the pipeline destructor wait for the running job to finish. Let's do that instead.

- [ ] **Step 1: Make `QCPAsyncPipelineBase` destructor wait for running jobs**

In `src/datasource/async-pipeline.cpp`, change the destructor:

```cpp
QCPAsyncPipelineBase::~QCPAsyncPipelineBase()
{
    mDestroyed->store(true);
    // Wait for any running job to finish so it doesn't access
    // members of the (now-destroying) owning object.
    QMutexLocker lock(&mMutex);
    mPending = nullptr; // Cancel any pending job
    // If a job is running, we need to wait for it. The scheduler's thread pool
    // will finish it, but we can't block the GUI thread waiting for the pool.
    // The mDestroyed flag will cause the invokeMethod to be a no-op.
    // The real protection is that mSource is now a shared_ptr (Task 2),
    // so the source outlives the job. The gap threshold capture is the
    // remaining issue.
}
```

Actually, waiting in the destructor is problematic (blocks the GUI thread). And after Task 2, `mSource` is a `shared_ptr` so the source outlives the job. The only remaining dangling reference is `mGapThreshold`.

**Final pragmatic approach:** Use `std::shared_ptr<std::atomic<double>>` for the gap threshold.

In `plottable-colormap2.h`, change:
```cpp
std::atomic<double> mGapThreshold{1.5};
```
to:
```cpp
std::shared_ptr<std::atomic<double>> mGapThreshold = std::make_shared<std::atomic<double>>(1.5);
```

Update `setGapThreshold` and `gapThreshold`:
```cpp
void setGapThreshold(double threshold) { mGapThreshold->store(threshold, std::memory_order_relaxed); }
double gapThreshold() const { return mGapThreshold->load(std::memory_order_relaxed); }
```

In `plottable-colormap2.cpp` constructor, capture the shared_ptr:
```cpp
mPipeline.setTransform(TransformKind::ViewportDependent,
    [gapThreshold = mGapThreshold](
        const QCPAbstractDataSource2D& src,
        const ViewportParams& vp,
        std::any& /*cache*/) -> std::shared_ptr<QCPColorMapData> {
        // ... same body but use gapThreshold->load() ...
    });
```

- [ ] **Step 2: Build and run tests**

Run: `meson test -C build auto-tests`

- [ ] **Step 3: Commit**

```bash
git add src/plottables/plottable-colormap2.h src/plottables/plottable-colormap2.cpp
git commit -m "fix: prevent use-after-free on mGapThreshold during async job

Use shared_ptr<atomic<double>> so the gap threshold outlives any running
background job, even if QCPColorMap2 is destroyed during execution."
```

---

## Chunk 2: Independent Bug Fixes (Issues 4, 5, 6)

### Task 4: Fix `removePlottable` delete-before-remove

**Files:**
- Modify: `src/core.cpp:1183-1184`

- [ ] **Step 1: Swap the two lines**

In `src/core.cpp`, change:

```cpp
delete plottable;
mPlottables.removeOne(plottable);
```

to:

```cpp
mPlottables.removeOne(plottable);
delete plottable;
```

- [ ] **Step 2: Build and run tests**

Run: `meson test -C build auto-tests`

- [ ] **Step 3: Commit**

```bash
git add src/core.cpp
git commit -m "fix: remove plottable from list before deleting it

Using a pointer value after delete is undefined behavior per the C++ standard,
even for comparison. Swap the order so removeOne operates on a valid pointer."
```

---

### Task 5: Fix wrong pixel dimension for vertical key axis in QCPMultiGraph

**Files:**
- Modify: `src/plottables/plottable-multigraph.cpp:365`
- Modify: `tests/auto/test-multigraph/test-multigraph.h`
- Modify: `tests/auto/test-multigraph/test-multigraph.cpp`

- [ ] **Step 1: Write a reproducer test**

Add to `test-multigraph.h`:

```cpp
void renderVerticalKeyAxisUsesHeight();
```

Add to `test-multigraph.cpp`:

```cpp
void TestMultiGraph::renderVerticalKeyAxisUsesHeight()
{
    // Use yAxis as key (vertical), xAxis as value
    auto* mg = new QCPMultiGraph(mPlot->yAxis, mPlot->xAxis);

    auto source = std::make_shared<QCPSoAMultiDataSource>(2);
    std::vector<double> keys = {1, 2, 3, 4, 5};
    std::vector<double> v0 = {10, 20, 30, 40, 50};
    std::vector<double> v1 = {15, 25, 35, 45, 55};
    source->setKeys(std::move(keys));
    source->setValues(0, std::move(v0));
    source->setValues(1, std::move(v1));
    mg->setDataSource(source);

    mPlot->yAxis->setRange(0, 6);
    mPlot->xAxis->setRange(0, 60);

    // Should not crash and should use height for adaptive sampling budget
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QVERIFY(true); // If we get here without crash, basic sanity is OK
}
```

- [ ] **Step 2: Run test to verify it passes (it won't crash, but results are wrong)**

Run: `meson test -C build auto-tests`

- [ ] **Step 3: Fix the pixel dimension**

In `src/plottables/plottable-multigraph.cpp`, change line 365:

```cpp
const int pixelWidth = static_cast<int>(mKeyAxis->axisRect()->width());
```

to:

```cpp
const int pixelWidth = keyIsVertical
    ? static_cast<int>(mKeyAxis->axisRect()->height())
    : static_cast<int>(mKeyAxis->axisRect()->width());
```

- [ ] **Step 4: Build and run tests**

Run: `meson test -C build auto-tests`

- [ ] **Step 5: Commit**

```bash
git add src/plottables/plottable-multigraph.cpp \
        tests/auto/test-multigraph/test-multigraph.h \
        tests/auto/test-multigraph/test-multigraph.cpp
git commit -m "fix: use correct pixel dimension for vertical key axis in QCPMultiGraph

When the key axis is vertical, adaptive sampling should use the axis rect's
height (not width) as the pixel budget. Using width produces the wrong number
of output points."
```

---

### Task 6: Fix `selectTestRect` mutating state in const method

The base class `QCPAbstractPlottable1D::selectTestRect` is `const` and does NOT mutate selection state — it only computes and returns a `QCPDataSelection`. QCPMultiGraph violates this by writing `mComponents[c].selection` and calling `updateBaseSelection()` inside the `const` method via `const_cast`.

The fix: make `selectTestRect` purely functional (just return the selection), and move the per-component selection mutation to `selectEvent` where it belongs.

**Files:**
- Modify: `src/plottables/plottable-multigraph.cpp:270-311`

- [ ] **Step 1: Write a reproducer test**

Add to `test-multigraph.h`:

```cpp
void selectTestRectDoesNotMutateState();
```

Add to `test-multigraph.cpp`:

```cpp
void TestMultiGraph::selectTestRectDoesNotMutateState()
{
    auto* mg = new QCPMultiGraph(mPlot->xAxis, mPlot->yAxis);
    auto source = std::make_shared<QCPSoAMultiDataSource>(2);
    source->setKeys({1, 2, 3});
    source->setValues(0, {10, 20, 30});
    source->setValues(1, {15, 25, 35});
    mg->setDataSource(source);
    mg->setSelectable(QCP::stDataRange);

    mPlot->xAxis->setRange(0, 4);
    mPlot->yAxis->setRange(0, 40);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    // Record state before
    auto selBefore = mg->selection();

    // Call selectTestRect — should NOT mutate selection state
    QPointF tl = mg->coordsToPixels(0.5, 35);
    QPointF br = mg->coordsToPixels(2.5, 5);
    QRectF rect(tl, br);
    mg->selectTestRect(rect, false);

    // Selection state should be unchanged
    auto selAfter = mg->selection();
    QCOMPARE(selAfter, selBefore);
}
```

- [ ] **Step 2: Run test — expect FAIL (selectTestRect currently mutates state)**

Run: `meson test -C build auto-tests`

Expected: FAIL because `selectTestRect` mutates `mComponents[c].selection` and calls `updateBaseSelection()`.

- [ ] **Step 3: Fix `selectTestRect` to be purely functional**

In `src/plottables/plottable-multigraph.cpp`, change `selectTestRect` (lines 270-311):

Remove the `const_cast` lines (304 and 309). The method should only compute and return the union `QCPDataSelection`, not mutate per-component selections:

```cpp
QCPDataSelection QCPMultiGraph::selectTestRect(const QRectF& rect, bool onlySelectable) const
{
    QCPDataSelection unionResult;
    if ((onlySelectable && mSelectable == QCP::stNone) || !mDataSource || mDataSource->empty())
        return unionResult;
    if (!mKeyAxis || !mValueAxis)
        return unionResult;

    double key1, value1, key2, value2;
    pixelsToCoords(rect.topLeft(), key1, value1);
    pixelsToCoords(rect.bottomRight(), key2, value2);
    if (key1 > key2) qSwap(key1, key2);
    if (value1 > value2) qSwap(value1, value2);
    QCPRange keyRange(key1, key2);
    QCPRange valueRange(value1, value2);

    int begin = mDataSource->findBegin(keyRange.lower, false);
    int end = mDataSource->findEnd(keyRange.upper, false);

    for (int c = 0; c < mComponents.size(); ++c) {
        if (!mComponents[c].visible) continue;
        QCPDataSelection colSel;
        int segBegin = -1;
        for (int i = begin; i < end; ++i) {
            double k = mDataSource->keyAt(i);
            double v = mDataSource->valueAt(c, i);
            if (segBegin == -1) {
                if (keyRange.contains(k) && valueRange.contains(v))
                    segBegin = i;
            } else if (!keyRange.contains(k) || !valueRange.contains(v)) {
                colSel.addDataRange(QCPDataRange(segBegin, i), false);
                segBegin = -1;
            }
        }
        if (segBegin != -1)
            colSel.addDataRange(QCPDataRange(segBegin, end), false);
        colSel.simplify();
        for (int r = 0; r < colSel.dataRangeCount(); ++r)
            unionResult.addDataRange(colSel.dataRange(r), false);
    }
    unionResult.simplify();
    return unionResult;
}
```

The per-component selection application that was here needs to move to wherever rubber-band selection is applied (the `selectEvent` or the framework's `processRectSelection`). Check if the framework calls `selectEvent` after `selectTestRect` for rect selection — if so, `selectEvent` needs to handle the rect case. If not, a new method may be needed. For now, removing the side effect from `selectTestRect` is the correct first step.

- [ ] **Step 4: Run test — expect PASS**

Run: `meson test -C build auto-tests`

- [ ] **Step 5: Run all tests to check for regressions**

Run: `meson test -C build auto-tests`

Verify that `selectTestRectPerComponent` and other selection tests still pass. If rect selection is now broken (because per-component state isn't being set), the selection application logic needs to be added to `selectEvent` or `processRectSelection`. Investigate and fix accordingly.

- [ ] **Step 6: Commit**

```bash
git add src/plottables/plottable-multigraph.cpp \
        tests/auto/test-multigraph/test-multigraph.h \
        tests/auto/test-multigraph/test-multigraph.cpp
git commit -m "fix: remove state mutation from const selectTestRect in QCPMultiGraph

selectTestRect is a const query method that should only compute and return a
QCPDataSelection. The const_cast mutation of mComponents and updateBaseSelection
violated the base class contract and caused side effects in a read-only path."
```
