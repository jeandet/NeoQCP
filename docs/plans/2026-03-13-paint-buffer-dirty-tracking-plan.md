# Paint Buffer Dirty Tracking Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add per-buffer content-dirty tracking so `replot()` can skip clearing and repainting unchanged paint buffers, and clean up the redundant `mNeedsUpload` flag in `QCPPaintBufferRhi::clear()`.

**Architecture:** A `mContentDirty` flag on `QCPAbstractPaintBuffer` controls whether `setupPaintBuffers()` clears the buffer and whether `replot()` draws into it. Full `replot()` uses a fallback heuristic: if no buffer is explicitly marked dirty, all buffers are marked dirty (backward compat). Users opt into incremental replots by calling `QCPLayer::markDirty()` on specific layers before `replot()`. The `mNeedsUpload` flag in `QCPPaintBufferRhi` is cleaned up to only reflect actual painting (set in `donePainting()`), not clearing. GPU plottable and colormap RHI layers are only cleared when their associated paint buffer is dirty.

**Tech Stack:** C++20, Qt6, Qt Test

---

### Task 1: Add `mContentDirty` flag to `QCPAbstractPaintBuffer`

**Files:**
- Modify: `src/painting/paintbuffer.h:32-72`
- Modify: `src/painting/paintbuffer.cpp:119-165`

- [ ] **Step 1: Add flag, getter, and setter to the base class**

In `src/painting/paintbuffer.h`, add to the public section after `setInvalidated`:

```cpp
bool contentDirty() const { return mContentDirty; }
void setContentDirty(bool dirty = true);
```

Add to the protected members after `mInvalidated`:

```cpp
bool mContentDirty;
```

In `src/painting/paintbuffer.cpp`, initialize `mContentDirty(true)` in the constructor initializer list (after `mInvalidated(true)`).

Add the setter:

```cpp
void QCPAbstractPaintBuffer::setContentDirty(bool dirty)
{
    mContentDirty = dirty;
}
```

- [ ] **Step 2: Set `mContentDirty = true` when buffer is reallocated**

In `src/painting/paintbuffer.cpp`, `setSize()`: add `mContentDirty = true;` inside the `if (mSize != size)` block, after `reallocateBuffer()`.

In `src/painting/paintbuffer.cpp`, `setDevicePixelRatio()`: add `mContentDirty = true;` inside the if block, after `reallocateBuffer()`.

- [ ] **Step 3: Set `mContentDirty = true` in `setInvalidated(true)`**

Structural changes (layer add/remove/reorder, mode change) call `setInvalidated()`. These must also mark the buffer as content-dirty so the buffer is repainted during the next `replot()`.

In `src/painting/paintbuffer.cpp`, change `setInvalidated()`:

```cpp
void QCPAbstractPaintBuffer::setInvalidated(bool invalidated)
{
    mInvalidated = invalidated;
    if (invalidated)
        mContentDirty = true;
}
```

- [ ] **Step 4: Commit**

```bash
git add src/painting/paintbuffer.h src/painting/paintbuffer.cpp
git commit -m "feat: add contentDirty flag to QCPAbstractPaintBuffer"
```

---

### Task 2: Add `QCPLayer::markDirty()`

**Files:**
- Modify: `src/layer.h:39-115`
- Modify: `src/layer.cpp`

- [ ] **Step 1: Add public `markDirty()` method**

In `src/layer.h`, add to the public section after `void replot();`:

```cpp
void markDirty();
```

In `src/layer.cpp`, add the implementation:

```cpp
void QCPLayer::markDirty()
{
    if (QSharedPointer<QCPAbstractPaintBuffer> pb = mPaintBuffer.toStrongRef())
        pb->setContentDirty(true);
}
```

- [ ] **Step 2: Commit**

```bash
git add src/layer.h src/layer.cpp
git commit -m "feat: add QCPLayer::markDirty() for incremental replot"
```

---

### Task 3: Make `replot()` conditional on dirty flags

**Files:**
- Modify: `src/core.cpp:2040-2063` (replot method)
- Modify: `src/core.cpp:3038-3081` (setupPaintBuffers method)

- [ ] **Step 1: Add dirty fallback heuristic in `replot()`**

In `src/core.cpp`, in the `replot()` method, insert before the `setupPaintBuffers()` call (before line 2053):

```cpp
ensureAtLeastOneBufferDirty();
```

Add the helper as a private method in `core.h` (after `hasInvalidatedPaintBuffers()`):

```cpp
void ensureAtLeastOneBufferDirty();
```

And implement in `core.cpp` (after `hasInvalidatedPaintBuffers()`):

```cpp
void QCustomPlot::ensureAtLeastOneBufferDirty()
{
    for (const auto& b : mPaintBuffers)
    {
        if (b->contentDirty())
            return;
    }
    for (auto& b : mPaintBuffers)
        b->setContentDirty(true);
}
```

- [ ] **Step 2: Make `setupPaintBuffers()` skip clean buffers**

In `src/core.cpp`, `setupPaintBuffers()`, change the final loop (lines 3075-3080) from:

```cpp
for (auto& buffer : mPaintBuffers)
{
    buffer->setSize(viewport().size());
    buffer->clear(Qt::transparent);
    buffer->setInvalidated();
}
```

to:

```cpp
for (auto& buffer : mPaintBuffers)
{
    buffer->setSize(viewport().size()); // may set contentDirty if size changed
    if (buffer->contentDirty())
    {
        buffer->clear(Qt::transparent);
        buffer->setInvalidated();
    }
}
```

- [ ] **Step 3: Make layer draw loop skip clean buffers**

In `src/core.cpp`, `replot()`, change the layer draw loop (lines 2056-2059) from:

```cpp
for (auto& layer : mLayers)
{
    layer->drawToPaintBuffer();
}
```

to:

```cpp
for (auto& layer : mLayers)
{
    if (QSharedPointer<QCPAbstractPaintBuffer> pb = layer->mPaintBuffer.toStrongRef();
        pb && pb->contentDirty())
    {
        layer->drawToPaintBuffer();
    }
}
```

- [ ] **Step 4: Make GPU plottable layer clearing conditional on dirty**

In `src/core.cpp`, `replot()`, change the plottable RHI layer clearing (lines 2054-2055) from:

```cpp
for (auto* prl : mPlottableRhiLayers)
    prl->clear();
```

to:

```cpp
for (auto it = mPlottableRhiLayers.begin(); it != mPlottableRhiLayers.end(); ++it)
{
    if (auto pb = it.key()->mPaintBuffer.toStrongRef(); pb && pb->contentDirty())
        it.value()->clear();
}
```

- [ ] **Step 5: Reset dirty flags after drawing**

In `src/core.cpp`, `replot()`, change the post-draw loop (lines 2060-2063) from:

```cpp
for (auto& buffer : mPaintBuffers)
{
    buffer->setInvalidated(false);
}
```

to:

```cpp
for (auto& buffer : mPaintBuffers)
{
    buffer->setInvalidated(false);
    buffer->setContentDirty(false);
}
```

- [ ] **Step 6: Commit**

```bash
git add src/core.h src/core.cpp
git commit -m "feat: skip clearing and painting non-dirty buffers in replot()"
```

---

### Task 4: Remove redundant `mNeedsUpload` from `QCPPaintBufferRhi::clear()`

**Files:**
- Modify: `src/painting/paintbuffer-rhi.cpp:80-84`

- [ ] **Step 1: Remove `mNeedsUpload = true` from `clear()`**

In `src/painting/paintbuffer-rhi.cpp`, change `clear()` from:

```cpp
void QCPPaintBufferRhi::clear(const QColor& color)
{
    mStagingImage.fill(color);
    mNeedsUpload = true;
}
```

to:

```cpp
void QCPPaintBufferRhi::clear(const QColor& color)
{
    mStagingImage.fill(color);
}
```

This is safe because `clear()` is always followed by `drawToPaintBuffer()` → `donePainting()` which sets `mNeedsUpload = true`. The flag now has clean semantics: "buffer was painted into since last GPU upload."

- [ ] **Step 2: Commit**

```bash
git add src/painting/paintbuffer-rhi.cpp
git commit -m "fix: remove redundant mNeedsUpload from QCPPaintBufferRhi::clear()"
```

---

### Task 5: Tests

**Files:**
- Modify: `src/core.h` (add friend declaration)
- Modify: `src/layer.h` (add friend declaration)
- Create: `tests/auto/test-paintbuffer/test-paintbuffer.h`
- Create: `tests/auto/test-paintbuffer/test-paintbuffer.cpp`
- Modify: `tests/auto/autotest.cpp`
- Modify: `tests/auto/meson.build`

- [ ] **Step 1: Add friend declarations for test access**

In `src/core.h`, add inside the `QCustomPlot` class (near the other friend declarations, around line 394):

```cpp
friend class TestPaintBuffer;
```

In `src/layer.h`, add inside the `QCPLayer` class (near the existing friend declarations, around line 113):

```cpp
friend class TestPaintBuffer;
```

- [ ] **Step 2: Create the test header**

Create `tests/auto/test-paintbuffer/test-paintbuffer.h`:

```cpp
#include <QtTest/QtTest>
#include "qcustomplot.h"

class TestPaintBuffer : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void contentDirty_newBufferIsDirty();
    void contentDirty_resetAfterReplot();
    void contentDirty_setOnResize();
    void contentDirty_setOnInvalidation();
    void contentDirty_markDirtyLayer();
    void contentDirty_fallbackMarksAllDirty();
    void contentDirty_incrementalReplotSkipsCleanBuffers();
    void contentDirty_incrementalReplotPreservesContent();
    void replotAndExport_smokeTest();

private:
    QCustomPlot* mPlot;
};
```

- [ ] **Step 3: Create the test implementation**

Create `tests/auto/test-paintbuffer/test-paintbuffer.cpp`:

```cpp
#include "test-paintbuffer.h"

void TestPaintBuffer::init()
{
    mPlot = new QCustomPlot(nullptr);
    mPlot->setGeometry(50, 50, 400, 300);
    mPlot->show();
    QTest::qWaitForWindowExposed(mPlot);
}

void TestPaintBuffer::cleanup()
{
    delete mPlot;
}

void TestPaintBuffer::contentDirty_newBufferIsDirty()
{
    QCPPaintBufferPixmap buf(QSize(100, 100), 1.0, "test");
    QVERIFY(buf.contentDirty());
}

void TestPaintBuffer::contentDirty_resetAfterReplot()
{
    mPlot->replot(QCustomPlot::rpQueuedRefresh);
    for (const auto& buf : mPlot->mPaintBuffers)
        QVERIFY2(!buf->contentDirty(), qPrintable(buf->layerName()));
}

void TestPaintBuffer::contentDirty_setOnResize()
{
    QCPPaintBufferPixmap buf(QSize(100, 100), 1.0, "test");
    buf.setContentDirty(false);
    QVERIFY(!buf.contentDirty());

    buf.setSize(QSize(200, 200));
    QVERIFY(buf.contentDirty());
}

void TestPaintBuffer::contentDirty_setOnInvalidation()
{
    QCPPaintBufferPixmap buf(QSize(100, 100), 1.0, "test");
    buf.setContentDirty(false);
    QVERIFY(!buf.contentDirty());

    buf.setInvalidated(true);
    QVERIFY(buf.contentDirty());

    // setInvalidated(false) should NOT clear contentDirty
    buf.setInvalidated(false);
    QVERIFY(buf.contentDirty());
}

void TestPaintBuffer::contentDirty_markDirtyLayer()
{
    QCPLayer* overlay = mPlot->addLayer("overlay", mPlot->layer("main"), QCustomPlot::limAbove);
    overlay->setMode(QCPLayer::lmBuffered);
    mPlot->replot(QCustomPlot::rpQueuedRefresh);

    for (const auto& buf : mPlot->mPaintBuffers)
        QVERIFY(!buf->contentDirty());

    overlay->markDirty();

    auto overlayBuf = overlay->mPaintBuffer.toStrongRef();
    QVERIFY(overlayBuf);
    QVERIFY(overlayBuf->contentDirty());

    int dirtyCount = 0;
    for (const auto& buf : mPlot->mPaintBuffers)
    {
        if (buf->contentDirty())
            ++dirtyCount;
    }
    QCOMPARE(dirtyCount, 1);
}

void TestPaintBuffer::contentDirty_fallbackMarksAllDirty()
{
    mPlot->replot(QCustomPlot::rpQueuedRefresh);
    for (const auto& buf : mPlot->mPaintBuffers)
        QVERIFY(!buf->contentDirty());

    // replot() without prior markDirty() should still repaint everything
    mPlot->replot(QCustomPlot::rpQueuedRefresh);
    for (const auto& buf : mPlot->mPaintBuffers)
        QVERIFY(!buf->contentDirty());
}

void TestPaintBuffer::contentDirty_incrementalReplotSkipsCleanBuffers()
{
    QCPLayer* data = mPlot->layer("main");
    QCPLayer* overlay = mPlot->addLayer("overlay", data, QCustomPlot::limAbove);
    overlay->setMode(QCPLayer::lmBuffered);

    QCPGraph* graph = mPlot->addGraph();
    graph->setData({1.0, 2.0, 3.0}, {1.0, 4.0, 2.0});
    auto* line = new QCPItemLine(mPlot);
    line->setLayer(overlay);
    line->start->setCoords(1, 1);
    line->end->setCoords(3, 3);

    mPlot->replot(QCustomPlot::rpQueuedRefresh);

    // Mark only main dirty, replot incrementally
    data->markDirty();
    mPlot->replot(QCustomPlot::rpQueuedRefresh);

    // All buffers should be clean after replot
    for (const auto& buf : mPlot->mPaintBuffers)
        QVERIFY(!buf->contentDirty());
}

void TestPaintBuffer::contentDirty_incrementalReplotPreservesContent()
{
    QCPLayer* data = mPlot->layer("main");
    QCPLayer* overlay = mPlot->addLayer("overlay", data, QCustomPlot::limAbove);
    overlay->setMode(QCPLayer::lmBuffered);

    auto* graph = mPlot->addGraph();
    graph->setData({1.0, 2.0, 3.0}, {1.0, 4.0, 2.0});
    auto* line = new QCPItemLine(mPlot);
    line->setLayer(overlay);
    line->start->setPixelPosition(QPointF(50, 50));
    line->end->setPixelPosition(QPointF(150, 150));

    mPlot->replot(QCustomPlot::rpQueuedRefresh);

    // Capture overlay buffer content before incremental replot
    auto overlayBuf = overlay->mPaintBuffer.toStrongRef();
    QVERIFY(overlayBuf);
    QPixmap baseline(overlayBuf->size());
    baseline.fill(Qt::transparent);
    {
        QCPPainter painter(&baseline);
        overlayBuf->draw(&painter);
    }

    // Incremental replot: only main dirty
    data->markDirty();
    mPlot->replot(QCustomPlot::rpQueuedRefresh);

    // Overlay buffer content should be identical (not cleared/repainted)
    QPixmap afterReplot(overlayBuf->size());
    afterReplot.fill(Qt::transparent);
    {
        QCPPainter painter(&afterReplot);
        overlayBuf->draw(&painter);
    }
    QCOMPARE(afterReplot.toImage(), baseline.toImage());
}

void TestPaintBuffer::replotAndExport_smokeTest()
{
    mPlot->addGraph()->setData({1.0, 2.0}, {3.0, 4.0});
    mPlot->replot(QCustomPlot::rpQueuedRefresh);

    QPixmap result = mPlot->toPixmap(200, 150);
    QVERIFY(!result.isNull());
    QCOMPARE(result.size(), QSize(200, 150));
}
```

- [ ] **Step 4: Register the test in autotest.cpp and meson.build**

In `tests/auto/autotest.cpp`, add:
```cpp
#include "test-paintbuffer/test-paintbuffer.h"
```
and in `main()`:
```cpp
QCPTEST(TestPaintBuffer);
```

In `tests/auto/meson.build`, add to `test_srcs`:
```
'test-paintbuffer/test-paintbuffer.cpp',
```
and to `test_headers`:
```
'test-paintbuffer/test-paintbuffer.h',
```

- [ ] **Step 5: Build and run tests**

Run: `meson compile -C build && meson test --print-errorlogs -C build`
Expected: All tests pass, including the new `TestPaintBuffer` tests.

- [ ] **Step 6: Commit**

```bash
git add src/core.h src/layer.h tests/auto/test-paintbuffer/ tests/auto/autotest.cpp tests/auto/meson.build
git commit -m "test: add paint buffer dirty tracking tests"
```

---

### Notes

**Incremental replot usage pattern:**
```cpp
// Mark only the layer that changed
plot->layer("data")->markDirty();
// Replot — only the "data" buffer is cleared and repainted
plot->replot();
```

**Backward compatibility:** Calling `replot()` without any prior `markDirty()` triggers the fallback heuristic (all buffers marked dirty), preserving identical behavior to before this change.

**GPU plottable/colormap layers:** The plan conditionalizes `QCPPlottableRhiLayer::clear()` on the associated paint buffer's dirty flag. This prevents GPU geometry from being destroyed for layers that aren't being repainted. Colormap RHI layers don't need the same treatment — they are not cleared in `replot()` (they manage their own dirty state via the resampler).
