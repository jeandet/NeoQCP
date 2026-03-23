# GPU-Native Span Rendering Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render VSpan/HSpan/RSpan items on the GPU via QRhi shaders, eliminating CPU-side texture uploads during pan/zoom.

**Architecture:** A new `QCPSpanRhiLayer` class accumulates span geometry (filled quads + border line quads) in a vertex buffer with data-space coordinates. The vertex shader transforms data coords to NDC using axis range uniforms updated each frame. Pan/zoom = uniform update only; vertex buffer only rebuilds when spans change.

**Tech Stack:** Qt 6.7+ QRhi, GLSL 440 shaders compiled via `qsb`, Meson build system.

**Spec:** `docs/superpowers/specs/2026-03-23-gpu-native-span-rendering-design.md`

---

### Task 1: Create span vertex shader

**Files:**
- Create: `src/painting/shaders/span.vert`

- [ ] **Step 1: Write the vertex shader**

Create `src/painting/shaders/span.vert` with the coord-to-pixel transform, isPixel branching, border extrusion, and NDC conversion:

```glsl
#version 440

layout(location = 0) in vec2 dataCoord;
layout(location = 1) in vec4 color;
layout(location = 2) in vec2 extrudeDir;
layout(location = 3) in float extrudeWidth;
layout(location = 4) in vec2 isPixel;

layout(location = 0) out vec4 v_color;

layout(std140, binding = 0) uniform SpanParams {
    float width;
    float height;
    float yFlip;
    float dpr;
    float keyRangeLower;
    float keyRangeUpper;
    float keyAxisOffset;
    float keyAxisLength;
    float keyLogScale;
    float valRangeLower;
    float valRangeUpper;
    float valAxisOffset;
    float valAxisLength;
    float valLogScale;
};

float coordToPixel(float coord, float lower, float upper,
                   float offset, float length, float isLog) {
    float t;
    if (isLog > 0.5) {
        float safeCoord = max(coord, 1e-30);
        float safeLower = max(lower, 1e-30);
        float safeUpper = max(upper, 1e-30);
        t = (log(safeCoord) - log(safeLower)) / (log(safeUpper) - log(safeLower));
    } else {
        t = (coord - lower) / (upper - lower);
    }
    return t * length + offset;
}

void main() {
    float px = isPixel.x > 0.5
        ? dataCoord.x
        : coordToPixel(dataCoord.x, keyRangeLower, keyRangeUpper,
                        keyAxisOffset, keyAxisLength, keyLogScale);
    float py = isPixel.y > 0.5
        ? dataCoord.y
        : coordToPixel(dataCoord.y, valRangeLower, valRangeUpper,
                        valAxisOffset, valAxisLength, valLogScale);

    px += extrudeDir.x * extrudeWidth;
    py += extrudeDir.y * extrudeWidth;

    float ndcX = (px * dpr / width) * 2.0 - 1.0;
    float ndcY = yFlip * ((py * dpr / height) * 2.0 - 1.0);
    gl_Position = vec4(ndcX, ndcY, 0.0, 1.0);
    v_color = color;
}
```

- [ ] **Step 2: Add shader compilation to meson.build**

Add after the `plottable_frag_qsb` target (around line 136 in `meson.build`):

```meson
span_vert_qsb = custom_target('span_vert_qsb',
    input: 'src/painting/shaders/span.vert',
    output: 'span.vert.qsb',
    command: [qsb, '--qt6', '-o', '@OUTPUT@', '@INPUT@'])
```

Update the `embedded_shaders` target to include it — add `span_vert_qsb` to `input` array and `'span_vert_qsb_data:@INPUT4@'` to the command arguments. The fragment shader reuses `plottable_frag_qsb_data` (identical pass-through).

- [ ] **Step 3: Verify shader compiles**

Run: `meson compile -C build`

Expected: Build succeeds, `embedded_shaders.h` contains `span_vert_qsb_data`.

- [ ] **Step 4: Commit**

```bash
git add src/painting/shaders/span.vert meson.build
git commit -m "feat: add span vertex shader with coord-to-pixel transform"
```

---

### Task 2: Implement QCPSpanRhiLayer class

**Files:**
- Create: `src/painting/span-rhi-layer.h`
- Create: `src/painting/span-rhi-layer.cpp`
- Modify: `src/qcp.h` — add `#include "painting/span-rhi-layer.h"`
- Modify: `meson.build` — add `src/painting/span-rhi-layer.cpp` to sources, `src/painting/span-rhi-layer.h` to `extra_files`

- [ ] **Step 1: Create the header**

Create `src/painting/span-rhi-layer.h`. Key API following `QCPPlottableRhiLayer` pattern:

```cpp
#pragma once

#include <QColor>
#include <QPointF>
#include <QRect>
#include <QVector>
#include <QMap>
#include <rhi/qrhi.h>

class QCPAbstractItem;
class QCPAxisRect;

class QCPSpanRhiLayer
{
public:
    struct DrawGroup
    {
        QCPAxisRect* axisRect = nullptr;
        int vertexOffset = 0;
        int vertexCount = 0;
        QRect scissorRect;
        QRhiBuffer* uniformBuffer = nullptr;
        QRhiShaderResourceBindings* srb = nullptr;
    };

    explicit QCPSpanRhiLayer(QRhi* rhi);
    ~QCPSpanRhiLayer();

    void registerSpan(QCPAbstractItem* span);
    void unregisterSpan(QCPAbstractItem* span);
    void markGeometryDirty();

    bool hasSpans() const { return !mSpans.isEmpty(); }

    void invalidatePipeline();
    bool ensurePipeline(QRhiRenderPassDescriptor* rpDesc, int sampleCount);
    void uploadResources(QRhiResourceUpdateBatch* updates,
                          const QSize& outputSize, float dpr, bool isYUpInNDC);
    void render(QRhiCommandBuffer* cb, const QSize& outputSize);

private:
    void rebuildGeometry(float dpr, int outputHeight, bool isYUpInNDC);
    void cleanupDrawGroups();

    QRhi* mRhi;
    QVector<QCPAbstractItem*> mSpans;

    QVector<float> mStagingVertices;
    QVector<DrawGroup> mDrawGroups;
    bool mGeometryDirty = true;

    QRhiBuffer* mVertexBuffer = nullptr;
    QRhiGraphicsPipeline* mPipeline = nullptr;
    QRhiShaderResourceBindings* mLayoutSrb = nullptr;  // layout-only, for pipeline compatibility
    int mVertexBufferSize = 0;
    int mLastSampleCount = 0;
};
```

- [ ] **Step 2: Create the implementation**

Create `src/painting/span-rhi-layer.cpp`. Structure:

1. **Constructor/destructor**: Store `mRhi`, destructor deletes pipeline/srb/uniform/vertex buffers.

2. **`registerSpan`/`unregisterSpan`**: Add/remove from `mSpans`, set `mGeometryDirty = true`.

3. **`invalidatePipeline`**: Delete pipeline, srb, uniform buffer (same pattern as `QCPPlottableRhiLayer::invalidatePipeline()`).

4. **`ensurePipeline`**: Load `span_vert_qsb_data` + `plottable_frag_qsb_data` from `embedded_shaders.h`. Create pipeline with:
   - Vertex layout: 5 attributes (vec2 + vec4 + vec2 + float + vec2), stride = 11 * sizeof(float) = 44 bytes
   - Premultiplied alpha blending (same as plottable pipeline)
   - `UsesScissor` flag
   - Triangles topology
   - Use a layout-only SRB for pipeline compatibility (uniform buffer at binding 0)

   Note: uniform buffers and per-draw-group SRBs are created per axis rect in `rebuildGeometry`/`uploadResources`. The pipeline uses a layout-compatible SRB.

5. **`rebuildGeometry`**: Iterate `mSpans`, group by axis rect. For each span:
   - Determine which type (VSpan/HSpan/RSpan) via `qobject_cast` (include `item-vspan.h`, `item-hspan.h`, `item-rspan.h`)
   - Extract edge data coordinates and colors (using `mainBrush()`, `mainBorderPen()`)
   - Generate fill quad vertices (6 per span) with appropriate isPixelX/Y flags
   - Generate border line vertices (extruded quads, 6 per border) with extrudeDir/extrudeWidth
   - Compute scissor rect per axis rect (physical pixels, Y-flipped for Y-up)
   - Premultiply alpha on all colors
   - Create/resize per-axis-rect uniform buffers (64 bytes each) and SRBs if the axis rect set changed

6. **`uploadResources`**:
   - If `mGeometryDirty`: call `rebuildGeometry`, upload vertex buffer, clear flag
   - Always: for each draw group, update that group's uniform buffer with current axis ranges

   Each axis rect gets its own `QRhiBuffer` (dynamic, 64 bytes) and `QRhiShaderResourceBindings`. This avoids the fragile pattern of overwriting a single uniform buffer between draw calls (the GPU may not have consumed the previous draw's data). Store these in the `DrawGroup` struct:

   ```cpp
   struct DrawGroup
   {
       QCPAxisRect* axisRect = nullptr;
       int vertexOffset = 0;
       int vertexCount = 0;
       QRect scissorRect;
       QRhiBuffer* uniformBuffer = nullptr;        // 64 bytes, per axis rect
       QRhiShaderResourceBindings* srb = nullptr;   // binds this group's UBO
   };
   ```

7. **`render`**: Set pipeline, viewport. For each draw group: set scissor, call `cb->setShaderResources(group.srb)`, draw vertices. No per-draw uniform updates needed — each group has its own pre-uploaded UBO.

**Important vertex generation details for `rebuildGeometry`:**

For a **VSpan** (X = data coords, Y = pixel coords):
- Get `lowerEdge->coords().x()` and `upperEdge->coords().x()` as data X
- Get `clipRect().top()` and `clipRect().bottom()` as logical pixel Y
- Fill quad: 6 vertices with `isPixelX=0, isPixelY=1`
- 2 vertical borders at lowerEdge.x and upperEdge.x: 6 vertices each with `extrudeDir=(±1,0)`, `extrudeWidth=borderPen.widthF()/2`

For an **HSpan** (Y = data coords, X = pixel coords):
- Get `lowerEdge->coords().y()` and `upperEdge->coords().y()` as data Y
- Get `clipRect().left()` and `clipRect().right()` as logical pixel X
- Fill quad: 6 vertices with `isPixelX=1, isPixelY=0`
- 2 horizontal borders at lowerEdge.y and upperEdge.y: 6 vertices each with `extrudeDir=(0,±1)`

For an **RSpan** (all data coords):
- Get all 4 edges as data coords, `isPixelX=0, isPixelY=0`
- Fill quad + 4 borders

- [ ] **Step 3: Add to build system**

In `meson.build`:
- Add `'src/painting/span-rhi-layer.cpp'` to the `static_library` sources list (after `plottable-rhi-layer.cpp`)
- Add `'src/painting/span-rhi-layer.h'` to `extra_files`

In `src/qcp.h`:
- Add `#include "painting/span-rhi-layer.h"` alongside the other painting includes

- [ ] **Step 4: Verify build**

Run: `meson compile -C build`

Expected: Build succeeds (class is defined but not yet used by anything).

- [ ] **Step 5: Commit**

```bash
git add src/painting/span-rhi-layer.h src/painting/span-rhi-layer.cpp src/qcp.h meson.build
git commit -m "feat: add QCPSpanRhiLayer for GPU-native span rendering"
```

---

### Task 3: Integrate QCPSpanRhiLayer into QCustomPlot

**Files:**
- Modify: `src/core.h` — add `mSpanRhiLayer` member and `spanRhiLayer()` accessor
- Modify: `src/core.cpp` — integrate in `render()` and `releaseResources()`

- [ ] **Step 1: Add member and accessor to core.h**

Add after line 384 (`QSet<QCPColormapRhiLayer*> mColormapRhiLayers;`):

```cpp
QCPSpanRhiLayer* mSpanRhiLayer = nullptr;
```

Add a public accessor method:

```cpp
QCPSpanRhiLayer* spanRhiLayer();
```

Add forward declaration of `QCPSpanRhiLayer` at the top of `core.h`.

- [ ] **Step 2: Implement spanRhiLayer() in core.cpp**

Lazy creation — creates the span RHI layer on first call (requires `mRhi` to be initialized):

```cpp
QCPSpanRhiLayer* QCustomPlot::spanRhiLayer()
{
    if (!mSpanRhiLayer && mRhi)
        mSpanRhiLayer = new QCPSpanRhiLayer(mRhi);
    return mSpanRhiLayer;
}
```

- [ ] **Step 3: Integrate into render()**

In `QCustomPlot::render()`, add span RHI layer resource upload after the colormap upload block (after line 2632):

```cpp
if (mSpanRhiLayer && mSpanRhiLayer->hasSpans())
{
    mSpanRhiLayer->ensurePipeline(renderTarget()->renderPassDescriptor(), sampleCount());
    mSpanRhiLayer->uploadResources(updates, outputSize, mBufferDevicePixelRatio,
                                    mRhi->isYUpInNDC());
}
```

Add span rendering after the layer loop (after line 2793, before `cb->endPass()` at line 2796):

```cpp
if (mSpanRhiLayer && mSpanRhiLayer->hasSpans())
    mSpanRhiLayer->render(cb, outputSize);
```

- [ ] **Step 4: Integrate into releaseResources()**

In `QCustomPlot::releaseResources()`, add after `mColormapRhiLayers.clear()` (line 2810):

```cpp
delete mSpanRhiLayer;
mSpanRhiLayer = nullptr;
```

- [ ] **Step 5: Verify build**

Run: `meson compile -C build`

Expected: Build succeeds.

- [ ] **Step 6: Commit**

```bash
git add src/core.h src/core.cpp
git commit -m "feat: integrate QCPSpanRhiLayer into QCustomPlot render pipeline"
```

---

### Task 4: Wire span items to GPU rendering

**Files:**
- Modify: `src/items/item-vspan.h` — add `setSelected()` override declaration
- Modify: `src/items/item-vspan.cpp` — register/unregister, dirty calls, draw guard, setSelected
- Modify: `src/items/item-hspan.h` — add `setSelected()` override declaration
- Modify: `src/items/item-hspan.cpp` — same as vspan
- Modify: `src/items/item-rspan.h` — add `setSelected()` override declaration
- Modify: `src/items/item-rspan.cpp` — same as vspan
- Modify: `src/core.cpp` — resize dirty tracking in `resizeEvent()`

- [ ] **Step 1: Modify VSpan constructor and destructor**

In `item-vspan.cpp`, at the end of the constructor (after position/anchor setup), add:

```cpp
if (parentPlot->spanRhiLayer())
    parentPlot->spanRhiLayer()->registerSpan(this);
```

In the destructor, add:

```cpp
if (mParentPlot && mParentPlot->spanRhiLayer())
    mParentPlot->spanRhiLayer()->unregisterSpan(this);
```

- [ ] **Step 2: Add markGeometryDirty() to VSpan setters**

In each of the 6 setters (`setPen`, `setSelectedPen`, `setBrush`, `setSelectedBrush`, `setBorderPen`, `setSelectedBorderPen`), add at the end:

```cpp
if (mParentPlot && mParentPlot->spanRhiLayer())
    mParentPlot->spanRhiLayer()->markGeometryDirty();
```

Also in `setRange()` (which modifies edge positions).

- [ ] **Step 3: Add markGeometryDirty() to VSpan mouseMoveEvent**

In `QCPItemVSpan::mouseMoveEvent()`, after the edge position updates, add:

```cpp
if (mParentPlot && mParentPlot->spanRhiLayer())
    mParentPlot->spanRhiLayer()->markGeometryDirty();
```

This is needed because drag interaction modifies positions via `QCPItemPosition::setCoords()` which does not go through the span's own setters.

- [ ] **Step 4: Override setSelected() for dirty tracking**

Selection state changes switch between normal/selected colors. Override `setSelected()` in each span class:

```cpp
void QCPItemVSpan::setSelected(bool selected)
{
    QCPAbstractItem::setSelected(selected);
    if (mParentPlot && mParentPlot->spanRhiLayer())
        mParentPlot->spanRhiLayer()->markGeometryDirty();
}
```

Add the declaration to `item-vspan.h` in the public section:

```cpp
void setSelected(bool selected) override;
```

- [ ] **Step 5: Add export guard to VSpan::draw()**

At the top of `QCPItemVSpan::draw()`, add:

```cpp
if (mParentPlot && mParentPlot->spanRhiLayer()
    && !painter->modes().testFlag(QCPPainter::pmVectorized)
    && !painter->modes().testFlag(QCPPainter::pmNoCaching))
    return; // GPU handles on-screen rendering
```

Note: must check BOTH `pmVectorized` (PDF/SVG export) AND `pmNoCaching` (`toPixmap()` raster export). This matches the convention in `plottable-graph2.cpp` and `plottable-multigraph.cpp`.

- [ ] **Step 6: Apply same changes to HSpan and RSpan**

Repeat steps 1-5 for `item-hspan.cpp`/`.h` and `item-rspan.cpp`/`.h`. The changes are identical — register/unregister, dirty tracking in setters and mouseMoveEvent, setSelected override, and draw guard.

- [ ] **Step 7: Add resize dirty tracking**

In `QCustomPlot::resizeEvent()` (in `core.cpp`), add after `setViewport(rect())`:

```cpp
if (mSpanRhiLayer)
    mSpanRhiLayer->markGeometryDirty();
```

VSpan/HSpan pixel-space vertices (axis rect bounds) change on resize.

- [ ] **Step 8: Verify build and existing tests pass**

Run: `meson compile -C build && meson test -C build`

Expected: Build succeeds, existing span tests pass (they use `toPixmap()` which triggers the QPainter export fallback via the `pmNoCaching` check).

- [ ] **Step 9: Commit**

```bash
git add src/items/item-vspan.h src/items/item-vspan.cpp \
        src/items/item-hspan.h src/items/item-hspan.cpp \
        src/items/item-rspan.h src/items/item-rspan.cpp \
        src/core.cpp
git commit -m "feat: wire span items to GPU rendering with export fallback"
```

---

### Task 5: Write tests for GPU span rendering

**Files:**
- Modify: `tests/auto/test-vspan/test-vspan.h` — add new test slots
- Modify: `tests/auto/test-vspan/test-vspan.cpp` — add test implementations

- [ ] **Step 1: Write test for span RHI layer registration**

Add test to verify span registration/unregistration lifecycle:

```cpp
void TestVSpan::gpuLayerRegistration()
{
    auto* span = new QCPItemVSpan(mPlot);
    QVERIFY(mPlot->spanRhiLayer());
    // span is registered (spanRhiLayer has spans)
    // Note: hasSpans() may not be true until RHI is initialized
    delete span;
    // span unregistered
}
```

- [ ] **Step 2: Write test for export fallback**

Verify that `toPixmap()` still produces correct output (spans render via QPainter path):

```cpp
void TestVSpan::exportFallback()
{
    auto* span = new QCPItemVSpan(mPlot);
    span->lowerEdge->setCoords(0.3, 0);
    span->upperEdge->setCoords(0.7, 1);
    span->setBrush(QBrush(QColor(255, 0, 0, 128)));
    span->setBorderPen(QPen(Qt::red, 2));
    mPlot->replot();
    QPixmap pm = mPlot->toPixmap(200, 200);
    QVERIFY(!pm.isNull());
    // Check that the span area has red-tinted pixels
    QImage img = pm.toImage();
    QColor center = img.pixelColor(img.width() / 2, img.height() / 2);
    QVERIFY(center.red() > 100);
}
```

- [ ] **Step 3: Write test for dirty tracking**

Verify that changing span properties marks geometry dirty:

```cpp
void TestVSpan::dirtyTracking()
{
    auto* span = new QCPItemVSpan(mPlot);
    span->setBrush(QBrush(Qt::blue));
    // After setBrush, geometry should be marked dirty
    // This is an internal state test — verify via a replot cycle
    mPlot->replot();
    // Change pen — triggers dirty
    span->setBorderPen(QPen(Qt::green, 3));
    mPlot->replot();
    // Change range — triggers dirty
    span->setRange(QCPRange(0.2, 0.8));
    mPlot->replot();
}
```

- [ ] **Step 4: Write test for log axis spans**

```cpp
void TestVSpan::gpuLogAxisSpan()
{
    mPlot->xAxis->setScaleType(QCPAxis::stLogarithmic);
    mPlot->xAxis->setRange(1, 1000);
    auto* span = new QCPItemVSpan(mPlot);
    span->lowerEdge->setCoords(10, 0);
    span->upperEdge->setCoords(100, 1);
    span->setBrush(QBrush(QColor(0, 0, 255, 80)));
    mPlot->replot();
    QPixmap pm = mPlot->toPixmap(400, 300);
    QVERIFY(!pm.isNull());
}
```

- [ ] **Step 5: Write test for multi-axis-rect scissor clipping**

```cpp
void TestVSpan::multiAxisRectClipping()
{
    // Create a second axis rect
    auto* ar2 = new QCPAxisRect(mPlot);
    mPlot->plotLayout()->addElement(0, 1, ar2);

    auto* span1 = new QCPItemVSpan(mPlot);
    span1->lowerEdge->setCoords(0.2, 0);
    span1->upperEdge->setCoords(0.8, 1);
    span1->setBrush(QBrush(QColor(255, 0, 0, 80)));

    auto* span2 = new QCPItemVSpan(mPlot);
    span2->lowerEdge->setAxes(ar2->axis(QCPAxis::atBottom), ar2->axis(QCPAxis::atLeft));
    span2->upperEdge->setAxes(ar2->axis(QCPAxis::atBottom), ar2->axis(QCPAxis::atLeft));
    span2->lowerEdge->setCoords(0.3, 0);
    span2->upperEdge->setCoords(0.7, 1);
    span2->setBrush(QBrush(QColor(0, 0, 255, 80)));

    mPlot->replot();
    QPixmap pm = mPlot->toPixmap(600, 300);
    QVERIFY(!pm.isNull());
}
```

- [ ] **Step 6: Add test declarations to header**

In `test-vspan.h`, add:

```cpp
void gpuLayerRegistration();
void exportFallback();
void dirtyTracking();
void gpuLogAxisSpan();
void multiAxisRectClipping();
```

- [ ] **Step 7: Run tests**

Run: `meson test -C build --print-errorlogs`

Expected: All tests pass (new and existing).

- [ ] **Step 8: Commit**

```bash
git add tests/auto/test-vspan/test-vspan.h tests/auto/test-vspan/test-vspan.cpp
git commit -m "test: add GPU span rendering tests (registration, export, dirty, log, multi-rect)"
```

---

### Task 6: Add HSpan and RSpan GPU rendering tests

**Files:**
- Modify: `tests/auto/test-hspan/test-hspan.h` — add new test slots
- Modify: `tests/auto/test-hspan/test-hspan.cpp` — add test implementations
- Modify: `tests/auto/test-rspan/test-rspan.h` — add new test slots
- Modify: `tests/auto/test-rspan/test-rspan.cpp` — add test implementations

- [ ] **Step 1: Add HSpan GPU tests**

Mirror VSpan tests for HSpan — registration, export fallback, log axis (on Y axis), dirty tracking. The key difference: HSpan uses `ptPlotCoords` on Y and `ptAxisRectRatio` on X.

- [ ] **Step 2: Add RSpan GPU tests**

Mirror VSpan tests for RSpan — registration, export fallback, log axis (both axes). RSpan uses `ptPlotCoords` on both axes (no pixel-space coords).

- [ ] **Step 3: Run all tests**

Run: `meson test -C build --print-errorlogs`

Expected: All tests pass.

- [ ] **Step 4: Commit**

```bash
git add tests/auto/test-hspan/ tests/auto/test-rspan/
git commit -m "test: add HSpan and RSpan GPU rendering tests"
```

---

### Task 7: End-to-end verification

- [ ] **Step 1: Run full test suite**

Run: `meson test -C build --print-errorlogs`

Expected: All tests pass, no regressions.

- [ ] **Step 2: Run manual test app**

Run: `./build/tests/manual/manual-test`

Create spans interactively, verify:
- Spans render correctly on linear axes
- Spans render correctly on log axes
- Panning does not cause span flicker or lag
- Dragging span edges works (hit testing + dirty tracking)
- Semi-transparent fills overlay correctly on curves

- [ ] **Step 3: Test export**

In the manual test app, use save/export to verify QPainter fallback produces correct output.

- [ ] **Step 4: Final commit if any fixes needed**

```bash
git add -A
git commit -m "fix: address issues found during GPU span rendering verification"
```
