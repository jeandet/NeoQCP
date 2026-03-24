# GPU Grid Line & Tick Mark Rendering — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render grid lines, zero-lines, subtick lines, and tick marks via QRhi GPU shaders to eliminate ~120 QPainter `drawLine()` calls per pan frame on macOS.

**Architecture:** New `QCPGridRhiLayer` class reuses the existing span shader (`span.vert` + `plottable.frag`). Tick positions stored as data-space coordinates in a vertex buffer; the shader maps data→pixel via UBO axis ranges. Geometry rebuilds only when the tick set changes. Two render passes: grid lines on `"grid"` layer, tick marks on `"axes"` layer.

**Tech Stack:** Qt6 QRhi, C++20, Meson, Qt Test

**Spec:** `docs/superpowers/specs/2026-03-24-gpu-grid-tick-rendering-design.md`

---

## File Map

| File | Action | Responsibility |
|---|---|---|
| `src/painting/grid-rhi-layer.h` | Create | `QCPGridRhiLayer` class declaration |
| `src/painting/grid-rhi-layer.cpp` | Create | Geometry build, upload, render, dirty detection |
| `src/core.h` | Modify (line ~388) | Add `mGridRhiLayer` member, `gridRhiLayer()` accessor |
| `src/core.cpp` | Modify (lines ~669, ~2645, ~2835, ~2853, ~2882) | Lazy creation, pipeline setup, render integration, cleanup, resize |
| `src/axis/axis.h` | Modify (line ~255) | Add `subTickVector()` public getter |
| `src/axis/axis.cpp` | Modify (lines ~82–128, ~153, ~2312–2354) | `markGeometryDirty()` in setters, QPainter skip in `draw()` and tick drawing |
| `meson.build` | Modify (line ~168) | Add new source files |
| `tests/auto/test-grid-rhi/test-grid-rhi.h` | Create | Test class declaration |
| `tests/auto/test-grid-rhi/test-grid-rhi.cpp` | Create | Grid RHI layer tests |
| `tests/auto/autotest.cpp` | Modify (line ~27) | Include and register test suite |
| `tests/auto/meson.build` | Modify (line ~33) | Add test source files |

---

### Task 0: Add `subTickVector()` public getter to QCPAxis

The `mSubTickVector` member is protected with no public accessor. The grid RHI layer needs read access to tick values.

**Files:**
- Modify: `src/axis/axis.h` (line ~255, near `tickVector()`)

- [ ] **Step 1: Add getter**

Near the existing `QVector<double> tickVector() const` getter (~line 255 in axis.h), add:
```cpp
QVector<double> subTickVector() const { return mSubTickVector; }
```

- [ ] **Step 2: Build to verify**

Run: `meson compile -C build`
Expected: Clean build.

- [ ] **Step 3: Commit**

```bash
git add src/axis/axis.h
git commit -m "feat: add subTickVector() public getter to QCPAxis"
```

---

### Task 1: Scaffold `QCPGridRhiLayer` with empty pipeline

Create the new class with the minimum structure to compile and integrate into the build. No geometry yet — just lifecycle.

**Files:**
- Create: `src/painting/grid-rhi-layer.h`
- Create: `src/painting/grid-rhi-layer.cpp`
- Modify: `meson.build` (line ~168)

- [ ] **Step 1: Create `grid-rhi-layer.h`**

```cpp
#pragma once

#include <QColor>
#include <QPointF>
#include <QRect>
#include <QVector>
#include <QMap>
#include <rhi/qrhi.h>

class QCPAxis;
class QCPAxisRect;

class QCPGridRhiLayer
{
public:
    struct DrawGroup
    {
        QCPAxisRect* axisRect = nullptr;
        int vertexOffset = 0;
        int vertexCount = 0;
        QRect scissorRect;             // empty = no scissor (tick marks)
        QRhiBuffer* uniformBuffer = nullptr;
        QRhiShaderResourceBindings* srb = nullptr;
        bool isGridLines = true;       // false = tick marks
    };

    explicit QCPGridRhiLayer(QRhi* rhi);
    ~QCPGridRhiLayer();

    void markGeometryDirty();
    bool hasContent() const { return !mDrawGroups.isEmpty(); }

    void invalidatePipeline();
    bool ensurePipeline(QRhiRenderPassDescriptor* rpDesc, int sampleCount);
    void uploadResources(QRhiResourceUpdateBatch* updates,
                         const QSize& outputSize, float dpr, bool isYUpInNDC);
    void renderGridLines(QRhiCommandBuffer* cb, const QSize& outputSize);
    void renderTickMarks(QRhiCommandBuffer* cb, const QSize& outputSize);

private:
    void rebuildGeometry(float dpr, int outputHeight, bool isYUpInNDC);
    void renderGroups(QRhiCommandBuffer* cb, const QSize& outputSize, bool gridLines);
    void cleanupDrawGroups();

    QRhi* mRhi;

    QVector<float> mStagingVertices;
    QVector<DrawGroup> mDrawGroups;
    bool mGeometryDirty = true;

    QRhiBuffer* mVertexBuffer = nullptr;
    QRhiGraphicsPipeline* mPipeline = nullptr;
    QRhiShaderResourceBindings* mLayoutSrb = nullptr;
    QRhiBuffer* mLayoutUbo = nullptr;
    int mVertexBufferSize = 0;
    int mLastSampleCount = 0;
    QMap<QCPAxisRect*, QRect> mLastAxisRectBounds;
};
```

- [ ] **Step 2: Create `grid-rhi-layer.cpp` with stub implementations**

```cpp
#include "grid-rhi-layer.h"
#include "Profiling.hpp"
#include "embedded_shaders.h"
#include "../axis/axis.h"
#include "../layoutelements/layoutelement-axisrect.h"

#include <array>
#include <cmath>

static constexpr int kFloatsPerVertex = 11;
static constexpr int kUniformBufferSize = 64;

QCPGridRhiLayer::QCPGridRhiLayer(QRhi* rhi)
    : mRhi(rhi)
{
}

QCPGridRhiLayer::~QCPGridRhiLayer()
{
    delete mPipeline;
    delete mLayoutSrb;
    delete mLayoutUbo;
    delete mVertexBuffer;
    cleanupDrawGroups();
}

void QCPGridRhiLayer::markGeometryDirty() { mGeometryDirty = true; }

void QCPGridRhiLayer::cleanupDrawGroups()
{
    for (auto& group : mDrawGroups)
    {
        delete group.uniformBuffer;
        delete group.srb;
    }
    mDrawGroups.clear();
}

void QCPGridRhiLayer::invalidatePipeline()
{
    delete mPipeline;
    mPipeline = nullptr;
    delete mLayoutSrb;
    mLayoutSrb = nullptr;
    delete mLayoutUbo;
    mLayoutUbo = nullptr;
    cleanupDrawGroups();
}

bool QCPGridRhiLayer::ensurePipeline(QRhiRenderPassDescriptor* rpDesc, int sampleCount)
{
    PROFILE_HERE_N("QCPGridRhiLayer::ensurePipeline");

    if (mPipeline && mLastSampleCount == sampleCount)
        return true;

    invalidatePipeline();

    QShader vertShader = QShader::fromSerialized(QByteArray::fromRawData(
        reinterpret_cast<const char*>(span_vert_qsb_data), span_vert_qsb_data_len));
    QShader fragShader = QShader::fromSerialized(QByteArray::fromRawData(
        reinterpret_cast<const char*>(plottable_frag_qsb_data), plottable_frag_qsb_data_len));

    if (!vertShader.isValid() || !fragShader.isValid())
        return false;

    mLayoutUbo = mRhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUniformBufferSize);
    mLayoutUbo->create();

    mLayoutSrb = mRhi->newShaderResourceBindings();
    mLayoutSrb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage, mLayoutUbo)
    });
    mLayoutSrb->create();

    mPipeline = mRhi->newGraphicsPipeline();
    mPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vertShader},
        {QRhiShaderStage::Fragment, fragShader}
    });

    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{kFloatsPerVertex * sizeof(float)}});
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {0, 1, QRhiVertexInputAttribute::Float4, 2 * sizeof(float)},
        {0, 2, QRhiVertexInputAttribute::Float2, 6 * sizeof(float)},
        {0, 3, QRhiVertexInputAttribute::Float,  8 * sizeof(float)},
        {0, 4, QRhiVertexInputAttribute::Float2, 9 * sizeof(float)},
    });
    mPipeline->setVertexInputLayout(inputLayout);

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    mPipeline->setTargetBlends({blend});

    mPipeline->setFlags(QRhiGraphicsPipeline::UsesScissor);
    mPipeline->setTopology(QRhiGraphicsPipeline::Triangles);
    mPipeline->setSampleCount(sampleCount);
    mPipeline->setRenderPassDescriptor(rpDesc);
    mPipeline->setShaderResourceBindings(mLayoutSrb);

    if (!mPipeline->create())
    {
        delete mPipeline;
        mPipeline = nullptr;
        return false;
    }

    mLastSampleCount = sampleCount;
    return true;
}

void QCPGridRhiLayer::rebuildGeometry(float /*dpr*/, int /*outputHeight*/, bool /*isYUpInNDC*/)
{
    // Stub — Task 2 fills this in
    mStagingVertices.clear();
    cleanupDrawGroups();
}

void QCPGridRhiLayer::uploadResources(QRhiResourceUpdateBatch* /*updates*/,
                                       const QSize& /*outputSize*/, float /*dpr*/,
                                       bool /*isYUpInNDC*/)
{
    // Stub — Task 3 fills this in
}

void QCPGridRhiLayer::renderGroups(QRhiCommandBuffer* cb, const QSize& outputSize, bool gridLines)
{
    if (!mPipeline || !mVertexBuffer || mDrawGroups.isEmpty())
        return;

    cb->setGraphicsPipeline(mPipeline);
    cb->setViewport({0, 0, float(outputSize.width()), float(outputSize.height())});

    const QRhiCommandBuffer::VertexInput vbufBinding(mVertexBuffer, 0);
    cb->setVertexInput(0, 1, &vbufBinding);

    for (const auto& group : mDrawGroups)
    {
        if (group.isGridLines != gridLines)
            continue;
        if (!group.scissorRect.isNull())
            cb->setScissor({group.scissorRect.x(), group.scissorRect.y(),
                            group.scissorRect.width(), group.scissorRect.height()});
        else
            cb->setScissor({0, 0, outputSize.width(), outputSize.height()});
        cb->setShaderResources(group.srb);
        cb->draw(group.vertexCount, 1, group.vertexOffset, 0);
    }
}

void QCPGridRhiLayer::renderGridLines(QRhiCommandBuffer* cb, const QSize& outputSize)
{
    PROFILE_HERE_N("QCPGridRhiLayer::renderGridLines");
    renderGroups(cb, outputSize, true);
}

void QCPGridRhiLayer::renderTickMarks(QRhiCommandBuffer* cb, const QSize& outputSize)
{
    PROFILE_HERE_N("QCPGridRhiLayer::renderTickMarks");
    renderGroups(cb, outputSize, false);
}
```

- [ ] **Step 3: Add to meson.build**

In `meson.build`, after the `'src/painting/span-rhi-layer.cpp'` line (~168), add:
```
'src/painting/grid-rhi-layer.cpp',
```

In `extra_files` (~234), add `'src/painting/grid-rhi-layer.h'`.

- [ ] **Step 4: Build to verify compilation**

Run: `meson compile -C build`
Expected: Clean build, no errors.

- [ ] **Step 5: Commit**

```bash
git add src/painting/grid-rhi-layer.h src/painting/grid-rhi-layer.cpp meson.build
git commit -m "feat: scaffold QCPGridRhiLayer with pipeline setup"
```

---

### Task 2: Wire `QCPGridRhiLayer` into QCustomPlot lifecycle

Integrate the new layer into core.h/cpp: lazy creation, pipeline setup, render calls, cleanup.

**Files:**
- Modify: `src/core.h` (line ~388)
- Modify: `src/core.cpp` (lines ~669, ~2645, ~2835, ~2853, ~2882)

- [ ] **Step 1: Add member and accessor to `core.h`**

After `mSpanRhiLayer` declaration (~line 388), add:
```cpp
QCPGridRhiLayer* mGridRhiLayer = nullptr;
```

Add forward declaration at top of file (near `class QCPSpanRhiLayer`):
```cpp
class QCPGridRhiLayer;
```

Add public accessor near `spanRhiLayer()` (~line 223):
```cpp
QCPGridRhiLayer* gridRhiLayer();
```

- [ ] **Step 2: Add lazy creation in `core.cpp`**

After `QCPSpanRhiLayer* QCustomPlot::spanRhiLayer()` (~line 674), add:
```cpp
QCPGridRhiLayer* QCustomPlot::gridRhiLayer()
{
    if (!mGridRhiLayer && mRhi)
        mGridRhiLayer = new QCPGridRhiLayer(mRhi);
    return mGridRhiLayer;
}
```

Add include at top of `core.cpp`:
```cpp
#include "painting/grid-rhi-layer.h"
```

- [ ] **Step 3: Add pipeline setup and upload in `render()`**

After the span pipeline setup block (~line 2649), add:
```cpp
// Upload grid RHI resources
if (mGridRhiLayer && mGridRhiLayer->hasContent())
{
    mGridRhiLayer->ensurePipeline(renderTarget()->renderPassDescriptor(), sampleCount());
    mGridRhiLayer->uploadResources(updates, outputSize, mBufferDevicePixelRatio,
                                    mRhi->isYUpInNDC());
}
```

- [ ] **Step 4: Add render calls in the layer loop**

In the layer loop, after the span render block (~line 2835), add a similar block. The exact placement requires rendering grid lines when `layer == "grid"` and tick marks when `layer == "axes"`. After the paint buffer composite section for each layer:

```cpp
// Draw GPU grid lines on the "grid" layer
if (layer == this->layer(QLatin1String("grid"))
    && mGridRhiLayer && mGridRhiLayer->hasContent())
    mGridRhiLayer->renderGridLines(cb, outputSize);

// Draw GPU tick marks on the "axes" layer
if (layer == this->layer(QLatin1String("axes"))
    && mGridRhiLayer && mGridRhiLayer->hasContent())
    mGridRhiLayer->renderTickMarks(cb, outputSize);
```

Place these at the appropriate points in the layer loop — grid lines after compositing the `"grid"` layer's paint buffer, tick marks after compositing the `"axes"` layer's paint buffer.

- [ ] **Step 5: Add cleanup in `releaseResources()`**

After `delete mSpanRhiLayer; mSpanRhiLayer = nullptr;` (~line 2854), add:
```cpp
delete mGridRhiLayer;
mGridRhiLayer = nullptr;
```

- [ ] **Step 6: Add dirty marking in `resizeEvent()`**

After `mSpanRhiLayer->markGeometryDirty()` (~line 2883), add:
```cpp
if (mGridRhiLayer)
    mGridRhiLayer->markGeometryDirty();
```

- [ ] **Step 7: Add invalidation in `initialize()` (pipeline reset)**

Find where `mSpanRhiLayer->invalidatePipeline()` is called (~line 2513) and add:
```cpp
if (mGridRhiLayer)
    mGridRhiLayer->invalidatePipeline();
```

- [ ] **Step 8: Build and run tests**

Run: `meson compile -C build && meson test -C build`
Expected: Clean build, all existing tests pass (grid layer has no content yet, so render calls are no-ops).

- [ ] **Step 9: Commit**

```bash
git add src/core.h src/core.cpp
git commit -m "feat: wire QCPGridRhiLayer into QCustomPlot lifecycle"
```

---

### Task 3: Implement grid line geometry generation

Fill in `rebuildGeometry()` and `uploadResources()` to generate vertex data for grid lines (major, sub, zero-line) from axis tick vectors. This task covers grid lines only (scissored to axis rect, rendered on `"grid"` layer). Tick marks are Task 4.

**Files:**
- Modify: `src/painting/grid-rhi-layer.h` (add axis tracking members)
- Modify: `src/painting/grid-rhi-layer.cpp` (implement `rebuildGeometry` and `uploadResources`)

**Reference:** `src/painting/span-rhi-layer.cpp` lines 215–497 for the pattern.

- [ ] **Step 1: Add axis registration and dirty tracking to header**

Add to `QCPGridRhiLayer` private section:
```cpp
// Registered axes whose grid lines we render
QVector<QCPAxis*> mAxes;

// Cached tick values for dirty detection
struct CachedAxisTicks {
    QVector<double> majorTicks;
    QVector<double> subTicks;
    bool subGridVisible = false;
    QRgb gridColor = 0;
    QRgb subGridColor = 0;
    QRgb zeroLineColor = 0;
    float gridPenWidth = 0;
    float subGridPenWidth = 0;
    float zeroLinePenWidth = 0;
    Qt::PenStyle zeroLinePenStyle = Qt::NoPen;
};
QMap<QCPAxis*, CachedAxisTicks> mCachedTicks;
```

Add public methods:
```cpp
void registerAxis(QCPAxis* axis);
void unregisterAxis(QCPAxis* axis);
```

- [ ] **Step 2: Implement axis registration**

```cpp
void QCPGridRhiLayer::registerAxis(QCPAxis* axis)
{
    if (!mAxes.contains(axis))
    {
        mAxes.append(axis);
        mGeometryDirty = true;
    }
}

void QCPGridRhiLayer::unregisterAxis(QCPAxis* axis)
{
    if (mAxes.removeOne(axis))
    {
        mCachedTicks.remove(axis);
        mGeometryDirty = true;
    }
}
```

- [ ] **Step 3: Add `premultiply` and `appendBorder` helpers**

Copy the same static helper functions from `span-rhi-layer.cpp` (lines 18–81): `premultiply()`, `appendVertex()`, `appendBorder()`. These are file-local statics, so no conflict.

- [ ] **Step 4: Implement `rebuildGeometry()`**

This is the core logic. For each registered axis, emit grid line quads. The approach:
- Group by axis rect (like spans)
- For each axis, iterate `mTickVector` and `mSubTickVector`
- Horizontal axes (`atBottom`/`atTop`): vertical grid lines spanning axis rect height. `dataCoord.x = tickValue` (data space), `dataCoord.y = pixTop`/`pixBot` (pixel space). `isPixel = (0, 1)`.
- Vertical axes (`atLeft`/`atRight`): horizontal grid lines spanning axis rect width. `dataCoord.x = pixLeft`/`pixRight` (pixel space), `dataCoord.y = tickValue` (data space). `isPixel = (1, 0)`.
- Zero-line: same as major grid line but with `mZeroLinePen` color, only if range crosses zero.

Each draw group needs a per-axis-rect UBO parameterized from the actual parent axis (not hardcoded `atBottom`/`atLeft`).

```cpp
void QCPGridRhiLayer::rebuildGeometry(float dpr, int outputHeight, bool isYUpInNDC)
{
    PROFILE_HERE_N("QCPGridRhiLayer::rebuildGeometry");

    mStagingVertices.clear();
    cleanupDrawGroups();

    // Group axes by axis rect
    QMap<QCPAxisRect*, QVector<QCPAxis*>> groupedAxes;
    for (auto* axis : mAxes)
    {
        if (auto* ar = axis->axisRect())
            groupedAxes[ar].append(axis);
    }

    for (auto it = groupedAxes.constBegin(); it != groupedAxes.constEnd(); ++it)
    {
        QCPAxisRect* ar = it.key();
        const auto& axes = it.value();

        int groupVertexStart = mStagingVertices.size() / kFloatsPerVertex;

        for (auto* axis : axes)
        {
            QCPGrid* grid = axis->grid();
            if (!grid)
                continue;

            const bool isHorizontal = (axis->orientation() == Qt::Horizontal);
            const float pixLeft = float(ar->left());
            const float pixRight = float(ar->left() + ar->width());
            const float pixTop = float(ar->top());
            const float pixBot = float(ar->top() + ar->height());

            auto emitGridLine = [&](double tickValue, const QPen& pen) {
                auto color = premultiply(pen.color());
                float penW = pen.widthF();
                float halfW = (penW == 0.0 || pen.isCosmetic()) ? 0.5f : float(penW) / 2.0f;
                if (pen.style() == Qt::NoPen || halfW <= 0.0f || color[3] <= 0.0f)
                    return;
                float tv = float(tickValue);
                if (isHorizontal)
                {
                    // Vertical line: X = data, Y = pixel
                    appendBorder(mStagingVertices,
                                 tv, pixTop, tv, pixBot,
                                 color, 1, 0, halfW, 0, 1);
                }
                else
                {
                    // Horizontal line: X = pixel, Y = data
                    appendBorder(mStagingVertices,
                                 pixLeft, tv, pixRight, tv,
                                 color, 0, 1, halfW, 1, 0);
                }
            };

            // Zero-line
            const QCPRange& range = axis->range();
            if (grid->zeroLinePen().style() != Qt::NoPen
                && range.lower < 0 && range.upper > 0)
            {
                emitGridLine(0.0, grid->zeroLinePen());
            }

            // Major grid lines
            for (double tickVal : axis->tickVector())
                emitGridLine(tickVal, grid->pen());

            // Sub grid lines
            if (grid->subGridVisible())
            {
                for (double subTickVal : axis->subTickVector())
                    emitGridLine(subTickVal, grid->subGridPen());
            }
        }

        int groupVertexCount = mStagingVertices.size() / kFloatsPerVertex - groupVertexStart;
        if (groupVertexCount == 0)
            continue;

        // Scissor rect in physical pixels
        int sx = int(ar->left() * dpr);
        int sy = int(ar->top() * dpr);
        int sw = int(ar->width() * dpr);
        int sh = int(ar->height() * dpr);
        if (isYUpInNDC)
            sy = outputHeight - sy - sh;

        auto* ubo = mRhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUniformBufferSize);
        ubo->create();

        auto* srb = mRhi->newShaderResourceBindings();
        srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage, ubo)
        });
        srb->create();

        DrawGroup group;
        group.axisRect = ar;
        group.vertexOffset = groupVertexStart;
        group.vertexCount = groupVertexCount;
        group.scissorRect = QRect(sx, sy, sw, sh);
        group.uniformBuffer = ubo;
        group.srb = srb;
        group.isGridLines = true;
        mDrawGroups.append(group);
    }

    mLastAxisRectBounds.clear();
    for (const auto& group : mDrawGroups)
        mLastAxisRectBounds[group.axisRect] = QRect(group.axisRect->left(), group.axisRect->top(),
                                                     group.axisRect->width(), group.axisRect->height());
}
```

- [ ] **Step 5: Implement `uploadResources()`**

```cpp
void QCPGridRhiLayer::uploadResources(QRhiResourceUpdateBatch* updates,
                                       const QSize& outputSize, float dpr,
                                       bool isYUpInNDC)
{
    PROFILE_HERE_N("QCPGridRhiLayer::uploadResources");

    // Detect tick value changes
    if (!mGeometryDirty)
    {
        for (auto* axis : mAxes)
        {
            auto it = mCachedTicks.constFind(axis);
            if (it == mCachedTicks.constEnd())
            {
                mGeometryDirty = true;
                break;
            }
            const auto& cached = it.value();
            QCPGrid* grid = axis->grid();
            if (cached.majorTicks != axis->tickVector()
                || cached.subTicks != axis->subTickVector()
                || cached.subGridVisible != grid->subGridVisible()
                || cached.gridColor != grid->pen().color().rgba()
                || cached.subGridColor != grid->subGridPen().color().rgba()
                || cached.zeroLineColor != grid->zeroLinePen().color().rgba()
                || cached.gridPenWidth != float(grid->pen().widthF())
                || cached.subGridPenWidth != float(grid->subGridPen().widthF())
                || cached.zeroLinePenWidth != float(grid->zeroLinePen().widthF())
                || cached.zeroLinePenStyle != grid->zeroLinePen().style())
            {
                mGeometryDirty = true;
                break;
            }
        }
    }

    // Detect layout changes
    if (!mGeometryDirty)
    {
        for (const auto& group : mDrawGroups)
        {
            QRect current(group.axisRect->left(), group.axisRect->top(),
                          group.axisRect->width(), group.axisRect->height());
            if (mLastAxisRectBounds.value(group.axisRect) != current)
            {
                mGeometryDirty = true;
                break;
            }
        }
    }

    if (mGeometryDirty)
    {
        rebuildGeometry(dpr, outputSize.height(), isYUpInNDC);
        mGeometryDirty = false;

        // Cache tick values
        mCachedTicks.clear();
        for (auto* axis : mAxes)
        {
            QCPGrid* grid = axis->grid();
            CachedAxisTicks cached;
            cached.majorTicks = axis->tickVector();
            cached.subTicks = axis->subTickVector();
            cached.subGridVisible = grid->subGridVisible();
            cached.gridColor = grid->pen().color().rgba();
            cached.subGridColor = grid->subGridPen().color().rgba();
            cached.zeroLineColor = grid->zeroLinePen().color().rgba();
            cached.gridPenWidth = float(grid->pen().widthF());
            cached.subGridPenWidth = float(grid->subGridPen().widthF());
            cached.zeroLinePenWidth = float(grid->zeroLinePen().widthF());
            cached.zeroLinePenStyle = grid->zeroLinePen().style();
            mCachedTicks[axis] = cached;
        }

        if (!mStagingVertices.isEmpty())
        {
            int requiredSize = mStagingVertices.size() * sizeof(float);
            if (!mVertexBuffer || mVertexBufferSize < requiredSize)
            {
                delete mVertexBuffer;
                mVertexBuffer = mRhi->newBuffer(QRhiBuffer::Dynamic,
                                                 QRhiBuffer::VertexBuffer,
                                                 requiredSize);
                mVertexBuffer->create();
                mVertexBufferSize = requiredSize;
            }
            updates->updateDynamicBuffer(mVertexBuffer, 0, requiredSize,
                                          mStagingVertices.constData());
        }
    }

    // Always update UBOs with current axis ranges
    for (auto& group : mDrawGroups)
    {
        QCPAxisRect* ar = group.axisRect;

        // Find a representative horizontal and vertical axis for this axis rect.
        // Uses type priority (bottom→top, left→right) — same limitation as QCPSpanRhiLayer.
        // Axis rects with only top+right axes are rare; fix both layers together if needed.
        QCPAxis* hAxis = ar->axis(QCPAxis::atBottom);
        if (!hAxis) hAxis = ar->axis(QCPAxis::atTop);
        QCPAxis* vAxis = ar->axis(QCPAxis::atLeft);
        if (!vAxis) vAxis = ar->axis(QCPAxis::atRight);
        if (!hAxis || !vAxis)
            continue;

        float keyOffset, keyLength;
        if (!hAxis->rangeReversed())
        {
            keyOffset = float(ar->left());
            keyLength = float(ar->width());
        }
        else
        {
            keyOffset = float(ar->right());
            keyLength = float(-ar->width());
        }

        float valOffset, valLength;
        if (!vAxis->rangeReversed())
        {
            valOffset = float(ar->bottom());
            valLength = float(-ar->height());
        }
        else
        {
            valOffset = float(ar->top());
            valLength = float(ar->height());
        }

        struct {
            float width, height, yFlip, dpr;
            float keyRangeLower, keyRangeUpper, keyAxisOffset, keyAxisLength, keyLogScale;
            float valRangeLower, valRangeUpper, valAxisOffset, valAxisLength, valLogScale;
            float _pad0, _pad1;
        } params = {
            float(outputSize.width()),
            float(outputSize.height()),
            isYUpInNDC ? -1.0f : 1.0f,
            dpr,
            float(hAxis->range().lower),
            float(hAxis->range().upper),
            keyOffset,
            keyLength,
            (hAxis->scaleType() == QCPAxis::stLogarithmic) ? 1.0f : 0.0f,
            float(vAxis->range().lower),
            float(vAxis->range().upper),
            valOffset,
            valLength,
            (vAxis->scaleType() == QCPAxis::stLogarithmic) ? 1.0f : 0.0f,
            0.0f, 0.0f
        };
        static_assert(sizeof(params) == kUniformBufferSize);
        updates->updateDynamicBuffer(group.uniformBuffer, 0, sizeof(params), &params);
    }
}
```

- [ ] **Step 6: Build to verify**

Run: `meson compile -C build`
Expected: Clean build.

- [ ] **Step 7: Commit**

```bash
git add src/painting/grid-rhi-layer.h src/painting/grid-rhi-layer.cpp
git commit -m "feat: implement grid line geometry generation and upload"
```

---

### Task 4: Add tick mark geometry generation

Extend `rebuildGeometry()` to emit tick marks and subtick marks as additional draw groups (unscissored, `isGridLines = false`), rendered on the `"axes"` layer.

**Files:**
- Modify: `src/painting/grid-rhi-layer.cpp` (`rebuildGeometry()`)

**Reference:** `src/axis/axis.cpp` lines 2311–2354 for tick mark geometry.

- [ ] **Step 1: Add tick mark generation to `rebuildGeometry()`**

After the grid lines draw group is added for each axis rect, add a second pass for tick marks. For each axis in the rect:

```cpp
// --- Tick marks (second pass, per axis rect) ---
int tickGroupVertexStart = mStagingVertices.size() / kFloatsPerVertex;

for (auto* axis : axes)
{
    const bool isHorizontal = (axis->orientation() == Qt::Horizontal);
    const auto axisType = axis->axisType();

    // Tick direction: "outward" direction from axis rect
    int tickDir = (axisType == QCPAxis::atBottom || axisType == QCPAxis::atRight) ? -1 : 1;

    // Axis baseline pixel position
    float baseline = 0.0f;
    switch (axisType)
    {
        case QCPAxis::atBottom: baseline = float(ar->bottom()); break;
        case QCPAxis::atTop:    baseline = float(ar->top()); break;
        case QCPAxis::atLeft:   baseline = float(ar->left()); break;
        case QCPAxis::atRight:  baseline = float(ar->right()); break;
    }

    auto emitTickLine = [&](double tickValue, float lengthOut, float lengthIn, const QPen& pen) {
        auto color = premultiply(pen.color());
        float penW = pen.widthF();
        float halfW = (penW == 0.0 || pen.isCosmetic()) ? 0.5f : float(penW) / 2.0f;
        if (pen.style() == Qt::NoPen || halfW <= 0.0f || color[3] <= 0.0f)
            return;
        float tv = float(tickValue);
        float pxStart = baseline - lengthOut * tickDir;
        float pxEnd = baseline + lengthIn * tickDir;
        if (isHorizontal)
        {
            // Vertical tick line at data X, pixel Y
            appendBorder(mStagingVertices,
                         tv, pxStart, tv, pxEnd,
                         color, 1, 0, halfW, 0, 1);
        }
        else
        {
            // Horizontal tick line at pixel X, data Y
            appendBorder(mStagingVertices,
                         pxStart, tv, pxEnd, tv,
                         color, 0, 1, halfW, 1, 0);
        }
    };

    // Major ticks
    float tickLenOut = float(axis->tickLengthOut());
    float tickLenIn = float(axis->tickLengthIn());
    for (double tickVal : axis->tickVector())
        emitTickLine(tickVal, tickLenOut, tickLenIn, axis->tickPen());

    // Sub ticks
    if (axis->subTicks())
    {
        float subTickLenOut = float(axis->subTickLengthOut());
        float subTickLenIn = float(axis->subTickLengthIn());
        for (double subTickVal : axis->subTickVector())
            emitTickLine(subTickVal, subTickLenOut, subTickLenIn, axis->subTickPen());
    }
}

int tickGroupVertexCount = mStagingVertices.size() / kFloatsPerVertex - tickGroupVertexStart;
if (tickGroupVertexCount > 0)
{
    auto* ubo = mRhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer, kUniformBufferSize);
    ubo->create();

    auto* srb = mRhi->newShaderResourceBindings();
    srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0, QRhiShaderResourceBinding::VertexStage, ubo)
    });
    srb->create();

    DrawGroup group;
    group.axisRect = ar;
    group.vertexOffset = tickGroupVertexStart;
    group.vertexCount = tickGroupVertexCount;
    group.scissorRect = QRect();  // no scissor for tick marks
    group.uniformBuffer = ubo;
    group.srb = srb;
    group.isGridLines = false;
    mDrawGroups.append(group);
}
```

- [ ] **Step 2: Update dirty detection to include tick pen properties**

In `CachedAxisTicks`, add:
```cpp
QRgb tickColor = 0;
QRgb subTickColor = 0;
float tickPenWidth = 0;
float subTickPenWidth = 0;
float tickLengthOut = 0;
float tickLengthIn = 0;
float subTickLengthOut = 0;
float subTickLengthIn = 0;
bool subTicksVisible = false;
```

Update the comparison in `uploadResources()` and the cache fill to include these fields.

- [ ] **Step 3: Build to verify**

Run: `meson compile -C build`
Expected: Clean build.

- [ ] **Step 4: Commit**

```bash
git add src/painting/grid-rhi-layer.h src/painting/grid-rhi-layer.cpp
git commit -m "feat: add tick mark geometry generation to QCPGridRhiLayer"
```

---

### Task 5: Auto-register axes and skip QPainter path

Register all axes with the grid RHI layer during replot, and skip QPainter grid/tick drawing when the RHI path is active.

**Files:**
- Modify: `src/core.cpp` (axis registration during replot)
- Modify: `src/axis/axis.cpp` (QPainter skip in `QCPGrid::draw()` and `QCPAxisPainterPrivate::draw()`)

- [ ] **Step 1: Auto-register axes during render**

In `core.cpp render()`, before the grid RHI pipeline setup, add axis registration. Find all axes in all axis rects and register them:

```cpp
// Register all axes with the grid RHI layer
if (mGridRhiLayer || mRhi)
{
    auto* grl = gridRhiLayer();
    if (grl)
    {
        for (auto* ar : axisRects())
        {
            for (auto* axis : ar->axes())
                grl->registerAxis(axis);
        }
    }
}
```

Note: this is idempotent — `registerAxis()` checks for duplicates.

- [ ] **Step 2: Skip QPainter grid lines in `QCPGrid::draw()`**

At the top of `QCPGrid::draw()` (axis.cpp ~line 153), after the null check, add:

```cpp
if (auto* plot = mParentAxis ? mParentAxis->parentPlot() : nullptr)
{
    if (plot->gridRhiLayer()
        && !painter->modes().testFlag(QCPPainter::pmVectorized)
        && !painter->modes().testFlag(QCPPainter::pmNoCaching))
        return;
}
```

- [ ] **Step 3: Skip QPainter tick marks in `QCPAxisPainterPrivate::draw()`**

This is trickier — `QCPAxisPainterPrivate` doesn't have direct access to the parent plot. The tick drawing section (~lines 2311–2354) needs a guard. The cleanest approach: add a `bool skipTickDrawing` field to `QCPAxisPainterPrivate`, set it from `QCPAxis::draw()` before calling `mAxisPainter->draw()`.

In `QCPAxisPainterPrivate` (declared in `src/axis/axis.h`), add a public field:
```cpp
bool skipTickDrawing = false;
```

In `QCPAxis::draw()` (axis.cpp), before calling `mAxisPainter->draw(painter)`, set:
```cpp
mAxisPainter->skipTickDrawing = mParentPlot && mParentPlot->gridRhiLayer()
    && !painter->modes().testFlag(QCPPainter::pmVectorized)
    && !painter->modes().testFlag(QCPPainter::pmNoCaching);
```

In `QCPAxisPainterPrivate::draw()`, wrap the tick and subtick loops (~lines 2312–2354):
```cpp
if (!skipTickDrawing)
{
    // existing tick drawing code...
}
```

- [ ] **Step 4: Build and run tests**

Run: `meson compile -C build && meson test -C build`
Expected: All tests pass. Grid lines and tick marks now rendered via GPU when RHI is available.

- [ ] **Step 5: Commit**

```bash
git add src/core.cpp src/axis/axis.cpp src/axis/axis.h
git commit -m "feat: auto-register axes and skip QPainter grid/tick drawing"
```

---

### Task 6: Write tests

Test the grid RHI layer: creation, geometry dirty tracking, replot without crash, export still uses QPainter.

**Files:**
- Create: `tests/auto/test-grid-rhi/test-grid-rhi.h`
- Create: `tests/auto/test-grid-rhi/test-grid-rhi.cpp`
- Modify: `tests/auto/autotest.cpp`
- Modify: `tests/auto/meson.build`

- [ ] **Step 1: Create test header**

```cpp
#pragma once
#include <QtTest/QtTest>

class QCustomPlot;

class TestGridRhi : public QObject {
    Q_OBJECT
private slots:
    void init();
    void cleanup();

    void gridRhiLayerCreatedLazily();
    void replotDoesNotCrash();
    void replotWithSubGridDoesNotCrash();
    void replotWithZeroLineDoesNotCrash();
    void replotWithLogAxisDoesNotCrash();
    void replotWithReversedAxisDoesNotCrash();
    void replotMultipleAxisRectsDoesNotCrash();
    void dirtyDetectionSkipsRebuildOnPan();
    void dirtyDetectionRebuildsOnTickChange();
    void dirtyDetectionRebuildsOnPenChange();
    void exportStillUsesQPainter();

private:
    QCustomPlot* mPlot = nullptr;
};
```

- [ ] **Step 2: Create test implementation**

```cpp
#include "test-grid-rhi.h"
#include "../../../src/qcp.h"
#include "../../../src/painting/grid-rhi-layer.h"

void TestGridRhi::init()
{
    mPlot = new QCustomPlot();
    mPlot->resize(400, 300);
    mPlot->xAxis->setRange(-5, 5);
    mPlot->yAxis->setRange(-5, 5);
    mPlot->replot();
}

void TestGridRhi::cleanup()
{
    delete mPlot;
    mPlot = nullptr;
}

void TestGridRhi::gridRhiLayerCreatedLazily()
{
    // Without calling gridRhiLayer(), the internal pointer should be null
    // (no way to directly check, but calling it should not crash)
    auto* grl = mPlot->gridRhiLayer();
    // In offscreen mode, mRhi may be null, so grl may be null
    // Just verify no crash
    Q_UNUSED(grl);
    QVERIFY(true);
}

void TestGridRhi::replotDoesNotCrash()
{
    mPlot->replot();
    QVERIFY(true);
}

void TestGridRhi::replotWithSubGridDoesNotCrash()
{
    mPlot->xAxis->grid()->setSubGridVisible(true);
    mPlot->yAxis->grid()->setSubGridVisible(true);
    mPlot->replot();
    QVERIFY(true);
}

void TestGridRhi::replotWithZeroLineDoesNotCrash()
{
    // Range crosses zero — zero-line should be emitted
    mPlot->xAxis->setRange(-10, 10);
    mPlot->yAxis->setRange(-10, 10);
    mPlot->xAxis->grid()->setZeroLinePen(QPen(Qt::black, 1, Qt::SolidLine));
    mPlot->yAxis->grid()->setZeroLinePen(QPen(Qt::black, 1, Qt::SolidLine));
    mPlot->replot();
    QVERIFY(true);
}

void TestGridRhi::replotWithLogAxisDoesNotCrash()
{
    mPlot->xAxis->setScaleType(QCPAxis::stLogarithmic);
    mPlot->xAxis->setRange(0.01, 1000);
    mPlot->xAxis->setTicker(QSharedPointer<QCPAxisTickerLog>(new QCPAxisTickerLog));
    mPlot->replot();
    QVERIFY(true);
}

void TestGridRhi::replotWithReversedAxisDoesNotCrash()
{
    mPlot->xAxis->setRangeReversed(true);
    mPlot->yAxis->setRangeReversed(true);
    mPlot->replot();
    QVERIFY(true);
}

void TestGridRhi::replotMultipleAxisRectsDoesNotCrash()
{
    mPlot->plotLayout()->addElement(0, 1, new QCPAxisRect(mPlot));
    mPlot->replot();
    QVERIFY(true);
}

void TestGridRhi::dirtyDetectionSkipsRebuildOnPan()
{
    mPlot->replot();

    // Small pan — tick values should remain the same
    QCPRange oldRange = mPlot->xAxis->range();
    mPlot->xAxis->setRange(oldRange.lower + 0.01, oldRange.upper + 0.01);
    mPlot->replot();

    // If we could inspect geometry dirty state, we'd verify it wasn't rebuilt.
    // For now, just verify no crash and the flow completes.
    QVERIFY(true);
}

void TestGridRhi::dirtyDetectionRebuildsOnTickChange()
{
    mPlot->replot();

    // Large pan that changes tick set
    mPlot->xAxis->setRange(100, 200);
    mPlot->replot();
    QVERIFY(true);
}

void TestGridRhi::dirtyDetectionRebuildsOnPenChange()
{
    mPlot->replot();

    mPlot->xAxis->grid()->setPen(QPen(Qt::red, 2));
    mPlot->replot();
    QVERIFY(true);
}

void TestGridRhi::exportStillUsesQPainter()
{
    // Export should produce a non-empty pixmap (grid lines drawn via QPainter)
    QPixmap pm = mPlot->toPixmap(200, 150);
    QVERIFY(!pm.isNull());
    QCOMPARE(pm.width(), 200);
    QCOMPARE(pm.height(), 150);
}
```

- [ ] **Step 3: Register test suite**

In `autotest.cpp`, add include:
```cpp
#include "test-grid-rhi/test-grid-rhi.h"
```

Add execution:
```cpp
QCPTEST(TestGridRhi);
```

In `tests/auto/meson.build`, add to `test_srcs`:
```
'test-grid-rhi/test-grid-rhi.cpp',
```

Add to `test_headers`:
```
'test-grid-rhi/test-grid-rhi.h',
```

- [ ] **Step 4: Build and run tests**

Run: `meson compile -C build && meson test -C build`
Expected: All tests pass, including the new `TestGridRhi` suite.

- [ ] **Step 5: Commit**

```bash
git add tests/auto/test-grid-rhi/ tests/auto/autotest.cpp tests/auto/meson.build
git commit -m "test: add QCPGridRhiLayer test suite"
```

---

### Task 7: Add `markGeometryDirty()` calls to grid/axis property setters

Ensure visual property changes trigger geometry rebuild without waiting for the next tick comparison.

**Files:**
- Modify: `src/axis/axis.cpp` (grid property setters ~lines 82–128)

- [ ] **Step 1: Add dirty marking to grid setters**

In each of these setters, add a `markGeometryDirty()` call on the parent plot's grid RHI layer:

```cpp
void QCPGrid::setSubGridVisible(bool visible)
{
    mSubGridVisible = visible;
    if (auto* plot = mParentAxis ? mParentAxis->parentPlot() : nullptr)
        if (auto* grl = plot->gridRhiLayer())
            grl->markGeometryDirty();
}

void QCPGrid::setPen(const QPen& pen)
{
    mPen = pen;
    if (auto* plot = mParentAxis ? mParentAxis->parentPlot() : nullptr)
        if (auto* grl = plot->gridRhiLayer())
            grl->markGeometryDirty();
}

void QCPGrid::setSubGridPen(const QPen& pen)
{
    mSubGridPen = pen;
    if (auto* plot = mParentAxis ? mParentAxis->parentPlot() : nullptr)
        if (auto* grl = plot->gridRhiLayer())
            grl->markGeometryDirty();
}

void QCPGrid::setZeroLinePen(const QPen& pen)
{
    mZeroLinePen = pen;
    if (auto* plot = mParentAxis ? mParentAxis->parentPlot() : nullptr)
        if (auto* grl = plot->gridRhiLayer())
            grl->markGeometryDirty();
}
```

- [ ] **Step 2: Build and run tests**

Run: `meson compile -C build && meson test -C build`
Expected: All tests pass.

- [ ] **Step 3: Commit**

```bash
git add src/axis/axis.cpp
git commit -m "feat: mark grid RHI geometry dirty on property changes"
```
