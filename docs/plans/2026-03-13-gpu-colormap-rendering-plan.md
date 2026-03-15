# GPU-Accelerated Colormap Rendering Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Render QCPColorMap2's resampled image as a GPU texture quad, bypassing QPainter for widget rendering while keeping QPainter for export paths.

**Architecture:** QCPColormapRhiLayer owns a GPU texture + quad vertex buffer. During replot, QCPColorMap2::draw() pushes the colormap image and axis-rect pixel coordinates to the layer. During render, core.cpp uploads the texture and draws the quad with nearest-neighbor sampling and scissor clipping. The composite shader is reused — quad vertices are pre-converted to NDC on the CPU.

**Tech Stack:** Qt 6.7+ QRhi, existing composite.vert/frag shaders, QImage::Format_RGBA8888_Premultiplied

**Spec:** `docs/plans/2026-03-13-gpu-colormap-rendering-design.md`

---

### Task 1: Create QCPColormapRhiLayer

**Files:**
- Create: `src/painting/colormap-rhi-layer.h`
- Create: `src/painting/colormap-rhi-layer.cpp`

- [ ] **Step 1: Write the header**

```cpp
// src/painting/colormap-rhi-layer.h
#pragma once

#include <QImage>
#include <QRect>
#include <QRectF>
#include <rhi/qrhi.h>

class QCPColormapRhiLayer
{
public:
    explicit QCPColormapRhiLayer(QRhi* rhi);
    ~QCPColormapRhiLayer();

    // Called during replot (CPU side)
    void setImage(const QImage& image);
    void setQuadRect(const QRectF& pixelRect);
    void setScissorRect(const QRect& scissor);

    // Called during render (GPU side)
    void invalidatePipeline();
    bool ensurePipeline(QRhiRenderPassDescriptor* rpDesc, int sampleCount);
    void uploadResources(QRhiResourceUpdateBatch* updates,
                         const QSize& outputSize, float dpr, bool isYUpInNDC);
    void render(QRhiCommandBuffer* cb, const QSize& outputSize);

    bool hasContent() const { return !mStagingImage.isNull(); }

private:
    QRhi* mRhi;

    // CPU staging
    QImage mStagingImage;
    QRectF mQuadPixelRect;
    QRect mScissorRect;
    bool mTextureDirty = false;
    bool mGeometryDirty = false;

    // GPU resources
    QRhiTexture* mTexture = nullptr;
    QRhiSampler* mSampler = nullptr;
    QRhiBuffer* mVertexBuffer = nullptr;
    QRhiBuffer* mIndexBuffer = nullptr;
    QRhiShaderResourceBindings* mSrb = nullptr;
    QRhiGraphicsPipeline* mPipeline = nullptr;
    int mLastSampleCount = 0;
    QSize mTextureSize;
};
```

- [ ] **Step 2: Write the implementation**

```cpp
// src/painting/colormap-rhi-layer.cpp
#include "colormap-rhi-layer.h"
#include "embedded_shaders.h"
#include "Profiling.hpp"

QCPColormapRhiLayer::QCPColormapRhiLayer(QRhi* rhi)
    : mRhi(rhi)
{
}

QCPColormapRhiLayer::~QCPColormapRhiLayer()
{
    delete mPipeline;
    delete mSrb;
    delete mSampler;
    delete mVertexBuffer;
    delete mIndexBuffer;
    delete mTexture;
}

void QCPColormapRhiLayer::invalidatePipeline()
{
    delete mPipeline;
    mPipeline = nullptr;
    delete mSrb;
    mSrb = nullptr;
}

void QCPColormapRhiLayer::setImage(const QImage& image)
{
    mStagingImage = image;
    mTextureDirty = true;
}

void QCPColormapRhiLayer::setQuadRect(const QRectF& pixelRect)
{
    mQuadPixelRect = pixelRect;
    mGeometryDirty = true;
}

void QCPColormapRhiLayer::setScissorRect(const QRect& scissor)
{
    mScissorRect = scissor;
}

bool QCPColormapRhiLayer::ensurePipeline(QRhiRenderPassDescriptor* rpDesc,
                                          int sampleCount)
{
    if (mPipeline && mLastSampleCount == sampleCount)
        return true;

    invalidatePipeline();

    // Reuse composite shaders — position(float2) + texcoord(float2), sampler at binding 1
    QShader vertShader = QShader::fromSerialized(QByteArray::fromRawData(
        reinterpret_cast<const char*>(composite_vert_qsb_data), composite_vert_qsb_data_len));
    QShader fragShader = QShader::fromSerialized(QByteArray::fromRawData(
        reinterpret_cast<const char*>(composite_frag_qsb_data), composite_frag_qsb_data_len));

    if (!vertShader.isValid() || !fragShader.isValid())
    {
        qDebug() << "Failed to load composite shaders for colormap";
        return false;
    }

    // Create nearest-neighbor sampler
    if (!mSampler)
    {
        mSampler = mRhi->newSampler(QRhiSampler::Nearest, QRhiSampler::Nearest,
                                     QRhiSampler::None,
                                     QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge);
        mSampler->create();
    }

    // Create SRB with texture + sampler at binding 1 (matches composite.frag)
    // Texture may not exist yet — use a layout-compatible SRB, actual binding in uploadResources
    mSrb = mRhi->newShaderResourceBindings();
    mSrb->setBindings({
        QRhiShaderResourceBinding::sampledTexture(
            1, QRhiShaderResourceBinding::FragmentStage,
            mTexture, mSampler)
    });
    // SRB creation is deferred until texture exists (see uploadResources)
    if (mTexture)
        mSrb->create();

    mPipeline = mRhi->newGraphicsPipeline();
    mPipeline->setShaderStages({
        {QRhiShaderStage::Vertex, vertShader},
        {QRhiShaderStage::Fragment, fragShader}
    });

    // Vertex layout: position(float2) + texcoord(float2) = 4 floats per vertex
    QRhiVertexInputLayout inputLayout;
    inputLayout.setBindings({{4 * sizeof(float)}});
    inputLayout.setAttributes({
        {0, 0, QRhiVertexInputAttribute::Float2, 0},
        {0, 1, QRhiVertexInputAttribute::Float2, 2 * sizeof(float)}
    });
    mPipeline->setVertexInputLayout(inputLayout);

    // Premultiplied alpha blending
    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable = true;
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    mPipeline->setTargetBlends({blend});

    mPipeline->setFlags(QRhiGraphicsPipeline::UsesScissor);
    mPipeline->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    mPipeline->setSampleCount(sampleCount);
    mPipeline->setRenderPassDescriptor(rpDesc);

    // If SRB isn't created yet (no texture), defer pipeline creation
    if (mTexture && mSrb->isLayoutCompatible(mSrb))
    {
        mPipeline->setShaderResourceBindings(mSrb);
        if (!mPipeline->create())
        {
            qDebug() << "Failed to create colormap pipeline";
            delete mPipeline;
            mPipeline = nullptr;
            return false;
        }
    }

    mLastSampleCount = sampleCount;
    return true;
}

void QCPColormapRhiLayer::uploadResources(QRhiResourceUpdateBatch* updates,
                                            const QSize& outputSize, float dpr,
                                            bool isYUpInNDC)
{
    if (mStagingImage.isNull())
        return;

    // (Re)create texture if size changed
    QSize imgSize = mStagingImage.size();
    if (!mTexture || mTextureSize != imgSize)
    {
        delete mTexture;
        mTexture = mRhi->newTexture(QRhiTexture::RGBA8, imgSize);
        mTexture->create();
        mTextureSize = imgSize;
        mTextureDirty = true;

        // Rebind SRB with new texture
        if (mSrb)
        {
            mSrb->setBindings({
                QRhiShaderResourceBinding::sampledTexture(
                    1, QRhiShaderResourceBinding::FragmentStage,
                    mTexture, mSampler)
            });
            mSrb->create();
        }

        // Pipeline needs SRB to be finalized
        if (mPipeline && !mPipeline->shaderResourceBindings())
        {
            mPipeline->setShaderResourceBindings(mSrb);
            mPipeline->create();
        }
    }

    // Upload texture data
    if (mTextureDirty)
    {
        QRhiTextureSubresourceUploadDescription subDesc(mStagingImage);
        updates->uploadTexture(mTexture, QRhiTextureUploadDescription(
            QRhiTextureUploadEntry(0, 0, subDesc)));
        mTextureDirty = false;
    }

    // Update quad vertex buffer with NDC coordinates
    if (mGeometryDirty || !mVertexBuffer)
    {
        // Convert pixel rect to NDC: x_ndc = (x_px * dpr / width) * 2 - 1
        const float w = float(outputSize.width());
        const float h = float(outputSize.height());
        const float yFlip = isYUpInNDC ? -1.0f : 1.0f;

        auto toNDC = [&](float px, float py) -> std::pair<float, float> {
            float ndcX = (px * dpr / w) * 2.0f - 1.0f;
            float ndcY = yFlip * ((py * dpr / h) * 2.0f - 1.0f);
            return {ndcX, ndcY};
        };

        auto [x0, y0] = toNDC(mQuadPixelRect.left(), mQuadPixelRect.top());
        auto [x1, y1] = toNDC(mQuadPixelRect.right(), mQuadPixelRect.bottom());

        // UV: image top-left = (0,0), bottom-right = (1,1)
        // For Y-up NDC (OpenGL): V needs flipping (top of image = larger NDC Y)
        float u0 = 0.0f, u1 = 1.0f;
        float vTop, vBot;
        if (isYUpInNDC)
        {
            vTop = 0.0f;  // NDC top (larger y) → image top (v=0)
            vBot = 1.0f;  // NDC bottom (smaller y) → image bottom (v=1)
        }
        else
        {
            vTop = 0.0f;
            vBot = 1.0f;
        }

        // Triangle strip: bottom-left, bottom-right, top-left, top-right
        const float verts[] = {
            x0, y1,  u0, vBot,   // bottom-left
            x1, y1,  u1, vBot,   // bottom-right
            x0, y0,  u0, vTop,   // top-left
            x1, y0,  u1, vTop,   // top-right
        };

        if (!mVertexBuffer)
        {
            mVertexBuffer = mRhi->newBuffer(QRhiBuffer::Dynamic,
                                             QRhiBuffer::VertexBuffer,
                                             sizeof(verts));
            mVertexBuffer->create();
        }

        updates->updateDynamicBuffer(mVertexBuffer, 0, sizeof(verts), verts);
        mGeometryDirty = false;
    }
}

void QCPColormapRhiLayer::render(QRhiCommandBuffer* cb, const QSize& outputSize)
{
    PROFILE_HERE_N("QCPColormapRhiLayer::render");

    if (!mPipeline || !mVertexBuffer || !mSrb || !mTexture)
        return;

    cb->setGraphicsPipeline(mPipeline);
    cb->setViewport({0, 0, float(outputSize.width()), float(outputSize.height())});
    cb->setShaderResources(mSrb);

    const QRhiCommandBuffer::VertexInput vbufBinding(mVertexBuffer, 0);
    cb->setVertexInput(0, 1, &vbufBinding);

    cb->setScissor({mScissorRect.x(), mScissorRect.y(),
                    mScissorRect.width(), mScissorRect.height()});
    cb->draw(4); // triangle strip, 4 vertices
}
```

- [ ] **Step 3: Add to meson.build**

In `meson.build`, add `'src/painting/colormap-rhi-layer.cpp'` to the sources list (after `plottable-rhi-layer.cpp`) and `'src/painting/colormap-rhi-layer.h'` to `extra_files`.

- [ ] **Step 4: Build and verify compilation**

Run: `meson compile -C build`
Expected: clean build, no errors

- [ ] **Step 5: Commit**

```bash
git add src/painting/colormap-rhi-layer.h src/painting/colormap-rhi-layer.cpp meson.build
git commit -m "feat: add QCPColormapRhiLayer for GPU texture quad rendering"
```

---

### Task 2: Integrate into Core Render Loop

**Files:**
- Modify: `src/core.h` (add member + accessor)
- Modify: `src/core.cpp` (initialize, render, releaseResources)

- [ ] **Step 1: Add member and accessor to core.h**

After `mPlottableRhiLayers` declaration (~line 354), add:

```cpp
QMap<QCPLayer*, QCPColormapRhiLayer*> mColormapRhiLayers;
```

After `plottableRhiLayer()` declaration (~line 126), add:

```cpp
QCPColormapRhiLayer* colormapRhiLayer(QCPLayer* layer);
```

Add forward declaration near the top (after `class QCPPlottableRhiLayer;`):

```cpp
class QCPColormapRhiLayer;
```

- [ ] **Step 2: Add colormapRhiLayer() accessor in core.cpp**

After `plottableRhiLayer()` (~line 1021), add:

```cpp
QCPColormapRhiLayer* QCustomPlot::colormapRhiLayer(QCPLayer* layer)
{
    if (!mRhi)
        return nullptr;
    if (!mColormapRhiLayers.contains(layer))
    {
        auto* crl = new QCPColormapRhiLayer(mRhi);
        mColormapRhiLayers[layer] = crl;
    }
    return mColormapRhiLayers[layer];
}
```

- [ ] **Step 3: Update initialize() to invalidate colormap pipelines on resize**

In `initialize()` (~line 2393), after the loop that invalidates plottable pipelines, add:

```cpp
for (auto* crl : mColormapRhiLayers)
    crl->invalidatePipeline();
```

- [ ] **Step 4: Update render() to upload and draw colormap layers**

In `render()`, after the plottable RHI layers upload loop (~line 2494), add:

```cpp
// Upload colormap GPU resources
for (auto* crl : mColormapRhiLayers)
{
    crl->ensurePipeline(renderTarget()->renderPassDescriptor(), sampleCount());
    crl->uploadResources(updates, outputSize, mBufferDevicePixelRatio,
                          mRhi->isYUpInNDC());
}
```

In the per-layer render loop, after compositing the paint buffer and BEFORE the plottable RHI layer render (~line 2597), add:

```cpp
// Draw colormap texture quads for this layer (between paint buffer and line plottables)
if (auto* crl = mColormapRhiLayers.value(layer, nullptr))
{
    if (crl->hasContent())
    {
        crl->render(cb, outputSize);

        // Restore composite pipeline state for next layer
        cb->setGraphicsPipeline(mCompositePipeline);
        cb->setViewport({0, 0, float(outputSize.width()), float(outputSize.height())});
        cb->setVertexInput(0, 1, &vbufBinding, mQuadIndexBuffer, 0,
                           QRhiCommandBuffer::IndexUInt16);
    }
}
```

- [ ] **Step 5: Update releaseResources() to clean up colormap layers**

In `releaseResources()` (~line 2624), after `qDeleteAll(mPlottableRhiLayers)`, add:

```cpp
qDeleteAll(mColormapRhiLayers);
mColormapRhiLayers.clear();
```

- [ ] **Step 6: Add include for colormap-rhi-layer.h in core.cpp**

Add near the other painting includes:

```cpp
#include "painting/colormap-rhi-layer.h"
```

- [ ] **Step 7: Build and verify**

Run: `meson compile -C build`
Expected: clean build

- [ ] **Step 8: Commit**

```bash
git add src/core.h src/core.cpp
git commit -m "feat: integrate QCPColormapRhiLayer into core render loop"
```

---

### Task 3: Wire QCPColorMap2 to GPU Path

**Files:**
- Modify: `src/plottables/plottable-colormap2.h`
- Modify: `src/plottables/plottable-colormap2.cpp`

- [ ] **Step 1: Change mMapImage format to RGBA8888**

In `plottable-colormap2.cpp`, `updateMapImage()` (~line 157), change:

```cpp
// Before:
mMapImage = QImage(keySize, valueSize, QImage::Format_ARGB32_Premultiplied);
// After:
mMapImage = QImage(keySize, valueSize, QImage::Format_RGBA8888_Premultiplied);
```

The `QCPColorGradient::colorize()` produces QRgb (ARGB32) values. For RGBA8888 format, the QImage handles the byte reordering internally when writing via `scanLine()` — since we write QRgb values via `reinterpret_cast<QRgb*>`, we need to convert. Instead, keep ARGB32 for colorize and convert the final image:

```cpp
void QCPColorMap2::updateMapImage()
{
    if (!mResampledData)
        return;

    int keySize = mResampledData->keySize();
    int valueSize = mResampledData->valueSize();
    if (keySize == 0 || valueSize == 0)
        return;

    QImage argbImage(keySize, valueSize, QImage::Format_ARGB32_Premultiplied);

    std::vector<double> rowData(keySize);
    for (int y = 0; y < valueSize; ++y)
    {
        for (int x = 0; x < keySize; ++x)
            rowData[x] = mResampledData->cell(x, y);

        QRgb* pixels = reinterpret_cast<QRgb*>(argbImage.scanLine(valueSize - 1 - y));
        mGradient.colorize(rowData.data(), mDataRange, pixels, keySize);
    }

    // Convert to RGBA8888 for direct GPU texture upload (QRhiTexture::RGBA8)
    mMapImage = argbImage.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    mMapImageInvalidated = false;
}
```

- [ ] **Step 2: Update draw() to branch GPU vs QPainter**

Replace the `draw()` method:

```cpp
void QCPColorMap2::draw(QCPPainter* painter)
{
    PROFILE_HERE_N("QCPColorMap2::draw");
    if (!mKeyAxis || !mValueAxis)
        return;

    if (!mResampledData && mDataSource)
    {
        requestResample();
        return;
    }

    if (!mResampledData)
        return;

    if (mMapImageInvalidated)
        updateMapImage();

    if (mMapImage.isNull())
        return;

    QCPRange keyRange = mResampledData->keyRange();
    QCPRange valueRange = mResampledData->valueRange();

    QPointF topLeft = QPointF(mKeyAxis->coordToPixel(keyRange.lower),
                              mValueAxis->coordToPixel(valueRange.upper));
    QPointF bottomRight = QPointF(mKeyAxis->coordToPixel(keyRange.upper),
                                  mValueAxis->coordToPixel(valueRange.lower));
    QRectF imageRect(topLeft, bottomRight);

    // GPU path: push image + rect to RHI layer, skip QPainter
    if (mParentPlot && mParentPlot->rhi())
    {
        auto* crl = mParentPlot->colormapRhiLayer(layer());
        if (crl)
        {
            bool mirrorX = mKeyAxis->rangeReversed();
            bool mirrorY = !mValueAxis->rangeReversed();
            Qt::Orientations flips;
            if (mirrorX) flips |= Qt::Horizontal;
            if (mirrorY) flips |= Qt::Vertical;
            crl->setImage(mMapImage.flipped(flips));

            crl->setQuadRect(imageRect.normalized());

            // Scissor rect = axis rect in physical pixels
            auto* axisRect = mKeyAxis->axisRect();
            QRect clipRect = axisRect->rect();
            double dpr = mParentPlot->bufferDevicePixelRatio();
            int sx = clipRect.x() * dpr;
            int sy = clipRect.y() * dpr;
            int sw = clipRect.width() * dpr;
            int sh = clipRect.height() * dpr;
            if (mParentPlot->rhi()->isYUpInNDC())
                sy = mParentPlot->rhi()->resourceSizeLimit() > 0
                    ? mParentPlot->height() * dpr - sy - sh
                    : mParentPlot->height() * dpr - sy - sh; // Y-flip for OpenGL

            // Simpler: use renderTarget pixel size
            crl->setScissorRect(QRect(sx, sy, sw, sh));
            return; // skip QPainter path
        }
    }

    // QPainter fallback (export paths: PDF, SVG, pixmap)
    bool mirrorX = mKeyAxis->rangeReversed();
    bool mirrorY = !mValueAxis->rangeReversed();
    Qt::Orientations flips;
    if (mirrorX) flips |= Qt::Horizontal;
    if (mirrorY) flips |= Qt::Vertical;

    applyDefaultAntialiasingHint(painter);
    // For QPainter, convert back to ARGB32 which QPainter handles natively
    painter->drawImage(imageRect, mMapImage.convertToFormat(
        QImage::Format_ARGB32_Premultiplied).flipped(flips));
}
```

Note: the scissor Y-flip logic above is tricky. Look at how `QCPPlottableRhiLayer::addPlottable()` does it (`outputHeight - sy - sh` when `isYUpInNDC`). The `outputHeight` is the render target pixel height, obtained from `mParentPlot->height() * dpr`. Refine during implementation.

- [ ] **Step 3: Add include for core.h if not already present**

Verify `#include <core.h>` is already in `plottable-colormap2.cpp` (it is).

- [ ] **Step 4: Build and verify**

Run: `meson compile -C build`
Expected: clean build

- [ ] **Step 5: Run existing tests**

Run: `meson test -C build --print-errorlogs`
Expected: all tests pass

- [ ] **Step 6: Commit**

```bash
git add src/plottables/plottable-colormap2.cpp
git commit -m "feat: wire QCPColorMap2 to GPU texture rendering path"
```

---

### Task 4: Test and Verify

**Files:**
- Modify: `tests/manual/mainwindow.cpp` (visual verification)

- [ ] **Step 1: Run the manual test**

Run: `build/tests/manual/manual`

Navigate to the QCPColorMap2 test. Verify:
- Colormap renders correctly (no visual glitches)
- Nearest-neighbor sampling — sharp cell boundaries, no blurring
- Pan/zoom is smooth
- No crashes

- [ ] **Step 2: Test export fallback**

In the manual test, right-click → export as PNG. Verify the exported image matches the on-screen rendering.

- [ ] **Step 3: Test with gapped data**

Navigate to the gapped data colormap test. Verify gap handling still works correctly.

- [ ] **Step 4: Run automated tests**

Run: `meson test -C build --print-errorlogs`
Expected: all tests pass

- [ ] **Step 5: Final commit with any fixes**

If any fixes were needed during testing, commit them.

---

### Implementation Notes

**Scissor rect Y-flip:** The scissor Y origin differs between backends. For Y-up (OpenGL), Y=0 is bottom. For Y-down (Metal/D3D), Y=0 is top. Use `mRhi->isYUpInNDC()` to determine which, and flip as `sy = outputHeight - sy - sh` for Y-up. The render target pixel height is `renderTarget()->pixelSize().height()` during render, or `mParentPlot->height() * dpr` during replot.

**Image flipping:** The `mMapImage` is produced with row 0 = top of the image (as standard QImage). The `flipped()` call handles axis reversal. This should be done before uploading to the GPU texture to avoid needing a UV flip in the shader.

**Pipeline deferred creation:** The SRB needs a valid texture pointer. If `ensurePipeline()` is called before the first `uploadResources()`, the texture doesn't exist yet. Handle this by deferring SRB/pipeline creation until the texture is first created in `uploadResources()`.

**Performance:** The format conversion `ARGB32 → RGBA8888` in `updateMapImage()` is a memcpy with byte swapping. For very large colormaps, consider producing RGBA8888 directly in the gradient colorize step (future optimization).
