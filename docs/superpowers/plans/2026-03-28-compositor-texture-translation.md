# Compositor-Level Texture Translation (Phase B) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Skip CPU staging buffer repaint for data layers during pan when all plottables support GPU translation offset.

**Architecture:** Add `canSkipRepaintForTranslation()` to `QCPLayer`. Gate `drawToPaintBuffer()` in `replot()` behind this check. The compositor already applies `pixelOffset()` to the old texture — no other rendering changes needed.

**Tech Stack:** C++20, Qt6 QRhi, Qt Test

---

## File Map

| File | Action | Responsibility |
|------|--------|---------------|
| `src/layer.h` | Modify | Add `canSkipRepaintForTranslation()` declaration |
| `src/layer.cpp` | Modify | Implement `canSkipRepaintForTranslation()`, add `#include "items/item.h"` |
| `src/core.cpp` | Modify | Gate `drawToPaintBuffer()` behind skip check |
| `tests/auto/test-paintbuffer/test-paintbuffer.h` | Modify | Add test declarations |
| `tests/auto/test-paintbuffer/test-paintbuffer.cpp` | Modify | Add test implementations |

---

### Task 1: Add `canSkipRepaintForTranslation()` with failing test

**Files:**
- Modify: `tests/auto/test-paintbuffer/test-paintbuffer.h:24` (before `private:`)
- Modify: `tests/auto/test-paintbuffer/test-paintbuffer.cpp` (append)
- Modify: `src/layer.h:85` (after `pixelOffset()`)
- Modify: `src/layer.cpp:364` (after `pixelOffset()`)

- [ ] **Step 1: Write the failing test**

In `tests/auto/test-paintbuffer/test-paintbuffer.h`, add before the `private:` line:

```cpp
    void skipRepaint_graph2PanOnly();
    void skipRepaint_disabledWithItems();
    void skipRepaint_disabledWithLegacyGraph();
    void skipRepaint_disabledOnInvalidation();
```

In `tests/auto/test-paintbuffer/test-paintbuffer.cpp`, append:

```cpp
void TestPaintBuffer::skipRepaint_graph2PanOnly()
{
    // Setup: Graph2 with data, do a full replot to establish baseline
    auto* graph2 = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    std::vector<double> keys = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> vals = {1.0, 4.0, 2.0, 5.0, 3.0};
    graph2->setData(std::move(keys), std::move(vals));
    mPlot->xAxis->setRange(0, 6);
    mPlot->yAxis->setRange(0, 6);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    // Simulate a pan: shift key axis range, mark affected layers dirty
    QCPLayer* mainLayer = mPlot->layer("main");
    QVERIFY(mainLayer);

    mPlot->xAxis->setRange(0.5, 6.5);
    auto* axisRect = mPlot->axisRect();
    axisRect->markAffectedLayersDirty();

    // Main layer should be dirty after markAffectedLayersDirty
    auto mainBuf = mainLayer->mPaintBuffer.toStrongRef();
    QVERIFY(mainBuf);
    QVERIFY(mainBuf->contentDirty());

    // canSkipRepaintForTranslation should be true (Graph2 with valid stallPixelOffset)
    QVERIFY(mainLayer->canSkipRepaintForTranslation());
}

void TestPaintBuffer::skipRepaint_disabledWithItems()
{
    auto* graph2 = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    std::vector<double> keys = {1.0, 2.0, 3.0};
    std::vector<double> vals = {1.0, 4.0, 2.0};
    graph2->setData(std::move(keys), std::move(vals));

    // Add an item to the main layer (items default to "main")
    auto* line = new QCPItemLine(mPlot);
    line->start->setCoords(1, 1);
    line->end->setCoords(3, 3);

    mPlot->xAxis->setRange(0, 6);
    mPlot->yAxis->setRange(0, 6);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    mPlot->xAxis->setRange(0.5, 6.5);
    mPlot->axisRect()->markAffectedLayersDirty();

    QCPLayer* mainLayer = mPlot->layer("main");
    QVERIFY(!mainLayer->canSkipRepaintForTranslation());
}

void TestPaintBuffer::skipRepaint_disabledWithLegacyGraph()
{
    // Legacy QCPGraph does not support stallPixelOffset
    auto* graph = mPlot->addGraph();
    graph->setData({1.0, 2.0, 3.0}, {1.0, 4.0, 2.0});
    mPlot->xAxis->setRange(0, 6);
    mPlot->yAxis->setRange(0, 6);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    mPlot->xAxis->setRange(0.5, 6.5);
    mPlot->axisRect()->markAffectedLayersDirty();

    QCPLayer* mainLayer = mPlot->layer("main");
    QVERIFY(!mainLayer->canSkipRepaintForTranslation());
}

void TestPaintBuffer::skipRepaint_disabledOnInvalidation()
{
    auto* graph2 = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    std::vector<double> keys = {1.0, 2.0, 3.0};
    std::vector<double> vals = {1.0, 4.0, 2.0};
    graph2->setData(std::move(keys), std::move(vals));
    mPlot->xAxis->setRange(0, 6);
    mPlot->yAxis->setRange(0, 6);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    // Invalidate buffer (simulates resize), then mark dirty for pan
    auto mainBuf = mPlot->layer("main")->mPaintBuffer.toStrongRef();
    QVERIFY(mainBuf);
    mainBuf->setInvalidated(true);

    mPlot->xAxis->setRange(0.5, 6.5);
    mPlot->axisRect()->markAffectedLayersDirty();

    QCPLayer* mainLayer = mPlot->layer("main");
    QVERIFY(!mainLayer->canSkipRepaintForTranslation());
}
```

- [ ] **Step 2: Add stub declaration and implementation (returns false)**

In `src/layer.h`, after line 85 (`[[nodiscard]] QPointF pixelOffset() const;`), add:

```cpp
    [[nodiscard]] bool canSkipRepaintForTranslation() const;
```

In `src/layer.cpp`, add at the top (after existing includes):

```cpp
#include "items/item.h"
```

After the `pixelOffset()` method (after the closing `}` around line 365), add:

```cpp
bool QCPLayer::canSkipRepaintForTranslation() const
{
    return false; // stub — tests should fail for the positive case
}
```

- [ ] **Step 3: Run tests to verify the positive case fails**

Run: `meson test -C build --test-args '-run skipRepaint' auto-tests --print-errorlogs`
Expected: `skipRepaint_graph2PanOnly` FAILS (returns false), other three PASS (they test the false cases).

- [ ] **Step 4: Implement `canSkipRepaintForTranslation()`**

Replace the stub in `src/layer.cpp`:

```cpp
bool QCPLayer::canSkipRepaintForTranslation() const
{
    auto pb = mPaintBuffer.toStrongRef();
    if (!pb || pb->invalidated())
        return false;

    if (pixelOffset().isNull())
        return false;

    for (auto* child : mChildren)
    {
        if (qobject_cast<QCPAbstractItem*>(child))
            return false;
    }
    return true;
}
```

- [ ] **Step 5: Run tests to verify all pass**

Run: `meson test -C build --test-args '-run skipRepaint' auto-tests --print-errorlogs`
Expected: All 4 `skipRepaint_*` tests PASS.

- [ ] **Step 6: Commit**

```bash
git add src/layer.h src/layer.cpp tests/auto/test-paintbuffer/test-paintbuffer.h tests/auto/test-paintbuffer/test-paintbuffer.cpp
git commit -m "feat: add QCPLayer::canSkipRepaintForTranslation()

Checks whether a layer's CPU staging buffer repaint can be skipped
during pan by relying on compositor-level texture translation.
Returns true when all plottables agree on a translation offset,
no items are present, and the buffer is not structurally invalidated."
```

---

### Task 2: Gate `drawToPaintBuffer()` behind the skip check

**Files:**
- Modify: `src/core.cpp:2133-2140` (the draw loop in `replot()`)
- Modify: `tests/auto/test-paintbuffer/test-paintbuffer.h` (add test)
- Modify: `tests/auto/test-paintbuffer/test-paintbuffer.cpp` (add test)

- [ ] **Step 1: Write the failing test**

In `tests/auto/test-paintbuffer/test-paintbuffer.h`, add before `private:`:

```cpp
    void skipRepaint_bufferNotReuploadedOnPan();
```

In `tests/auto/test-paintbuffer/test-paintbuffer.cpp`, append:

```cpp
void TestPaintBuffer::skipRepaint_bufferNotReuploadedOnPan()
{
    // Setup: Graph2 with data
    auto* graph2 = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    std::vector<double> keys = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> vals = {1.0, 4.0, 2.0, 5.0, 3.0};
    graph2->setData(std::move(keys), std::move(vals));
    mPlot->xAxis->setRange(0, 6);
    mPlot->yAxis->setRange(0, 6);

    // Full replot to establish baseline (paints into staging buffer)
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    // After replot, all buffers should be clean
    QCPLayer* mainLayer = mPlot->layer("main");
    auto mainBuf = mainLayer->mPaintBuffer.toStrongRef();
    QVERIFY(mainBuf);
    QVERIFY(!mainBuf->contentDirty());

    // Simulate pan
    mPlot->xAxis->setRange(0.5, 6.5);
    mPlot->axisRect()->markAffectedLayersDirty();
    QVERIFY(mainBuf->contentDirty());

    // Replot — skip should kick in, donePainting() should NOT be called
    // so mNeedsUpload should stay false on the main buffer
    // (it was cleared by the render() path of the previous frame,
    //  but since we're in offscreen test mode, we check donePainting wasn't called
    //  by verifying contentDirty was cleared but the buffer wasn't repainted)
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    // After replot, contentDirty should be cleared (normal behavior)
    QVERIFY(!mainBuf->contentDirty());

    // Verify the skip actually happened: the main layer should still report
    // a valid pixelOffset (the range shifted but no full repaint happened,
    // so stallPixelOffset still returns a non-null offset for the next frame)
    QVERIFY(!mainLayer->pixelOffset().isNull());
}
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson test -C build --test-args '-run skipRepaint_bufferNotReuploadedOnPan' auto-tests --print-errorlogs`
Expected: FAIL — `pixelOffset()` returns `{}` because the current code calls `drawToPaintBuffer()` which runs `draw()` on Graph2, updating `mRenderedRange` to the current viewport, making `stallPixelOffset()` return `{}`.

- [ ] **Step 3: Apply the gate in `replot()`**

In `src/core.cpp`, replace the draw loop (lines 2133-2140):

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

With:

```cpp
    for (auto& layer : mLayers)
    {
        if (QSharedPointer<QCPAbstractPaintBuffer> pb = layer->mPaintBuffer.toStrongRef();
            pb && pb->contentDirty() && !layer->canSkipRepaintForTranslation())
        {
            layer->drawToPaintBuffer();
        }
    }
```

- [ ] **Step 4: Run tests to verify all pass**

Run: `meson test -C build auto-tests --print-errorlogs`
Expected: ALL tests pass (full suite, not just the new ones — no regressions).

- [ ] **Step 5: Commit**

```bash
git add src/core.cpp tests/auto/test-paintbuffer/test-paintbuffer.h tests/auto/test-paintbuffer/test-paintbuffer.cpp
git commit -m "perf: skip CPU staging buffer repaint during pan (Phase B)

When all plottables on a layer report a valid GPU translation offset
(stallPixelOffset), skip drawToPaintBuffer() for that layer. The
compositor already shifts the old texture by pixelOffset(). Saves
QImage clear + QPainter repaint + texture upload per pan frame."
```

---

### Task 3: Run full test suite and verify no regressions

**Files:** None (verification only)

- [ ] **Step 1: Build and run all tests**

Run: `meson test -C build --print-errorlogs`
Expected: All tests pass.

- [ ] **Step 2: Run benchmarks to sanity-check no performance regression**

Run: `meson test --benchmark -C build --print-errorlogs`
Expected: No significant regression in any benchmark.
