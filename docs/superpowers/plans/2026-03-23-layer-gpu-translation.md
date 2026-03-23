# Layer-Level GPU Translation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the GPU translation fast path to the compositing layer so all QPainter-rendered plottables slide smoothly during pan while async resampling is busy.

**Architecture:** Promote the `main` layer to `lmBuffered` so it gets a dedicated GPU texture. Add a translation uniform to the composite vertex shader. During render, compute pixel offset from busy plottables and shift the layer texture accordingly.

**Tech Stack:** C++20, Qt6 QRhi, GLSL 440, Meson build system

**Spec:** `docs/superpowers/specs/2026-03-23-layer-gpu-translation-design.md`

---

## File Structure

| File | Action | Responsibility |
|------|--------|---------------|
| `src/painting/shaders/composite.vert` | Modify | Add `LayerParams` UBO with translate + yFlip |
| `src/plottables/plottable.h` | Modify | Add `virtual QPointF stallPixelOffset() const` |
| `src/plottables/plottable-graph2.h` | Modify | Override `stallPixelOffset()` |
| `src/plottables/plottable-graph2.cpp` | Modify | Implement `stallPixelOffset()` |
| `src/plottables/plottable-multigraph.h` | Modify | Override `stallPixelOffset()` |
| `src/plottables/plottable-multigraph.cpp` | Modify | Implement `stallPixelOffset()` |
| `src/layer.h` | Modify | Add `QPointF pixelOffset() const` |
| `src/layer.cpp` | Modify | Implement `pixelOffset()` |
| `src/core.cpp` | Modify | Buffered main layer, UBO creation, composite loop changes |
| `src/painting/paintbuffer-rhi.h` | Modify | Store UBO pointer for SRB creation |
| `tests/auto/test-pipeline/test-layer-translation.cpp` | Create | Layer-level translation tests |
| `tests/auto/test-pipeline/test-pipeline.h` | Modify | Add new test slot declarations |
| `tests/auto/meson.build` | Modify | Add new test source file |

---

### Task 1: Promote `main` layer to buffered

This is the prerequisite — the `main` layer must have its own `QCPPaintBufferRhi` so its texture can be translated independently from axes/grid/legend.

**Files:**
- Modify: `src/core.cpp:455` (after `setCurrentLayer`)

- [ ] **Step 1: Add buffered mode for main layer**

In `src/core.cpp`, after line 455 (`layer(QLatin1String("overlay"))->setMode(QCPLayer::lmBuffered);`), add:

```cpp
layer(QLatin1String("main"))->setMode(QCPLayer::lmBuffered);
```

- [ ] **Step 2: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All existing tests pass. The `main` layer now gets its own paint buffer — this is a rendering change but should be visually identical since the compositor draws all buffers in layer order with premultiplied alpha blending.

- [ ] **Step 3: Commit**

```bash
git add src/core.cpp
git commit -m "refactor: promote main layer to lmBuffered for independent compositing"
```

---

### Task 2: Add `stallPixelOffset()` virtual method to `QCPAbstractPlottable`

**Files:**
- Modify: `src/plottables/plottable.h` (public section, near other public virtual methods)

- [ ] **Step 1: Add the virtual method**

In `src/plottables/plottable.h`, add in the **public** section (near `interface1D()` or the other public virtual methods — NOT near `pipelineBusy()` which is protected):

```cpp
virtual QPointF stallPixelOffset() const { return {}; }
```

- [ ] **Step 2: Build**

Run: `meson compile -C build`
Expected: Clean build. No existing code calls this yet.

- [ ] **Step 3: Commit**

```bash
git add src/plottables/plottable.h
git commit -m "feat: add stallPixelOffset() virtual method to QCPAbstractPlottable"
```

---

### Task 3: Implement `stallPixelOffset()` for QCPGraph2 and QCPMultiGraph

Extract the offset computation that currently lives inline in each plottable's `draw()` method into the new virtual method.

**Files:**
- Modify: `src/plottables/plottable-graph2.h` (add override declaration)
- Modify: `src/plottables/plottable-graph2.cpp` (implement override, refactor `draw()` to call it)
- Modify: `src/plottables/plottable-multigraph.h` (add override declaration)
- Modify: `src/plottables/plottable-multigraph.cpp` (implement override, refactor `draw()` to call it)
- Test: `tests/auto/test-pipeline/test-layer-translation.cpp`

- [ ] **Step 1: Write test for `stallPixelOffset()` on Graph2**

Create `tests/auto/test-pipeline/test-layer-translation.cpp`:

```cpp
#include "test-pipeline.h"
#include <qcustomplot.h>

void TestPipeline::stallPixelOffsetGraph2Busy()
{
    auto* graph = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    QVector<double> keys(200000), values(200000);
    for (int i = 0; i < 200000; ++i) {
        keys[i] = i;
        values[i] = std::sin(i * 0.01);
    }
    graph->setData(std::move(keys), std::move(values));

    mPlot->xAxis->setRange(0, 100000);
    mPlot->yAxis->setRange(-1.5, 1.5);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QTRY_VERIFY_WITH_TIMEOUT(!graph->pipeline().isBusy(), 5000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    // Should be zero when not busy
    QCOMPARE(graph->stallPixelOffset(), QPointF(0, 0));

    // Pan to trigger busy state
    mPlot->xAxis->setRange(10000, 110000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    if (graph->pipeline().isBusy())
    {
        QPointF offset = graph->stallPixelOffset();
        // Offset should be nonzero when busy (panned right → positive X offset)
        QVERIFY(!offset.isNull());
    }
}

void TestPipeline::stallPixelOffsetIdleIsZero()
{
    auto* graph = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    QVector<double> keys(100), values(100);
    for (int i = 0; i < 100; ++i) {
        keys[i] = i;
        values[i] = i;
    }
    graph->setData(std::move(keys), std::move(values));

    mPlot->xAxis->setRange(0, 100);
    mPlot->yAxis->setRange(0, 100);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QTRY_VERIFY_WITH_TIMEOUT(!graph->pipeline().isBusy(), 5000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QCOMPARE(graph->stallPixelOffset(), QPointF(0, 0));
}
```

Add test slot declarations in `tests/auto/test-pipeline/test-pipeline.h`:

```cpp
// Layer-level GPU translation
void stallPixelOffsetGraph2Busy();
void stallPixelOffsetIdleIsZero();
```

Add to `tests/auto/meson.build`:

```
'test-pipeline/test-layer-translation.cpp',
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: Compile error — `stallPixelOffset()` not declared on `QCPGraph2`.

- [ ] **Step 3: Implement `stallPixelOffset()` for QCPGraph2**

In `src/plottables/plottable-graph2.h`, add in the public section (near `hasRenderedRange()`):

```cpp
QPointF stallPixelOffset() const override;
```

In `src/plottables/plottable-graph2.cpp`, add the implementation:

```cpp
QPointF QCPGraph2::stallPixelOffset() const
{
    if (mPipeline.isBusy() && mHasRenderedRange)
        return qcp::computeViewportOffset(mKeyAxis.data(), mValueAxis.data(),
                                          mRenderedRange.key, mRenderedRange.value);
    return {};
}
```

Then refactor `draw()` to use it — replace the inline offset computation block with:

```cpp
QPointF gpuOffset = stallPixelOffset();
if (!mPipeline.isBusy())
{
    mRenderedRange = {mKeyAxis->range(), mValueAxis->range()};
    mHasRenderedRange = true;
}
```

- [ ] **Step 4: Implement `stallPixelOffset()` for QCPMultiGraph**

Same pattern. In `src/plottables/plottable-multigraph.h`:

```cpp
QPointF stallPixelOffset() const override;
```

In `src/plottables/plottable-multigraph.cpp`:

```cpp
QPointF QCPMultiGraph::stallPixelOffset() const
{
    if (mPipeline.isBusy() && mHasRenderedRange)
        return qcp::computeViewportOffset(mKeyAxis.data(), mValueAxis.data(),
                                          mRenderedRange.key, mRenderedRange.value);
    return {};
}
```

Refactor `draw()` the same way as Graph2.

- [ ] **Step 5: Run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All tests pass including the two new ones.

- [ ] **Step 6: Commit**

```bash
git add src/plottables/plottable-graph2.h src/plottables/plottable-graph2.cpp \
        src/plottables/plottable-multigraph.h src/plottables/plottable-multigraph.cpp \
        tests/auto/test-pipeline/test-layer-translation.cpp \
        tests/auto/test-pipeline/test-pipeline.h tests/auto/meson.build
git commit -m "feat: extract stallPixelOffset() from Graph2/MultiGraph draw path"
```

---

### Task 4: Add `pixelOffset()` to `QCPLayer`

**Files:**
- Modify: `src/layer.h` (add method declaration)
- Modify: `src/layer.cpp` (implement method)
- Test: `tests/auto/test-pipeline/test-layer-translation.cpp`

- [ ] **Step 1: Write test for layer offset**

Add to `tests/auto/test-pipeline/test-layer-translation.cpp`:

```cpp
void TestPipeline::layerPixelOffsetFromBusyChild()
{
    auto* graph = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    QVector<double> keys(200000), values(200000);
    for (int i = 0; i < 200000; ++i) {
        keys[i] = i;
        values[i] = std::sin(i * 0.01);
    }
    graph->setData(std::move(keys), std::move(values));

    mPlot->xAxis->setRange(0, 100000);
    mPlot->yAxis->setRange(-1.5, 1.5);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QTRY_VERIFY_WITH_TIMEOUT(!graph->pipeline().isBusy(), 5000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QCPLayer* mainLayer = mPlot->layer("main");
    QVERIFY(mainLayer);

    // Idle: layer offset should be zero
    QCOMPARE(mainLayer->pixelOffset(), QPointF(0, 0));

    // Pan to trigger busy state
    mPlot->xAxis->setRange(10000, 110000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    if (graph->pipeline().isBusy())
    {
        QPointF layerOffset = mainLayer->pixelOffset();
        QPointF plottableOffset = graph->stallPixelOffset();
        // Layer offset should match the plottable's offset
        QCOMPARE(layerOffset, plottableOffset);
    }
}

void TestPipeline::layerPixelOffsetZeroWhenNoAsyncChildren()
{
    // main layer with no plottables should return zero
    QCPLayer* mainLayer = mPlot->layer("main");
    QVERIFY(mainLayer);
    QCOMPARE(mainLayer->pixelOffset(), QPointF(0, 0));
}
```

Add test slot declarations in `tests/auto/test-pipeline/test-pipeline.h`:

```cpp
void layerPixelOffsetFromBusyChild();
void layerPixelOffsetZeroWhenNoAsyncChildren();
```

- [ ] **Step 2: Run test to verify it fails**

Run: `meson compile -C build`
Expected: Compile error — `pixelOffset()` not declared on `QCPLayer`.

- [ ] **Step 3: Implement `pixelOffset()` on QCPLayer**

In `src/layer.h`, add in the public getters section (after `mode()` around line 92):

```cpp
QPointF pixelOffset() const;
```

In `src/layer.cpp`, add the implementation. This needs to include `plottable.h` (check if already included):

```cpp
QPointF QCPLayer::pixelOffset() const
{
    QPointF result;
    QCPAxisRect* firstAxisRect = nullptr;
    for (auto* child : mChildren)
    {
        if (auto* plottable = qobject_cast<QCPAbstractPlottable*>(child))
        {
            QPointF offset = plottable->stallPixelOffset();
            if (!offset.isNull())
            {
                auto* ar = plottable->keyAxis() ? plottable->keyAxis()->axisRect() : nullptr;
                if (!firstAxisRect)
                {
                    firstAxisRect = ar;
                    result = offset;
                }
                else if (ar != firstAxisRect)
                {
                    // Multiple axis rects with different pan states — cannot
                    // apply a single offset. Degrade gracefully to no translation.
                    return {};
                }
            }
        }
    }
    return result;
}
```

- [ ] **Step 4: Run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/layer.h src/layer.cpp \
        tests/auto/test-pipeline/test-layer-translation.cpp \
        tests/auto/test-pipeline/test-pipeline.h
git commit -m "feat: add pixelOffset() to QCPLayer delegating to child plottables"
```

---

### Task 5: Add translation uniform to composite vertex shader

**Files:**
- Modify: `src/painting/shaders/composite.vert`

- [ ] **Step 1: Update the composite vertex shader**

Replace the contents of `src/painting/shaders/composite.vert` with:

```glsl
#version 440

layout(location = 0) in vec2 position;
layout(location = 1) in vec2 texcoord;

layout(location = 0) out vec2 v_texcoord;

layout(std140, binding = 1) uniform LayerParams {
    float translateX;
    float translateY;
    float viewportW;
    float viewportH;
    float yFlip;
} lp;

void main()
{
    float dx = (lp.translateX / lp.viewportW) * 2.0;
    float dy = lp.yFlip * (lp.translateY / lp.viewportH) * 2.0;
    v_texcoord = texcoord;
    gl_Position = vec4(position.x + dx, position.y + dy, 0.0, 1.0);
}
```

- [ ] **Step 2: Rebuild to verify shader compiles**

Run: `meson compile -C build`
Expected: The build system runs `qsb --qt6` on the modified shader, then `embed_shaders.py` re-embeds it. Build succeeds.

- [ ] **Step 3: Commit**

```bash
git add src/painting/shaders/composite.vert
git commit -m "feat: add LayerParams translation uniform to composite vertex shader"
```

---

### Task 6: Wire up composite UBO in `render()`

This is the core integration — create the uniform buffer, update it per draw call, and modify the SRB layout.

**Files:**
- Modify: `src/core.h` (add `mCompositeUbo` member)
- Modify: `src/core.cpp` (initialize(), render(), resize cleanup)

- [ ] **Step 1: Add UBO member to QCustomPlot**

In `src/core.h`, find the private RHI members section (near `mCompositePipeline`, `mLayoutSrb`, `mSampler`, `mQuadVertexBuffer`, `mQuadIndexBuffer`). Add:

```cpp
QRhiBuffer* mCompositeUbo = nullptr;
```

- [ ] **Step 2: Create UBO in `initialize()`**

In `src/core.cpp`, in the first-run initialization block of `initialize()` (after the quad buffers are created, around line 2545), add:

```cpp
// Uniform buffer for per-layer composite translation (5 floats, padded to 32 for std140)
mCompositeUbo = mRhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32);
mCompositeUbo->create();
```

In the resize cleanup block (around line 2490, where `mCompositePipeline` and `mLayoutSrb` are deleted), add cleanup:

```cpp
delete mCompositeUbo;
mCompositeUbo = nullptr;
```

Also add the same cleanup to `releaseResources()` (around line 2738, where `mCompositePipeline`, `mLayoutSrb`, `mSampler`, `mQuadVertexBuffer`, `mQuadIndexBuffer` are deleted):

```cpp
delete mCompositeUbo;
mCompositeUbo = nullptr;
```

- [ ] **Step 3: Lazily recreate UBO in `render()` if null (handles resize)**

In `src/core.cpp`, at the top of the pipeline creation block in `render()` (inside `if (!mCompositePipeline)`), add before the shader loading:

```cpp
if (!mCompositeUbo)
{
    mCompositeUbo = mRhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, 32);
    mCompositeUbo->create();
}
```

This handles the case where resize deletes the UBO but `render()` needs it before `initialize()` runs again.

- [ ] **Step 4: Update layout SRB to include UBO binding**

In `src/core.cpp`, in the pipeline creation block of `render()` (around line 2660), modify the `mLayoutSrb` bindings from:

```cpp
mLayoutSrb->setBindings({
    QRhiShaderResourceBinding::sampledTexture(
        0, QRhiShaderResourceBinding::FragmentStage,
        nullptr, mSampler)
});
```

to:

```cpp
mLayoutSrb->setBindings({
    QRhiShaderResourceBinding::sampledTexture(
        0, QRhiShaderResourceBinding::FragmentStage,
        nullptr, mSampler),
    QRhiShaderResourceBinding::uniformBuffer(
        1, QRhiShaderResourceBinding::VertexStage,
        mCompositeUbo)
});
```

- [ ] **Step 5: Update per-buffer SRB creation to include UBO**

In the compositing loop of `render()` (around line 2697), modify the SRB creation from:

```cpp
srb->setBindings({
    QRhiShaderResourceBinding::sampledTexture(
        0, QRhiShaderResourceBinding::FragmentStage,
        rhiBuffer->texture(), mSampler)
});
```

to:

```cpp
srb->setBindings({
    QRhiShaderResourceBinding::sampledTexture(
        0, QRhiShaderResourceBinding::FragmentStage,
        rhiBuffer->texture(), mSampler),
    QRhiShaderResourceBinding::uniformBuffer(
        1, QRhiShaderResourceBinding::VertexStage,
        mCompositeUbo)
});
```

- [ ] **Step 6: Write UBO data before each composite draw call**

In the compositing loop, before the `cb->setGraphicsPipeline(mCompositePipeline);` line, add the UBO update. The layer's pixel offset is retrieved from the layer associated with this buffer. For `lmBuffered` layers, the layer is the current iteration's layer. For shared buffers (logical layers), offset is always zero since no async plottables live on those layers.

```cpp
QPointF layerOffset = layer->pixelOffset();
struct {
    float translateX, translateY, viewportW, viewportH, yFlip;
} compositeParams = {
    float(layerOffset.x()),
    float(layerOffset.y()),
    float(outputSize.width()),
    float(outputSize.height()),
    mRhi->isYUpInNDC() ? -1.0f : 1.0f
};
updates = mRhi->nextResourceUpdateBatch();
updates->updateDynamicBuffer(mCompositeUbo, 0, sizeof(compositeParams), &compositeParams);
cb->resourceUpdate(updates);
```

Note: `updates` was consumed by `cb->beginPass()` earlier, so we need a fresh batch. The `nextResourceUpdateBatch()` call is cheap.

- [ ] **Step 7: Build and run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All tests pass. With no busy plottables, `layerOffset` is always `(0,0)` so the shader adds zero translation — visually identical to before.

- [ ] **Step 8: Commit**

```bash
git add src/core.h src/core.cpp
git commit -m "feat: wire composite UBO for layer-level GPU translation"
```

---

### Task 7: Add scissor clipping for translated layers

**Files:**
- Modify: `src/core.cpp` (render loop — add scissor when offset is nonzero)
- Test: `tests/auto/test-pipeline/test-layer-translation.cpp`

- [ ] **Step 1: Write scissor clipping test**

Add to `tests/auto/test-pipeline/test-layer-translation.cpp`:

```cpp
void TestPipeline::layerTranslationClippedToAxisRect()
{
    // GPU scissor clipping is hardware-enforced and cannot be verified via toPixmap()
    // (which uses the QPainter export path, bypassing RHI). This test is a smoke test:
    // verify that a large pan with layer translation enabled doesn't crash or assert.
    auto* graph = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    QVector<double> keys(200000), values(200000);
    for (int i = 0; i < 200000; ++i) {
        keys[i] = i;
        values[i] = 1.0;
    }
    graph->setData(std::move(keys), std::move(values));
    graph->setPen(QPen(Qt::red, 2));

    mPlot->xAxis->setRange(0, 100000);
    mPlot->yAxis->setRange(-2, 2);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QTRY_VERIFY_WITH_TIMEOUT(!graph->pipeline().isBusy(), 5000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    // Large pan — layer offset will be very large, scissor must prevent GPU artifacts
    mPlot->xAxis->setRange(200000, 300000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    // Verify the layer has a nonzero offset (translation is active)
    QCPLayer* mainLayer = mPlot->layer("main");
    if (graph->pipeline().isBusy())
        QVERIFY(!mainLayer->pixelOffset().isNull());

    // Another replot should not crash with the large offset + scissor
    mPlot->replot(QCustomPlot::rpImmediateRefresh);
}
```

Add test slot to `tests/auto/test-pipeline/test-pipeline.h`:

```cpp
void layerTranslationClippedToAxisRect();
```

- [ ] **Step 2: Add scissor clipping in render loop**

In `src/core.cpp`, in the compositing loop, after writing the UBO and before the draw call, add scissor clipping when offset is nonzero:

```cpp
bool needsScissor = !layerOffset.isNull();
if (needsScissor)
{
    // Find the axis rect for scissor clipping
    QRect clipRect;
    for (auto* child : layer->children())
    {
        if (auto* plottable = qobject_cast<QCPAbstractPlottable*>(child))
        {
            clipRect = clipRect.isNull() ? plottable->clipRect()
                                         : clipRect.united(plottable->clipRect());
        }
    }
    if (!clipRect.isNull())
    {
        double dpr = bufferDevicePixelRatio();
        int sx = static_cast<int>(clipRect.x() * dpr);
        int sy = static_cast<int>(clipRect.y() * dpr);
        int sw = static_cast<int>(clipRect.width() * dpr);
        int sh = static_cast<int>(clipRect.height() * dpr);
        if (mRhi->isYUpInNDC())
            sy = outputSize.height() - sy - sh;
        cb->setScissor({sx, sy, sw, sh});
    }
}
```

After the `cb->drawIndexed(6);`, reset scissor if it was set:

```cpp
if (needsScissor)
{
    // Reset to full viewport for subsequent draws
    cb->setScissor({0, 0, outputSize.width(), outputSize.height()});
}
```

Also, the pipeline must have scissor test enabled. In the pipeline creation block, add before `mCompositePipeline->create()`:

```cpp
QRhiGraphicsPipeline::Flags flags = mCompositePipeline->flags();
flags |= QRhiGraphicsPipeline::UsesScissor;
mCompositePipeline->setFlags(flags);
```

- [ ] **Step 3: Run tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/core.cpp tests/auto/test-pipeline/test-layer-translation.cpp \
        tests/auto/test-pipeline/test-pipeline.h
git commit -m "feat: add scissor clipping for translated composite layers"
```

---

### Task 8: Regression tests and cleanup

**Files:**
- Test: `tests/auto/test-pipeline/test-layer-translation.cpp`
- Modify: `tests/auto/test-pipeline/test-pipeline.h`

- [ ] **Step 1: Add regression test — buffered main layer doesn't break rendering**

Add to `tests/auto/test-pipeline/test-layer-translation.cpp`:

```cpp
void TestPipeline::bufferedMainLayerRendersSameAsLogical()
{
    // Verify that the main layer being lmBuffered doesn't break basic rendering.
    // Create a simple graph and verify it renders without crashing.
    auto* graph = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    QVector<double> keys(100), values(100);
    for (int i = 0; i < 100; ++i) {
        keys[i] = i;
        values[i] = std::sin(i * 0.1);
    }
    graph->setData(std::move(keys), std::move(values));

    mPlot->xAxis->setRange(0, 100);
    mPlot->yAxis->setRange(-1.5, 1.5);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QTRY_VERIFY_WITH_TIMEOUT(!graph->pipeline().isBusy(), 5000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    // Verify main layer is indeed buffered
    QCOMPARE(mPlot->layer("main")->mode(), QCPLayer::lmBuffered);

    // Export should work
    QPixmap pixmap = mPlot->toPixmap(400, 300);
    QVERIFY(!pixmap.isNull());
}
```

Add test slot to `tests/auto/test-pipeline/test-pipeline.h`:

```cpp
void bufferedMainLayerRendersSameAsLogical();
```

- [ ] **Step 2: Add regression test — existing GPU translation still works**

Add to `tests/auto/test-pipeline/test-layer-translation.cpp`:

```cpp
void TestPipeline::existingGraph2TranslationUnaffected()
{
    // Verify the per-plottable GPU translation (Graph2 vertex shader offset)
    // still works correctly alongside the new layer-level translation.
    auto* graph = new QCPGraph2(mPlot->xAxis, mPlot->yAxis);
    QVector<double> keys(200000), values(200000);
    for (int i = 0; i < 200000; ++i) {
        keys[i] = i;
        values[i] = std::sin(i * 0.01);
    }
    graph->setData(std::move(keys), std::move(values));

    mPlot->xAxis->setRange(0, 100000);
    mPlot->yAxis->setRange(-1.5, 1.5);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QTRY_VERIFY_WITH_TIMEOUT(!graph->pipeline().isBusy(), 5000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    QVERIFY(graph->hasRenderedRange());

    // Pan
    mPlot->xAxis->setRange(10000, 110000);
    mPlot->replot(QCustomPlot::rpImmediateRefresh);

    if (graph->pipeline().isBusy())
    {
        QVERIFY(graph->hasRenderedRange());
        QPointF offset = graph->stallPixelOffset();
        QVERIFY(!offset.isNull());
    }
}
```

Add test slot:

```cpp
void existingGraph2TranslationUnaffected();
```

- [ ] **Step 3: Run all tests**

Run: `meson compile -C build && meson test -C build --print-errorlogs`
Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/auto/test-pipeline/test-layer-translation.cpp \
        tests/auto/test-pipeline/test-pipeline.h
git commit -m "test: add regression tests for layer-level GPU translation"
```
